// Thread-safe, timestamped event log. The event functions take only names and
// numbers, never raw request text, so a token cannot reach the log.
#ifndef SERVER_LOGGER_H
#define SERVER_LOGGER_H

#include <cstdint>
#include <mutex>
#include <ostream>
#include <string>

class Logger {
public:
    explicit Logger(std::ostream& out) : out_(out) {}

    void info(const std::string& message) { write("INFO", message); }
    void warn(const std::string& message) { write("WARN", message); }
    void error(const std::string& message) { write("ERROR", message); }

    void server_started();
    void listening(const std::string& address, int port);
    void client_connected(uint64_t client_id, const std::string& address);
    void request(uint64_t client_id, const std::string& command_name, int code);
    void protocol_error(uint64_t client_id, int code, const std::string& reason);
    void client_disconnected(uint64_t client_id, const std::string& reason);
    void server_shutdown();

private:
    void write(const char* level, const std::string& message);

    std::ostream& out_;
    std::mutex mutex_;
};

#endif
