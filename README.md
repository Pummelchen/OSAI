# OSAI

**OSAI** is a server-only operating system project for **CPU-bound AI models** and **embedded app agents**.

The purpose of OSAI is to let normal applications embed small CPU-only AI agents that understand their own source code, accept human instructions, generate code changes, rebuild, test, sync with Git, and improve the running application with minimal operating-system interference.

OSAI is not a Linux distribution, a BSD fork, a desktop OS, or a GPU AI runtime. It is a focused OS architecture for server-hosted CPU AI workloads on **ARM SoCs**, **modern Intel Desktop CPUs**, and **modern Intel Xeon CPUs**.

## Core Benefits

Compared with a carefully tuned Linux/BSD baseline, OSAI targets:

| Area | Target improvement |
|---|---:|
| TCP/UDP latency | **Up to 10–45% lower latency** |
| Effective CPU-AI memory bandwidth | **3–18% higher effective bandwidth** |
| Sustained usable CPU-core performance | **2–12% higher sustained performance** |
| Scheduler jitter and thread migration | **Near-zero migration and near-zero hot-path jitter** |

These are architecture targets, not guaranteed benchmark results. OSAI cannot make DRAM, LPDDR, cache fabric, or CPU cores physically faster than the underlying silicon. The expected gains come from removing avoidable OS overhead: scheduler migration, unnecessary context switches, page faults after warmup, background kernel work, generic network paths, memory duplication, bad NUMA placement, and shared resource contention.

## Target Use Case

OSAI is designed for the next generation of “smart” applications:

```text
human request
  -> app-local CPU AI agent
  -> source-code understanding
  -> patch generation
  -> rebuild / test
  -> Git sync
  -> hot reload or redeploy
  -> running app becomes smarter
```

The goal is predictable, low-latency CPU AI execution without relying on CUDA, Metal, or vendor GPU acceleration.

## More Detail

The detailed architecture, hardware targets, performance model, service manifest, and roadmap have been moved to the wiki-style documentation under [`docs/wiki/`](docs/wiki/).

## Project Status

OSAI is currently a design-stage project. The next milestone is to define benchmark baselines, architecture documents, and a minimal bootable prototype plan.
