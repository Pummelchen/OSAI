<!--
AI onboarding file.
Mode: bootstrap
Indexed commit: 8458ff956831e1b3b44a0cbcb396352ce28e3a01
Last generated: 2026-06-25T09:20:22Z
Generator: generic high-end AI coding agent
Purpose: Help future AI sessions understand this repository quickly.
Audience: Any high-capability AI coding agent, regardless of vendor or model family.
Human edits are allowed. Future refreshes should preserve valid human edits.
-->
# AI Index: XAIOS

## Snapshot

| Field | Value |
|---|---|
| Repository | `Pummelchen/XAIOS` |
| Indexed commit | `8458ff956831e1b3b44a0cbcb396352ce28e3a01` |
| Operation mode | `bootstrap` |
| Default branch | `main` |
| Primary languages | C99, Assembly, Python, Shell |
| Runtime target | Freestanding OS, currently AArch64 UEFI on QEMU `virt`; x86_64 and hardware ports are present/planned areas. |

## Read first

1. `AI_INDEX.md` — this map.
2. `AGENTS.md` — repository-specific working rules for any AI coding agent.
3. `.ai/START_HERE.md` — compact first-session prompt.
4. `.ai/PROJECT_MAP.md` — top-level and module map.
5. `.ai/COMMANDS.md` and `.ai/TESTING.md` — validation commands and gates.
6. `.ai/KNOWN_UNKNOWNS.md` — conflicts, stale docs, and questions to ask humans.

Always inspect current source files before editing. These onboarding files are guidance, not a source-code substitute.

## Verified repository purpose

XAIOS is a freestanding operating-system project for CPU-only AI inference and embedded AI-agent workloads. It builds UEFI boot artifacts, a monolithic kernel, userspace programs, initramfs images, QEMU runners, and Python validation gates.

Evidence:
- `README.md`
- `docs/ARCHITECTURE.md`
- `docs/GETTING-STARTED.md`
- `kernel/core/kmain.c`
- `scripts/build-image.sh`

## Architecture summary

Boot starts in `boot/uefi/loader_main.c`, which loads `kernel.elf` and passes boot information to `kmain()` in `kernel/core/kmain.c`. `kmain()` initializes exceptions, timers, SMP/topology, NUMA/PMM/VMM, SMMU/PCI/GIC, VirtIO block/network, persistence, mutable filesystem, security, source index, Git workspace, sandboxing, services, syscalls, scheduler, model arena, CPU-AI runtime, AI Cell, agent protocol, telemetry, and then runs userspace programs.

Runtime structure:
- Kernel code: `kernel/`
- Userspace runtime/apps/daemon: `userspace/`
- Build and QEMU gates: `scripts/`
- ABI/release-candidate contracts: `contracts/`
- User docs: `docs/`, `wiki/`, `HARDWARE-READINESS.md`, `PROJECT-TRACKER.md`

## Directory map

| Path | Responsibility | Notes |
|---|---|---|
| `boot/uefi/` | AArch64 UEFI loader and linker script. | Earliest boot code. |
| `kernel/arch/aarch64/` | EL1 entry, vectors, timer, GIC, MMU, SMP, SMMU, PCI, RTC, watchdog. | Hardware-sensitive. |
| `kernel/core/` | `kmain`, logging, telemetry, panic/assert, stack canaries. | `kmain.c` is the central init map. |
| `kernel/mm/` | PMM, NUMA, VMM support, heap/arena, ELF loading. | Boot and process-loader sensitive. |
| `kernel/dev/virtio/` | VirtIO transport, block, network drivers. | Recent HEAD changes block-device selection. |
| `kernel/fs/` | Initramfs and mutable persistent filesystem. | Coupled to `scripts/create-initfs.py` and contract JSON. |
| `kernel/net/` | ARP, IPv4/IPv6, ICMP/ICMPv6, NDP, DNS, routing, socket buffers. | Validate with network gates. |
| `kernel/runtime/` | AI Cell, CPU-AI runtime, model arena, security, sandbox, persistence, update, remote login, agent protocol, source index, Git workspace. | Security/AI-runtime sensitive. |
| `kernel/user/` | Process table, service supervisor, syscall dispatch. | API and capability sensitive. |
| `userspace/` | EL0 runtime, init, service manager, worker, apps, SSH daemon. | Built into initramfs by `scripts/build-image.sh`. |
| `scripts/` | Build scripts, QEMU runners, gates, report generation, initfs creation. | Primary validation surface. |
| `contracts/` | Machine-readable QEMU release-candidate contract. | May lag newer source. |
| `.github/workflows/` | CI compile, ABI, build/smoke, regression jobs. | Ubuntu toolchain/QEMU path. |

## Entrypoints and commands

| Task | Command |
|---|---|
| Toolchain verification | `make bootstrap` |
| Default build | `make all` |
| Build AArch64 image | `make image` |
| Build x86_64 image | `make image-x86_64` |
| Interactive AArch64 QEMU | `make qemu` or `make qemu-aarch64` |
| Dry-run QEMU commands | `make qemu-dry-run` |
| Primary smoke gate | `make qemu-smoke` |
| Full readiness gate | `make qemu-readiness-gate` |
| Full OS release-candidate gate | `make qemu-full-os-rc` |
| Compile syntax check | `make compile-check` |
| SSH bridge | `make xaios-ssh-bridge` |

## Common task map

| Change type | Start with | Also inspect/update |
|---|---|---|
| Boot/UEFI | `boot/uefi/`, `scripts/build-image.sh` | `kernel/core/kmain.c`, `docs/ARCHITECTURE.md` |
| Kernel subsystem | Relevant `kernel/*` module | Matching header, `kmain()` init/self-test order, QEMU gate markers |
| Syscall/API | `kernel/include/xaios/syscall.h` | `kernel/user/syscall.c`, `userspace/include/xaios_user.h`, `docs/API.md`, `contracts/qemu-rc-v1.json`, `scripts/qemu_gate_lib.py` |
| Userspace app | `userspace/apps/` | `scripts/build-image.sh`, `kernel/core/kmain.c`, `scripts/qemu-smoke.py` |
| SSH/network | `userspace/sshd/`, `kernel/net/`, `kernel/runtime/network_stack.c` | Socket syscalls and network gates |
| Security/update | `kernel/runtime/security.c`, `kernel/runtime/update.c` | `SECURITY.md`, `.ai/SECURITY.md`, QEMU security/update gates |
| CPU-AI/model format | `kernel/runtime/cpu_ai_runtime.c`, `kernel/runtime/model_arena.c`, `tools/convert_gguf_to_xaios.py` | `contracts/qemu-rc-v1.json`, model docs, smoke markers |
| CI/gates | `.github/workflows/ci.yml`, `Makefile`, `scripts/qemu-*.py` | `.ai/TESTING.md`, `HARDWARE-READINESS.md` |

## Important conventions

- C is freestanding C99, compiled with `-Wall -Wextra -Werror`; do not assume libc or POSIX in kernel/userspace.
- Userspace APIs use `userspace/include/xaios_user.h` and fixed-size buffers; no documented userspace `malloc` path.
- New kernel modules should include or update self-tests and ensure init/self-test order in `kmain()` is correct.
- Syscalls require capability mapping and user-buffer validation.
- QEMU gates are correctness evidence. Do not make hardware performance claims from QEMU-only results.

## Security-sensitive areas

- `kernel/runtime/security.c`
- `kernel/user/syscall.c`
- `kernel/fs/`, `kernel/runtime/persistence.c`, `kernel/runtime/update.c`
- `kernel/runtime/sandbox.c`, `kernel/runtime/git_workspace.c`, `kernel/runtime/source_index.c`
- `userspace/sshd/`
- `scripts/run-qemu-aarch64.sh` host forwarding and local SSH bridge

## Generated or do-not-edit zones

- Ignored generated outputs: `build/`, `out/`, `dist/`, `*.img`, `*.efi`, `*.elf`, `*.bin`, `*.map`, `*.log`, `.cache/`, `__pycache__/`, `compile_commands.json`.
- Do not hand-edit generated QEMU reports under `build/`.
- `.qoder/repowiki/` appears to be generated repository-wiki material; do not treat it as source of truth.

## Known conflicts and unknowns

High-impact items are tracked in `.ai/KNOWN_UNKNOWNS.md`:

- Performance language in README/wiki/tracker conflicts with `HARDWARE-READINESS.md` and `contracts/qemu-rc-v1.json`, which restrict QEMU outputs to correctness evidence.
- Source declares syscalls through `XAIOS_SYSCALL_AGENT_DISPATCH` number 34, while `docs/API.md` documents through 33 and `contracts/qemu-rc-v1.json` lists through 28.
- `LICENSE` contains MIT text but also says “License to be decided”; README also says license is undecided.
- Some docs omit current `agenttest`/agent-dispatch paths visible in source.

## What changed since last index

Initial bootstrap. No previous AI onboarding manifest or recognizable generic onboarding file set was found on `main`.
