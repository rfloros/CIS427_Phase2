#include "server/logger.h"

#include <ctime>

namespace {

std::string timestamp_now() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &local);
    return text;
}

}  // namespace

void Logger::write(const char* level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    out_ << timestamp_now() << " " << level << " " << message << std::endl;  // flush every line
}

void Logger::server_started() { info("server starting"); }

void Logger::listening(const std::string& address, int port) {
    info("listening on " + address + ":" + std::to_string(port));
}

void Logger::client_connected(uint64_t client_id, const std::string& address) {
    info("client " + std::to_string(client_id) + " connected from " + address);
}

void Logger::request(uint64_t client_id, const std::string& command_name, int code) {
    info("client " + std::to_string(client_id) + " request " + command_name + " -> " +
         std::to_string(code));
}

void Logger::protocol_error(uint64_t client_id, int code, const std::string& reason) {
    warn("client " + std::to_string(client_id) + " protocol error " + std::to_string(code) + ": " +
         reason);
}

void Logger::client_disconnected(uint64_t client_id, const std::string& reason) {
    info("client " + std::to_string(client_id) + " disconnected: " + reason);
}

void Logger::server_shutdown() { info("server shutting down"); }
