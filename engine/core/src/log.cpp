#include <atomic>
#include <cstdio>
#include <iterator>

#include "core/log.h"
#include "core/assert.h"

namespace aurora {
namespace logging {

namespace {

struct Sink {
    LogSinkFn fn;
    void* user_data;
};

constexpr usize max_sinks = 8;

void console_sink(const LogMessage& message, void* /*user_data*/) {
    const char* file = message.file;
    for (const char* p = message.file; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            file = p + 1;
        }
    }

    const int length = static_cast<int>(message.text.size());
    if (message.level >= LogLevel::Warn) {
        std::fprintf(stderr, "[%-5s] %.*s  (%s:%d)\n", level_name(message.level), length, message.text.data(), file, message.line);
    } else {
        std::fprintf(stderr, "[%-5s] %.*s\n", level_name(message.level), length, message.text.data());
    }
}

constinit Sink g_sinks[max_sinks] = {{console_sink, nullptr}};
constinit std::atomic<usize> g_sink_count{1};
constinit thread_local bool t_dispatching = false;

static_assert(std::atomic<usize>::is_always_lock_free);

struct FixedText {
    using value_type = char;

    char* data;
    usize capacity;
    usize size = 0;
    bool  truncated = false;

    void push_back(char c) {
        if (size < capacity) {
            data[size++] = c;
        } else {
            truncated = true;
        }
    }
};

} // namespace

void add_sink(LogSinkFn fn, void* user_data) {
    const usize count = g_sink_count.load(std::memory_order_relaxed);
    AURORA_ASSERT(count < max_sinks, "sink limit of {} reached", max_sinks);
    if (count == max_sinks) {
        return;
    }
    g_sinks[count] = {fn, user_data};
    g_sink_count.store(count + 1, std::memory_order_release);
}

const char* level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
    }
    return "?";
}

namespace detail {

std::string_view vformat_into(char (&buffer)[max_message_length], std::string_view fmt, std::format_args args) {
    FixedText text{buffer, max_message_length};
    std::vformat_to(std::back_inserter(text), fmt, args);

    if (text.truncated) {
        buffer[max_message_length - 3] = '.';
        buffer[max_message_length - 2] = '.';
        buffer[max_message_length - 1] = '.';
    }
    return {buffer, text.size};
}

void dispatch(LogLevel level, const char* file, i32 line, std::string_view text) {
    if (t_dispatching) {
        return;
    }
    t_dispatching = true;

    const LogMessage message{level, text, file, line};
    const usize count = g_sink_count.load(std::memory_order_acquire);
    for (usize i = 0; i < count; ++i) {
        g_sinks[i].fn(message, g_sinks[i].user_data);
    }

    t_dispatching = false;
}

void vwrite(LogLevel level, const char* file, i32 line, std::string_view fmt, std::format_args args) {
    char buffer[max_message_length];
    dispatch(level, file, line, vformat_into(buffer, fmt, args));
}

} // namespace detail
} // namespace logging
} // namespace aurora
