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
# Architecture

## Verified high-level architecture

XAIOS is a freestanding OS repository, not a hosted application. The current source builds an AArch64 UEFI/QEMU path, kernel ELF, userspace ELFs, a FAT boot image, a VirtIO test block image, and a persistent mutable storage image.

Evidence:
- `docs/ARCHITECTURE.md`
- `scripts/build-image.sh`
- `kernel/core/kmain.c`
- `scripts/run-qemu-aarch64.sh`

## Boot/runtime flow

1. UEFI firmware loads `BOOTAA64.EFI` from the FAT image.
2. `boot/uefi/loader_main.c` loads `kernel.elf` and passes `xaios_boot_info_t`.
3. `kernel/core/kmain.c` initializes architecture, memory, device, filesystem, security, network, process, AI runtime, and telemetry subsystems.
4. The kernel loads `/init`, `/bin/service-manager`, workers, and userspace apps from initramfs.
5. The kernel emits telemetry and enters an idle `wfe` loop.

## Major components

| Component | Files | Responsibility |
|---|---|---|
| Bootloader | `boot/uefi/loader_main.c`, `boot/uefi/linker.ld` | UEFI entry and kernel handoff. |
| Kernel init | `kernel/core/kmain.c` | Strict init/self-test order and userspace launch. |
| Memory | `kernel/mm/`, `kernel/arch/aarch64/mmu.c` | Physical/virtual memory, NUMA, heaps, arenas, ELF loading. |
| Devices | `kernel/dev/virtio/`, `kernel/arch/aarch64/pci.c`, `kernel/arch/aarch64/smmu.c` | VirtIO block/net, PCI, SMMU. |
| Filesystems | `kernel/fs/`, `scripts/create-initfs.py` | RO initramfs and mutable persistent filesystem. |
| Process/API | `kernel/user/`, `userspace/include/xaios_user.h` | Process table, service supervisor, syscall ABI/wrappers. |
| Network | `kernel/net/`, `kernel/runtime/network_stack.c` | Protocols, socket buffers, network telemetry. |
| Security | `kernel/runtime/security.c` | Capabilities, credential-material rejection, update policy, sandbox path checks. |
| AI runtime | `kernel/runtime/cpu_ai_runtime.c`, `kernel/runtime/model_arena.c`, `kernel/runtime/ai_cell.c` | CPU-only inference simulation/runtime, shared model arena, resource isolation. |
| Remote/admin | `kernel/runtime/remote_login.c`, `userspace/sshd/` | Remote login and SSH/SFTP surfaces. |

## Trust boundaries

- EL0 userspace crosses into kernel via syscall dispatch in `kernel/user/syscall.c`.
- Syscalls are gated by capability masks in `kernel/include/xaios/syscall.h` and dispatch table entries.
- Filesystem access is constrained by security policy in `kernel/runtime/security.c`.
- Mutable state and updates cross persistence/update boundaries in `kernel/fs/`, `kernel/runtime/persistence.c`, and `kernel/runtime/update.c`.
- Network/SSH paths cross from QEMU host forwarding into socket and SSH code.
- Model conversion reads external model files on the host and writes XAIOS-native images.

## Data/job flows

- Build flow: `make image` -> `scripts/build-image.sh` -> Clang/LLD/mtools -> `build/xaios-aarch64.img`, kernel/userspace ELFs, VirtIO images.
- Smoke flow: `make qemu-smoke` -> `scripts/qemu-smoke.py` -> boot QEMU -> scan output markers and telemetry.
- ABI flow: `scripts/qemu-abi-contract.py` -> `scripts/qemu_gate_lib.py` -> compare source syscall header/initfs/model constants against `contracts/qemu-rc-v1.json`.
- Userspace app flow: compile app from `userspace/apps/`, pack into initramfs, launch from `kmain()` with per-app capability mask.

## Current architecture risks

- Contract/docs lag source for newer syscalls and `agenttest` paths.
- Hardware performance is not validated by QEMU gates.
- Security update signature validation is documented in source as QEMU dev-mode format validation, not production cryptographic verification.
