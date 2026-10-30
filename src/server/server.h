// Server runtime: the accept loop, one handler thread per client, and one
// timer thread per game. Each game has its own "lane" (a mutex) so one game's
// slow writes never delay another game; see docs/design-changes.md.
#ifndef SERVER_SERVER_H
#define SERVER_SERVER_H

#include <atomic>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "common/connection.h"
#include "server/game.h"
#include "server/logger.h"

enum class WorkerKind { Handler, GameTimer };

struct ServerOptions {
    std::vector<Question> questions;
    Timings timings;
    uint64_t seed = 0;
    Logger* logger = nullptr;
    int port = 0;  // 0 picks a free port (tests only; the command line rejects 0)
    // Tests only: return true to make the next launch of that kind of worker fail.
    std::function<bool(WorkerKind)> fail_worker_launch;
};

class Server {
public:
    explicit Server(const ServerOptions& options);
    ~Server();

    // Binds and listens. On failure `error` says why.
    bool start(std::string& error);
    int port() const { return port_; }

    // Runs the accept loop until request_stop(); then joins every worker.
    void run();

    // Safe to call from a signal handler or any thread.
    void request_stop();

    // Tests only: handler and game-timer threads not yet joined.
    size_t worker_count();

private:
    // Serializes everything that happens in one game, from the state change
    // until its messages are written, so reply-before-push holds.
    struct Lane {
        std::mutex mutex;
    };
    using LanePtr = std::shared_ptr<Lane>;
    using ConnectionPtr = std::shared_ptr<Connection>;

    struct Worker {
        std::thread thread;
        std::atomic<bool> done{false};
    };
    using WorkerList = std::list<std::unique_ptr<Worker>>;

    // An effect paired with the connection it goes to, resolved while the
    // state change was still protected.
    struct Delivery {
        ConnectionPtr connection;
        Effect effect;
    };

    void accept_loop();
    void start_connection_worker(int fd);
    void handle_connection(ConnId id, ConnectionPtr connection);
    void process_line(ConnId id, ConnectionPtr connection, const LanePtr& own_lane,
                      const std::string& line);
    void process_too_long_line(ConnId id, const ConnectionPtr& connection);
    void drain_until_peer_closes(Connection& connection);
    void finish_connection(ConnId id, const LanePtr& own_lane);
    void handle_create(ConnId id, const Command& command, const LanePtr& own_lane);

    void game_timer_loop(std::string code, LanePtr lane);
    bool start_game_timer(const std::string& code, const LanePtr& lane);
    bool launch(WorkerList& list, WorkerKind kind, std::function<void()> body);
    void reap_finished(WorkerList& list);
    void join_all(WorkerList& list);

    // The lane that serializes this connection's next command, with its mutex
    // held. A game's lane may be replaced between lookup and lock, so the
    // lookup is repeated after locking until both agree.
    std::unique_lock<std::mutex> lock_lane(ConnId id, const Command* command,
                                           const LanePtr& own_lane, LanePtr& lane);
    LanePtr find_lane(ConnId id, const Command* command, const LanePtr& own_lane);

    // Both need routing_mutex_. They end finished games' lanes and pair each
    // effect with its connection.
    std::vector<Delivery> route(const Outcome& outcome);
    // Logs, then writes in order. Called with a lane held and no other lock.
    void apply(const Outcome& outcome, std::vector<Delivery> deliveries);
    void deliver(const std::vector<Delivery>& deliveries);

    double now_seconds() const;

    ServerOptions options_;
    Logger& logger_;
    GameManager manager_;

    int listen_fd_ = -1;
    int port_ = 0;
    int wake_pipe_[2] = {-1, -1};
    std::atomic<bool> stopping_{false};

    // Lock order: a lane, then routing_mutex_, then the manager's own mutex
    // (inside its calls). routing_mutex_ never covers socket I/O.
    std::mutex routing_mutex_;
    std::map<std::string, LanePtr> lanes_;               // by game code
    std::map<ConnId, ConnectionPtr> live_connections_;   // both guarded by routing_mutex_
    ConnId next_conn_id_ = 0;

    std::mutex workers_mutex_;
    WorkerList handlers_;
    WorkerList game_timers_;

    std::chrono::steady_clock::time_point started_at_;
};

#endif
