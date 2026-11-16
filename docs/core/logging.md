# Logging

```cpp
#include "core/log.h"

AURORA_LOG_INFO("window created: {}x{}", width, height);
AURORA_LOG_WARN("texture {} is not a power of two", name);
AURORA_LOG_ERROR("could not open {}", path);
AURORA_LOG_TRACE("frame {} took {} ms", frame, ms);   // Debug only
```

## Concepts

### Formatting checked at compile time

Messages use `std::format` syntax. The format string is a `std::format_string`, so a wrong placeholder count or an argument without a formatter is a **compile error**, not a garbled message at runtime.

The header only *packs* the arguments (`std::make_format_args`); the formatting itself (`std::vformat_to`) runs in `log.cpp`. Each call site therefore instantiates very little code, which halved the compile time of files that log.

### No allocation

A message is formatted into a 1024-byte buffer on the stack. Longer messages are cut and end with `...`. Logging never touches the heap, so it is safe inside allocators, inside the heap's own error paths, and during shutdown.

### Levels and compile-time filtering

| Level | Use for |
|---|---|
| `Trace` | Detail that is only useful while debugging a specific system |
| `Info` | Normal events worth seeing: startup, loading, configuration |
| `Warn` | Something unexpected that the engine can handle |
| `Error` | Something failed; the engine continues in a degraded way |
| `Fatal` | Used by `AURORA_FATAL`; the program aborts afterwards |

`logging::min_level` is a compile-time constant: `Trace` in Debug, `Info` otherwise. A filtered call generates no code but is still type-checked, so a wrong format in a `Trace` call fails to compile in Release too.

### Sinks

Output goes through **sinks**: functions called for every message. A console sink that writes to `stderr` is registered by default. Warnings and errors also show `(file:line)`.

```cpp
void file_sink(const LogMessage& message, void* user_data) {
    auto* file = static_cast<std::FILE*>(user_data);
    std::fprintf(file, "%.*s\n", static_cast<int>(message.text.size()), message.text.data());
}

logging::add_sink(file_sink, log_file);
```

`LogMessage` carries the level, the text (a `std::string_view` valid only during the call), the file and the line.

## Guarantees and restrictions

- **Any thread may log.** Each thread formats into its own stack buffer and calls the sinks directly. There is no engine lock.
- **Sinks are published lock-free.** A sink can be added while other threads are logging: the main thread writes the slot, then publishes the new count with release ordering; logging threads read it with acquire ordering.
- **Only the main thread adds sinks**, at most 8, and sinks cannot be removed. Removal is the hard part of a lock-free list, and nothing needs it.
- **A sink must be thread-safe** (it may run on several threads at once), **must not log** (a message logged from inside a sink is dropped, so a mistake cannot recurse forever), and must live until the end of the program.
- **Hidden lock:** the console sink calls `fprintf`, and the C runtime locks the stream for each call. That is what keeps lines from interleaving. One `fprintf` per message to a single unbuffered stream also keeps the order and survives a crash.

## Pitfalls and advice

- **Do not log in hot loops**, even at `Trace`: in Debug every call formats a message and walks the sinks.
- **Format arguments must be named values** in the low-level `vformat_into` API (`std::make_format_args` takes references). The `AURORA_LOG_*` macros handle this for you.
- **The level must be a compile-time constant.** `AURORA_LOG(level_variable, ...)` does not compile; pick the macro for the level.
- **A sink that allocates is fine**, but a sink that allocates while the heap reports an error could recurse into the heap. The heap therefore logs only after releasing its locks.

## How other engines do it

Unreal (`UE_LOG`) and Godot (`print_line`) both route messages through registered outputs, like Aurora's sinks. Aurora differs by checking formats at compile time and by never allocating.

## Implementation notes

- `core/src/log.cpp`: the sink array and count are `constinit`, so logging works during static initialization, before `main`.
- A `thread_local` flag drops messages logged while a sink is running.
- Truncation writes into a small fixed-capacity back-inserter that stops at the buffer size.
- Tests: `tests/core/log_tests.cpp`.

## Status

Implemented. Deferred: asynchronous logging (a queue and a logger thread), only if the profiler shows threads waiting on the log after the job system exists; runtime level filtering through a console variable.
