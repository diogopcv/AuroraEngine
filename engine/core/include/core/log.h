#pragma once

#include "core/types.h"
#include <format>
#include <string_view>

namespace aurora {

enum class LogLevel : u8 {
    Trace,
    Info,
    Warn,
    Error,
    Fatal,
};

struct LogMessage {
    LogLevel         level;
    std::string_view text;
    const char*      file;
    i32              line;
};

using LogSinkFn = void (*)(const LogMessage& message, void* user_data);

namespace logging {

inline constexpr LogLevel min_level = AURORA_DEBUG ? LogLevel::Trace : LogLevel::Info;
constexpr usize max_message_length = 1024;

void add_sink(LogSinkFn fn, void* user_data = nullptr);
const char* level_name(LogLevel level);

namespace detail {

std::string_view vformat_into(char (&buffer)[max_message_length], std::string_view fmt, std::format_args args);
void dispatch(LogLevel level, const char* file, i32 line, std::string_view text);
void vwrite(LogLevel level, const char* file, i32 line, std::string_view fmt, std::format_args args);

template <typename... Args>
void write(LogLevel level, const char* file, i32 line, std::format_string<Args...> fmt, Args&&... args) {
    if (level < aurora::logging::min_level) {
        return;
    }
    vwrite(level, file, line, fmt.get(), std::make_format_args(args...));
}

} // namespace detail
} // namespace logging
} // namespace aurora

#define AURORA_LOG(level, fmt, ...) \
    ::aurora::logging::detail::write((level), __FILE__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)

#if AURORA_DEBUG
    #define AURORA_LOG_TRACE(fmt, ...) AURORA_LOG(::aurora::LogLevel::Trace, fmt __VA_OPT__(,) __VA_ARGS__)
#else
    #define AURORA_LOG_TRACE(fmt, ...) ((void)0)
#endif

#define AURORA_LOG_INFO(fmt, ...)  AURORA_LOG(::aurora::LogLevel::Info,  fmt __VA_OPT__(,) __VA_ARGS__)
#define AURORA_LOG_WARN(fmt, ...)  AURORA_LOG(::aurora::LogLevel::Warn,  fmt __VA_OPT__(,) __VA_ARGS__)
#define AURORA_LOG_ERROR(fmt, ...) AURORA_LOG(::aurora::LogLevel::Error, fmt __VA_OPT__(,) __VA_ARGS__)
