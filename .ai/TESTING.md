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
# Testing

## Test model

No conventional unit-test framework was detected. Validation is based on:

- kernel/userspace self-tests run during boot;
- Python QEMU gates that search serial output and telemetry markers;
- ABI/contract Python checks;
- CI compile checks and QEMU smoke/regression jobs.

Evidence:
- `kernel/core/kmain.c`
- `scripts/qemu-smoke.py`
- `scripts/qemu-abi-contract.py`
- `scripts/qemu_gate_lib.py`
- `.github/workflows/ci.yml`
- `Makefile`

## Main validations

| Validation | Command | When to use |
|---|---|---|
| Syntax-only C check | `make compile-check` | Small C changes; quicker than booting QEMU. |
| Smoke gate | `make qemu-smoke` | Most code changes. |
| ABI contract | `python3 scripts/qemu-abi-contract.py` | Syscall, initfs, contract, model format changes. |
| Regression | `make qemu-regression-suite` | Broader kernel/userspace changes. |
| Network suite | `make qemu-network-suite` | Network/socket/SSH-adjacent changes. |
| CPU-AI suite | `make qemu-cpu-ai-suite` | AI runtime/model changes. |
| Readiness | `make qemu-readiness-gate` | Changes that may affect QEMU readiness. |
| Full OS RC | `make qemu-full-os-rc` | Release-candidate or hardware-entry decisions. |

## CI behavior

GitHub Actions workflow `.github/workflows/ci.yml` runs:

- compile-check for kernel C files;
- compile-check for userspace C files;
- ABI contract validation;
- `make image` followed by `scripts/qemu-smoke.py`;
- `make image` followed by `scripts/qemu-regression-suite.py`.

CI installs toolchain packages with apt and sets `XAIOS_QEMU_SMOKE_TIMEOUT=120` for QEMU smoke/regression jobs.

## Focused testing guidance

- Syscall/API change: run `make compile-check` and `python3 scripts/qemu-abi-contract.py`; then `make qemu-smoke` if QEMU is available.
- Userspace app change: run `make image && make qemu-smoke`; update smoke markers if expected output changes.
- Security/update change: run relevant security/update gates plus smoke.
- Filesystem/persistence change: run filesystem/update/readiness gates.
- Network/SSH change: run network suite and SSH smoke if environment supports it.
- Docs-only change: validate Markdown links, manifest JSON, changed-file scope, and secret-like patterns; source tests may be skipped with explanation.

## Fixtures and generated reports

- QEMU marker lists live in scripts such as `scripts/qemu-smoke.py`.
- Contract data lives in `contracts/qemu-rc-v1.json`.
- Gate reports are generated under `build/` and should not be committed.

## Known testing caveats

- QEMU gates are correctness evidence only and do not authorize hardware performance claims.
- `contracts/qemu-rc-v1.json` appears behind source for syscalls beyond 28; validate intent before treating contract coverage as complete.
- Local QEMU gates require host QEMU/firmware/toolchain availability.
