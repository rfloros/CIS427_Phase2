#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <vector>

#include "client/client.h"
#include "client/client_config.h"

namespace {

// Returns a connected socket, or -1 if no address of the host accepts us.
int connect_to(const std::string& host, int port) {
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0) return -1;

    int fd = -1;
    for (addrinfo* candidate = found; candidate != nullptr; candidate = candidate->ai_next) {
        fd = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, candidate->ai_addr, candidate->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(found);
    return fd;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    ClientConfig config;
    std::string error;
    if (!parse_client_args(args, config, error)) {
        std::cerr << "error: " << error << "\n" << client_usage() << "\n";
        return 2;
    }

    signal(SIGPIPE, SIG_IGN);
    const int fd = connect_to(config.host, config.port);
    if (fd < 0) {
        std::cerr << "Could not connect to " << config.host << ":" << config.port << "\n";
        return 1;
    }

    Client client(fd, ClientOptions{});
    return client.run();
}
