// Phase 3: several clients at once over real sockets with fast timers.
#include <sys/socket.h>

#include <atomic>
#include <mutex>

#include "integration/test_support.h"
#include "minitest.h"

using namespace support;

namespace {

bool is_kind(const Message& message, const std::string& kind) {
    return message.is_push && !message.lines.empty() &&
           message.lines[0].rfind("PUSH " + kind, 0) == 0;
}

Message join(RawClient& client, const std::string& name, const std::string& code) {
    return client.request("JOIN_GAME " + code + " " + name);
}

// A client whose receive buffer is tiny and that never reads, so the server's
// writes to it eventually block.
int connect_stalled(int port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    int small = 4096;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &small, sizeof(small));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    return fd;
}

void send_blocking(int fd, const std::string& text) {
    size_t sent = 0;
    while (sent < text.size()) {
        const ssize_t n = send(fd, text.data() + sent, text.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) return;
        sent += static_cast<size_t>(n);
    }
}

}  // namespace

TEST_CASE("T11 host stays connected while a second client joins; both get their own leaderboard") {
    TestServer server;
    RawClient host(server.port());
    const Created game = create_game(host, "Robert", 3);
    RawClient guest(server.port());
    const Message joined = join(guest, "Matthew", game.code);
    CHECK_EQ(joined.lines[0], std::string("200 OK"));
    const Message from_host = host.request("LEADERBOARD");
    const Message from_guest = guest.request("LEADERBOARD");
    CHECK(from_host.lines == std::vector<std::string>({"200 OK", "Robert:0", "Matthew:0"}));
    CHECK(from_guest.lines == from_host.lines);
}

TEST_CASE("T10 T22 T23 T41 lobby rules with several clients") {
    TestServer server;
    RawClient host(server.port());
    const Created game = create_game(host, "Robert", 3);
    RawClient bob(server.port());
    RawClient carol(server.port());
    CHECK_EQ(join(bob, "Bob", game.code).lines[0], std::string("200 OK"));
    const Message carol_join = join(carol, "Carol", game.code);
    CHECK_EQ(carol_join.lines[1], "3 players in game " + game.code);
    CHECK(carol_join.lines[2] != game.token);

    CHECK_EQ(bob.request("START_ROUND").lines[0].substr(0, 3), std::string("402"));
    CHECK_EQ(bob.request("END_GAME").lines[0].substr(0, 3), std::string("402"));
    RawClient duplicate(server.port());
    CHECK_EQ(join(duplicate, "Bob", game.code).lines[0].substr(0, 3), std::string("409"));
    CHECK_EQ(join(duplicate, "Dave", game.code).lines[0], std::string("200 OK"));

    CHECK_EQ(carol.request("QUIT").lines[0], std::string("200 OK"));
    CHECK(carol.wait_for_close());
    const Message board = host.request("LEADERBOARD");
    CHECK(board.lines == std::vector<std::string>({"200 OK", "Robert:0", "Bob:0", "Dave:0"}));
}

TEST_CASE("T41 mid-game join waits for the next question") {
    Timings timings = fast_timings();
    timings.question = 1.0;
    TestServer server(timings);
    RawClient host(server.port());
    const Created game = create_game(host, "Robert", 3);
    host.send_line("START_ROUND");
    host.next();
    host.next();  // QUESTION 1
    RawClient late(server.port());
    std::vector<Message> pushes;
    late.send_line("JOIN_GAME " + game.code + " Late");
    CHECK_EQ(late.next().lines[0], std::string("200 OK"));
    CHECK_EQ(late.request("SUBMIT_ANSWER A").lines[0].substr(0, 3), std::string("420"));
    // The next push the late player sees is the second question, not question 1.
    Message push = late.next();
    while (!push.lines.empty() && !is_kind(push, "QUESTION")) push = late.next();
    CHECK(is_kind(push, "QUESTION 2"));
    CHECK_EQ(late.request(std::string("SUBMIT_ANSWER ") + correct_for(push)).lines[0],
             std::string("200 OK"));
}

TEST_CASE("T41 host QUIT and END_GAME reach every player with the right reason") {
    TestServer server;
    {
        RawClient host(server.port());
        const Created game = create_game(host, "Robert", 3);
        RawClient bob(server.port());
        RawClient carol(server.port());
        join(bob, "Bob", game.code);
        join(carol, "Carol", game.code);
        host.send_line("QUIT");
        CHECK(host.read_all_until_close() == "200 OK\n\n");
        CHECK_EQ(bob.next().lines[0], std::string("PUSH GAME_END HOST_QUIT"));
        CHECK(bob.wait_for_close());
        CHECK_EQ(carol.next().lines[0], std::string("PUSH GAME_END HOST_QUIT"));
        CHECK(carol.wait_for_close());
    }
    RawClient host(server.port());
    const Created game = create_game(host, "Robert", 3);
    RawClient bob(server.port());
    RawClient carol(server.port());
    join(bob, "Bob", game.code);
    join(carol, "Carol", game.code);
    host.send_line("END_GAME");
    CHECK_EQ(host.next().lines[0], std::string("200 OK"));
    CHECK_EQ(host.next().lines[0], std::string("PUSH GAME_END HOST_ENDED"));
    CHECK(host.wait_for_close());
    CHECK_EQ(bob.next().lines[0], std::string("PUSH GAME_END HOST_ENDED"));
    CHECK(bob.wait_for_close());
    CHECK_EQ(carol.next().lines[0], std::string("PUSH GAME_END HOST_ENDED"));
    CHECK(carol.wait_for_close());
}

TEST_CASE("T36 three clients answer at once: every answer counts once with the right score") {
    Timings timings = fast_timings();
    timings.question = 3.0;
    for (int round = 0; round < 5; ++round) {
        TestServer server(timings);
        RawClient host(server.port());
        const Created game = create_game(host, "Robert", 2);
        RawClient bob(server.port());
        RawClient carol(server.port());
        join(bob, "Bob", game.code);
        join(carol, "Carol", game.code);
        host.send_line("START_ROUND");
        host.next();
        const Message question = host.next();
        bob.next();
        carol.next();
        const char right = correct_for(question);
        const char wrong = right == 'A' ? 'B' : 'A';

        std::atomic<int> ready{0};
        std::vector<Message> replies(3);
        const auto answer = [&](RawClient& client, char choice, Message& reply) {
            ++ready;
            while (ready < 3) std::this_thread::yield();
            client.send_line(std::string("SUBMIT_ANSWER ") + choice);
            reply = client.next();
        };
        std::thread t1(answer, std::ref(host), right, std::ref(replies[0]));
        std::thread t2(answer, std::ref(bob), wrong, std::ref(replies[1]));
        std::thread t3(answer, std::ref(carol), right, std::ref(replies[2]));
        t1.join();
        t2.join();
        t3.join();
        for (const auto& reply : replies) CHECK_EQ(reply.lines[0], std::string("200 OK"));
        const Message result = host.next();
        CHECK(is_kind(result, "ROUND_RESULT 1"));
        CHECK(result.lines == std::vector<std::string>({"PUSH ROUND_RESULT 1",
                                                        std::string("Correct answer: ") + right,
                                                        "Robert:1", "Carol:1", "Bob:0"}));
    }
}

TEST_CASE("T37 two clients race the same name: exactly one 200 and one 409") {
    for (int round = 0; round < 10; ++round) {
        TestServer server;
        RawClient host(server.port());
        const Created game = create_game(host, "Robert", 2);
        RawClient first(server.port());
        RawClient second(server.port());
        std::atomic<int> ready{0};
        Message a, b;
        const auto attempt = [&](RawClient& client, Message& reply) {
            ++ready;
            while (ready < 2) std::this_thread::yield();
            reply = join(client, "Sam", game.code);
        };
        std::thread t1(attempt, std::ref(first), std::ref(a));
        std::thread t2(attempt, std::ref(second), std::ref(b));
        t1.join();
        t2.join();
        const int wins = (a.lines[0] == "200 OK") + (b.lines[0] == "200 OK");
        const int clashes = (a.lines[0].substr(0, 3) == "409") + (b.lines[0].substr(0, 3) == "409");
        CHECK_EQ(wins, 1);
        CHECK_EQ(clashes, 1);
        CHECK(host.request("LEADERBOARD").lines ==
              std::vector<std::string>({"200 OK", "Robert:0", "Sam:0"}));
    }
}

TEST_CASE("T38 ending game A leaves game B running with its score intact") {
    Timings timings = fast_timings();
    timings.question = 2.0;
    TestServer server(timings);
    RawClient host_a(server.port());
    RawClient host_b(server.port());
    const Created a = create_game(host_a, "Ann", 3);
    const Created b = create_game(host_b, "Ben", 3);
    CHECK(a.code != b.code);
    RawClient player_a(server.port());
    RawClient player_b(server.port());
    join(player_a, "Amy", a.code);
    join(player_b, "Bea", b.code);

    host_b.send_line("START_ROUND");
    host_b.next();
    const Message question = host_b.next();
    player_b.next();
    CHECK_EQ(host_b.request(std::string("SUBMIT_ANSWER ") + correct_for(question)).lines[0],
             std::string("200 OK"));

    host_a.send_line("END_GAME");
    CHECK_EQ(host_a.next().lines[0], std::string("200 OK"));
    CHECK_EQ(host_a.next().lines[0], std::string("PUSH GAME_END HOST_ENDED"));
    CHECK_EQ(player_a.next().lines[0], std::string("PUSH GAME_END HOST_ENDED"));
    CHECK(player_a.wait_for_close());

    CHECK_EQ(player_b.request(std::string("SUBMIT_ANSWER ") + correct_for(question)).lines[0],
             std::string("200 OK"));
    Message result = host_b.next();
    while (!result.lines.empty() && !is_kind(result, "ROUND_RESULT")) result = host_b.next();
    CHECK(result.lines == std::vector<std::string>({"PUSH ROUND_RESULT 1",
                                                    std::string("Correct answer: ") +
                                                        correct_for(question),
                                                    "Ben:1", "Bea:1"}));
}

TEST_CASE("T39 leaderboard requests during pushes: every message is whole and blank-terminated") {
    Timings timings = fast_timings();
    timings.question = 0.15;
    timings.intermission = 0.05;
    timings.final_intermission = 0.1;
    TestServer server(timings);
    RawClient host(server.port());
    const Created game = create_game(host, "Robert", 5);
    std::vector<std::unique_ptr<RawClient>> players;
    for (const char* name : {"Bob", "Carol", "Dave"}) {
        players.emplace_back(new RawClient(server.port()));
        join(*players.back(), name, game.code);
    }
    host.send_line("START_ROUND");

    std::atomic<bool> stop{false};
    std::vector<std::thread> spammers;
    for (auto& player : players) {
        spammers.emplace_back([&, client = player.get()] {
            for (int i = 0; i < 100 && !stop; ++i) {
                client->send_line("LEADERBOARD");
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }
    // Each message is its lines then one blank line. A status or push line in
    // the middle of a message, or a stray blank line, means output interleaved.
    const std::string raw = host.read_all_until_close(4000);
    int messages = 0;
    bool whole = true;
    int lines_in_message = 0;
    std::istringstream in(raw);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            if (lines_in_message == 0) whole = false;
            ++messages;
            lines_in_message = 0;
            continue;
        }
        ++lines_in_message;
        const bool starts_message = line.rfind("PUSH ", 0) == 0 || line.rfind("200 ", 0) == 0;
        if (lines_in_message > 1 && starts_message) whole = false;
    }
    if (lines_in_message != 0) whole = false;
    stop = true;
    for (auto& spammer : spammers) spammer.join();
    CHECK(whole);
    CHECK(messages > 5);
}

TEST_CASE("T35 reply before push with a second player connected, 30 games") {
    TestServer server;
    for (int i = 0; i < 30; ++i) {
        RawClient host(server.port());
        const Created game = create_game(host, "Robert", 2);
        RawClient guest(server.port());
        join(guest, "Matthew", game.code);
        host.send_line("START_ROUND");
        const Message first = host.next();
        const Message second = host.next();
        CHECK(!first.is_push && first.lines[0] == "200 OK");
        CHECK(is_kind(second, "QUESTION 1"));
        CHECK(is_kind(guest.next(), "QUESTION 1"));
        host.send_line("END_GAME");
        host.wait_for_close();
    }
}

TEST_CASE("T45 three idle connections do not delay a fourth client's new game") {
    TestServer server;
    RawClient idle1(server.port());
    RawClient idle2(server.port());
    RawClient idle3(server.port());
    RawClient fourth(server.port());
    const auto start = std::chrono::steady_clock::now();
    const Created game = create_game(fourth, "Robert", 2);
    CHECK_EQ(game.code.size(), static_cast<size_t>(4));
    CHECK(seconds_since(start) < 0.5);
}

TEST_CASE("T45 twenty healthy players: replies under 500 ms, questions under 1 s") {
    Timings timings = fast_timings();
    timings.question = 2.0;
    TestServer server(timings);
    RawClient host(server.port());
    const Created game = create_game(host, "Host", 2);
    std::vector<std::unique_ptr<RawClient>> players;
    for (int i = 0; i < 19; ++i) {
        players.emplace_back(new RawClient(server.port()));
        CHECK_EQ(join(*players.back(), "P" + std::to_string(i), game.code).lines[0],
                 std::string("200 OK"));
    }
    const auto board_start = std::chrono::steady_clock::now();
    CHECK_EQ(host.request("LEADERBOARD").lines.size(), static_cast<size_t>(21));
    CHECK(seconds_since(board_start) < 0.5);

    const auto start = std::chrono::steady_clock::now();
    host.send_line("START_ROUND");
    CHECK_EQ(host.next().lines[0], std::string("200 OK"));
    CHECK(seconds_since(start) < 0.5);
    for (auto& player : players) CHECK(is_kind(player->next(), "QUESTION 1"));
    CHECK(seconds_since(start) < 1.0);
}

TEST_CASE("T46 a dropped player reconnects with score kept while others play on") {
    Timings timings = fast_timings();
    timings.question = 1.5;
    timings.reconnect = 3.0;
    TestServer server(timings);
    RawClient host(server.port());
    const Created game = create_game(host, "Robert", 3);
    auto bob = std::make_unique<RawClient>(server.port());
    const Message joined = join(*bob, "Bob", game.code);
    const std::string bob_token = joined.lines[2];
    host.send_line("START_ROUND");
    host.next();
    const Message question = host.next();
    bob->next();
    CHECK_EQ(bob->request(std::string("SUBMIT_ANSWER ") + correct_for(question)).lines[0],
             std::string("200 OK"));
    bob->kill();
    CHECK_EQ(host.request(std::string("SUBMIT_ANSWER ") + correct_for(question)).lines[0],
             std::string("200 OK"));
    Message result = host.next();
    CHECK(is_kind(result, "ROUND_RESULT 1"));
    CHECK(result.lines == std::vector<std::string>({"PUSH ROUND_RESULT 1",
                                                    std::string("Correct answer: ") +
                                                        correct_for(question),
                                                    "Robert:1", "Bob:1"}));
    RawClient again(server.port());
    const Message back = again.request("RECONNECT Bob " + game.code + " " + bob_token);
    CHECK(back.lines == std::vector<std::string>({"200 OK", "1 1"}));
    CHECK(host.request("LEADERBOARD").lines ==
          std::vector<std::string>({"200 OK", "Robert:1", "Bob:1"}));
}

TEST_CASE("T40 stalled game-A readers do not delay game B") {
    TestServer server;
    RawClient host_a(server.port());
    const Created a = create_game(host_a, "Ann", 3);
    RawClient host_b(server.port());
    const Created b = create_game(host_b, "Ben", 3);
    RawClient player_b(server.port());
    join(player_b, "Bea", b.code);

    // Many long names make each LEADERBOARD reply about 1 KB, so a few hundred
    // unread requests fill the server's send buffer.
    std::vector<std::unique_ptr<RawClient>> crowd;
    for (int i = 0; i < 40; ++i) {
        crowd.emplace_back(new RawClient(server.port()));
        join(*crowd.back(), "Crowd_Player_Nr_" + std::to_string(i), a.code);
    }
    std::vector<int> stalled;
    for (int i = 0; i < 3; ++i) {
        stalled.push_back(connect_stalled(server.port()));
        send_blocking(stalled.back(), "JOIN_GAME " + a.code + " S" + std::to_string(i) + "\n");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Each stalled client floods LEADERBOARD without reading, so the server's
    // reply writes to it block until the 200 ms send timeout.
    std::atomic<int> blocked{0};
    std::atomic<bool> go{false};
    std::vector<std::thread> flooders;
    for (const int fd : stalled) {
        flooders.emplace_back([&, fd] {
            std::string flood;
            while (flood.size() < 64 * 1024) flood += "LEADERBOARD\n";
            bool counted = false;
            auto last_progress = std::chrono::steady_clock::now();
            const auto start = last_progress;
            while (!go && seconds_since(start) < 8.0) {
                const ssize_t n = send(fd, flood.data(), flood.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
                if (n > 0) {
                    last_progress = std::chrono::steady_clock::now();
                    continue;
                }
                // A handler that is reading takes bytes within microseconds, so
                // no progress for 100 ms means its reply write is stuck.
                if (!counted && seconds_since(last_progress) > 0.1) {
                    counted = true;
                    ++blocked;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
    }
    const auto wait_start = std::chrono::steady_clock::now();
    while (blocked < 3 && seconds_since(wait_start) < 8.0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_EQ(blocked.load(), 3);

    double slowest = 0;
    for (int i = 0; i < 12; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const auto start = std::chrono::steady_clock::now();
        const Message board = host_b.request("LEADERBOARD");
        slowest = std::max(slowest, seconds_since(start));
        CHECK(board.lines == std::vector<std::string>({"200 OK", "Ben:0", "Bea:0"}));
    }
    CHECK(slowest < 0.5);

    go = true;
    for (auto& flooder : flooders) flooder.join();
    for (const int fd : stalled) close(fd);
}

TEST_CASE("T44 stop with idle and playing clients ends within a bound and logs one shutdown") {
    TestServer server;
    RawClient idle1(server.port());
    RawClient idle2(server.port());
    RawClient host(server.port());
    const Created game = create_game(host, "Robert", 3);
    RawClient bob(server.port());
    join(bob, "Bob", game.code);
    host.send_line("START_ROUND");
    host.next();
    const auto start = std::chrono::steady_clock::now();
    server.stop();
    CHECK(seconds_since(start) < 1.5);
    CHECK(idle1.wait_for_close());
    CHECK(bob.wait_for_close());
    const std::string log = server.log_text();
    const std::string line = " INFO server shutting down\n";
    size_t shutdowns = 0;
    for (size_t at = log.find(line); at != std::string::npos; at = log.find(line, at + 1)) {
        ++shutdowns;
    }
    CHECK_EQ(shutdowns, static_cast<size_t>(1));
}

TEST_CASE("T44 finished handlers and game timers are reaped, not retained") {
    Timings timings = fast_timings();
    timings.question = 0.05;
    timings.final_intermission = 0.05;
    TestServer server(timings);
    for (int i = 0; i < 12; ++i) {
        RawClient host(server.port());
        create_game(host, "Robert", 1);
        host.send_line("END_GAME");
        host.next();
        host.wait_for_close();
    }
    for (int i = 0; i < 12; ++i) {
        RawClient quick(server.port());  // connects and leaves
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    RawClient extra(server.port());  // one more accept reaps what has finished
    CHECK(create_game(extra, "Robert", 1).code.size() == 4);
    CHECK(server.server().worker_count() <= 4);
}

TEST_CASE("T44 a handler thread that cannot start closes that client and the server goes on") {
    std::atomic<int> failures_left{1};
    TestServer server(fast_timings(), 6, 1234, [&](WorkerKind kind) {
        return kind == WorkerKind::Handler && failures_left.fetch_sub(1) > 0;
    });
    RawClient unlucky(server.port());
    unlucky.send_line("CREATE_GAME Robert 2");
    CHECK(unlucky.wait_for_close());
    RawClient next(server.port());
    CHECK(create_game(next, "Bob", 2).code.size() == 4);
    CHECK(server.log_text().find("cannot start a handler thread") != std::string::npos);
}

TEST_CASE("T44 a game timer that cannot start: no 200, no leftover game, server still accepts") {
    std::atomic<int> failures_left{1};
    TestServer server(fast_timings(), 6, 1234, [&](WorkerKind kind) {
        return kind == WorkerKind::GameTimer && failures_left.fetch_sub(1) > 0;
    });
    RawClient unlucky(server.port());
    unlucky.send_line("CREATE_GAME Robert 2");
    CHECK(unlucky.next(1000).lines.empty());
    CHECK(unlucky.wait_for_close());
    RawClient next(server.port());
    const Created game = create_game(next, "Bob", 2);
    CHECK_EQ(game.code.size(), static_cast<size_t>(4));
    CHECK(next.request("LEADERBOARD").lines == std::vector<std::string>({"200 OK", "Bob:0"}));
    CHECK(server.log_text().find("cannot start a game timer thread") != std::string::npos);
}

TEST_CASE("T44 a join racing a rolled-back creation cannot enter the doomed game") {
    // The same seed gives the same first join code, so the joiner can aim at it.
    GameManager probe(make_bank(6), fast_timings(), 1234);
    const ParseResult create = parse_command("CREATE_GAME Robert 2", 6);
    const std::string doomed = probe.handle(1, create.command, 0).effects[0].lines[1];

    std::atomic<bool> joiner_ready{false};
    TestServer server(fast_timings(), 6, 1234, [&](WorkerKind kind) {
        if (kind != WorkerKind::GameTimer) return false;
        joiner_ready = true;
        // Give the joiner time to reach the new game's lane before rollback.
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        return true;
    });
    RawClient creator(server.port());
    RawClient joiner(server.port());
    creator.send_line("CREATE_GAME Robert 2");
    while (!joiner_ready) std::this_thread::yield();
    joiner.send_line("JOIN_GAME " + doomed + " Sam");
    const Message join_reply = joiner.next(2000);
    CHECK_EQ(join_reply.lines[0].substr(0, 3), std::string("404"));
    CHECK(creator.next(500).lines.empty());
}

TEST_CASE("T42 socket: host drops from a lobby, the player gets HOST_EXPIRED then EOF, old tokens die") {
    TestServer server;  // 1.0 s reconnect window
    RawClient host(server.port());
    const Created game = create_game(host, "Robert", 3);
    RawClient bob(server.port());
    const Message bob_join = join(bob, "Bob", game.code);
    host.kill();
    const Message end = bob.next(3000);
    CHECK(end.lines == std::vector<std::string>({"PUSH GAME_END HOST_EXPIRED", "FINAL RESULTS", "Bob:0"}));
    CHECK(bob.wait_for_close());
    RawClient late(server.port());
    CHECK_EQ(late.request("RECONNECT Robert " + game.code + " " + game.token).lines[0].substr(0, 3),
             std::string("410"));
    CHECK_EQ(late.request("RECONNECT Bob " + game.code + " " + bob_join.lines[2]).lines[0].substr(0, 3),
             std::string("410"));
    CHECK(server.log_text().find("ended: HOST_EXPIRED") != std::string::npos);
}

TEST_CASE("T42 socket: an all-pending lobby is removed when the host's window ends") {
    TestServer server;
    RawClient host(server.port());
    const Created game = create_game(host, "Robert", 3);
    RawClient bob(server.port());
    const Message bob_join = join(bob, "Bob", game.code);
    host.kill();
    bob.kill();
    std::this_thread::sleep_for(std::chrono::milliseconds(1400));
    RawClient late(server.port());
    CHECK_EQ(late.request("RECONNECT Bob " + game.code + " " + bob_join.lines[2]).lines[0].substr(0, 3),
             std::string("410"));
    CHECK_EQ(join(late, "Sam", game.code).lines[0].substr(0, 3), std::string("404"));
}

TEST_CASE("T47 socket: host expires during play, the game still reaches COMPLETE") {
    Timings timings = fast_timings();
    timings.question = 0.4;
    timings.reconnect = 0.5;
    TestServer server(timings);
    RawClient host(server.port());
    const Created game = create_game(host, "Robert", 3);
    RawClient bob(server.port());
    const Message bob_join = join(bob, "Bob", game.code);
    host.send_line("START_ROUND");
    host.next();
    host.next();
    host.kill();

    bool saw_expired = false;
    Message last;
    while (true) {
        Message message;
        if (bob.read_message(message, 4000) != RawClient::Read::Message) break;
        saw_expired = saw_expired || is_kind(message, "GAME_END HOST_EXPIRED");
        last = message;
    }
    CHECK(!saw_expired);
    CHECK(last.lines == std::vector<std::string>({"PUSH GAME_END COMPLETE", "FINAL RESULTS", "Bob:0"}));
    RawClient late(server.port());
    CHECK_EQ(late.request("RECONNECT Robert " + game.code + " " + game.token).lines[0].substr(0, 3),
             std::string("410"));
}

TEST_CASE("T43 socket: a join in the final intermission gets no question and GAME_END COMPLETE") {
    Timings timings = fast_timings();
    timings.final_intermission = 0.8;
    TestServer server(timings);
    RawClient host(server.port());
    const Created game = create_game(host, "Robert", 1);
    host.send_line("START_ROUND");
    host.next();
    const Message question = host.next();
    host.send_line(std::string("SUBMIT_ANSWER ") + correct_for(question));
    host.next();
    CHECK(is_kind(host.next(), "ROUND_RESULT 1"));

    RawClient late(server.port());
    CHECK_EQ(join(late, "Late", game.code).lines[0], std::string("200 OK"));
    const Message end = late.next(3000);
    CHECK(end.lines == std::vector<std::string>({"PUSH GAME_END COMPLETE", "FINAL RESULTS", "Robert:1", "Late:0"}));
    CHECK(late.wait_for_close());
}
