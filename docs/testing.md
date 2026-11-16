# Testing

Unit tests use [doctest](https://github.com/doctest/doctest) and run through CTest. They live in `tests/`, outside the engine: no engine layer depends on them.

## Running tests

```powershell
cmake --build --preset ninja-debug
ctest --preset ninja-debug                     # every test
ctest --preset ninja-debug -L arena            # one suite (suites are CTest labels)
.\build\windows-ninja\bin\Debug\core_tests.exe --test-case="*stale*"
```

Each `TEST_CASE` is a separate CTest entry, named `<layer>_tests.<test name>`. In Visual Studio "Open Folder" mode the tests appear in Test Explorer. In a `.sln`, build `RUN_TESTS` or run `ctest`.

## Writing a test

```cpp
#include "test/aurora_test.h"   // never <doctest/doctest.h> directly

#include "core/memory/arena.h"

using namespace aurora;

TEST_SUITE("arena") {

TEST_CASE("mark and rewind reuse memory") {
    Arena arena(1_MB, MemoryTag::Core);
    const ArenaMark mark = arena.mark();
    void* first = arena.push(64);
    arena.rewind(mark);
    CHECK(arena.push(64) == first);
}

} // TEST_SUITE("arena")
```

- `CHECK` keeps going after a failure; `REQUIRE` stops the test. Both print the values on failure.
- Unprefixed doctest macros (`TEST_CASE`, `CHECK`) are allowed only in `tests/`.
- Test code may use exceptions; doctest's `REQUIRE` relies on them.

## Testing that an assert fires

Engine asserts normally break into the debugger. The test runner installs an assert handler that counts failures instead, so a test can check that an assert fired and then continue:

```cpp
TEST_CASE("rewinding forward asserts and leaves the arena unchanged") {
    Arena arena(1_MB, MemoryTag::Core);
    static_cast<void>(arena.push(32));
    ArenaMark ahead{arena.position() + 16};
    CHECK_ASSERTS(arena.rewind(ahead));
    CHECK(arena.position() == 32);
}
```

`CHECK_ASSERTS(statement)` fails if the statement did not trigger at least one `AURORA_ASSERT`. It is compiled out when asserts are disabled (Release), together with the statement, so do not put code with needed side effects inside it.

This works only when the code under test has a safe way to continue after the assert, which is why engine functions check a precondition with an assert and then guard against it (`AURORA_ASSERT(index < size); if (index >= size) return;`). Fatal errors abort the process and cannot be tested this way; there are no death tests yet.

## Testing global state

Some state is global and never reset: log sinks, heap statistics, committed memory counters. Tests therefore **compare values before and after**, never absolute values:

```cpp
const heap::TagStats before = heap::stats(MemoryTag::Core);
void* block = AURORA_ALLOC(100, 16, MemoryTag::Core);
CHECK(heap::stats(MemoryTag::Core).live_blocks == before.live_blocks + 1);
heap::free(block, MemoryTag::Core);
```

A suite that cannot work with differences gets its own test executable.

A test that registers a log sink registers it once per process (there are at most 8 sinks and they cannot be removed) and enables it only during the test.

## Counting allocations

To check that an allocation happened, call `::operator new` or `heap::alloc` directly. The compiler may remove a `new`-expression paired with its `delete` when the object does not escape (C++14 allows it), and then the allocation is never counted.

## Testing concurrent code

Concurrent code gets stress tests: several threads repeat the operation many times, and the test checks invariants at the end (counts add up, each destructor ran exactly once). ThreadSanitizer is not available with MSVC; during development, the concurrent suites are also run with ThreadSanitizer in a separate Linux build that is not part of the repository.

## Adding tests for a layer

Each layer has one test executable, created by `engine_add_tests()`:

```cmake
# tests/math/CMakeLists.txt
engine_add_tests(math
    SOURCES
        vec_tests.cpp
        mat_tests.cpp
    DEPS
        aurora::math)
```

and `add_subdirectory(math)` in `tests/CMakeLists.txt`. A new test file is one line in the layer's list. Test sources are always compiled into the executable, never into a library: the linker drops unreferenced objects from static libraries, and the test registrations with them.

## How the framework is tested

`tests/core/framework_selftests.cpp` has a test that fails on purpose. It is skipped in normal runs. Two extra CTest entries run it explicitly with `--no-skip`:

| Entry | Passes when |
|---|---|
| `core_tests.selftest.exit_code` | the executable returns an error (`WILL_FAIL`) |
| `core_tests.selftest.location` | the output contains `framework_selftests.cpp(line)` or `framework_selftests.cpp:line` |

Two entries are needed because CTest ignores the exit code when it matches the output against a pattern.

The same technique tests the [shutdown memory report](core/memory/memory-report.md): `sandbox.shutdown.clean` runs the sandbox and expects `memory: no leaks`; `sandbox.shutdown.leak` runs `sandbox --leak` and expects the injected block to be reported. Together they prove that a failing test makes the run fail and points at the right line, which is what a broken custom `main` would silently stop doing.

The intentionally failing tests are marked `doctest::no_breaks()`, so a debugger does not stop on them.
