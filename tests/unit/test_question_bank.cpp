// Slice 3 (part): question bank loading rules.
#include <fstream>
#include <sstream>

#include "minitest.h"
#include "common/protocol.h"
#include "server/question_bank.h"

namespace {

QuestionBank parse_text(const std::string& text) {
    std::istringstream input(text);
    return parse_question_bank(input);
}

const char* kGood = "What is 1+1?|One|Two|Three|Four|B\n";

}  // namespace

TEST_CASE("Slice3 bank: a valid line loads with its fields") {
    const QuestionBank bank = parse_text(kGood);
    CHECK_EQ(bank.questions.size(), static_cast<size_t>(1));
    CHECK_EQ(bank.questions[0].text, std::string("What is 1+1?"));
    CHECK_EQ(bank.questions[0].options[1], std::string("Two"));
    CHECK_EQ(bank.questions[0].correct, 'B');
    CHECK(bank.warnings.empty());
}

TEST_CASE("Slice3 bank: blank lines and comments are ignored without warnings") {
    const QuestionBank bank = parse_text(std::string("# comment\n\n   \n") + kGood);
    CHECK_EQ(bank.questions.size(), static_cast<size_t>(1));
    CHECK(bank.warnings.empty());
}

TEST_CASE("Slice3 bank: malformed lines are skipped with a warning naming the line") {
    const std::string text = std::string("only|three|fields\n") +             // line 1
                             "q|a|b|c|d|E\n" +                               // 2: bad letter
                             "q|a|b|c|d|b\n" +                               // 3: lower case letter
                             "q||b|c|d|A\n" +                                // 4: empty field
                             "q|a|b|c|d|A|extra\n" +                         // 5: too many fields
                             "q|a|b|c|d|AB\n" +                              // 6: letter too long
                             "caf\xC3\xA9|a|b|c|d|A\n" +                     // 7: non-ASCII
                             kGood;                                          // 8: good
    const QuestionBank bank = parse_text(text);
    CHECK_EQ(bank.questions.size(), static_cast<size_t>(1));
    CHECK_EQ(bank.warnings.size(), static_cast<size_t>(7));
    CHECK(bank.warnings[0].find("line 1") != std::string::npos);
    CHECK(bank.warnings[6].find("line 7") != std::string::npos);
}

TEST_CASE("Slice3 bank: CRLF line endings are accepted") {
    const QuestionBank bank = parse_text("What?|a|b|c|d|C\r\n");
    CHECK_EQ(bank.questions.size(), static_cast<size_t>(1));
    CHECK_EQ(bank.questions[0].correct, 'C');
}

TEST_CASE("Slice3 bank: every formatted question line fits the 512 byte limit") {
    const std::string too_long_text(500, 'x');
    const std::string too_long_option(509, 'y');
    const std::string fits_text(480, 'x');
    const QuestionBank bank =
        parse_text(too_long_text + "|a|b|c|d|A\n" + "q|" + too_long_option + "|b|c|d|A\n" +
                   fits_text + "|a|b|c|d|A\n");
    CHECK_EQ(bank.questions.size(), static_cast<size_t>(1));
    CHECK_EQ(bank.warnings.size(), static_cast<size_t>(2));
    const Question& q = bank.questions[0];
    const auto lines = push_question(2147483647, q.text, q.options);
    for (const auto& line : lines) CHECK(line.size() + 1 <= 512);
}

TEST_CASE("Slice3 bank: loading a missing file reports an error") {
    QuestionBank bank;
    std::string error;
    CHECK(!load_question_bank("/nonexistent/questions.txt", bank, error));
    CHECK(!error.empty());
}

TEST_CASE("Slice3 bank: a file with zero valid questions is refused") {
    const std::string path = "/tmp/termtrivia_empty_bank.txt";
    { std::ofstream(path) << "# nothing here\nbad line\n"; }
    QuestionBank bank;
    std::string error;
    CHECK(!load_question_bank(path, bank, error));
    CHECK(!error.empty());
    CHECK_EQ(bank.warnings.size(), static_cast<size_t>(1));  // warnings still returned for logging
}

TEST_CASE("Slice3 bank: the shipped questions.txt has at least 20 valid questions") {
    QuestionBank bank;
    std::string error;
    CHECK(load_question_bank("questions.txt", bank, error));
    CHECK(bank.questions.size() >= 20);
    CHECK(bank.warnings.empty());
}
