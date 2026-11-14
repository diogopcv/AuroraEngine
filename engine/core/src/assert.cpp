#include "core/assert.h"

#include <atomic>
#include <cstdlib>

namespace aurora {

namespace {

constinit std::atomic<AssertHandler> g_assert_handler{nullptr};

static_assert(std::atomic<AssertHandler>::is_always_lock_free);

} // namespace

void set_assert_handler(AssertHandler handler) {
    g_assert_handler.store(handler, std::memory_order_release);
}

namespace detail {

bool report_failure(const char* kind, const char* expression, const char* file, i32 line, std::string_view message) {
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

    const AssertHandler handler = g_assert_handler.load(std::memory_order_acquire);
    if (handler == nullptr) {
        return true;
    }
    return handler(expression, file, line, message);
}

void fatal_exit() {
    std::abort();
}

} // namespace detail
} // namespace aurora
