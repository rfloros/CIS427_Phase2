#include "client/client_config.h"

#include "common/net_util.h"

const char* client_usage() { return "usage: ./client <host> <port>"; }

bool parse_client_args(const std::vector<std::string>& args, ClientConfig& config,
                       std::string& error) {
    if (args.size() != 2) {
        error = "expected a host and a port";
        return false;
    }
    if (args[0].empty()) {
        error = "host must not be empty";
        return false;
    }
    if (!parse_port(args[1], config.port)) {
        error = "invalid port (use 1 to 65535): " + args[1];
        return false;
    }
    config.host = args[0];
    return true;
}
