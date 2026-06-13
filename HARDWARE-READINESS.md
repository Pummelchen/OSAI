# Hardware Readiness Gate

OSAI is not ready for Intel Desktop hardware until the QEMU contract is frozen
and the local QEMU matrix is green.

## Current Gate

The milestone 33 QEMU hardware-readiness gate is:

- `make qemu-readiness-gate`

That command runs the full local QEMU matrix and then validates the generated
artifacts. It writes:

- `build/qemu-benchmark-report.json`
- `build/qemu-preview-manifest.json`
- `build/qemu-readiness-report.json`

The readiness report schema is `osai.qemu.hardware_readiness_gate.v1`.

The benchmark harness is a correctness benchmark only. It does not authorize
performance claims against Linux, BSD, or hardware targets.

## Frozen QEMU Contracts

Before moving to Intel Desktop bring-up, these contracts must remain stable:

- AArch64 UEFI loader can load and transfer control to the kernel.
- Kernel parses the UEFI memory map and initializes PMM/VMM.
- Controlled page, read-only write, and NX execute faults are reported through
  the exception path.
- Real EL0 `/init` ELF is loaded from the VirtIO-backed read-only filesystem.
- Syscalls enforce process capabilities and user pointer validation.
- Security policy rejects credential material and unsigned update payloads.
- Persistence metadata can snapshot and roll back boot, service, workspace, and
  sandbox records.
- VirtIO block and VirtIO net self-tests pass.
- AI Cell resource enforcement, shared model arena, private KV/cache, source
  index, Git workspace, sandbox, CPU-AI runtime, and low-latency network smoke
  paths all emit telemetry.
- Hot AI core telemetry reports zero migration and zero involuntary context
  switches in the QEMU gate.

## Intel Desktop Entry Criteria

Intel Desktop work can begin only after:

- `make qemu-readiness-gate` passes locally.
- The QEMU preview manifest exists at `build/qemu-preview-manifest.json`.
- The QEMU benchmark report exists at `build/qemu-benchmark-report.json`.
- The QEMU readiness report exists at `build/qemu-readiness-report.json`.
- The readiness report status is `pass`.
- No QEMU benchmark result is represented as a hardware performance claim.
- The GitHub Wiki platform pages are updated for the current gate.

## First Intel Desktop Deliverables

- UEFI x86_64 boot path.
- Serial console and early exception reporting.
- PMM/VMM initialization from the x86_64 firmware memory map.
- APIC/timer discovery.
- PCI discovery sufficient for NVMe and NIC bring-up planning.
- P-core/E-core placement policy metadata.
- Initial tuned Linux/BSD baseline plan for later measured comparisons.
