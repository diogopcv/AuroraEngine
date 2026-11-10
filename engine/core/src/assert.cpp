#include "core/assert.h"

#include <cstdlib>

namespace aurora {
namespace detail {

void report_failure(const char* kind, const char* expression, const char* file, i32 line, std::string_view message) {
    const LogLevel level = (kind[0] == 'F') ? LogLevel::Fatal : LogLevel::Error;

    char buffer[logging::max_message_length];
    std::string_view text;

    if (expression != nullptr && !message.empty()) {
        text = logging::detail::vformat_into(buffer, "{} failed: {} | {}", std::make_format_args(kind, expression, message));
    } else if (expression != nullptr) {
        text = logging::detail::vformat_into(buffer, "{} failed: {}", std::make_format_args(kind, expression));
    } else {
        text = message;
    }

    logging::detail::dispatch(level, file, line, text);
}

void fatal_exit() {
    std::abort();
}

} // namespace detail
} // namespace aurora
