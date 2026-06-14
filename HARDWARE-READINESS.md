# Hardware Readiness Gate

OSAI is not ready for Intel Desktop hardware until the QEMU contract is frozen
and the local QEMU matrix is green.

## Current Gates

The milestone 33 QEMU hardware-readiness gate is:

- `make qemu-readiness-gate`

That command runs the full local QEMU matrix and then validates the generated
artifacts. It writes:

- `build/qemu-benchmark-report.json`
- `build/qemu-preview-manifest.json`
- `build/qemu-cpu-matrix-report.json`
- `build/qemu-readiness-report.json`

The readiness report schema is `osai.qemu.hardware_readiness_gate.v1`.
The frozen release-candidate contract schema is
`osai.qemu.release_candidate_contract.v1` and lives at
`contracts/qemu-rc-v1.json`.

The benchmark harness is a correctness benchmark only. It does not authorize
performance claims against Linux, BSD, or hardware targets.

The milestone 42 QEMU full OS release-candidate gate is:

- `make qemu-full-os-rc`

That command runs `make qemu-readiness-gate`, validates the generated reports,
checks the source syscall/capability ABI against `contracts/qemu-rc-v1.json`,
and writes:

- `build/qemu-full-os-rc-report.json`

The full OS RC report schema is
`osai.qemu.full_os_release_candidate.v1`. Intel Desktop implementation starts
only after that report has `status=pass` and `qemu_full_os_complete=true`.

## Frozen QEMU Contracts

Before moving to Intel Desktop bring-up, these contracts must remain stable:

- AArch64 UEFI loader can load and transfer control to the kernel.
- Kernel parses the UEFI memory map and initializes PMM/VMM.
- Controlled page, read-only write, and NX execute faults are reported through
  the exception path.
- Real EL0 `/init` ELF is loaded from the VirtIO-backed read-only filesystem.
- Syscalls enforce process capabilities and user pointer validation.
- Syscall ABI, telemetry schema, read-only initramfs format, persistence record
  format, and service descriptor format are frozen in
  `contracts/qemu-rc-v1.json`.
- Security policy enforces capabilities, filesystem boundaries, workspace and
  sandbox ownership, rollback authorization, credential rejection, and
  signed-update format validation.
- Persistence metadata can snapshot and roll back boot, service, workspace, and
  sandbox records.
- VirtIO block and VirtIO net self-tests pass.
- AI Cell resource enforcement, shared model arena, private KV/cache, source
  index, Git workspace, sandbox, CPU-AI runtime, and low-latency network smoke
  paths all emit telemetry.
- Hot AI core telemetry reports zero migration and zero involuntary context
  switches in the QEMU gate.
- CPU matrix tiers validate the default ARM64 host/HVF smoke path, ARM64 TCG
  boot probes for `cortex-a53`, `cortex-a72`, `neoverse-n1`, `neoverse-n2`,
  `neoverse-v1`, and `max`, plus x86_64 Intel/AMD command profiles.

## Out of Scope Before Intel

The QEMU release-candidate gate intentionally does not claim:

- performance wins against Linux or BSD;
- complete x86_64 full OS contract parity beyond the milestone 43-48
  bring-up path;
- Intel APIC interrupt routing, HPET, TSC-deadline timers, PCIe, NVMe, and
  NIC hardware drivers;
- production update signing and key management;
- a production mutable filesystem;
- production tokenizer/model runtimes beyond the QEMU CPU-only deterministic
  model format;
- network throughput benchmarking;
- multi-user security policy and remote administration hardening.

## Intel Desktop Entry Criteria

Intel Desktop work can begin only after:

- `make qemu-full-os-rc` passes locally.
- The QEMU full OS RC report exists at
  `build/qemu-full-os-rc-report.json`.
- The full OS RC report status is `pass`.
- The full OS RC report has `qemu_full_os_complete=true`.
- `make qemu-readiness-gate` passes locally.
- The QEMU preview manifest exists at `build/qemu-preview-manifest.json`.
- The QEMU benchmark report exists at `build/qemu-benchmark-report.json`.
- The QEMU CPU matrix report exists at `build/qemu-cpu-matrix-report.json`.
- The QEMU readiness report exists at `build/qemu-readiness-report.json`.
- The readiness report status is `pass`.
- The release-candidate contract exists at `contracts/qemu-rc-v1.json` and
  remains frozen.
- No QEMU benchmark result is represented as a hardware performance claim.
- The GitHub Wiki platform pages are updated for the current gate.

## First Intel Desktop Deliverables

- UEFI x86_64 boot path: milestone 43 gate is `make qemu-x86_64-smoke`.
- Serial console and early exception reporting: milestone 44 gate is
  `make qemu-x86_64-smoke`.
- PMM/VMM initialization from the x86_64 firmware memory map: milestones 45
  and 46 gate through `make qemu-x86_64-smoke`.
- APIC/timer discovery: milestone 47 gate is `make qemu-x86_64-smoke`.
- PCI discovery sufficient for NVMe and NIC bring-up planning: milestone 48
  gate is `make qemu-x86_64-smoke`.
- P-core/E-core placement policy metadata.
- Initial tuned Linux/BSD baseline plan for later measured comparisons.
