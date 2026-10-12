#include "server/config.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include "common/net_util.h"

namespace {

// Accepts plain decimals such as "20" or "0.25". Zero, negatives, NaN, and
// infinity are refused because a timer must be positive and finite.
bool parse_seconds(const std::string& text, double& seconds) {
    if (text.empty() || !std::isdigit(static_cast<unsigned char>(text[0]))) return false;
    const bool plain_decimal = std::all_of(text.begin(), text.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) || c == '.';
    });
    if (!plain_decimal) return false;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (*end != '\0' || !std::isfinite(value) || value <= 0) return false;
    seconds = value;
    return true;
}

bool parse_seed(const std::string& text, uint64_t& seed) {
    if (text.empty() || text.size() > 19) return false;
    if (!std::all_of(text.begin(), text.end(),
                     [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
        return false;
    }
    seed = std::stoull(text);
    return true;
}

}  // namespace

const char* server_usage() {
    return "usage: ./server <port> [--questions FILE] [--log FILE] [--seed N]\n"
           "                [--question-secs N] [--intermission-secs N]\n"
           "                [--final-intermission-secs N] [--reconnect-secs N]";
}

const char* client_usage() { return "usage: ./client <host> <port>"; }

bool parse_server_args(const std::vector<std::string>& args, ServerConfig& config,
                       std::string& error) {
    config = ServerConfig{};
    bool have_port = false;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg.rfind("--", 0) != 0) {
            if (have_port || !parse_port(arg, config.port)) {
                error = have_port ? "unexpected extra argument: " + arg
                                  : "invalid port (use 1 to 65535): " + arg;
                return false;
            }
            have_port = true;
            continue;
        }

        if (i + 1 >= args.size()) {
            error = "missing value after " + arg;
            return false;
        }
        const std::string& value = args[++i];

        double* seconds_field = nullptr;
        if (arg == "--questions") {
            config.questions_path = value;
        } else if (arg == "--log") {
            config.log_path = value;
        } else if (arg == "--seed") {
            if (!parse_seed(value, config.seed)) {
                error = "--seed needs a whole number, got: " + value;
                return false;
            }
            config.has_seed = true;
        } else if (arg == "--question-secs") {
            seconds_field = &config.timings.question;
        } else if (arg == "--intermission-secs") {
            seconds_field = &config.timings.intermission;
        } else if (arg == "--final-intermission-secs") {
            seconds_field = &config.timings.final_intermission;
        } else if (arg == "--reconnect-secs") {
            seconds_field = &config.timings.reconnect;
        } else {
            error = "unknown option: " + arg;
            return false;
        }

        if (seconds_field != nullptr && !parse_seconds(value, *seconds_field)) {
            error = arg + " needs a positive number of seconds, got: " + value;
            return false;
        }
    }

    if (!have_port) {
        error = "missing port";
        return false;
    }
    return true;
}

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
