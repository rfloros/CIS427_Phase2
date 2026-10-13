// Slice 4: logger format and command-line parsing.
#include <cmath>
#include <regex>
#include <sstream>
#include <thread>

#include "common/net_util.h"
#include "minitest.h"
#include "client/client_config.h"
#include "server/config.h"
#include "server/logger.h"

namespace {

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) lines.push_back(line);
    return lines;
}

bool parses(std::vector<std::string> args, ServerConfig& config) {
    std::string error;
    return parse_server_args(args, config, error);
}

}  // namespace

TEST_CASE("Slice4 log line starts with an ISO timestamp and a level") {
    std::ostringstream out;
    Logger logger(out);
    logger.info("hello");
    logger.warn("careful");
    logger.error("broken");
    const auto lines = lines_of(out.str());
    CHECK_EQ(lines.size(), static_cast<size_t>(3));
    const std::regex format(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2} (INFO|WARN|ERROR) .+$)");
    for (const auto& line : lines) CHECK(std::regex_match(line, format));
    CHECK(lines[0].find(" INFO hello") != std::string::npos);
    CHECK(lines[1].find(" WARN careful") != std::string::npos);
    CHECK(lines[2].find(" ERROR broken") != std::string::npos);
}

TEST_CASE("T13 required event functions write their facts") {
    std::ostringstream out;
    Logger logger(out);
    logger.server_started();
    logger.listening("0.0.0.0", 5000);
    logger.client_connected(3, "127.0.0.1:50122");
    logger.request(3, "CREATE_GAME", 200);
    logger.protocol_error(3, 300, "unknown command");
    logger.client_disconnected(3, "connection closed by peer");
    logger.server_shutdown();
    const std::string text = out.str();
    for (const char* expected :
         {"server starting", "listening on 0.0.0.0:5000", "client 3 connected from 127.0.0.1:50122",
          "client 3 request CREATE_GAME -> 200", "client 3 protocol error 300: unknown command",
          "client 3 disconnected: connection closed by peer", "server shutting down"}) {
        CHECK(text.find(expected) != std::string::npos);
    }
}

TEST_CASE("T13 the request log records only the command name and the outcome") {
    std::ostringstream out;
    Logger logger(out);
    // The raw line "RECONNECT Robert 7QXA K3M9" never reaches the logger: it only
    // receives the command name and result code.
    logger.request(1, "RECONNECT", 410);
    CHECK(out.str().find("K3M9") == std::string::npos);
    CHECK(out.str().find("RECONNECT -> 410") != std::string::npos);
}

TEST_CASE("Slice4 logger is thread safe: lines never mix") {
    std::ostringstream out;
    Logger logger(out);
    auto spam = [&](const std::string& word) {
        for (int i = 0; i < 200; ++i) logger.info(word + " " + word + " " + word);
    };
    std::thread a(spam, "aaaa");
    std::thread b(spam, "bbbb");
    a.join();
    b.join();
    const auto lines = lines_of(out.str());
    CHECK_EQ(lines.size(), static_cast<size_t>(400));
    const std::regex format(R"(^\S+ INFO (aaaa aaaa aaaa|bbbb bbbb bbbb)$)");
    for (const auto& line : lines) CHECK(std::regex_match(line, format));
}

TEST_CASE("T25 ports: valid and invalid values") {
    int port = 0;
    CHECK(parse_port("5000", port) && port == 5000);
    CHECK(parse_port("1", port) && port == 1);
    CHECK(parse_port("65535", port) && port == 65535);
    for (const char* bad : {"abc", "0", "70000", "65536", "-1", "", "80x", " 80", "+80", "99999999999999"}) {
        CHECK(!parse_port(bad, port));
    }
}

TEST_CASE("Slice4 server arguments: defaults match the spec") {
    ServerConfig config;
    CHECK(parses({"5000"}, config));
    CHECK_EQ(config.port, 5000);
    CHECK_EQ(config.questions_path, std::string("questions.txt"));
    CHECK_EQ(config.log_path, std::string("server.log"));
    CHECK(!config.has_seed);
    CHECK(config.timings.question == 20 && config.timings.intermission == 5);
    CHECK(config.timings.final_intermission == 10 && config.timings.reconnect == 60);
}

TEST_CASE("Slice4 server arguments: every flag is applied") {
    ServerConfig config;
    CHECK(parses({"5001", "--questions", "q.txt", "--log", "x.log", "--seed", "42",
                  "--question-secs", "0.5", "--intermission-secs", "0.2",
                  "--final-intermission-secs", "0.3", "--reconnect-secs", "1.5"},
                 config));
    CHECK_EQ(config.port, 5001);
    CHECK_EQ(config.questions_path, std::string("q.txt"));
    CHECK_EQ(config.log_path, std::string("x.log"));
    CHECK(config.has_seed && config.seed == 42);
    CHECK(config.timings.question == 0.5 && config.timings.intermission == 0.2);
    CHECK(config.timings.final_intermission == 0.3 && config.timings.reconnect == 1.5);
}

TEST_CASE("Slice4 server arguments: flags may come before the port") {
    ServerConfig config;
    CHECK(parses({"--seed", "7", "5002"}, config));
    CHECK_EQ(config.port, 5002);
}

TEST_CASE("Slice4 server arguments: bad port, unknown flag, missing values are errors") {
    ServerConfig config;
    std::string error;
    CHECK(!parse_server_args({}, config, error) && !error.empty());
    for (const auto& args : std::vector<std::vector<std::string>>{
             {"abc"}, {"0"}, {"70000"}, {"5000", "6000"}, {"5000", "--bogus"},
             {"5000", "--seed"}, {"5000", "--seed", "x"}, {"5000", "--seed", "-1"},
             {"5000", "--questions"}, {"5000", "--log"}, {"5000", "--question-secs"}}) {
        error.clear();
        CHECK(!parse_server_args(args, config, error));
        CHECK(!error.empty());
    }
}

TEST_CASE("Slice4 server arguments: timing values must be positive and finite") {
    ServerConfig config;
    for (const char* flag : {"--question-secs", "--intermission-secs", "--final-intermission-secs",
                             "--reconnect-secs"}) {
        for (const char* bad : {"0", "-1", "-0.5", "nan", "inf", "-inf", "abc", "", "1e999", "0.5s", "0x10"}) {
            CHECK(!parses({"5000", flag, bad}, config));
        }
        CHECK(parses({"5000", flag, "0.25"}, config));
    }
}

TEST_CASE("Slice4 client arguments") {
    ClientConfig config;
    std::string error;
    CHECK(parse_client_args({"localhost", "5000"}, config, error));
    CHECK_EQ(config.host, std::string("localhost"));
    CHECK_EQ(config.port, 5000);
    for (const auto& args : std::vector<std::vector<std::string>>{
             {}, {"localhost"}, {"localhost", "abc"}, {"localhost", "0"},
             {"localhost", "70000"}, {"localhost", "5000", "extra"}, {"", "5000"}}) {
        error.clear();
        CHECK(!parse_client_args(args, config, error));
        CHECK(!error.empty());
    }
}
