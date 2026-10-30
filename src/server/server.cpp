#include "server/server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <system_error>

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

size_t Server::worker_count() {
    std::lock_guard<std::mutex> lock(workers_mutex_);
    return handlers_.size() + game_timers_.size();
}

void Server::run() {
    accept_loop();
    stopping_ = true;
    // Handlers first: they are the only threads that start game timers.
    join_all(handlers_);
    join_all(game_timers_);
    logger_.server_shutdown();
}

// ---------------------------------------------------------------- workers

bool Server::launch(WorkerList& list, WorkerKind kind, std::function<void()> body) {
    if (options_.fail_worker_launch && options_.fail_worker_launch(kind)) return false;
    auto worker = std::make_unique<Worker>();
    Worker* raw = worker.get();
    try {
        raw->thread = std::thread([raw, body] {
            body();
            raw->done = true;  // last step: the thread can now be joined without waiting
        });
    } catch (const std::system_error&) {
        return false;
    }
    std::lock_guard<std::mutex> lock(workers_mutex_);
    list.push_back(std::move(worker));
    return true;
}

void Server::reap_finished(WorkerList& list) {
    std::vector<std::unique_ptr<Worker>> finished;
    {
        std::lock_guard<std::mutex> lock(workers_mutex_);
        for (auto it = list.begin(); it != list.end();) {
            if ((*it)->done) {
                finished.push_back(std::move(*it));
                it = list.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& worker : finished) worker->thread.join();
}

void Server::join_all(WorkerList& list) {
    WorkerList all;
    {
        std::lock_guard<std::mutex> lock(workers_mutex_);
        all.swap(list);
    }
    for (auto& worker : all) worker->thread.join();
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

        reap_finished(handlers_);
        reap_finished(game_timers_);
        const int fd = accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) continue;  // the client vanished before we accepted it
        start_connection_worker(fd);
    }
}

void Server::start_connection_worker(int fd) {
    auto connection = std::make_shared<Connection>(fd);
    connection->set_send_timeout_ms(kSendTimeoutMs);
    ConnId id;
    {
        std::lock_guard<std::mutex> lock(routing_mutex_);
        id = ++next_conn_id_;
        live_connections_[id] = connection;
    }
    logger_.client_connected(id, peer_address(fd));
    const bool started = launch(handlers_, WorkerKind::Handler,
                                [this, id, connection] { handle_connection(id, connection); });
    if (started) return;
    {
        std::lock_guard<std::mutex> lock(routing_mutex_);
        live_connections_.erase(id);
    }
    logger_.error("cannot start a handler thread for client " + std::to_string(id) +
                  "; connection closed");
}

// ---------------------------------------------------------------- connections

void Server::handle_connection(ConnId id, ConnectionPtr connection) {
    const auto own_lane = std::make_shared<Lane>();
    std::string reason = "closed by peer";
    while (!stopping_) {
        if (connection->is_broken()) {
            reason = "send failed";
            break;
        }
        if (connection->is_closing()) {
            // The server ended this session on purpose (QUIT, game end).
            drain_until_peer_closes(*connection);
            reason = "closed by server";
            break;
        }

        std::string line;
        const auto status = connection->read_line(line, kHandlerPollMs);
        if (status == Connection::ReadStatus::Timeout) continue;
        if (status == Connection::ReadStatus::Closed) break;
        if (status == Connection::ReadStatus::Error) {
            reason = "read error";
            break;
        }
        if (status == Connection::ReadStatus::TooLong) {
            process_too_long_line(id, connection);
        } else {
            process_line(id, connection, own_lane, line);
        }
    }
    if (connection->is_closing()) reason = "session ended by server";
    if (stopping_) reason = "server shutting down";
    finish_connection(id, own_lane);
    logger_.client_disconnected(id, reason);
}

void Server::process_line(ConnId id, ConnectionPtr connection, const LanePtr& own_lane,
                          const std::string& line) {
    const ParseResult parsed = parse_command(line, manager_.available_questions());
    if (parsed.ok && parsed.command.type == CommandType::CreateGame) {
        handle_create(id, parsed.command, own_lane);
        return;
    }

    LanePtr held;
    const auto lock = lock_lane(id, parsed.ok ? &parsed.command : nullptr, own_lane, held);
    Outcome outcome;
    std::vector<Delivery> deliveries;
    if (parsed.ok) {
        {
            std::lock_guard<std::mutex> routing(routing_mutex_);
            outcome = manager_.handle(id, parsed.command, now_seconds());
            deliveries = route(outcome);
        }
        logger_.request(id, command_name(parsed.command.type), outcome.code);
    } else {
        outcome.code = parsed.error_code;
        outcome.effects.push_back(
            {id, reply_error(parsed.error_code, manager_.available_questions()), false});
        deliveries.push_back({connection, outcome.effects.back()});
        logger_.protocol_error(id, parsed.error_code,
                               "rejected " + logged_command_word(line) + " request");
    }
    apply(outcome, std::move(deliveries));
}

void Server::process_too_long_line(ConnId id, const ConnectionPtr& connection) {
    logger_.protocol_error(id, 300, "line longer than 512 bytes");
    Outcome outcome;
    outcome.effects.push_back({id, reply_error(300), false});
    apply(outcome, {{connection, outcome.effects.back()}});
}

// A new game's lane is held from before the game exists until its 200 OK is
// written, so nothing can join it before the timer thread is known to run.
void Server::handle_create(ConnId id, const Command& command, const LanePtr& own_lane) {
    LanePtr held;
    const auto own_lock = lock_lane(id, &command, own_lane, held);
    const auto fresh = std::make_shared<Lane>();
    const std::lock_guard<std::mutex> fresh_lock(fresh->mutex);

    Outcome outcome;
    std::vector<Delivery> deliveries;
    {
        std::lock_guard<std::mutex> routing(routing_mutex_);
        outcome = manager_.handle(id, command, now_seconds());
        if (!outcome.created_game.empty()) lanes_[outcome.created_game] = fresh;
        deliveries = route(outcome);
    }
    if (!outcome.created_game.empty() && !start_game_timer(outcome.created_game, fresh)) {
        {
            std::lock_guard<std::mutex> routing(routing_mutex_);
            lanes_.erase(outcome.created_game);
            manager_.discard_game(outcome.created_game);
        }
        logger_.error("cannot start a game timer thread; game creation for client " +
                      std::to_string(id) + " cancelled");
        // No reply: the one-reply rule yields to resource exhaustion.
        const auto found = deliveries.front().connection;
        found->shutdown_both();
        return;
    }
    logger_.request(id, command_name(command.type), outcome.code);
    apply(outcome, std::move(deliveries));
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
// live_connections_ under the routing mutex, before the handler lets go of it,
// means no later effect is routed to it.
void Server::finish_connection(ConnId id, const LanePtr& own_lane) {
    LanePtr held;
    const auto lock = lock_lane(id, nullptr, own_lane, held);
    Outcome outcome;
    std::vector<Delivery> deliveries;
    {
        std::lock_guard<std::mutex> routing(routing_mutex_);
        outcome = manager_.on_disconnect(id, now_seconds());
        deliveries = route(outcome);
        live_connections_.erase(id);
    }
    apply(outcome, std::move(deliveries));
}

// ---------------------------------------------------------------- game timers

bool Server::start_game_timer(const std::string& code, const LanePtr& lane) {
    reap_finished(game_timers_);
    return launch(game_timers_, WorkerKind::GameTimer,
                  [this, code, lane] { game_timer_loop(code, lane); });
}

void Server::game_timer_loop(std::string code, LanePtr lane) {
    while (!stopping_) {
        bool finished = false;
        {
            std::lock_guard<std::mutex> hold(lane->mutex);
            Outcome outcome;
            std::vector<Delivery> deliveries;
            {
                std::lock_guard<std::mutex> routing(routing_mutex_);
                const auto found = lanes_.find(code);
                if (found == lanes_.end() || found->second != lane) return;  // not our game
                outcome = manager_.tick_game(code, now_seconds());
                deliveries = route(outcome);
            }
            apply(outcome, std::move(deliveries));
            for (const auto& ended : outcome.ended_games) finished = finished || ended == code;
        }
        if (finished) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(kTimerTickMs));
    }
}

// ---------------------------------------------------------------- lanes and delivery

Server::LanePtr Server::find_lane(ConnId id, const Command* command, const LanePtr& own_lane) {
    std::string code = manager_.game_code_of(id);
    const bool names_a_game = command != nullptr && (command->type == CommandType::JoinGame ||
                                                     command->type == CommandType::Reconnect);
    if (code.empty() && names_a_game) code = command->code;
    if (code.empty()) return own_lane;
    std::lock_guard<std::mutex> routing(routing_mutex_);
    const auto found = lanes_.find(code);
    return found == lanes_.end() ? own_lane : found->second;
}

std::unique_lock<std::mutex> Server::lock_lane(ConnId id, const Command* command,
                                               const LanePtr& own_lane, LanePtr& lane) {
    while (true) {
        lane = find_lane(id, command, own_lane);
        std::unique_lock<std::mutex> lock(lane->mutex);
        if (find_lane(id, command, own_lane) == lane) return lock;
    }
}

std::vector<Server::Delivery> Server::route(const Outcome& outcome) {
    for (const auto& code : outcome.ended_games) lanes_.erase(code);
    std::vector<Delivery> deliveries;
    for (const auto& effect : outcome.effects) {
        const auto found = live_connections_.find(effect.conn);
        if (found == live_connections_.end()) continue;  // already gone
        deliveries.push_back({found->second, effect});
    }
    return deliveries;
}

void Server::apply(const Outcome& outcome, std::vector<Delivery> deliveries) {
    for (const auto& event : outcome.events) logger_.info(event);
    deliver(deliveries);
}

void Server::deliver(const std::vector<Delivery>& deliveries) {
    for (const auto& delivery : deliveries) {
        Connection& connection = *delivery.connection;
        const bool sent = delivery.effect.close_after
                              ? connection.send_final_message(delivery.effect.lines)
                              : connection.send_message(delivery.effect.lines);
        // The handler notices the flag, cleans up, and closes.
        if (!sent) connection.shutdown_both();
    }
}
