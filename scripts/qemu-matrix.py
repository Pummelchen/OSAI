#!/usr/bin/env python3
import os
import subprocess
import sys
import time


SCENARIOS = [
    ("qemu-smoke", ["make", "qemu-smoke"], 120),
    ("qemu-benchmark", ["python3", "./scripts/qemu-benchmark.py"], 120),
    ("qemu-persistence-reboot", ["make", "qemu-persistence-reboot"], 140),
    ("qemu-preview", ["make", "qemu-preview"], 120),
    ("qemu-fault-matrix", ["make", "qemu-fault-matrix"], 180),
    ("qemu-x86_64-smoke", ["make", "qemu-x86_64-smoke"], 120),
    ("qemu-cpu-matrix", ["make", "qemu-cpu-matrix"], 900),
    ("qemu-dry-run-aarch64", ["./scripts/run-qemu-aarch64.sh", "--dry-run"], 10),
    ("qemu-dry-run-x86_64", ["./scripts/run-qemu-x86_64.sh", "--dry-run"], 10),
]


def run_scenario(name: str, cmd, timeout: int, env) -> int:
    print(f"\n[QEMU matrix] running {name}: {' '.join(cmd)}")
    start = time.time()
    proc = subprocess.run(
        cmd,
        stdout=None,
        stderr=None,
        timeout=timeout,
        env=env,
        text=True,
        check=False,
    )
    elapsed = time.time() - start
    print(f"[QEMU matrix] {name} exit={proc.returncode} elapsed={elapsed:.2f}s")
    return proc.returncode


def main() -> int:
    env = os.environ.copy()
    env.setdefault("OSAI_QEMU_SMOKE_TIMEOUT", "60")
    failures = []
    for name, cmd, timeout in SCENARIOS:
        rc = run_scenario(name, cmd, timeout, env)
        if rc != 0:
            failures.append((name, rc))
    if failures:
        print("\nqemu-matrix: failed scenarios:")
        for name, rc in failures:
            print(f"  {name}: rc={rc}")
        return 1
    print("\nqemu-matrix: all scenarios passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
