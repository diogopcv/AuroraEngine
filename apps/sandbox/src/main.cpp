#include "core/assert.h"
#include "core/log.h"
#include "core/memory/arena.h"
#include "core/memory/heap.h"
#include "core/memory/memory_report.h"
#include "core/types.h"

#include <atomic>
#include <cstring>

using namespace aurora;

namespace {

struct ProblemCounter {
    std::atomic<i32> warnings{0};
    std::atomic<i32> errors{0};
};

constinit ProblemCounter g_problems;

void counting_sink(const LogMessage& message, void* user_data) {
    auto* counter = static_cast<ProblemCounter*>(user_data);
    if (message.level == LogLevel::Warn) {
        counter->warnings.fetch_add(1, std::memory_order_relaxed);
    } else if (message.level >= LogLevel::Error) {
        counter->errors.fetch_add(1, std::memory_order_relaxed);
    }
}

bool has_arg(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    logging::add_sink(counting_sink, &g_problems);

    AURORA_LOG_INFO("sandbox initiated ({})", AURORA_DEBUG ? "Debug" : "Release");
    AURORA_LOG_TRACE("trace only appears in Debug mode: {} + {} = {}", 2, 3, 2 + 3);
    AURORA_LOG_INFO("u64 has {} bytes, usize has {} bytes", sizeof(u64), sizeof(usize));
    AURORA_LOG_WARN("this is an example warning");

    if (has_arg(argc, argv, "--assert")) {
        const i32 index = 7;
        const i32 count = 4;
        AURORA_ASSERT(index < count, "index {} out of bounds [0, {})", index, count);
        AURORA_LOG_INFO("continuing after assert");
    }

    if (has_arg(argc, argv, "--fatal")) {
        AURORA_FATAL("unrecoverable error example (code {})", 42);
    }

    {
        ScratchScope s = scratch();
        u64* squares = s.arena().push_array<u64>(1000);
        u64 sum = 0;
        for (u64 i = 0; i < 1000; ++i) {
            squares[i] = i * i;
            sum += squares[i];
        }
        AURORA_LOG_INFO("scratch: the first 1000 squares add up to {}", sum);
    }

    if (has_arg(argc, argv, "--leak")) {
        void* leak = AURORA_ALLOC(64, 16, MemoryTag::Debug);
        static_cast<void>(leak);
        AURORA_LOG_INFO("leaking 64 bytes on purpose");
    }

    const i32 warnings = g_problems.warnings.load();
    const i32 errors   = g_problems.errors.load();
    AURORA_LOG_INFO("end: {} warning(s), {} error(s)", warnings, errors);

    memory::shutdown_report();
    return 0;
}
