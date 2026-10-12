#include "server/server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>

namespace {

const int kSendTimeoutMs = 200;        // a blocked write gives up after this
const int kHandlerPollMs = 50;         // how often an idle handler checks for stop/close
const int kTimerTickMs = 20;
const double kDrainSeconds = 0.5;      // how long to wait for the peer to finish after a close

// Only well-known command names are ever written to the log, never user text.
std::string logged_command_word(const std::string& line) {
    const std::string word = line.substr(0, line.find(' '));
    for (const char* known : {"CREATE_GAME", "JOIN_GAME", "START_ROUND", "SUBMIT_ANSWER",
                              "LEADERBOARD", "RECONNECT", "QUIT", "END_GAME"}) {
        if (word == known) return word;
    }
    return "unknown";
}

std::string peer_address(int fd) {
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    if (getpeername(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) return "unknown";
    char text[INET_ADDRSTRLEN] = "?";
    inet_ntop(AF_INET, &address.sin_addr, text, sizeof(text));
    return std::string(text) + ":" + std::to_string(ntohs(address.sin_port));
}

}  // namespace

Server::Server(const ServerOptions& options)
    : options_(options),
      logger_(*options.logger),
      manager_(options.questions, options.timings, options.seed),
      started_at_(std::chrono::steady_clock::now()) {}

Server::~Server() {
    if (listen_fd_ >= 0) close(listen_fd_);
    if (wake_pipe_[0] >= 0) close(wake_pipe_[0]);
    if (wake_pipe_[1] >= 0) close(wake_pipe_[1]);
}

double Server::now_seconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at_).count();
}

bool Server::start(std::string& error) {
    if (pipe(wake_pipe_) != 0) {
        error = std::string("cannot create wake-up pipe: ") + std::strerror(errno);
        return false;
    }
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        error = std::string("cannot create socket: ") + std::strerror(errno);
        return false;
    }
    const int yes = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<uint16_t>(options_.port));
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        error = "cannot bind port " + std::to_string(options_.port) + ": " + std::strerror(errno);
        return false;
    }
    if (listen(listen_fd_, 16) != 0) {
        error = std::string("cannot listen: ") + std::strerror(errno);
        return false;
    }

    socklen_t length = sizeof(address);
    getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &length);
    port_ = ntohs(address.sin_port);
    logger_.listening("0.0.0.0", port_);
    return true;
}

void Server::request_stop() {
    stopping_ = true;
    const char wake = 'x';
    // write() is async-signal-safe; the accept loop is waiting on this pipe.
    ssize_t ignored = write(wake_pipe_[1], &wake, 1);
    (void)ignored;
}

void Server::run() {
    timer_thread_ = std::thread([this] { timer_loop(); });
    accept_loop();
    stopping_ = true;
    timer_thread_.join();
    logger_.server_shutdown();
}

void Server::timer_loop() {
    while (!stopping_) {
        {
            std::lock_guard<std::mutex> lock(coordinator_);
            apply(0, manager_.tick(now_seconds()));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kTimerTickMs));
    }
}

void Server::accept_loop() {
    while (!stopping_) {
        pollfd waiting[2] = {{listen_fd_, POLLIN, 0}, {wake_pipe_[0], POLLIN, 0}};
        if (poll(waiting, 2, -1) < 0) {
            if (errno == EINTR) continue;
            logger_.error(std::string("poll failed: ") + std::strerror(errno));
            return;
        }
        if (waiting[1].revents != 0) return;  // stop requested

        const int fd = accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) continue;  // the client vanished before we accepted it

        Connection connection(fd);
        connection.set_send_timeout_ms(kSendTimeoutMs);
        ConnId id;
        {
            std::lock_guard<std::mutex> lock(coordinator_);
            id = ++next_conn_id_;
            live_connections_[id] = &connection;
        }
        logger_.client_connected(id, peer_address(fd));
        handle_connection(id, connection);
        // `connection` closes its descriptor here, after finish_connection()
        // removed it from live_connections_.
    }
}

void Server::handle_connection(ConnId id, Connection& connection) {
    std::string reason = "closed by peer";
    while (!stopping_) {
        if (connection.is_broken()) {
            reason = "send failed";
            break;
        }
        if (connection.is_closing()) {
            // The server ended this session on purpose (QUIT, game end).
            drain_until_peer_closes(connection);
            reason = "closed by server";
            break;
        }

        std::string line;
        const auto status = connection.read_line(line, kHandlerPollMs);
        if (status == Connection::ReadStatus::Timeout) continue;
        if (status == Connection::ReadStatus::Closed) break;
        if (status == Connection::ReadStatus::Error) {
            reason = "read error";
            break;
        }
        if (status == Connection::ReadStatus::TooLong) {
            process_too_long_line(id);
        } else {
            process_line(id, line);
        }
    }
    if (stopping_) reason = "server shutting down";
    finish_connection(id);
    logger_.client_disconnected(id, reason);
}

void Server::process_line(ConnId id, const std::string& line) {
    std::lock_guard<std::mutex> lock(coordinator_);
    const ParseResult parsed = parse_command(line, manager_.available_questions());
    Outcome outcome;
    if (parsed.ok) {
        outcome = manager_.handle(id, parsed.command, now_seconds());
        logger_.request(id, command_name(parsed.command.type), outcome.code);
    } else {
        outcome.code = parsed.error_code;
        outcome.effects.push_back(
            {id, reply_error(parsed.error_code, manager_.available_questions()), false});
        logger_.protocol_error(id, parsed.error_code,
                               "rejected " + logged_command_word(line) + " request");
    }
    apply(id, outcome);
}

void Server::process_too_long_line(ConnId id) {
    std::lock_guard<std::mutex> lock(coordinator_);
    logger_.protocol_error(id, 300, "line longer than 512 bytes");
    Outcome outcome;
    outcome.effects.push_back({id, reply_error(300), false});
    apply(id, outcome);
}

// Reads whatever the client already sent without acting on it, then waits a
// moment for the client to close. Closing with unread data would reset the
// connection and could destroy the final message before the client reads it.
void Server::drain_until_peer_closes(Connection& connection) {
    const auto start = std::chrono::steady_clock::now();
    while (!stopping_ &&
           std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() <
               kDrainSeconds) {
        std::string ignored;
        const auto status = connection.read_line(ignored, kHandlerPollMs);
        if (status == Connection::ReadStatus::Closed || status == Connection::ReadStatus::Error) {
            return;
        }
    }
}

// Runs on every way a connection can end. Taking the connection out of
// live_connections_ under the coordinator, before the descriptor is closed,
// means no later effect can reach a reused descriptor.
void Server::finish_connection(ConnId id) {
    std::lock_guard<std::mutex> lock(coordinator_);
    apply(id, manager_.on_disconnect(id, now_seconds()));
    live_connections_.erase(id);
}

void Server::apply(ConnId id, const Outcome& outcome) {
    (void)id;
    for (const auto& event : outcome.events) logger_.info(event);
    deliver(outcome.effects);
}

void Server::deliver(const std::vector<Effect>& effects) {
    for (const auto& effect : effects) {
        const auto found = live_connections_.find(effect.conn);
        if (found == live_connections_.end()) continue;  // already gone
        Connection& connection = *found->second;

        if (!connection.send_message(effect.lines)) {
            // The handler notices the flag, cleans up, and closes.
            connection.shutdown_both();
            continue;
        }
        if (effect.close_after) {
            connection.shutdown_write();
            connection.mark_closing();
        }
    }
}
