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
# Commands

Run commands from the repository root unless noted.

## Toolchain install / verification

macOS prerequisites documented in `docs/GETTING-STARTED.md`:

```sh
brew install llvm lld qemu mtools python3
make bootstrap
```

Ubuntu/Debian prerequisites documented in `docs/GETTING-STARTED.md` and CI:

```sh
sudo apt-get update
sudo apt-get install -y clang lld qemu-system-arm qemu-efi-aarch64 mtools python3
make bootstrap
```

Optional Python dev dependency:

```sh
python3 -m pip install -r requirements-dev.txt
```

Model converter dependencies, when using `tools/convert_gguf_to_xaios.py`:

```sh
python3 -m pip install gguf numpy
```

## Build

| Task | Command |
|---|---|
| Default build | `make all` |
| AArch64 image | `make image` |
| x86_64 image | `make image-x86_64` |
| Clean generated outputs | `make clean` |
| Clean persistent image | `make clean-persistent` |
| Syntax-only C compile check | `make compile-check` |

## Run locally

| Task | Command |
|---|---|
| AArch64 QEMU | `make qemu` or `make qemu-aarch64` |
| x86_64 QEMU | `make qemu-x86_64` |
| Dry-run QEMU command lines | `make qemu-dry-run` |
| SSH bridge | `make xaios-ssh-bridge` |
| Connect to local SSH bridge | `ssh -p 2222 admin@localhost` |

`run-qemu-aarch64.sh` supports environment overrides such as `XAIOS_AAVMF_CODE`, `XAIOS_QEMU_ACCEL`, `XAIOS_QEMU_CPU`, `XAIOS_QEMU_MACHINE`, `XAIOS_QEMU_MEMORY`, `XAIOS_QEMU_SMP`, and `XAIOS_QEMU_HOSTFWD_PORT`.

## Test/gates

| Gate | Command | Notes |
|---|---|---|
| Primary smoke | `make qemu-smoke` | Boots full OS and scans markers. |
| Process | `make qemu-process-gate` | Process lifecycle/scheduler. |
| OS control | `make qemu-osctl-gate` | Control-plane telemetry. |
| Filesystem | `make qemu-filesystem-gate` | Mutable filesystem. |
| Network | `make qemu-network-suite` or `make qemu-network-full-gate` | TCP/UDP/network paths. |
| CPU-AI | `make qemu-cpu-ai-suite` or `make qemu-cpu-ai-runtime-gate` | CPU-only AI runtime. |
| AI Cell | `make qemu-ai-cell-gate` | Resource contracts. |
| Security | `make qemu-security-gate` | Security policy markers. |
| Update | `make qemu-update-gate` | Update/rollback paths. |
| Regression | `make qemu-regression-suite` | Broader regression suite. |
| ABI contract | `make qemu-abi-contract` or `python3 scripts/qemu-abi-contract.py` | Contract/source validation. |
| Readiness | `make qemu-readiness-gate` | Full local QEMU readiness. |
| Full OS RC | `make qemu-full-os-rc` | Release-candidate gate. |

## Format/lint/typecheck

No standalone formatter, linter, or typechecker command was detected beyond C compiler warnings-as-errors and Python gate execution.

## Database, migrations, Docker, deploy

No database migration tooling, Dockerfile/compose setup, or deploy command was detected in inspected files.

## Release/readiness artifacts

Readiness and RC gates write reports under `build/`, including `build/qemu-readiness-report.json` and `build/qemu-full-os-rc-report.json`. These are generated artifacts and should not be committed.
