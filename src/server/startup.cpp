#include "server/startup.h"

#include <atomic>
#include <fstream>
#include <iostream>

#include "server/logger.h"
#include "server/question_bank.h"
#include "server/server.h"

namespace {

std::atomic<Server*> running_server{nullptr};

}  // namespace

void stop_running_server() {
    Server* server = running_server.load();
    if (server != nullptr) server->request_stop();
}

int run_server(const ServerConfig& config, const EntropyFn& entropy, std::ostream& error_out) {
    std::ofstream log_file(config.log_path, std::ios::app);
    if (!log_file) {
        error_out << "cannot open log file " << config.log_path << "\n";
        return 1;
    }
    Logger logger(log_file);
    logger.server_started();

    QuestionBank bank;
    std::string error;
    const bool loaded = load_question_bank(config.questions_path, bank, error);
    for (const auto& warning : bank.warnings) logger.warn(warning);
    if (!loaded) {
        logger.error(error);
        error_out << error << "\n";
        return 1;
    }

    // Without --seed, randomness comes from the OS so join codes cannot be guessed.
    uint64_t seed = config.seed;
    if (!config.has_seed && !entropy(seed)) {
        error = "cannot read OS entropy; refusing to start";
        logger.error(error);
        error_out << error << "\n";
        return 1;
    }

    ServerOptions options;
    options.questions = bank.questions;
    options.timings = config.timings;
    options.seed = seed;
    options.logger = &logger;
    options.port = config.port;

    Server server(options);
    if (!server.start(error)) {
        logger.error(error);
        error_out << error << "\n";
        return 1;
    }
    std::cout << "TermTrivia server listening on port " << server.port()
              << " (press Ctrl+C to stop)" << std::endl;

    running_server = &server;
    server.run();
    running_server = nullptr;
    return 0;
}
