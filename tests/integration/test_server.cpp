// Slice 5: the real server over real sockets with fast timers.
#include <sys/socket.h>

#include <atomic>

#include "integration/test_support.h"
#include "minitest.h"

using namespace support;

namespace {

bool is_kind(const Message& message, const std::string& kind) {
    return message.is_push && !message.lines.empty() &&
           message.lines[0].rfind("PUSH " + kind, 0) == 0;
}

std::string joined(const Message& message) {
    std::string text;
    for (const auto& line : message.lines) text += line + "\n";
    return text;
}

}  // namespace

TEST_CASE("T01 T02 create then start: reply first, first question within 1 second") {
    TestServer server;
    RawClient host(server.port());
    const Created game = create_game(host, "Robert", 3);
    CHECK_EQ(game.code.size(), static_cast<size_t>(4));
    const auto start = std::chrono::steady_clock::now();
    host.send_line("START_ROUND");
    const Message reply = host.next();
    const double reply_seconds = seconds_since(start);
    CHECK(!reply.is_push);
    CHECK_EQ(reply.lines[0], std::string("200 OK"));
    const Message question = host.next();
    CHECK(is_kind(question, "QUESTION 1 "));
    CHECK_EQ(question.lines.size(), static_cast<size_t>(5));
    CHECK(seconds_since(start) < 1.0);
    CHECK(reply_seconds < 0.5);
}

TEST_CASE("T03 answering closes the round at once with the result after the 200") {
    TestServer server;
    RawClient host(server.port());
    create_game(host, "Robert", 3);
    host.send_line("START_ROUND");
    host.next();
    const Message question = host.next();
    const auto start = std::chrono::steady_clock::now();
    host.send_line(std::string("SUBMIT_ANSWER ") + correct_for(question));
    const Message ok = host.next();
    CHECK_EQ(ok.lines[0], std::string("200 OK"));
    const Message result = host.next();
    CHECK(is_kind(result, "ROUND_RESULT 1"));
    CHECK_EQ(result.lines[2], std::string("Robert:1"));
    CHECK(seconds_since(start) < 0.4);  // well before the 0.6 second timer
}

TEST_CASE("T04 round closes on the timer with the score unchanged, next question after the intermission") {
    TestServer server;
    RawClient host(server.port());
    create_game(host, "Robert", 3);
    host.send_line("START_ROUND");
    host.next();
    const Message question = host.next();
    CHECK(is_kind(question, "QUESTION 1"));
    const auto opened = std::chrono::steady_clock::now();
    const Message result = host.next();
    const double round_seconds = seconds_since(opened);
    CHECK(is_kind(result, "ROUND_RESULT 1"));
    CHECK_EQ(result.lines[2], std::string("Robert:0"));
    CHECK(round_seconds > 0.5 && round_seconds < 0.8);
    const auto closed = std::chrono::steady_clock::now();
    const Message next = host.next();
    const double gap = seconds_since(closed);
    CHECK(is_kind(next, "QUESTION 2"));
    CHECK(gap > 0.15 && gap < 0.45);
}

TEST_CASE("T06 leaderboard over the socket") {
    TestServer server;
    RawClient host(server.port());
    create_game(host, "Robert", 3);
    const Message board = host.request("LEADERBOARD");
    CHECK(board.lines == std::vector<std::string>({"200 OK", "Robert:0"}));
}

TEST_CASE("T07 game ends with COMPLETE, the connection closes, and the server keeps listening") {
    TestServer server;
    RawClient host(server.port());
    create_game(host, "Robert", 1);
    host.send_line("START_ROUND");
    host.next();
    const Message question = host.next();
    host.send_line(std::string("SUBMIT_ANSWER ") + correct_for(question));
    host.next();
    host.next();  // ROUND_RESULT
    const auto closed = std::chrono::steady_clock::now();
    const Message end = host.next();
    CHECK(seconds_since(closed) > 0.2);
    CHECK(end.lines == std::vector<std::string>({"PUSH GAME_END COMPLETE", "FINAL RESULTS", "Robert:1"}));
    CHECK(host.wait_for_close());

    RawClient again(server.port());
    CHECK(create_game(again, "Robert", 1).code.size() == 4);
}

TEST_CASE("T08 END_GAME: 200, GAME_END HOST_ENDED, close, no more questions, server accepts new clients") {
    TestServer server;
    RawClient host(server.port());
    create_game(host, "Robert", 3);
    host.send_line("START_ROUND");
    host.next();
    host.next();
    host.send_line("END_GAME");
    CHECK_EQ(host.next().lines[0], std::string("200 OK"));
    CHECK_EQ(host.next().lines[0], std::string("PUSH GAME_END HOST_ENDED"));
    CHECK(host.wait_for_close());
    RawClient again(server.port());
    CHECK(create_game(again, "Bob", 2).code.size() == 4);
}

TEST_CASE("T09 host QUIT: exactly 200 OK, a blank line, then EOF; the old token is dead") {
    TestServer server;
    RawClient host(server.port());
    const Created game = create_game(host, "Robert", 3);
    host.send_line("QUIT");
    CHECK_EQ(host.read_all_until_close(), std::string("200 OK\n\n"));
    RawClient again(server.port());
    const Message reply = again.request("RECONNECT Robert " + game.code + " " + game.token);
    CHECK_EQ(reply.lines[0].substr(0, 3), std::string("410"));
}

TEST_CASE("T14 T15 empty and unknown lines give 300 and the connection stays usable") {
    TestServer server;
    RawClient client(server.port());
    CHECK_EQ(client.request("").lines[0], std::string("300 message format error"));
    CHECK_EQ(client.request("HELLO").lines[0], std::string("300 message format error"));
    CHECK_EQ(client.request("LEADERBOARD").lines[0].substr(0, 3), std::string("401"));
}

TEST_CASE("T16 bad CREATE_GAME arguments") {
    TestServer server;
    RawClient client(server.port());
    for (const char* line : {"CREATE_GAME Robert abc", "CREATE_GAME Robert 0",
                             "CREATE_GAME Robert 9999", "CREATE_GAME Robert"}) {
        CHECK_EQ(client.request(line).lines[0],
                 std::string("423 Requested question count must be between 1 and 6."));
    }
    CHECK_EQ(client.request("CREATE_GAME").lines[0], std::string("300 message format error"));
    CHECK_EQ(client.request("CREATE_GAME Robert 3 4").lines[0], std::string("300 message format error"));
    CHECK_EQ(client.request("LEADERBOARD").lines[0].substr(0, 3), std::string("401"));  // no game made
}

TEST_CASE("T17 T18 T19 T20 state errors over the socket") {
    TestServer server;
    RawClient client(server.port());
    CHECK_EQ(client.request("JOIN_GAME ZZZZ Matthew").lines[0].substr(0, 3), std::string("404"));
    for (const char* line : {"LEADERBOARD", "SUBMIT_ANSWER A", "START_ROUND"}) {
        CHECK_EQ(client.request(line).lines[0].substr(0, 3), std::string("401"));
    }
    create_game(client, "Robert", 3);
    CHECK_EQ(client.request("SUBMIT_ANSWER A").lines[0].substr(0, 3), std::string("420"));
    CHECK_EQ(client.request("START_ROUND").lines[0], std::string("200 OK"));
    client.next();  // question 1
    CHECK_EQ(client.request("START_ROUND").lines[0].substr(0, 3), std::string("421"));
    CHECK_EQ(client.request("SUBMIT_ANSWER E").lines[0], std::string("300 message format error"));
    CHECK_EQ(client.request("SUBMIT_ANSWER A").lines[0], std::string("200 OK"));
}

TEST_CASE("T21 600 character line gives one 300, then the next command works") {
    TestServer server;
    RawClient client(server.port());
    client.send(std::string(600, 'x') + "\nLEADERBOARD\n");
    CHECK_EQ(client.next().lines[0], std::string("300 message format error"));
    CHECK_EQ(client.next().lines[0].substr(0, 3), std::string("401"));
    // 512 bytes with the newline is still fine, 513 is not.
    CHECK_EQ(client.request(std::string(511, 'y')).lines[0], std::string("300 message format error"));
}

TEST_CASE("T31 command split across two sends") {
    TestServer server;
    RawClient client(server.port());
    client.send("CREATE_GAME Rob");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    client.send("ert 3\n");
    const Message reply = client.next();
    CHECK_EQ(reply.lines.size(), static_cast<size_t>(3));
    CHECK_EQ(reply.lines[0], std::string("200 OK"));
    CHECK(client.next(200).lines.empty());  // exactly one reply
}

TEST_CASE("T32 two commands in one send give two replies in order") {
    TestServer server;
    RawClient client(server.port());
    create_game(client, "Robert", 3);
    client.send("LEADERBOARD\nLEADERBOARD\n");
    CHECK(client.next().lines == std::vector<std::string>({"200 OK", "Robert:0"}));
    CHECK(client.next().lines == std::vector<std::string>({"200 OK", "Robert:0"}));
}

TEST_CASE("T33 one byte at a time gives the same reply") {
    TestServer server;
    RawClient client(server.port());
    for (char c : std::string("CREATE_GAME Robert 3\n")) {
        client.send(std::string(1, c));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const Message reply = client.next();
    CHECK_EQ(reply.lines.size(), static_cast<size_t>(3));
    CHECK_EQ(reply.lines[0], std::string("200 OK"));
}

TEST_CASE("T35 reply is written before the first question, 50 fresh games") {
    TestServer server;
    int in_order = 0;
    for (int i = 0; i < 50; ++i) {
        RawClient host(server.port());
        create_game(host, "Robert", 2);
        host.send_line("START_ROUND");
        const Message first = host.next();
        const Message second = host.next();
        if (!first.is_push && first.lines[0] == "200 OK" && is_kind(second, "QUESTION 1 ")) {
            ++in_order;
        }
        host.send_line("END_GAME");
        host.wait_for_close();
    }
    CHECK_EQ(in_order, 50);
}

TEST_CASE("T26 T27 killed client can reconnect inside the window and gets the live question") {
    TestServer server;
    Created game;
    {
        RawClient host(server.port());
        game = create_game(host, "Robert", 3);
        host.send_line("START_ROUND");
        host.next();
        host.next();
        host.kill();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    RawClient again(server.port());  // server still listening after the kill
    std::vector<Message> pushes;
    const Message reply = again.request("RECONNECT Robert " + game.code + " " + game.token, &pushes);
    CHECK_EQ(reply.lines[0], std::string("200 OK"));
    CHECK_EQ(reply.lines[1], std::string("0 1"));
    const Message question = again.next();
    CHECK(is_kind(question, "QUESTION 1 "));
    CHECK_EQ(again.request("SUBMIT_ANSWER A").lines[0], std::string("200 OK"));
}

TEST_CASE("T28 reconnect rejections are identical 410 replies, including after the window") {
    TestServer server;
    Created game;
    {
        RawClient host(server.port());
        game = create_game(host, "Robert", 3);
        host.kill();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::vector<std::string> replies;
    {
        RawClient attacker(server.port());
        replies.push_back(joined(attacker.request("RECONNECT Robert " + game.code + " ZZZZ")));
        replies.push_back(joined(attacker.request("RECONNECT Roberta " + game.code + " " + game.token)));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));  // window is 1.0 second
    RawClient late(server.port());
    replies.push_back(joined(late.request("RECONNECT Robert " + game.code + " " + game.token)));
    CHECK_EQ(replies[0], replies[1]);
    CHECK_EQ(replies[1], replies[2]);
    CHECK_EQ(replies[0].substr(0, 3), std::string("410"));
}

TEST_CASE("T30 host disconnect does not end the game; the host returns with host powers") {
    Timings timings = fast_timings();
    timings.reconnect = 5;
    TestServer server(timings);
    Created game;
    {
        RawClient host(server.port());
        game = create_game(host, "Robert", 3);
        host.send_line("START_ROUND");
        host.next();
        host.next();
        host.kill();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // rounds keep going
    RawClient again(server.port());
    std::vector<Message> pushes;
    const Message reply = again.request("RECONNECT Robert " + game.code + " " + game.token, &pushes);
    CHECK_EQ(reply.lines[0], std::string("200 OK"));
    CHECK(reply.lines[1] != "0 1");  // the game moved past question 1 meanwhile
    CHECK_EQ(again.request("END_GAME").lines[0], std::string("200 OK"));
}

TEST_CASE("Slice5 a second client waits in the backlog until the first leaves") {
    TestServer server;
    RawClient first(server.port());
    create_game(first, "Robert", 2);
    RawClient second(server.port());
    second.send_line("CREATE_GAME Bob 2");
    CHECK(second.next(300).lines.empty());  // not served yet
    first.kill();
    const Message reply = second.next();
    CHECK_EQ(reply.lines[0], std::string("200 OK"));
}

TEST_CASE("Slice5 a client reset in the middle of a request does not hurt the server") {
    TestServer server;
    {
        RawClient client(server.port());
        client.send("CREATE_GAME Rob");
        client.kill();
    }
    RawClient next(server.port());
    CHECK(create_game(next, "Bob", 2).code.size() == 4);
}

TEST_CASE("Slice5 stop wakes a server blocked in accept") {
    TestServer server;
    const auto start = std::chrono::steady_clock::now();
    server.stop();
    CHECK(seconds_since(start) < 1.0);
    CHECK(server.log_text().find("server shutting down") != std::string::npos);
}

TEST_CASE("Slice5 stop wakes a handler blocked in recv without any protocol push") {
    TestServer server;
    RawClient client(server.port());
    create_game(client, "Robert", 3);
    const auto start = std::chrono::steady_clock::now();
    server.stop();
    CHECK(seconds_since(start) < 1.0);
    CHECK(client.wait_for_close());
    const std::string log = server.log_text();
    CHECK(log.find("server shutting down") != std::string::npos);
}

TEST_CASE("Slice5 timer GAME_END reaches an idle client before EOF") {
    TestServer server;
    RawClient host(server.port());
    create_game(host, "Robert", 1);
    host.send_line("START_ROUND");
    host.next();
    host.next();
    CHECK(is_kind(host.next(), "ROUND_RESULT 1"));
    const Message end = host.next();
    CHECK_EQ(end.lines[0], std::string("PUSH GAME_END COMPLETE"));
    CHECK(host.wait_for_close());
}

TEST_CASE("Slice5 commands racing the timer GAME_END: complete GAME_END arrives before EOF") {
    Timings timings = fast_timings();
    timings.question = 0.1;
    timings.final_intermission = 0.1;
    for (int round = 0; round < 15; ++round) {
        TestServer server(timings);
        RawClient host(server.port());
        create_game(host, "Robert", 1);
        host.send_line("START_ROUND");
        // Spam commands until the server closes; a half-written message must never appear.
        std::vector<Message> seen;
        std::atomic<bool> stop{false};
        std::thread spammer([&] {
            for (int i = 0; i < 400 && !stop; ++i) {
                host.send_line("LEADERBOARD");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        bool closed = false;
        while (!closed) {
            Message message;
            const auto status = host.read_message(message, 3000);
            if (status == RawClient::Read::Message) seen.push_back(message);
            else closed = true;
        }
        stop = true;
        spammer.join();
        CHECK(!seen.empty());
        CHECK_EQ(seen.back().lines[0], std::string("PUSH GAME_END COMPLETE"));
        for (const auto& message : seen) CHECK(!message.lines.empty());
    }
}

TEST_CASE("Slice5 reconnect racing the round close never shows QUESTION n after ROUND_RESULT n") {
    Timings timings = fast_timings();
    timings.question = 0.05;
    timings.intermission = 0.02;
    timings.final_intermission = 0.05;
    timings.reconnect = 5;
    TestServer server(timings);
    Created game;
    {
        RawClient host(server.port());
        game = create_game(host, "Robert", 6);
        host.send_line("START_ROUND");
        host.next();
        host.next();
        host.kill();
    }
    bool ordered = true;
    int last_result = 0;
    for (int attempt = 0; attempt < 40; ++attempt) {
        RawClient again(server.port());
        std::vector<Message> pushes;
        again.send_line("RECONNECT Robert " + game.code + " " + game.token);
        const Message reply = again.next();
        if (reply.lines.empty() || reply.lines[0] != "200 OK") break;  // game finished
        // Watch pushes briefly, then drop the connection again.
        for (int i = 0; i < 4; ++i) {
            const Message push = again.next(40);
            if (push.lines.empty() || is_kind(push, "GAME_END")) break;
            const int n = std::stoi(push.lines[0].substr(push.lines[0].find(' ', 5) + 1));
            if (is_kind(push, "ROUND_RESULT")) last_result = n;
            if (is_kind(push, "QUESTION") && n <= last_result) ordered = false;
        }
        again.kill();
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    CHECK(ordered);
}

TEST_CASE("Slice5 submit racing the early close: the 200 always precedes ROUND_RESULT") {
    TestServer server;
    int correct_order = 0;
    for (int i = 0; i < 40; ++i) {
        RawClient host(server.port());
        create_game(host, "Robert", 2);
        host.send_line("START_ROUND");
        host.next();
        const Message question = host.next();
        host.send_line(std::string("SUBMIT_ANSWER ") + correct_for(question));
        const Message first = host.next();
        const Message second = host.next();
        if (!first.is_push && first.lines[0] == "200 OK" && is_kind(second, "ROUND_RESULT 1")) {
            ++correct_order;
        }
        host.send_line("END_GAME");
        host.wait_for_close();
    }
    CHECK_EQ(correct_order, 40);
}

TEST_CASE("Slice5 rapid games on reused sockets never get a stale push or double close") {
    Timings timings = fast_timings();
    timings.question = 0.05;
    timings.intermission = 0.02;
    timings.final_intermission = 0.05;
    TestServer server(timings);
    for (int i = 0; i < 30; ++i) {
        RawClient host(server.port());
        // The very first message on a fresh connection must be our own reply.
        host.send_line("CREATE_GAME Robert 1");
        const Message reply = host.next();
        CHECK(!reply.is_push && reply.lines.size() == 3);
        host.send_line("START_ROUND");
        CHECK_EQ(host.next().lines[0], std::string("200 OK"));
        if (i % 2 == 0) {
            host.wait_for_close(2000);  // game ends by itself
        } else {
            host.send_line("QUIT");
            host.wait_for_close(2000);
        }
    }
    RawClient last(server.port());
    CHECK(create_game(last, "Robert", 1).code.size() == 4);
}

TEST_CASE("T13 the server log records connections, requests, errors, and disconnects, without tokens") {
    TestServer server;
    Created game;
    {
        RawClient host(server.port());
        game = create_game(host, "Robert", 3);
        host.request("HELLO");
        host.request("RECONNECT Robert " + game.code + " ZZZZ");
        host.kill();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    RawClient again(server.port());
    again.request("RECONNECT Robert " + game.code + " " + game.token);
    again.send_line("QUIT");
    again.wait_for_close();
    const std::string log = server.log_text();
    for (const char* expected : {"listening on", "client 1 connected from 127.0.0.1",
                                 "client 1 request CREATE_GAME -> 200", "client 1 protocol error 300",
                                 "client 1 request RECONNECT -> 300",
                                 "client 1 disconnected", "client 2 request RECONNECT -> 200",
                                 "client 2 request QUIT -> 200", "server shutting down"}) {
        CHECK(log.find(expected) != std::string::npos);
    }
    CHECK(log.find(game.token) == std::string::npos);
}

TEST_CASE("Slice5 a client that stops reading is dropped by the send timeout and the next client is served") {
    TestServer server;
    const int stalled = socket(AF_INET, SOCK_STREAM, 0);
    int small = 4096;
    setsockopt(stalled, SOL_SOCKET, SO_RCVBUF, &small, sizeof(small));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(server.port()));
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    CHECK_EQ(connect(stalled, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);

    // Every HELLO draws an error reply; never reading them fills the server's
    // send buffer until a write blocks for the 200 ms timeout.
    std::string flood;
    while (flood.size() < 256 * 1024) flood += "HELLO\n";
    const auto start = std::chrono::steady_clock::now();
    size_t sent_total = 0;
    while (seconds_since(start) < 5.0 && sent_total < 64 * flood.size()) {
        const ssize_t sent = send(stalled, flood.data(), flood.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent > 0) sent_total += static_cast<size_t>(sent);
        else std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // The stalled client holds the only handler, so this one is served only
    // after the timeout removed the stalled connection.
    RawClient next(server.port());
    CHECK(next.connected());
    const Created game = create_game(next, "Robert", 1);
    CHECK_EQ(game.code.size(), static_cast<size_t>(4));
    CHECK(seconds_since(start) < 6.0);
    close(stalled);

    const std::string log = server.log_text();
    CHECK(log.find("client 1 disconnected: send failed") != std::string::npos);
    CHECK(log.find("client 2 request CREATE_GAME -> 200") != std::string::npos);
}
