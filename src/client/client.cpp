#include "client/client.h"

#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <iostream>

#include "client/client_ui.h"
#include "client/session_file.h"

Client::Client(int socket_fd, const ClientOptions& options)
    : options_(options), connection_(socket_fd) {
    if (!options_.print) {
        options_.print = [](const std::string& text) {
            std::cout << text << std::flush;
        };
    }
    if (pipe(wake_pipe_) != 0) wake_pipe_[0] = wake_pipe_[1] = -1;
}

Client::~Client() {
    if (wake_pipe_[0] >= 0) close(wake_pipe_[0]);
    if (wake_pipe_[1] >= 0) close(wake_pipe_[1]);
}

void Client::show(const std::string& text) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    options_.print(text);
}

void Client::show_prompt() {
    std::lock_guard<std::mutex> lock(output_mutex_);
    at_prompt_ = true;
    options_.print(prompt_text());
}

// Called from either thread; the main thread polls this pipe along with stdin.
void Client::finish() {
    finished_ = true;
    const char wake = 'x';
    ssize_t ignored = write(wake_pipe_[1], &wake, 1);
    (void)ignored;
    replies_.close();
}

int Client::run() {
    show(command_list_text() + "\n");
    Session saved;
    if (load_session(options_.session_path, saved)) {
        show("A saved session was found. Type RECONNECT to rejoin game " + saved.code + ".\n");
    }
    reader_ = std::thread([this] { reader_loop(); });
    show_prompt();

    // The main thread alone reads the keyboard. It waits on the input and on
    // the wake-up pipe, so a server close or GAME_END ends an idle prompt.
    std::string typed;
    while (!finished_) {
        pollfd waiting[2] = {{options_.input_fd, POLLIN, 0}, {wake_pipe_[0], POLLIN, 0}};
        if (poll(waiting, 2, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (waiting[1].revents != 0) break;

        char chunk[512];
        const ssize_t got = read(options_.input_fd, chunk, sizeof(chunk));
        if (got == 0) break;  // end of input: leave the session saved for RECONNECT
        if (got < 0) {
            if (errno == EINTR) continue;
            break;
        }
        typed.append(chunk, static_cast<size_t>(got));

        size_t newline;
        while (!finished_ && (newline = typed.find('\n')) != std::string::npos) {
            const std::string line = typed.substr(0, newline);
            typed.erase(0, newline + 1);
            handle_typed_line(line);
        }
    }

    exiting_ = true;
    connection_.shutdown_both();  // wakes the reader blocked in recv
    reader_.join();
    return 0;
}

void Client::handle_typed_line(const std::string& line) {
    at_prompt_ = false;
    Session saved;
    const bool have_saved = load_session(options_.session_path, saved);
    const InputAction action =
        interpret_input(line, have_saved ? std::optional<Session>(saved) : std::nullopt);

    if (action.kind != InputAction::Send) {
        if (!action.text.empty()) show(action.text + "\n");
        if (!finished_) show_prompt();
        return;
    }

    if (!connection_.send_raw(action.wire + "\n")) {
        on_server_closed();
        return;
    }
    Message reply;
    if (!replies_.pop(reply)) return;  // the connection closed while we waited

    const std::string word = action.wire.substr(0, action.wire.find(' '));
    show_reply(word, action.wire, reply);
    if (word == "QUIT" && reply.lines[0].rfind("200", 0) == 0) {
        remove_session(options_.session_path);
        finish();
        return;
    }
    if (!finished_) show_prompt();
}

void Client::show_reply(const std::string& command_word, const std::string& wire,
                        const Message& reply) {
    const bool success = reply.lines[0] == "200 OK";
    const bool has_three_lines = reply.lines.size() == 3;

    if (success && has_three_lines && command_word == "CREATE_GAME") {
        // lines: 200 OK, join code, token. The token is saved, never shown.
        const std::string name = wire.substr(wire.find(' ') + 1, wire.rfind(' ') - wire.find(' ') - 1);
        save_session(options_.session_path, Session{name, reply.lines[1], reply.lines[2]});
        show("Game created. Join code: " + reply.lines[1] +
             "\nShare this code with the other players.\n");
        return;
    }
    if (success && has_three_lines && command_word == "JOIN_GAME") {
        const size_t first_space = wire.find(' ');
        const std::string code = wire.substr(first_space + 1, wire.rfind(' ') - first_space - 1);
        const std::string name = wire.substr(wire.rfind(' ') + 1);
        save_session(options_.session_path, Session{name, code, reply.lines[2]});
        show(reply.lines[1] + "\n");
        return;
    }
    if (success && reply.lines.size() == 2 && command_word == "RECONNECT") {
        show("Reconnected. Your score and question number: " + reply.lines[1] + "\n");
        return;
    }

    std::string text;
    for (const auto& line : reply.lines) text += line + "\n";
    show(text);
}

void Client::reader_loop() {
    MessageAssembler assembler;
    while (true) {
        std::string line;
        const auto status = connection_.read_line(line, -1);
        if (status == Connection::ReadStatus::Line) {
            Message message;
            if (!assembler.add_line(line, message)) continue;
            if (message.is_push) {
                show_push(message);
            } else {
                replies_.push(message);
            }
        } else if (status == Connection::ReadStatus::TooLong) {
            continue;  // the server never sends lines this long
        } else {
            on_server_closed();
            return;
        }
        if (finished_) return;
    }
}

void Client::show_push(const Message& push) {
    const bool game_over = push.lines[0].rfind("PUSH GAME_END", 0) == 0;
    {
        std::lock_guard<std::mutex> lock(output_mutex_);
        options_.print("\n" + format_push(push) + "\n");
        if (game_over) {
            options_.print("The game is over. Goodbye!\n");
        } else if (at_prompt_) {
            options_.print(prompt_text());  // the user was idle: show the prompt again
        }
    }
    if (game_over) {
        remove_session(options_.session_path);
        finish();
    }
}

void Client::on_server_closed() {
    if (!exiting_ && !finished_) show("\nServer closed the connection.\n");
    finish();
}
