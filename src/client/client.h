// The terminal client: the main thread reads the user's commands, a reader
// thread owns the socket's receive side and shows pushes the moment they arrive.
#ifndef CLIENT_CLIENT_H
#define CLIENT_CLIENT_H

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "client/reply_queue.h"
#include "common/connection.h"
#include "common/message_assembler.h"

struct ClientOptions {
    std::string session_path = ".termtrivia_session";
    int input_fd = 0;  // where typed commands come from (stdin)
    // Everything shown to the user goes through here (stdout by default).
    std::function<void(const std::string&)> print;
};

class Client {
public:
    // Takes ownership of the connected socket.
    Client(int socket_fd, const ClientOptions& options);
    ~Client();

    // Runs until the user quits, the game ends, or the server closes.
    // Returns the process exit status (0 for every normal ending).
    int run();

private:
    void reader_loop();
    void show_push(const Message& push);
    void on_server_closed();
    void finish();  // wakes the main thread so it can exit

    void show(const std::string& text);
    void show_prompt();
    void handle_typed_line(const std::string& line);
    void show_reply(const std::string& command_word, const std::string& wire, const Message& reply);

    ClientOptions options_;
    Connection connection_;
    ReplyQueue replies_;
    std::thread reader_;

    std::mutex output_mutex_;
    std::atomic<bool> at_prompt_{false};
    std::atomic<bool> finished_{false};   // set by the reader (GAME_END, server closed)
    std::atomic<bool> exiting_{false};    // set by the main thread before it closes
    bool quit_sent_ = false;
    int wake_pipe_[2] = {-1, -1};
};

#endif
