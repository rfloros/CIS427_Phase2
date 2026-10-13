// What the user types and what the client prints, as pure functions.
#ifndef CLIENT_CLIENT_UI_H
#define CLIENT_CLIENT_UI_H

#include <optional>
#include <string>

#include "client/session_file.h"
#include "common/message_assembler.h"

struct InputAction {
    enum Kind {
        Nothing,  // empty input: just prompt again
        Show,     // print `text` and prompt again (help, usage, mistakes)
        Send      // send `wire` to the server
    };
    Kind kind = Nothing;
    std::string wire;
    std::string text;
};

std::string command_list_text();
// The line printed before every input position, with the valid formats.
std::string prompt_text();

// Turns one typed line into an action. The command word is case-insensitive
// and is uppercased; arguments keep their case. `saved` supplies the
// arguments of a bare RECONNECT.
InputAction interpret_input(const std::string& line, const std::optional<Session>& saved);

// A push as the user sees it ("PUSH " is dropped from the first line).
std::string format_push(const Message& push);

#endif
