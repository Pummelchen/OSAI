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
# Known unknowns and conflicts

## Conflicting: performance claims vs QEMU evidence

README, project tracker, and local wiki pages contain CPU-only performance targets and claims. `HARDWARE-READINESS.md` and `contracts/qemu-rc-v1.json` say QEMU benchmark/gate output is correctness evidence only and does not authorize hardware performance claims.

Recommendation: treat performance numbers as targets or unverified design claims unless a human provides measured hardware baselines.

Evidence:
- `README.md`
- `PROJECT-TRACKER.md`
- `wiki/Qwen3.6-INT6-Support.md`
- `HARDWARE-READINESS.md`
- `contracts/qemu-rc-v1.json`

## Conflicting: syscall/API documentation lag

Source defines syscalls through `XAIOS_SYSCALL_AGENT_DISPATCH` number 34, while `docs/API.md` documents through socket close number 33 and `contracts/qemu-rc-v1.json` lists only numbers 1-28. `scripts/qemu_gate_lib.py` validates contract entries against source, but does not obviously require the contract to cover every source syscall.

Recommendation: before changing syscalls, decide whether to update docs/API and contract coverage in the same PR.

Evidence:
- `kernel/include/xaios/syscall.h`
- `userspace/include/xaios_user.h`
- `kernel/user/syscall.c`
- `docs/API.md`
- `contracts/qemu-rc-v1.json`
- `scripts/qemu_gate_lib.py`

## Conflicting: license status

`LICENSE` starts with MIT license text but ends with “License to be decided.” `README.md` also says license is to be decided.

Recommendation: do not alter license language without human approval.

Evidence:
- `LICENSE`
- `README.md`

## Stale or incomplete docs

- `docs/ARCHITECTURE.md` userspace lifecycle omits source-visible `agenttest` PID 16.
- `PROJECT-TRACKER.md` references commands such as `make sshd` and some milestone commands that were not present in the inspected `Makefile`.
- `CONTRIBUTING.md` contains duplicate sections and an existing vendor-specific workflow heading; it was preserved because it is human-facing contributor documentation, not a generated AI onboarding file.

## Unknowns

- Exact complete repository file tree could not be cloned in this runtime because direct GitHub DNS resolution failed; inspection used the GitHub connector and targeted source/config/docs reads.
- Hardware validation status beyond repository docs is unknown.
- Production signing/key-management design is not complete in source comments inspected.
- Whether `.qoder/repowiki/` should be removed, ignored, or refreshed is unknown; it was not modified.

## Ask a human before editing

- Licensing text.
- Hardware performance claims or benchmark methodology.
- Production security model/signing claims.
- Removal of non-onboarding docs with vendor-specific wording.
- Any change that relaxes capability checks, credential-material rejection, update authorization, or sandbox path validation.
