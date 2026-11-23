# Roadmap

Six stages and seventeen steps. Each step depends only on the previous ones and has a "done when" criterion. The sequence defines the order of **first versions**: going back to a lower layer when a later step needs something is expected.

## Rules

1. **Do not skip from the triangle straight to the RHI.** Write the concrete DirectX 12 triangle first, then extract the abstraction from working code.
2. **Do not optimize before stage 6.** Simple, correct code first. The profiler (Tracy) goes in early as a measuring tool; optimizations come only after it shows the bottleneck. Dedicated threads (render, IO, audio) are architectural boundaries, not optimizations: they arrive at steps 8, 9 and 17.
3. **Return to lower layers when needed, not before.**
4. **Closed scope per step.** Extensions are listed and deferred.
5. **Order by first user, not by layer.** A lower-layer system gets its first version when something above needs it. Contracts that make later changes cheap are decided early: systems declare which components they read and write from step 7; simulation updates are functions over ranges of dense arrays without global state; components are plain data that reference other objects only through handles.
6. **Docs are part of done.** A step is done when its code, its tests and its guide in `docs/` are updated together.

## Step 1: the core layer

| # | Sub-step | Done when | Status |
|---|---|---|---|
| 1.1 | Build: CMake layers, presets, `types.h` | The sandbox builds and runs in Debug and Release | Done |
| 1.2 | Log and assert | The sandbox logs; an assert breaks at its line in Debug; a fatal error aborts in every build | Done |
| 1.3 | Tests: doctest, assert handler, `CHECK_ASSERTS` | A failing test reports file and line and the executable returns an error; `CHECK_ASSERTS` detects an assert and reports a missing one; tests appear individually in CTest | Done |
| 1.4 | Virtual memory and `VirtualBuffer` | Reserve 1 GB, grow by commit, the base address never changes | Done |
| 1.5 | Heap (mimalloc), memory tags, tracking, global `new`/`delete` | A deliberate leak shows in the report with file and line; `std::vector` allocations are counted | Done |
| 1.6 | Arena: marks, scopes, per-thread scratch, frame ring | Alignment, mark and rewind, Debug fill and the ring work in tests | Done |
| 1.7 | `Pool` and `HandlePool` | A stale handle returns `nullptr`; a reused slot gets a new generation; destroying a pool with live objects asserts | Done |
| 1.8 | `RefPool` and `Ref` | The last `Ref` frees the object; a multithreaded copy stress test keeps counts correct | Done |
| 1.9 | `DenseArray` | Swap-remove keeps it dense; one million elements without reallocation | Done |
| 1.10 | Shutdown report | A clean sandbox exit reports zero live bytes; an injected leak is reported | Done |
| 1.11 | String IDs, console variables and commands | `"name"_sid` hashes at compile time; variables set from a config file, the command line and code; queued writes applied at frame start | Next |
| 1.12 | `EventQueue` and `Delegate` | Events written in one phase are read in the next; stale subscription handles are detected | |

## All steps

| # | Step | Scope | Done when |
|---|---|---|---|
| **Stage 1** | **Foundation** | | |
| 1 | Build and core | Sub-steps 1.1–1.12 | All sub-steps done |
| 2 | Platform and engine loop | Win32 window (per-monitor DPI), input (scancodes, Raw Input) with action bindings, files (UTF-8 paths), time; platform events into event queues; `app::Engine` owning the systems, with the fixed-step loop | The window resizes and handles DPI changes; input actions are logged; the step counter matches real time; shutdown runs in reverse order and reports no leaks |
| 3 | Math | Documented conventions; vectors, matrices, quaternions, transforms; camera helpers with reversed-Z projection | Unit tests pass, including comparisons with DirectXMath |
| **Stage 2** | **First image** | | |
| 4 | DirectX 12 triangle | Device, queue, swap chain, two frames in flight with fences, shaders compiled offline, debug layer on | A triangle on screen; resizing without debug-layer errors; no leak warnings on exit |
| 5 | RHI | Buffers and textures as handles, bindless descriptors, upload ring, barriers, command lists | The triangle again, with no DirectX 12 call outside `rhi/d3d12/` |
| 6 | Render server | Meshes and camera through handles, frustum culling, basic directional light, debug draw; a `debug` module with the ImGui overlay and console window | A free camera around lit cubes, a debug grid, a frame-time panel and console variables editable live; cubes outside the view are not drawn |
| **Stage 3** | **World** | | |
| 7 | ECS | Entities as handles, components in dense arrays, queries, event queues in the scheduler; systems declare which components they read and write | A thousand moving entities |
| 8 | Hierarchy, extract and render thread | Parent/child transforms, the extract step, a render thread fed by frame packets, an option to run rendering inline | A working "solar system"; `render` includes nothing from `world`; the scene runs the same with the render thread on or off; resizing does not deadlock |
| 9 | Assets and cooker | Asset cooker (meshes, textures, shaders, incremental), `assets` with an IO thread, reference-counted handles, hot reload and a loader registry; mesh, texture and shader loaders in `render` | A cooked glTF model renders textured; loading does not stall the frame; editing a source asset updates the running game |
| **Stage 4** | **Image and simulation** | | |
| 10 | Lighting | PBR, one shadowed directional light, point lights, HDR with tone mapping, a simple render graph | A glTF scene with shadows and distinct materials |
| 11 | Collision | Rigid bodies, uniform-grid broadphase, sphere, plane and box narrowphase, ray and shape casts, triggers | Spheres and boxes fall and bounce, with contacts drawn by debug draw; a ray picks the body under the cursor |
| 12 | Solver | Sequential impulses, friction, warm starting, sleeping | A stack of 10 boxes stands still; sleeping bodies cost nothing |
| **Stage 5** | **Motion and effects** | | |
| 13 | Animation | Skeleton sampling, GPU skinning, crossfade | A skinned glTF character plays and crossfades between two clips |
| 14 | Particles | Attributes in dense arrays, one emitter type, instanced billboards, a sorted transparent pass | An emitter renders correctly blended in the lit scene |
| **Stage 6** | **Tools and scale** | | |
| 15 | Reflection and serialization | Manual type registration, a text scene format, level load and unload on a level heap, game states | A saved scene reloads identically; loading another level frees the previous one completely |
| 16 | Editor | Entity hierarchy, inspector from reflection, gizmos, play and stop | A scene is built and played without touching code |
| 17 | Jobs and audio | Thread pool, parallel systems, audio server with a lock-free audio thread and 3D attenuation and panning | The profiler shows work across cores; collisions play sounds |

## Deferred extensions

| Area | First version | Later |
|---|---|---|
| Tests | doctest, assert handler, `CHECK_ASSERTS` | Death tests (a child process) |
| Logging | Synchronous sinks | Asynchronous logging, only if the profiler shows contention |
| Memory | Single-owner allocators, per-thread scratch | Concurrent allocators for jobs (step 17), level heaps, `trim()` |
| Console | Variables, commands, config, ImGui window | Scripting |
| Render thread | Frame packets, inline mode at startup | Switching at runtime, a separate submission thread |
| IO | One thread, blocking reads | Overlapped IO, priorities, cancellation |
| Lighting | PBR, one shadowed directional light, point lights | Ambient occlusion, reflections, cascaded shadows |
| Physics | Spheres, planes, boxes | General convex shapes (GJK/EPA) |
| Animation | Playback, loop, crossfade, GPU skinning | State machines, IK, blend trees |
| Particles | CPU simulation, one emitter type | Parallel update, GPU particles |
| Cooker | Meshes, textures, shaders | Packaging |
| Audio | Command queues, fixed voice pool | Streaming music, effects |
