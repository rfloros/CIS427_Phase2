// Command-line settings for the client.
#ifndef CLIENT_CLIENT_CONFIG_H
#define CLIENT_CLIENT_CONFIG_H

#include <string>
#include <vector>

struct ClientConfig {
    std::string host;
    int port = 0;
};

// `args` excludes the program name. On failure `error` says why.
bool parse_client_args(const std::vector<std::string>& args, ClientConfig& config,
                       std::string& error);

const char* client_usage();

#endif
