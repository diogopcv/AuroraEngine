#include "test/aurora_test.h"

#include "core/log.h"

#include <string>
#include <string_view>

using namespace aurora;

namespace {

struct CaptureSink {
    bool        enabled       = false;
    bool        log_from_sink = false; // used by the reentrancy test
    u32         count         = 0;
    LogLevel    level         = LogLevel::Trace;
    std::string text;
    std::string file;
    i32         line          = 0;
};

CaptureSink g_capture;

void capture_sink(const LogMessage& message, void* user_data) {
    auto* capture = static_cast<CaptureSink*>(user_data);
    if (!capture->enabled) {
        return;
    }
    ++capture->count;
    capture->level = message.level;
    capture->text.assign(message.text);
    capture->file.assign(message.file);
    capture->line = message.line;

    if (capture->log_from_sink) {
        AURORA_LOG_INFO("logged from inside a sink"); 
    }
}

struct ScopedCapture {
    ScopedCapture() {
        static const bool registered = [] {
            logging::add_sink(capture_sink, &g_capture);
            return true;
        }();
        static_cast<void>(registered);
        g_capture = CaptureSink{};
        g_capture.enabled = true;
    }
    ~ScopedCapture() { g_capture.enabled = false; }

    ScopedCapture(const ScopedCapture&) = delete;
    ScopedCapture& operator=(const ScopedCapture&) = delete;
};

bool ends_with(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

} // namespace

TEST_SUITE("log") {

TEST_CASE("vformat_into formats into the caller's buffer") {
    const i32 a = 2;
    const i32 b = 3;
    const i32 sum = a + b; // make_format_args takes lvalues
    char buffer[logging::max_message_length];
    const std::string_view text = logging::detail::vformat_into(buffer, "{} + {} = {}", std::make_format_args(a, b, sum));
    CHECK(text == "2 + 3 = 5");
    CHECK(text.data() == buffer);
}

TEST_CASE("vformat_into truncates long messages and marks them with an ellipsis") {
    const std::string long_text(2000, 'x');
    char buffer[logging::max_message_length];
    const std::string_view text = logging::detail::vformat_into(buffer, "{}", std::make_format_args(long_text));
    CHECK(text.size() == logging::max_message_length);
    CHECK(ends_with(text, "..."));
}

TEST_CASE("a message that exactly fills the buffer is not truncated") {
    const std::string exact(logging::max_message_length, 'y');
    char buffer[logging::max_message_length];
    const std::string_view text = logging::detail::vformat_into(buffer, "{}", std::make_format_args(exact));
    CHECK(text.size() == logging::max_message_length);
    CHECK(text.back() == 'y');
}

TEST_CASE("sinks receive level, text, file and line") {
    ScopedCapture capture;
    const i32 line = __LINE__ + 1;
    AURORA_LOG_WARN("value is {}", 42);

    REQUIRE(g_capture.count == 1);
    CHECK(g_capture.level == LogLevel::Warn);
    CHECK(g_capture.text == "value is 42");
    CHECK(ends_with(g_capture.file, "log_tests.cpp"));
    CHECK(g_capture.line == line);
}

TEST_CASE("Trace reaches sinks only when min_level allows it") {
    ScopedCapture capture;
    AURORA_LOG_TRACE("trace {}", 1);
    const u32 expected = (logging::min_level <= LogLevel::Trace) ? 1u : 0u;
    CHECK(g_capture.count == expected);
}

TEST_CASE("a message logged from inside a sink is dropped") {
    ScopedCapture capture;
    g_capture.log_from_sink = true;
    AURORA_LOG_INFO("outer");
    CHECK(g_capture.count == 1);
    CHECK(g_capture.text == "outer");
}

TEST_CASE("level names") {
    CHECK(std::string_view(logging::level_name(LogLevel::Trace)) == "TRACE");
    CHECK(std::string_view(logging::level_name(LogLevel::Fatal)) == "FATAL");
}

} // TEST_SUITE("log")
