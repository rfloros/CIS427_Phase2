#include <signal.h>

#include <iostream>
#include <string>
#include <vector>

#include "server/config.h"
#include "server/startup.h"

namespace {

void on_interrupt(int) { stop_running_server(); }

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    ServerConfig config;
    std::string error;
    if (!parse_server_args(args, config, error)) {
        std::cerr << "error: " << error << "\n" << server_usage() << "\n";
        return 2;
    }

    struct sigaction action {};
    action.sa_handler = on_interrupt;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    signal(SIGPIPE, SIG_IGN);

    return run_server(config, os_entropy, std::cerr);
}
