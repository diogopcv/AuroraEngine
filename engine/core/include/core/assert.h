#pragma once

#include "core/log.h"

#if defined(_MSC_VER)
    #define AURORA_DEBUG_BREAK() __debugbreak()
#elif defined(__clang__)
    #define AURORA_DEBUG_BREAK() __builtin_debugtrap()
#else
    #include <csignal>
    #define AURORA_DEBUG_BREAK() std::raise(SIGTRAP)
#endif

#if !defined(AURORA_ASSERTS_ENABLED)
    #define AURORA_ASSERTS_ENABLED AURORA_DEBUG
#endif

namespace aurora {
namespace detail {

void report_failure(const char* kind, const char* expression, const char* file, i32 line, std::string_view message);

[[noreturn]] void fatal_exit();

template <typename... Args>
void report_failure_fmt(const char* kind, const char* expression, const char* file, i32 line, std::format_string<Args...> fmt, Args&&... args) {
    char buffer[logging::max_message_length];
    report_failure(kind, expression, file, line, logging::detail::vformat_into(buffer, fmt.get(), std::make_format_args(args...)));
}

} // namespace detail
} // namespace aurora

#define AURORA_ASSERT(cond, ...)                                              \
    do {                                                                      \
        if constexpr (AURORA_ASSERTS_ENABLED) {                               \
            if (!(cond)) [[unlikely]] {                                       \
                ::aurora::detail::report_failure_fmt(                         \
                    "ASSERT", #cond, __FILE__, __LINE__, "" __VA_ARGS__);     \
                AURORA_DEBUG_BREAK();                                         \
            }                                                                 \
        }                                                                     \
    } while (0)

#define AURORA_FATAL(...)                                                     \
    do {                                                                      \
        ::aurora::detail::report_failure_fmt(                                 \
            "FATAL", nullptr, __FILE__, __LINE__, __VA_ARGS__);               \
        if constexpr (AURORA_ASSERTS_ENABLED) {                               \
            AURORA_DEBUG_BREAK();                                             \
        }                                                                     \
        ::aurora::detail::fatal_exit();                                       \
    } while (0)

#define AURORA_UNREACHABLE() AURORA_FATAL("Unreachable code was executed")