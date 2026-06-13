# OSAI

**OSAI is a server-only operating system for CPU-only embedded AI agents.**

## Purpose

OSAI is designed for applications that embed small CPU-only AI agents directly into their own runtime and development workflow. The goal is to make those agents fast, predictable, isolated, and close to the source code they improve.

OSAI is not a Linux distribution, a BSD fork, a desktop OS, or a GPU AI runtime. It is a specialized server OS architecture for CPU-bound AI workloads and app-local automation.

## Why OSAI Exists

Most applications are still operationally dumb: they run business logic, expose APIs, store data, and wait for humans to improve them. OSAI targets a different model where normal applications can become smart applications.

In that model, each application can host an embedded AI agent that:

- understands the application's Git source tree;
- accepts human requests;
- generates patches;
- rebuilds and tests the application;
- reviews and syncs changes with Git;
- hot reloads or redeploys the improved service where appropriate.

The operating system is designed around making that loop fast and predictable. OSAI reduces avoidable interference from scheduling, memory duplication, background work, generic network paths, and cross-core movement on hot AI paths.

## Target Benefits

These are design targets, not guaranteed benchmark claims.

| Area | Target |
|---|---:|
| TCP/UDP latency | Up to 10-45% lower latency |
| Effective CPU-AI memory bandwidth | 3-18% higher |
| Sustained usable CPU-core performance | 2-12% higher |
| Scheduler jitter/migration | Near-zero on hot AI paths |

OSAI cannot exceed physical silicon limits. It cannot make DRAM, LPDDR, cache fabric, or CPU cores faster than the underlying hardware. The expected gains come from removing avoidable OS interference: scheduler migration, context switching, post-warmup page faults, generic socket overhead, memory duplication, poor NUMA placement, and unrelated interrupts.

## Target Platforms

The implementation order is:

1. QEMU on macOS for early bring-up and correctness.
2. Intel Desktop CPUs for the first real performance target.
3. Intel Xeon CPUs for multi-agent, NUMA-aware server deployments.
4. ARM/NVIDIA N1X/GB10-class systems for CPU-only AI on AArch64 SoCs.

OSAI has no CUDA, Metal, GPU, or vendor accelerator dependency.

## Documentation

Detailed design documentation lives in the GitHub Wiki:

- [Wiki Home](https://github.com/Pummelchen/OSAI/wiki)
- [Architecture](https://github.com/Pummelchen/OSAI/wiki/Architecture)
- [Implementation Plan](https://github.com/Pummelchen/OSAI/wiki/Implementation-Plan)
- [Platform Ports](https://github.com/Pummelchen/OSAI/wiki/QEMU-on-macOS)
- [Performance Targets](https://github.com/Pummelchen/OSAI/wiki/Performance-Targets)
- [Codex Work Packages](https://github.com/Pummelchen/OSAI/wiki/Codex-Work-Packages)

## Status

OSAI is currently in early design and bring-up.

The repository now contains the first AArch64 QEMU-on-macOS boot path: a UEFI loader, ELF64 kernel handoff, serial kernel logging, exception diagnostics, generic timer discovery, GIC discovery, SMP/per-core state discovery, split VirtIO transport/block/network drivers, a VirtIO-backed versioned read-only filesystem, manifest-backed `/init` selection, a real EL0 `/init` ELF loaded from that filesystem, syscall-based service control, a VMM-backed kernel heap, VMM map/unmap and guarded user stack checks, a generic PMM/VMM-backed arena manager, AI Cell lifecycle/resource enforcement, explicit hot-core lease metadata, zero migration/context-switch telemetry for leased AI cores, shared read-only model arenas, private KV/cache and source-index arenas, PMM/VMM self-tests, and JSON-like boot telemetry.

The first engineering target remains a useful bootable QEMU prototype on macOS. Production-oriented targets follow in this order: Intel Desktop, Intel Xeon, and ARM/NVIDIA N1X-compatible SoCs.

## License

License to be decided.
