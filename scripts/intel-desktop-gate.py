#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import time
from pathlib import Path


SCHEMA = "xaios.intel_desktop.hardware_gate.v1"
REPORT_PATH = Path("build/intel-desktop-gate-report.json")
REQUIRED_MARKERS = [
    "x86_64: Intel Desktop milestone 49 placement policy passed",
    "x86_64: Intel Desktop milestone 50 OS contract port passed",
    "x86_64: Intel Desktop milestone 51 hardware gate passed",
    "x86_64: hot-core telemetry migration_total=0 context_switch_total=0",
    "x86_64: hardware gate qemu_correctness=1 physical_required=1 baseline_required=1 performance_claims_allowed=0",
]


def run_smoke() -> subprocess.CompletedProcess:
    env = os.environ.copy()
    env.setdefault("XAIOS_QEMU_X86_ACCEL", "tcg")
    env.setdefault("XAIOS_QEMU_X86_CPU", "Skylake-Client")
    return subprocess.run(
        ["make", "qemu-x86_64-smoke"],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=env,
        timeout=int(env.get("XAIOS_INTEL_DESKTOP_GATE_TIMEOUT", "120")),
    )


def main() -> int:
    start = time.time()
    proc = run_smoke()
    output = proc.stdout or ""
    sys.stdout.write(output)
    missing = [marker for marker in REQUIRED_MARKERS if marker not in output]
    status = "pass" if proc.returncode == 0 and not missing else "fail"
    report = {
        "schema": SCHEMA,
        "status": status,
        "elapsed_seconds": round(time.time() - start, 3),
        "qemu_smoke_exit_code": proc.returncode,
        "milestones": {
            "49_placement_policy": "pass" if REQUIRED_MARKERS[0] in output else "fail",
            "50_os_contract_port": "pass" if REQUIRED_MARKERS[1] in output else "fail",
            "51_hardware_gate": "pass" if REQUIRED_MARKERS[2] in output else "fail",
        },
        "gates": {
            "qemu_correctness_ready": status == "pass",
            "hot_core_migration_zero": REQUIRED_MARKERS[3] in output,
            "physical_hardware_required": True,
            "tuned_linux_bsd_baseline_required": True,
            "performance_claims_allowed": False,
        },
        "missing_markers": missing,
        "notes": (
            "Intel Desktop gate validates QEMU correctness and planning contracts "
            "only. Hardware performance claims remain forbidden until measured on "
            "physical hardware against tuned Linux/BSD baselines."
        ),
    }

    REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
    REPORT_PATH.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"intel-desktop-gate: report written to {REPORT_PATH}")
    print(f"intel-desktop-gate: status={status}")
    return 0 if status == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
