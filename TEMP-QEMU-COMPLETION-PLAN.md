# Temporary QEMU Completion Plan

This file is a working plan for moving OSAI from the current QEMU status through milestone 33. It is intentionally temporary and should be removed or folded into the GitHub Wiki once the milestone sequence is stable.

## Current Baseline

Current baseline is QEMU milestone 19: AArch64 UEFI boot, PMM/VMM, exceptions, SMP, split VirtIO block/net, versioned VirtIO-backed read-only filesystem metadata, parsed config manifest, real EL0 `/init` ELF selected from the manifest, explicit syscall table, process/capability metadata, user pointer validation, bad syscall tests, userspace service-manager policy, syscall service control, VMM-backed heap, AI Cell resource MVP, structured boot telemetry, and `make qemu-smoke`.

## Milestone Plan

| Milestone | Focus | Required code deliverables | Required verification |
|---:|---|---|---|
| 13 | General memory arenas | Generic arena manager for model, KV/cache, source index, build output, log, and telemetry arenas; page ownership; map/unmap; lifecycle states | Arena self-test, QEMU smoke marker |
| 14 | Shared model weights | Read-only shared model arenas with ref-counted mappings and write-fault verification | Two cells share one model arena; controlled write fault path remains valid |
| 15 | Private KV/cache arenas | Per-cell private KV/cache arena allocation, prefaulting, and fault counters | AI Cell prepare maps private KV/cache with zero post-ready fault count |
| 16 | Scheduler/core isolation | Explicit core lease state machine, no-migration counters, hot-core idle loop ownership, IRQ routing metadata | Leased core telemetry shows zero migration |
| 17 | Structured read-only filesystem | Replace minimal initfs format with versioned read-only filesystem metadata and config manifest parsing | `/init` and service config load from filesystem image |
| 18 | Stable userspace ABI | Syscall table, capability handles, process table, user pointer validation, bad syscall tests | `/init` service control and bad syscall test pass |
| 19 | Userspace service manager | Move service manager logic into userspace, add restart/status/log policy | `/init` starts service manager and reports state |
| 20 | Build/test sandbox | Workspace directories, build artifact arena, sandbox manifest, rollback metadata | Sandbox lifecycle logs and invalid manifest rejection |
| 21 | Source index service | Source index arena, file records, symbol record stubs, incremental update API | Source index fixture loads in QEMU |
| 22 | Git workspace service | Git workspace metadata, patch apply/revert boundaries, sync state machine | Patch lifecycle fixture logs apply/revert |
| 23 | CPU AI runtime skeleton | CPU-only model loader boundary, text-piece processing boundary, scalar decode stub | Deterministic decode fixture under QEMU |
| 24 | AI Cell runtime integration | Run CPU AI runtime inside an AI Cell using shared weights and private KV/cache | One cell executes deterministic decode and emits counters |
| 25 | Low-latency UDP | Per-core queue metadata, UDP parser/emitter, echo/control path | UDP packet smoke and p50/p95/p99 counter emission |
| 26 | Low-latency TCP | Minimal TCP control path, connection state, per-flow ownership | TCP connect/request smoke and latency counters |
| 27 | Telemetry system | Event schema, counters for scheduler, faults, memory, net, AI Cell, services; JSON output | Machine-parseable telemetry block in smoke test |
| 28 | Security model | Capabilities, process permissions, service permissions, signed update stubs, credential rejection | Denied operations fail closed |
| 29 | Persistence and rollback | Snapshot metadata, boot config rollback, service rollback, workspace rollback | Rollback fixture returns service/workspace to prior state |
| 30 | Benchmark harness | QEMU correctness benchmarks, tuned-baseline result schema, no performance claims from QEMU | JSON benchmark fixture validates |
| 31 | CI boot matrix | Smoke, fault, driver, init, service, and telemetry test targets | All local matrix targets pass |
| 32 | QEMU developer preview | Full QEMU OS image with userspace services, AI Cell runtime, CPU AI stub, networking, docs | Single `make qemu-preview` boots full stack |
| 33 | Hardware readiness gate | Freeze QEMU contracts and define Intel Desktop port gates | Wiki gate checklist and green QEMU test matrix |

## Implementation Order

1. Finish memory ownership: generic arenas, shared weights, private KV/cache, source/build/log arenas.
2. Strengthen execution ownership: scheduler/core isolation, no-migration counters, process/capability ABI.
3. Move control-plane logic out of kernel tests and into userspace services.
4. Add app-agent substrate: filesystem manifests, build/test sandbox, source index, Git workspace state.
5. Add CPU-only AI runtime skeleton and bind it to AI Cells.
6. Add low-latency UDP/TCP correctness paths and telemetry.
7. Harden security, persistence, rollback, and benchmark/CI gates.
8. Cut QEMU developer preview and hardware readiness gate.

## Current Work Slice

The next code slice is milestone 20: add build/test sandbox metadata, workspace directories, build artifact arena wiring, sandbox manifest validation, and rollback metadata.
