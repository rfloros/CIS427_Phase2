// Command-line settings for the server and the client.
#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

#include <cstdint>
#include <string>
#include <vector>

#include "server/game.h"

struct ServerConfig {
    int port = 0;
    std::string questions_path = "questions.txt";
    std::string log_path = "server.log";
    bool has_seed = false;
    uint64_t seed = 0;
    Timings timings;  // defaults are the spec values: 20, 5, 10, 60 seconds
};

struct ClientConfig {
    std::string host;
    int port = 0;
};

// `args` excludes the program name. On failure `error` says why.
bool parse_server_args(const std::vector<std::string>& args, ServerConfig& config,
                       std::string& error);
bool parse_client_args(const std::vector<std::string>& args, ClientConfig& config,
                       std::string& error);

const char* server_usage();
const char* client_usage();

#endif
