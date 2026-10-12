#include "client/client_ui.h"

#include <cctype>
#include <climits>
#include <sstream>

#include "common/protocol.h"

namespace {

struct CommandHelp {
    const char* word;
    const char* usage;  // what to type
    const char* summary;
};

const CommandHelp kCommands[] = {
    {"CREATE_GAME", "CREATE_GAME <name> <count>",
     "create a game (name: 1-20 characters, no spaces or ':'; count: number of questions)"},
    {"JOIN_GAME", "JOIN_GAME <code> <name>", "join a game using its 4-character code"},
    {"START_ROUND", "START_ROUND", "host only: start the game"},
    {"SUBMIT_ANSWER", "SUBMIT_ANSWER <A|B|C|D>", "answer the open question"},
    {"LEADERBOARD", "LEADERBOARD", "show the scores"},
    {"RECONNECT", "RECONNECT", "rejoin your game after a disconnect (uses the saved session)"},
    {"END_GAME", "END_GAME", "host only: end the game for everyone"},
    {"QUIT", "QUIT", "leave for good"},
};

const CommandHelp* find_command(const std::string& word) {
    for (const auto& command : kCommands) {
        if (word == command.word) return &command;
    }
    return nullptr;
}

std::string to_upper(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

std::vector<std::string> split_on_whitespace(const std::string& line) {
    std::istringstream stream(line);
    std::vector<std::string> words;
    std::string word;
    while (stream >> word) words.push_back(word);
    return words;
}

InputAction show(const std::string& text) { return InputAction{InputAction::Show, "", text}; }

}  // namespace

std::string command_list_text() {
    std::string text = "Commands (not case-sensitive):\n";
    for (const auto& command : kCommands) {
        std::string usage = command.usage;
        usage.resize(std::max<size_t>(usage.size(), 28), ' ');
        text += "  " + usage + " " + command.summary + "\n";
    }
    text += "  help                         show this list";
    return text;
}

std::string prompt_text() {
    return "Commands: CREATE_GAME <name> <count> | JOIN_GAME <code> <name> | START_ROUND | "
           "SUBMIT_ANSWER <A|B|C|D> | LEADERBOARD | RECONNECT | END_GAME | QUIT | help\n> ";
}

InputAction interpret_input(const std::string& line, const std::optional<Session>& saved) {
    std::vector<std::string> words = split_on_whitespace(line);
    if (words.empty()) return InputAction{};

    words[0] = to_upper(words[0]);
    if (words[0] == "HELP") return show(command_list_text());

    const CommandHelp* command = find_command(words[0]);
    if (command == nullptr) {
        return show("Unknown command '" + words[0] + "'. Type help to see the commands.");
    }

    if (words[0] == "RECONNECT" && words.size() == 1) {
        if (!saved) {
            return show("No saved session to reconnect to. Use: RECONNECT <name> <code> <token>");
        }
        return InputAction{InputAction::Send,
                           "RECONNECT " + saved->name + " " + saved->code + " " + saved->token, ""};
    }

    std::string wire;
    for (const auto& word : words) wire += (wire.empty() ? "" : " ") + word;
    // The server still enforces the real question limit; this only checks the shape.
    const ParseResult parsed = parse_command(wire, INT_MAX);
    if (!parsed.ok) return show(std::string("Usage: ") + command->usage);

    // The parser upper-cases join codes, so send its normalized form.
    if (parsed.command.type == CommandType::JoinGame) {
        wire = "JOIN_GAME " + parsed.command.code + " " + parsed.command.name;
    }
    return InputAction{InputAction::Send, wire, ""};
}

std::string format_push(const Message& push) {
    std::string text = push.lines[0].substr(5);  // drop "PUSH "
    for (size_t i = 1; i < push.lines.size(); ++i) text += "\n" + push.lines[i];
    return text;
}
