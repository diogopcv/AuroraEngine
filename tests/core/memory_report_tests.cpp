#include "test/aurora_test.h"

#include "core/log.h"
#include "core/memory/arena.h"
#include "core/memory/heap.h"
#include "core/memory/memory_report.h"

#include <string>

using namespace aurora;

namespace {

struct ReportCapture {
    bool        enabled = false;
    std::string text;
};

ReportCapture g_report;

void report_sink(const LogMessage& message, void* user_data) {
    auto* capture = static_cast<ReportCapture*>(user_data);
    if (capture->enabled) {
        capture->text.append(message.text);
        capture->text.push_back('\n');
    }
}

struct ScopedReportCapture {
    ScopedReportCapture() {
        static const bool registered = [] {
            logging::add_sink(report_sink, &g_report);
            return true;
        }();
        static_cast<void>(registered);
        g_report.text.clear();
        g_report.enabled = true;
    }
    ~ScopedReportCapture() { g_report.enabled = false; }

    ScopedReportCapture(const ScopedReportCapture&) = delete;
    ScopedReportCapture& operator=(const ScopedReportCapture&) = delete;
};

} // namespace

TEST_SUITE("memory_report") {

TEST_CASE("a deliberate leak is reported with file and line") {
    ScopedReportCapture capture;
    const i32 line = __LINE__ + 1;
    void* leak = AURORA_ALLOC(64, 16, MemoryTag::Debug);
    REQUIRE(leak != nullptr);

    const memory::LeakSummary summary = memory::report_leaks();
    heap::free(leak, MemoryTag::Debug);

    CHECK(summary.heap_blocks >= 1);
    CHECK(summary.heap_bytes >= 64);
    CHECK(g_report.text.find("tag Debug has") != std::string::npos);
    if constexpr (AURORA_DEBUG) {
        const std::string callsite = "memory_report_tests.cpp(" + std::to_string(line) + "): 1 blocks";
        CHECK(g_report.text.find(callsite) != std::string::npos);
    }
}

TEST_CASE("committed virtual memory is reported") {
    ScopedReportCapture capture;
    memory::LeakSummary summary;
    {
        Arena arena(1_MB, MemoryTag::Debug);
        static_cast<void>(arena.push(10));
        summary = memory::report_leaks();
    }

    CHECK(summary.committed_bytes >= Arena::commit_chunk);
    CHECK(g_report.text.find("tag Debug has " + std::to_string(Arena::commit_chunk) + " bytes of committed virtual memory") != std::string::npos);
}

} // TEST_SUITE("memory_report")
