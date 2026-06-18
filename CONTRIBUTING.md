# Contributing to XAIOS

XAIOS is a freestanding AArch64 operating system for CPU-only embedded AI agents. Keep changes small, reviewable, and tied to the current implementation plan.

## Getting Started

See [docs/GETTING-STARTED.md](docs/GETTING-STARTED.md) for toolchain setup, building, and running.

## Documentation

- **[docs/API.md](docs/API.md)** — Userspace syscall API reference (33 syscalls, capabilities, data types)
- **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)** — System architecture, boot flow, memory layout, directory map
- **[docs/GETTING-STARTED.md](docs/GETTING-STARTED.md)** — Prerequisites, build instructions, app development guide

## Development Environment

| Platform | Toolchain |
|----------|-----------|
| macOS | `brew install llvm lld qemu mtools python3` |
| Linux | `apt install clang lld qemu-system-arm qemu-efi-aarch64 mtools python3` |

Build and smoke test:

```sh
make image && make qemu-smoke
```

The smoke test boots the full OS, runs all self-tests, executes every userspace app, and verifies JSON telemetry. It is the primary acceptance criterion for any change.

## Code Style

All C code is freestanding C99 compiled with `-Wall -Wextra -Werror`:

- **No libc.** Kernel uses `kernel/include/xaios/` headers. Userspace uses `userspace/include/xaios_user.h`.
- **Naming**: `snake_case` for functions/types, `UPPER_SNAKE` for macros/constants.
- **Prefixes**: Kernel functions use module prefix (`pmm_alloc_page`, `vmm_map_page`, `smmu_init`). Userspace wrappers use `xaios_` prefix.
- **Types**: Use `uint64_t`/`uint32_t` in kernel, `u64`/`u32` in userspace.
- **Error handling**: Return `xaios_status_t` (0 = `XAIOS_OK`). Use `kassert()` for invariants that must hold.
- **No dynamic allocation in userspace.** Stack buffers and fixed-size arrays only.
- **Self-tests**: Every kernel module has a `*_self_test()` function called during `kmain()` init. New modules must include one.

## Contribution Rules

- Use one task per commit or pull request.
- Run the relevant tests, build checks, or QEMU boot command before submitting.
- Keep boot logs and benchmark outputs when they support the change.
- Update the GitHub Wiki or repository notes when code changes alter architecture, build steps, APIs, or benchmark methodology.
- Do not make benchmark claims without measured data and a documented baseline.
- Do not commit credentials, GitHub tokens, private keys, SSH keys, passwords, or secret benchmark data.

## Adding a Userspace App

1. Create `userspace/apps/myapp.c` using `#include <xaios_user.h>`
2. Add the app name to `USER_APPS` in `scripts/build-image.sh`
3. Add `run_user_app("/bin/myapp", PID, app_caps)` in `kernel/core/kmain.c`
4. Add expected output markers to `TARGETS` in `scripts/qemu-smoke.py`
5. Verify with `make image && make qemu-smoke`

See [docs/GETTING-STARTED.md](docs/GETTING-STARTED.md) for a complete example.

## Adding a Kernel Module

1. Create header in `kernel/include/xaios/module.h` and source in the appropriate subdirectory
2. Add a `module_self_test()` function
3. Add the `.o` to `KERNEL_OBJECTS` in `scripts/build-image.sh`
4. Add init call and self-test call in `kernel/core/kmain.c` (respect init ordering)
5. Verify all files compile: `clang --target=aarch64-none-elf -std=c99 -ffreestanding -Wall -Wextra -Werror -Ikernel/include -fsyntax-only kernel/.../*.c`

## Codex Workflow

Future Codex sessions should use the [Codex Work Packages](https://github.com/Pummelchen/XAIOS/wiki/Codex-Work-Packages) wiki page as the operational task list. Complete one package at a time, run the stated checks, and keep commits focused.
# Contributing to XAIOS

XAIOS is a design-stage operating system project. Keep changes small, reviewable, and tied to the current implementation plan.

## Contribution Rules

- Use one task per commit or pull request.
- Run the relevant tests, build checks, or QEMU boot command before submitting.
- Keep boot logs and benchmark outputs when they support the change.
- Update the GitHub Wiki or repository notes when code changes alter architecture, build steps, APIs, or benchmark methodology.
- Do not make benchmark claims without measured data and a documented baseline.
- Do not commit credentials, GitHub tokens, private keys, SSH keys, passwords, or secret benchmark data.

## Codex Workflow

Future Codex sessions should use the [Codex Work Packages](https://github.com/Pummelchen/XAIOS/wiki/Codex-Work-Packages) wiki page as the operational task list. Complete one package at a time, run the stated checks, and keep commits focused.
