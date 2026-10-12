// Slice 6: the real Client class against a fake server and a scripted stdin.
#include <fcntl.h>
#include <sys/stat.h>

#include <atomic>
#include <fstream>

#include "client/client.h"
#include "client/client_ui.h"
#include "client/session_file.h"
#include "integration/test_support.h"
#include "minitest.h"

using namespace support;

namespace {

// Thread-safe text sink for what the client prints.
class Output {
public:
    void add(const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        text_ += text;
    }
    std::string text() {
        std::lock_guard<std::mutex> lock(mutex_);
        return text_;
    }
    bool wait_for(const std::string& needle, int timeout_ms = 2000) {
        const auto start = std::chrono::steady_clock::now();
        while (seconds_since(start) * 1000 < timeout_ms) {
            if (text().find(needle) != std::string::npos) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }
    int count(const std::string& needle) {
        const std::string all = text();
        int found = 0;
        for (size_t at = all.find(needle); at != std::string::npos; at = all.find(needle, at + 1)) {
            ++found;
        }
        return found;
    }

private:
    std::mutex mutex_;
    std::string text_;
};

int listen_on_loopback(int& port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    listen(fd, 4);
    socklen_t length = sizeof(address);
    getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length);
    port = ntohs(address.sin_port);
    return fd;
}

int connect_loopback(int port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    return fd;
}

// A client under test, wired to a fake server and a stdin pipe.
class Harness {
public:
    explicit Harness(const std::string& name)
        : session_path("/tmp/termtrivia_client_test_" + name + ".session") {
        std::remove(session_path.c_str());
    }

    void start() {
        int port = 0;
        const int listen_fd = listen_on_loopback(port);
        const int client_fd = connect_loopback(port);
        peer.reset(new Connection(accept(listen_fd, nullptr, nullptr)));
        close(listen_fd);
        pipe(stdin_pipe);
        ClientOptions options;
        options.session_path = session_path;
        options.input_fd = stdin_pipe[0];
        options.print = [this](const std::string& text) { output.add(text); };
        thread = std::thread([this, client_fd, options] {
            Client client(client_fd, options);
            exit_code = client.run();
            done = true;
        });
    }

    ~Harness() {
        close(stdin_pipe[1]);
        peer.reset();
        if (thread.joinable()) thread.join();
        close(stdin_pipe[0]);
        std::remove(session_path.c_str());
    }

    void type(const std::string& text) {
        ssize_t ignored = write(stdin_pipe[1], text.data(), text.size());
        (void)ignored;
    }
    // The next line the client sent to the server ("" on timeout).
    std::string server_reads(int timeout_ms = 2000) {
        std::string line;
        if (peer->read_line(line, timeout_ms) != Connection::ReadStatus::Line) return "";
        return line;
    }
    void server_sends(const std::string& bytes) { peer->send_raw(bytes); }
    bool wait_done(int timeout_ms = 2000) {
        const auto start = std::chrono::steady_clock::now();
        while (seconds_since(start) * 1000 < timeout_ms) {
            if (done) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    std::string session_path;
    Output output;
    std::unique_ptr<Connection> peer;
    int stdin_pipe[2] = {-1, -1};
    std::thread thread;
    std::atomic<int> exit_code{-1};
    std::atomic<bool> done{false};
};

}  // namespace

TEST_CASE("T12 the client lists all commands on connect and shows formats at the prompt") {
    Harness h("t12");
    h.start();
    CHECK(h.output.wait_for("> "));
    const std::string text = h.output.text();
    for (const char* command : {"CREATE_GAME <name> <count>", "JOIN_GAME <code> <name>", "START_ROUND",
                                "SUBMIT_ANSWER <A|B|C|D>", "LEADERBOARD", "RECONNECT", "END_GAME",
                                "QUIT", "help"}) {
        CHECK(text.find(command) != std::string::npos);
    }
}

TEST_CASE("T12 an invalid choice gives a message and a new prompt, not an exit") {
    Harness h("t12b");
    h.start();
    h.output.wait_for("> ");
    h.type("banana\n");
    CHECK(h.output.wait_for("Unknown command"));
    h.type("create_game Robert\n");
    CHECK(h.output.wait_for("Usage: CREATE_GAME <name> <count>"));
    h.type("SUBMIT_ANSWER E\n");
    CHECK(h.output.wait_for("Usage: SUBMIT_ANSWER <A|B|C|D>"));
    h.type("\n");
    CHECK(!h.wait_done(200));
    CHECK(h.server_reads(200).empty());  // nothing invalid was sent
    // and a good command still goes through
    h.type("leaderboard\n");
    CHECK_EQ(h.server_reads(), std::string("LEADERBOARD"));
}

TEST_CASE("Slice6 command word is uppercased but arguments keep their case; help stays local") {
    Harness h("case");
    h.start();
    h.output.wait_for("> ");
    h.type("help\n");
    CHECK(h.output.wait_for("show this list"));
    CHECK(h.server_reads(150).empty());
    h.type("create_game Robert 3\n");
    CHECK_EQ(h.server_reads(), std::string("CREATE_GAME Robert 3"));
    h.server_sends("200 OK\nAB12\nzz9Y\n\n");
    h.output.wait_for("Join code: AB12");
    h.type("Submit_Answer b\n");
    CHECK_EQ(h.server_reads(), std::string("SUBMIT_ANSWER b"));
}

TEST_CASE("T01 the client shows the join code, never the token, and saves a private session file") {
    Harness h("t01");
    h.start();
    h.output.wait_for("> ");
    h.type("CREATE_GAME Robert 3\n");
    h.server_reads();
    h.server_sends("200 OK\nAB12\nzz9Y\n\n");
    CHECK(h.output.wait_for("Join code: AB12"));
    CHECK(h.output.text().find("zz9Y") == std::string::npos);
    Session saved;
    CHECK(load_session(h.session_path, saved));
    CHECK_EQ(saved.name, std::string("Robert"));
    CHECK_EQ(saved.code, std::string("AB12"));
    CHECK_EQ(saved.token, std::string("zz9Y"));
    struct stat info;
    CHECK_EQ(stat(h.session_path.c_str(), &info), 0);
    CHECK_EQ(static_cast<int>(info.st_mode & 0777), 0600);
}

TEST_CASE("Slice6 JOIN_GAME saves the session and hides the token") {
    Harness h("join");
    h.start();
    h.output.wait_for("> ");
    h.type("join_game ab12 Matthew\n");
    CHECK_EQ(h.server_reads(), std::string("JOIN_GAME AB12 Matthew"));
    h.server_sends("200 OK\n2 players in game AB12\nQ7p2\n\n");
    CHECK(h.output.wait_for("2 players in game AB12"));
    CHECK(h.output.text().find("Q7p2") == std::string::npos);
    Session saved;
    CHECK(load_session(h.session_path, saved));
    CHECK_EQ(saved.name, std::string("Matthew"));
    CHECK_EQ(saved.code, std::string("AB12"));
    CHECK_EQ(saved.token, std::string("Q7p2"));
}

TEST_CASE("Slice6 RECONNECT with no arguments expands the saved session; without one nothing is sent") {
    Harness h("reconnect");
    h.start();
    h.output.wait_for("> ");
    h.type("reconnect\n");
    CHECK(h.output.wait_for("No saved session"));
    CHECK(h.server_reads(150).empty());
    save_session(h.session_path, Session{"Robert", "AB12", "zz9Y"});
    h.type("RECONNECT\n");
    CHECK_EQ(h.server_reads(), std::string("RECONNECT Robert AB12 zz9Y"));
    h.server_sends("200 OK\n2 3\n\n");
    CHECK(h.output.wait_for("Reconnected"));
    CHECK(h.output.text().find("zz9Y") == std::string::npos);
}

TEST_CASE("Slice6 a 410 reply leaves the session file for a retry") {
    Harness h("retry");
    h.start();
    h.output.wait_for("> ");
    save_session(h.session_path, Session{"Robert", "AB12", "zz9Y"});
    h.type("RECONNECT\n");
    h.server_reads();
    h.server_sends("410 Wrong name, game code, or token, or the reconnect window has expired.\n\n");
    CHECK(h.output.wait_for("410 Wrong name"));
    Session saved;
    CHECK(load_session(h.session_path, saved));
}

TEST_CASE("Slice6 QUIT sends QUIT, removes the session file, and exits with status 0") {
    Harness h("quit");
    h.start();
    h.output.wait_for("> ");
    save_session(h.session_path, Session{"Robert", "AB12", "zz9Y"});
    h.type("quit\n");
    CHECK_EQ(h.server_reads(), std::string("QUIT"));
    h.server_sends("200 OK\n\n");
    CHECK(h.wait_done());
    CHECK_EQ(h.exit_code.load(), 0);
    Session saved;
    CHECK(!load_session(h.session_path, saved));
}

TEST_CASE("T34 push between the command and its reply is printed once and is not taken as the reply") {
    Harness h("t34a");
    h.start();
    h.output.wait_for("> ");
    h.type("LEADERBOARD\n");
    h.server_reads();
    h.server_sends("PUSH ROUND_RESULT 1\nCorrect answer: B\nRobert:1\n\n200 OK\nRobert:1\n\n");
    CHECK(h.output.wait_for("200 OK"));
    CHECK_EQ(h.output.count("ROUND_RESULT 1"), 1);
    CHECK_EQ(h.output.count("Correct answer: B"), 1);
    h.type("LEADERBOARD\n");
    CHECK_EQ(h.server_reads(), std::string("LEADERBOARD"));  // prompt is usable again
}

TEST_CASE("T34 a reply and a push in one chunk, then a reply one byte at a time") {
    Harness h("t34b");
    h.start();
    h.output.wait_for("> ");
    h.type("LEADERBOARD\n");
    h.server_reads();
    h.server_sends("200 OK\nRobert:1\n\nPUSH QUESTION 2 Question 2\nA) alpha\nB) beta\nC) gamma\nD) delta\n\n");
    CHECK(h.output.wait_for("D) delta"));
    CHECK_EQ(h.output.count("QUESTION 2 Question 2"), 1);
    h.type("LEADERBOARD\n");
    h.server_reads();
    const std::string reply = "200 OK\nRobert:2\n\n";
    for (char c : reply) {
        h.server_sends(std::string(1, c));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(h.output.wait_for("Robert:2"));
    CHECK_EQ(h.output.count("Robert:2"), 1);
}

TEST_CASE("T34 a push split byte by byte while idle is displayed in full once") {
    Harness h("t34c");
    h.start();
    h.output.wait_for("> ");
    const std::string push = "PUSH QUESTION 1 Question 0\nA) alpha\nB) beta\nC) gamma\nD) delta\n\n";
    for (char c : push) {
        h.server_sends(std::string(1, c));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(h.output.wait_for("D) delta"));
    CHECK_EQ(h.output.count("QUESTION 1 Question 0"), 1);
}

TEST_CASE("Slice6 a push while idle is shown, the prompt comes back, and a partly typed line still works") {
    Harness h("idle");
    h.start();
    h.output.wait_for("> ");
    const int prompts_before = h.output.count("\n> ");
    h.type("LEADER");
    h.server_sends("PUSH ROUND_RESULT 1\nCorrect answer: A\nRobert:0\n\n");
    CHECK(h.output.wait_for("Correct answer: A"));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(h.output.count("\n> ") > prompts_before);  // prompt shown again after the push
    h.type("BOARD\n");
    CHECK_EQ(h.server_reads(), std::string("LEADERBOARD"));
}

TEST_CASE("Slice6 GAME_END push removes the session file and the client exits without input") {
    Harness h("gameend");
    h.start();
    h.output.wait_for("> ");
    save_session(h.session_path, Session{"Robert", "AB12", "zz9Y"});
    h.server_sends("PUSH GAME_END COMPLETE\nFINAL RESULTS\nRobert:2\n\n");
    CHECK(h.wait_done());
    CHECK_EQ(h.exit_code.load(), 0);
    CHECK(h.output.text().find("FINAL RESULTS") != std::string::npos);
    CHECK(h.output.text().find("Robert:2") != std::string::npos);
    Session saved;
    CHECK(!load_session(h.session_path, saved));
}

TEST_CASE("T29 server closes while the client waits at its prompt: message and clean exit") {
    Harness h("t29");
    h.start();
    h.output.wait_for("> ");
    h.peer.reset();
    CHECK(h.wait_done());
    CHECK_EQ(h.exit_code.load(), 0);
    CHECK(h.output.text().find("Server closed the connection") != std::string::npos);
}

TEST_CASE("T29 server closes while the client waits for a reply: it wakes and exits") {
    Harness h("t29b");
    h.start();
    h.output.wait_for("> ");
    h.type("LEADERBOARD\n");
    h.server_reads();
    h.peer.reset();
    CHECK(h.wait_done());
    CHECK_EQ(h.exit_code.load(), 0);
    CHECK(h.output.text().find("Server closed the connection") != std::string::npos);
}

TEST_CASE("Slice6 stdin EOF closes without QUIT and keeps the saved session") {
    Harness h("eof");
    h.start();
    h.output.wait_for("> ");
    save_session(h.session_path, Session{"Robert", "AB12", "zz9Y"});
    close(h.stdin_pipe[1]);
    h.stdin_pipe[1] = open("/dev/null", O_WRONLY);  // so the destructor can close something
    CHECK(h.wait_done());
    CHECK_EQ(h.exit_code.load(), 0);
    std::string line;
    // The server sees the connection end with no QUIT line in between.
    CHECK(h.peer->read_line(line, 1000) == Connection::ReadStatus::Closed);
    Session saved;
    CHECK(load_session(h.session_path, saved));
}

TEST_CASE("Slice6 session file helpers round trip and reject junk") {
    const std::string path = "/tmp/termtrivia_session_helper_test";
    std::remove(path.c_str());
    Session out;
    CHECK(!load_session(path, out));
    CHECK(save_session(path, Session{"Robert", "AB12", "zz9Y"}));
    CHECK(load_session(path, out));
    CHECK_EQ(out.token, std::string("zz9Y"));
    { std::ofstream(path) << "only two\n"; }
    CHECK(!load_session(path, out));
    remove_session(path);
    CHECK(!load_session(path, out));
}

TEST_CASE("Slice6 interpret_input table") {
    const std::optional<Session> none;
    CHECK(interpret_input("", none).kind == InputAction::Nothing);
    CHECK(interpret_input("   ", none).kind == InputAction::Nothing);
    CHECK(interpret_input("help", none).kind == InputAction::Show);
    CHECK(interpret_input("HELP", none).kind == InputAction::Show);
    InputAction send = interpret_input("  join_game   ab12   Matthew ", none);
    CHECK(send.kind == InputAction::Send);
    CHECK_EQ(send.wire, std::string("JOIN_GAME AB12 Matthew"));
    CHECK(interpret_input("JOIN_GAME ab Matthew", none).kind == InputAction::Show);
    CHECK(interpret_input("START_ROUND now", none).kind == InputAction::Show);
    CHECK(interpret_input("CREATE_GAME Rob:ert 3", none).kind == InputAction::Show);
    CHECK(interpret_input("CREATE_GAME Robert 0", none).kind == InputAction::Show);
    CHECK(interpret_input("CREATE_GAME Robert 3", none).kind == InputAction::Send);
    CHECK(interpret_input("RECONNECT", none).kind == InputAction::Show);
    const std::optional<Session> saved = Session{"Robert", "AB12", "zz9Y"};
    CHECK_EQ(interpret_input("reconnect", saved).wire, std::string("RECONNECT Robert AB12 zz9Y"));
    CHECK_EQ(interpret_input("RECONNECT Bob CD34 xx11", none).wire, std::string("RECONNECT Bob CD34 xx11"));
}
