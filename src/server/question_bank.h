// Loads the multiple-choice question file (one question per line, six fields
// separated by a vertical bar).
#ifndef SERVER_QUESTION_BANK_H
#define SERVER_QUESTION_BANK_H

#include <array>
#include <istream>
#include <string>
#include <vector>

struct Question {
    std::string text;
    std::array<std::string, 4> options;
    char correct = 'A';  // 'A' to 'D'
};

struct QuestionBank {
    std::vector<Question> questions;
    // One entry per skipped line; the server logs them once the logger exists.
    std::vector<std::string> warnings;
};

QuestionBank parse_question_bank(std::istream& input);

// False (with `error` set) if the file cannot be read or has no valid question.
// `bank.warnings` is filled either way.
bool load_question_bank(const std::string& path, QuestionBank& bank, std::string& error);

#endif
