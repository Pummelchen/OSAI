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
# AGENTS.md

Generic instructions for high-capability AI coding agents working in XAIOS.

## Start every session this way

1. Read `AI_INDEX.md`.
2. Read `.ai/START_HERE.md`, `.ai/PROJECT_MAP.md`, `.ai/COMMANDS.md`, `.ai/TESTING.md`, and `.ai/KNOWN_UNKNOWNS.md` as needed.
3. Inspect the current source/config files relevant to the task before editing.
4. Summarize verified facts, assumptions, inferences, and unknowns separately.
5. Make a small implementation plan before changing files.

These onboarding files are not authoritative when they conflict with source. Trust current source code first, then build/test config, CI, lockfiles/package metadata, tests, current docs, older docs, and only then inference.

## Scope and safety rules

- Do not modify product/source code unless the user asked for an implementation change.
- Do not commit credentials, private keys, API keys, tokens, passwords, or secret benchmark data.
- Do not use QEMU correctness gates as proof of hardware performance.
- Do not add model- or vendor-specific AI instruction files.
- Do not add external generated wiki/deep-repo-learning links to onboarding docs.
- Avoid destructive commands, production migrations, or deploy commands.
- Keep changes small and reviewable.

## Project-specific engineering rules

- Kernel and userspace C are freestanding C99. Do not assume libc, POSIX headers, dynamic allocation, filesystem APIs, or host OS services unless present in this repo.
- Follow existing naming: module prefixes in kernel code (`pmm_`, `vmm_`, `smmu_`, etc.) and `xaios_` for userspace wrappers.
- Use fixed-size buffers in userspace unless current source proves a safe allocator exists.
- Kernel modules should maintain `*_self_test()` coverage and correct init ordering in `kernel/core/kmain.c`.
- Syscall changes must update all relevant surfaces: `kernel/include/xaios/syscall.h`, `kernel/user/syscall.c`, `userspace/include/xaios_user.h`, `docs/API.md`, `contracts/qemu-rc-v1.json`, and ABI validation scripts if needed.
- Userspace app additions usually require edits to `userspace/apps/`, `scripts/build-image.sh`, `kernel/core/kmain.c`, and smoke-test markers.

## Validation expectations

For documentation-only changes, validate links, JSON, changed-file scope, and secret-like patterns.

For source changes, choose the smallest relevant validation set:

- Syntax-only C check: `make compile-check`
- Primary smoke: `make qemu-smoke`
- ABI contract: `python3 scripts/qemu-abi-contract.py`
- Regression: `make qemu-regression-suite`
- Readiness/RC: `make qemu-readiness-gate`, `make qemu-full-os-rc`

Do not claim tests passed unless you actually ran them.

## Planning and reporting

Before editing, state:

- verified source files inspected;
- assumptions and risks;
- planned files to change;
- validation to run or skip, with reason.

After editing, report:

- changed files;
- behavioral impact;
- tests/commands run;
- tests skipped and why;
- unresolved risks.

## Extra caution areas

- Boot and memory: `boot/uefi/`, `kernel/arch/aarch64/`, `kernel/mm/`, `kernel/core/kmain.c`
- Syscalls/capabilities: `kernel/include/xaios/syscall.h`, `kernel/user/syscall.c`, `userspace/include/xaios_user.h`
- Persistence/update/security: `kernel/fs/`, `kernel/runtime/persistence.c`, `kernel/runtime/update.c`, `kernel/runtime/security.c`
- SSH/network: `userspace/sshd/`, `kernel/net/`, `kernel/runtime/network_stack.c`
- AI runtime/model format: `kernel/runtime/cpu_ai_runtime.c`, `kernel/runtime/model_arena.c`, `tools/convert_gguf_to_xaios.py`
- Gates/contracts: `contracts/qemu-rc-v1.json`, `scripts/qemu_gate_lib.py`, `scripts/qemu-*.py`

## Refresh policy

Update these onboarding files when changes affect architecture, build/test commands, API/syscalls, contracts, CI, security model, deployment/run paths, major docs, or significant directory layout. On refresh, preserve correct human edits, remove stale generated claims, and record conflicts in `.ai/KNOWN_UNKNOWNS.md`.
