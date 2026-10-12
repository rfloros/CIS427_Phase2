#include "server/protocol.h"

#include <algorithm>
#include <cctype>

namespace {

const size_t kMaxNameLength = 20;
const size_t kCodeLength = 4;

std::vector<std::string> split_on_single_spaces(const std::string& line) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t space = line.find(' ', start);
        if (space == std::string::npos) {
            parts.push_back(line.substr(start));
            return parts;
        }
        parts.push_back(line.substr(start, space - start));
        start = space + 1;
    }
}

bool is_printable_ascii(const std::string& text) {
    return std::all_of(text.begin(), text.end(), [](char c) {
        const unsigned char byte = static_cast<unsigned char>(c);
        return byte >= 0x20 && byte <= 0x7E;
    });
}

bool is_four_alphanumeric(const std::string& text) {
    return text.size() == kCodeLength &&
           std::all_of(text.begin(), text.end(),
                       [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; });
}

std::string to_upper(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

ParseResult format_error() { return ParseResult{false, Command{}, 300}; }
ParseResult count_error() { return ParseResult{false, Command{}, 423}; }

ParseResult accept(const Command& command) { return ParseResult{true, command, 0}; }

// Whole-string non-negative integer; anything else (letters, signs, overflow)
// is "not a valid count".
bool parse_count(const std::string& text, int& value) {
    if (text.empty() || text.size() > 9) return false;
    if (!std::all_of(text.begin(), text.end(),
                     [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
        return false;
    }
    value = std::stoi(text);
    return true;
}

ParseResult parse_create_game(const std::vector<std::string>& parts, int available) {
    if (parts.size() < 2 || parts.size() > 3) return format_error();
    if (!is_valid_name(parts[1])) return format_error();  // name error wins over count error
    int count = 0;
    if (parts.size() == 2 || !parse_count(parts[2], count) || count < 1 || count > available) {
        return count_error();
    }
    Command command;
    command.type = CommandType::CreateGame;
    command.name = parts[1];
    command.num_questions = count;
    return accept(command);
}

ParseResult parse_join_game(const std::vector<std::string>& parts) {
    if (parts.size() != 3 || !is_four_alphanumeric(parts[1]) || !is_valid_name(parts[2])) {
        return format_error();
    }
    Command command;
    command.type = CommandType::JoinGame;
    command.code = to_upper(parts[1]);
    command.name = parts[2];
    return accept(command);
}

ParseResult parse_submit_answer(const std::vector<std::string>& parts) {
    if (parts.size() != 2 || parts[1].size() != 1) return format_error();
    const char choice = static_cast<char>(std::toupper(static_cast<unsigned char>(parts[1][0])));
    if (choice < 'A' || choice > 'D') return format_error();
    Command command;
    command.type = CommandType::SubmitAnswer;
    command.choice = choice;
    return accept(command);
}

ParseResult parse_reconnect(const std::vector<std::string>& parts) {
    if (parts.size() != 4 || !is_valid_name(parts[1]) || !is_four_alphanumeric(parts[2]) ||
        !is_four_alphanumeric(parts[3])) {
        return format_error();
    }
    Command command;
    command.type = CommandType::Reconnect;
    command.name = parts[1];
    command.code = to_upper(parts[2]);
    command.token = parts[3];
    return accept(command);
}

ParseResult parse_no_argument_command(const std::vector<std::string>& parts, CommandType type) {
    if (parts.size() != 1) return format_error();
    Command command;
    command.type = type;
    return accept(command);
}

}  // namespace

bool is_valid_name(const std::string& name) {
    // ':' would break the "name:score" leaderboard lines.
    return !name.empty() && name.size() <= kMaxNameLength && is_printable_ascii(name) &&
           name.find(' ') == std::string::npos && name.find(':') == std::string::npos;
}

ParseResult parse_command(const std::string& line, int available_questions) {
    if (line.empty() || !is_printable_ascii(line)) return format_error();
    const std::vector<std::string> parts = split_on_single_spaces(line);
    const std::string& word = parts[0];

    if (word == "CREATE_GAME") return parse_create_game(parts, available_questions);
    if (word == "JOIN_GAME") return parse_join_game(parts);
    if (word == "SUBMIT_ANSWER") return parse_submit_answer(parts);
    if (word == "RECONNECT") return parse_reconnect(parts);
    if (word == "START_ROUND") return parse_no_argument_command(parts, CommandType::StartRound);
    if (word == "LEADERBOARD") return parse_no_argument_command(parts, CommandType::Leaderboard);
    if (word == "QUIT") return parse_no_argument_command(parts, CommandType::Quit);
    if (word == "END_GAME") return parse_no_argument_command(parts, CommandType::EndGame);
    return format_error();
}

const char* command_name(CommandType type) {
    switch (type) {
        case CommandType::CreateGame: return "CREATE_GAME";
        case CommandType::JoinGame: return "JOIN_GAME";
        case CommandType::StartRound: return "START_ROUND";
        case CommandType::SubmitAnswer: return "SUBMIT_ANSWER";
        case CommandType::Leaderboard: return "LEADERBOARD";
        case CommandType::Reconnect: return "RECONNECT";
        case CommandType::Quit: return "QUIT";
        case CommandType::EndGame: return "END_GAME";
    }
    return "UNKNOWN";
}

std::string status_line(int code, int available_questions) {
    switch (code) {
        case 200: return "200 OK";
        case 300: return "300 message format error";
        case 401: return "401 You are not currently in a game, join or reconnect first.";
        case 402: return "402 Only the host may execute this command.";
        case 404: return "404 No game exists with that code.";
        case 409: return "409 That name is already in use in this game.";
        case 410:
            return "410 Wrong name, game code, or token, or the reconnect window has expired.";
        case 420: return "420 There is no active round to answer.";
        case 421: return "421 The game has already started.";
        case 422: return "422 You have already submitted an answer for this round.";
        case 423:
            return "423 Requested question count must be between 1 and " +
                   std::to_string(available_questions) + ".";
    }
    return std::to_string(code);
}

Lines reply_ok() { return {status_line(200)}; }

Lines reply_error(int code, int available_questions) {
    return {status_line(code, available_questions)};
}

Lines reply_create(const std::string& game_code, const std::string& token) {
    return {status_line(200), game_code, token};
}

Lines reply_join(int player_count, const std::string& game_code, const std::string& token) {
    return {status_line(200), std::to_string(player_count) + " players in game " + game_code,
            token};
}

Lines reply_leaderboard(const std::vector<Standing>& standings) {
    Lines lines{status_line(200)};
    for (const auto& standing : standings) {
        lines.push_back(standing.name + ":" + std::to_string(standing.score));
    }
    return lines;
}

Lines reply_reconnect(int score, int question_number) {
    return {status_line(200), std::to_string(score) + " " + std::to_string(question_number)};
}

Lines push_question(int number, const std::string& text,
                    const std::array<std::string, 4>& options) {
    Lines lines{"PUSH QUESTION " + std::to_string(number) + " " + text};
    for (size_t i = 0; i < options.size(); ++i) {
        lines.push_back(std::string(1, static_cast<char>('A' + i)) + ") " + options[i]);
    }
    return lines;
}

Lines push_round_result(int number, char correct, const std::vector<Standing>& standings) {
    Lines lines{"PUSH ROUND_RESULT " + std::to_string(number),
                std::string("Correct answer: ") + correct};
    for (const auto& standing : standings) {
        lines.push_back(standing.name + ":" + std::to_string(standing.score));
    }
    return lines;
}

Lines push_game_end(GameEndReason reason, const std::vector<Standing>& standings) {
    const char* reason_text = "COMPLETE";
    if (reason == GameEndReason::HostEnded) reason_text = "HOST_ENDED";
    if (reason == GameEndReason::HostQuit) reason_text = "HOST_QUIT";
    Lines lines{std::string("PUSH GAME_END ") + reason_text, "FINAL RESULTS"};
    for (const auto& standing : standings) {
        lines.push_back(standing.name + ":" + std::to_string(standing.score));
    }
    return lines;
}

std::vector<Standing> sort_standings(std::vector<Standing> standings) {
    std::stable_sort(standings.begin(), standings.end(),
                     [](const Standing& a, const Standing& b) { return a.score > b.score; });
    return standings;
}
