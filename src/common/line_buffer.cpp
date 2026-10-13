#include "common/line_buffer.h"

void LineBuffer::append(const char* data, size_t size) { buffer_.append(data, size); }

bool LineBuffer::next(LineEvent& event) {
    while (true) {
        const size_t newline = buffer_.find('\n');

        if (discarding_) {
            if (newline == std::string::npos) {
                buffer_.clear();
                return false;
            }
            buffer_.erase(0, newline + 1);
            discarding_ = false;
            continue;
        }

        if (newline == std::string::npos) {
            // Even a newline arriving next could not make this line fit.
            if (buffer_.size() >= kMaxLineBytes) {
                buffer_.clear();
                discarding_ = true;
                event = LineEvent{LineEvent::TooLong, ""};
                return true;
            }
            return false;
        }

        if (newline + 1 > kMaxLineBytes) {
            buffer_.erase(0, newline + 1);
            event = LineEvent{LineEvent::TooLong, ""};
            return true;
        }

        std::string text = buffer_.substr(0, newline);
        buffer_.erase(0, newline + 1);
        if (!text.empty() && text.back() == '\r') text.pop_back();
        event = LineEvent{LineEvent::Line, std::move(text)};
        return true;
    }
}
