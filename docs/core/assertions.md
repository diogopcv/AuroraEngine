# Assertions

```cpp
#include "core/assert.h"

AURORA_ASSERT(index < count);
AURORA_ASSERT(index < count, "index {} out of bounds [0, {})", index, count);
AURORA_FATAL("could not create the D3D12 device: {}", code);
AURORA_UNREACHABLE();
```

## Concepts

The engine separates two kinds of failure:

| Kind | Example | Mechanism |
|---|---|---|
| **Programming error**: the code is wrong | An index out of range, a handle destroyed twice | `AURORA_ASSERT` |
| **Expected failure**: the world is not as hoped | A missing file, a device that cannot be created | A return value (`bool`, `nullptr`), or `AURORA_FATAL` when the engine cannot continue |

There are no exceptions in engine code, so a function reports expected failures through its return value and leaves programming errors to asserts.

| Macro | Debug | Release |
|---|---|---|
| `AURORA_ASSERT(cond, ...)` | Evaluates `cond`; on failure logs the expression and message, then breaks into the debugger | Not evaluated; no code generated; still type-checked |
| `AURORA_FATAL(...)` | Logs, breaks into the debugger, aborts | Logs and aborts |
| `AURORA_UNREACHABLE()` | Same as `AURORA_FATAL` | Same as `AURORA_FATAL` |

`AURORA_ASSERTS_ENABLED` follows `AURORA_DEBUG` and can be forced on in other configurations.

The message is optional and uses the same compile-time-checked format as [logging](logging.md). The debugger stops at the line of the assert, not inside a helper function, because the break is a macro.

## Rules

1. **Conditions never have side effects.** In Release the condition disappears. Store the result first:

   ```cpp
   const bool closed = close_handle(h);
   AURORA_ASSERT(closed);
   ```

2. **Assert, then guard, when continuing is possible.** Engine functions check a precondition with an assert and then handle it anyway, so Release builds stay safe and tests can check the assert:

   ```cpp
   void Pool::free(void* block) {
       const bool belongs = is_block(block);
       AURORA_ASSERT(belongs, "{} is not a block of this pool", block);
       if (!belongs) {
           return;
       }
       ...
   }
   ```

3. **Use `AURORA_FATAL` only when the engine cannot continue**: out of memory, a missing device, an exhausted reservation.

4. **Helper functions used only inside asserts** need `[[maybe_unused]]`, or Release builds warn about them.

## The assert handler

What happens after a failed assert is logged can be replaced:

```cpp
using AssertHandler = bool (*)(const char* expression, const char* file, i32 line, std::string_view message);
void set_assert_handler(AssertHandler handler);   // nullptr restores the default
```

The handler returns `true` to break into the debugger, `false` to continue. Without a handler, the engine breaks. The test runner installs a handler that counts failures and returns `false`, which is how [`CHECK_ASSERTS`](../testing.md#testing-that-an-assert-fires) works. `AURORA_FATAL` calls the handler too, but always aborts.

Handler contract: it may be called from any thread at once, so it must be thread-safe; it must not log and must not throw. Set it from the main thread before other threads start. It is stored in an atomic pointer, written with release ordering and read with acquire ordering only when an assert fails, so the normal path costs nothing.

## How other engines do it

Unreal separates `check` (removed in shipping builds), `verify` (always evaluated) and `ensure` (reports once and continues). Aurora keeps one assert macro and the rule "store the result, then assert" instead of a `verify`.

## Implementation notes

- `core/src/assert.cpp`: `report_failure` formats `ASSERT failed: <expression> | <message>`, logs it, then asks the handler.
- The break uses `__debugbreak()` on MSVC, `__builtin_debugtrap()` on Clang and `raise(SIGTRAP)` elsewhere.
- `assert.h` includes `log.h`, so every file that asserts includes `<format>`; the [precompiled header](../build.md#precompiled-header) covers that cost.
- Tests: `tests/core/assert_tests.cpp`.

## Status

Implemented. Deferred: death tests, to test fatal errors in a child process.
