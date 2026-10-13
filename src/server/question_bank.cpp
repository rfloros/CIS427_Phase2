#include "server/question_bank.h"

#include <algorithm>
#include <fstream>

#include "common/protocol.h"

namespace {

const size_t kFieldCount = 6;

std::vector<std::string> split_on_bars(const std::string& line) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        const size_t bar = line.find('|', start);
        if (bar == std::string::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }
        fields.push_back(line.substr(start, bar - start));
        start = bar + 1;
    }
}

std::string trim_spaces(const std::string& text) {
    const size_t first = text.find_first_not_of(' ');
    if (first == std::string::npos) return "";
    return text.substr(first, text.find_last_not_of(' ') - first + 1);
}

bool is_printable_ascii(const std::string& text) {
    return std::all_of(text.begin(), text.end(), [](char c) {
        const unsigned char byte = static_cast<unsigned char>(c);
        return byte >= 0x20 && byte <= 0x7E;
    });
}

// Returns an empty string when the line is a good question, otherwise the reason.
std::string build_question(const std::string& line, Question& question) {
    std::vector<std::string> fields = split_on_bars(line);
    if (fields.size() != kFieldCount) return "expected 6 fields separated by |";
    for (auto& field : fields) {
        field = trim_spaces(field);
        if (field.empty()) return "a field is empty";
        if (!is_printable_ascii(field)) return "non-ASCII text";
    }
    if (fields[5].size() != 1 || fields[5][0] < 'A' || fields[5][0] > 'D') {
        return "correct letter must be A, B, C, or D";
    }

    question.text = fields[0];
    for (size_t i = 0; i < 4; ++i) question.options[i] = fields[1 + i];
    question.correct = fields[5][0];

    // The worst-case question number (10 digits) must still fit the line limit.
    for (const auto& pushed_line : push_question(2147483647, question.text, question.options)) {
        if (pushed_line.size() + 1 > 512) return "text too long for a 512 byte protocol line";
    }
    return "";
}

}  // namespace

QuestionBank parse_question_bank(std::istream& input) {
    QuestionBank bank;
    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string trimmed = trim_spaces(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        Question question;
        const std::string problem = build_question(line, question);
        if (problem.empty()) {
            bank.questions.push_back(question);
        } else {
            bank.warnings.push_back("skipped question file line " + std::to_string(line_number) +
                                    ": " + problem);
        }
    }
    return bank;
}

bool load_question_bank(const std::string& path, QuestionBank& bank, std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = "cannot open question file " + path;
        return false;
    }
    bank = parse_question_bank(file);
    if (bank.questions.empty()) {
        error = "question file " + path + " has no valid questions";
        return false;
    }
    return true;
}
