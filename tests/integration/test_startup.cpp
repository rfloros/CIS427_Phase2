// Slice 5: startup failures happen before the server listens.
#include <fstream>
#include <sstream>

#include "integration/test_support.h"
#include "minitest.h"
#include "server/startup.h"

using namespace support;

namespace {

std::string read_file(const std::string& path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

}  // namespace

TEST_CASE("Slice5 unavailable OS entropy refuses startup with an ERROR log, before listening") {
    const std::string log_path = "/tmp/termtrivia_entropy_test.log";
    std::remove(log_path.c_str());
    ServerConfig config;
    config.port = 1;  // never reached
    config.questions_path = "questions.txt";
    config.log_path = log_path;
    std::ostringstream errors;
    const int code = run_server(config, [](uint64_t&) { return false; }, errors);
    CHECK_EQ(code, 1);
    const std::string log = read_file(log_path);
    CHECK(log.find(" ERROR cannot read OS entropy") != std::string::npos);
    CHECK(log.find("listening on") == std::string::npos);
    CHECK(errors.str().find("entropy") != std::string::npos);
}

TEST_CASE("Slice5 a missing question file refuses startup") {
    const std::string log_path = "/tmp/termtrivia_nobank_test.log";
    std::remove(log_path.c_str());
    ServerConfig config;
    config.port = 1;
    config.questions_path = "/nonexistent/questions.txt";
    config.log_path = log_path;
    std::ostringstream errors;
    CHECK_EQ(run_server(config, os_entropy, errors), 1);
    CHECK(read_file(log_path).find(" ERROR ") != std::string::npos);
}

TEST_CASE("Slice5 a port that is already in use refuses startup") {
    TestServer running;
    const std::string log_path = "/tmp/termtrivia_inuse_test.log";
    std::remove(log_path.c_str());
    ServerConfig config;
    config.port = running.port();
    config.log_path = log_path;
    std::ostringstream errors;
    CHECK_EQ(run_server(config, os_entropy, errors), 1);
    CHECK(read_file(log_path).find("cannot bind") != std::string::npos);
}

TEST_CASE("Slice5 run_server serves a client and stops cleanly on request") {
    const std::string log_path = "/tmp/termtrivia_runserver_test.log";
    std::remove(log_path.c_str());
    // Find a free port by binding port 0 first.
    int port = 0;
    {
        TestServer probe;
        port = probe.port();
    }
    ServerConfig config;
    config.port = port;
    config.log_path = log_path;
    config.has_seed = true;
    config.seed = 5;
    std::ostringstream errors;
    int exit_code = -1;
    std::thread server_thread([&] { exit_code = run_server(config, os_entropy, errors); });
    RawClient client(port);
    for (int tries = 0; tries < 50 && !client.connected(); ++tries) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        client = RawClient(port);
    }
    CHECK(client.connected());
    CHECK(create_game(client, "Robert", 2).code.size() == 4);
    stop_running_server();
    server_thread.join();
    CHECK_EQ(exit_code, 0);
    const std::string log = read_file(log_path);
    CHECK(log.find("server starting") != std::string::npos);
    CHECK(log.find("listening on 0.0.0.0:" + std::to_string(port)) != std::string::npos);
    CHECK(log.find("server shutting down") != std::string::npos);
}
