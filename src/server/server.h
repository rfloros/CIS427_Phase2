// Server runtime: the accept loop, one connection handler at a time, and the
// timer thread. Phase 2 serves one client at a time; the handler is already a
// function that takes one connection, so Phase 3 can run it on a thread each.
#ifndef SERVER_SERVER_H
#define SERVER_SERVER_H

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common/connection.h"
#include "server/game.h"
#include "server/logger.h"

struct ServerOptions {
    std::vector<Question> questions;
    Timings timings;
    uint64_t seed = 0;
    Logger* logger = nullptr;
    int port = 0;  // 0 picks a free port (tests only; the command line rejects 0)
};

class Server {
public:
    explicit Server(const ServerOptions& options);
    ~Server();

    // Binds and listens. On failure `error` says why.
    bool start(std::string& error);
    int port() const { return port_; }

    // Runs the accept loop until request_stop(); then joins the timer thread.
    void run();

    // Safe to call from a signal handler or any thread.
    void request_stop();

private:
    void accept_loop();
    void timer_loop();
    void handle_connection(ConnId id, Connection& connection);
    void process_line(ConnId id, const std::string& line);
    void process_too_long_line(ConnId id);
    void drain_until_peer_closes(Connection& connection);
    void finish_connection(ConnId id);

    // Both need coordinator_ held. They log, then write the effects in order.
    void apply(const Outcome& outcome);
    void deliver(const std::vector<Effect>& effects);

    double now_seconds() const;

    ServerOptions options_;
    Logger& logger_;
    GameManager manager_;

    int listen_fd_ = -1;
    int port_ = 0;
    int wake_pipe_[2] = {-1, -1};
    std::atomic<bool> stopping_{false};
    std::thread timer_thread_;

    // Serializes every command, timer tick, and disconnect from the state
    // change until its messages are written, so reply-before-push holds.
    // Lock order: coordinator_, then the manager's own mutex (inside calls).
    std::mutex coordinator_;
    std::map<ConnId, Connection*> live_connections_;  // guarded by coordinator_
    ConnId next_conn_id_ = 0;
    std::chrono::steady_clock::time_point started_at_;
};

#endif
