#!/usr/bin/env python3
import os
import select
import subprocess
import sys
import time


TARGETS = [
    "OSAI loader starting",
    "OSAI loader target: x86_64 UEFI",
    "OSAI loader loaded kernel.elf",
    "OSAI loader validated ELF64 kernel",
    "OSAI loader copied kernel segments",
    "OSAI loader exiting boot services",
    "OSAI x86_64 kernel starting",
    "x86_64: UEFI boot info valid",
    "x86_64: COM1 serial online",
    "x86_64: Intel Desktop milestone 43 boot path passed",
]


def main() -> int:
    env = os.environ.copy()
    env.setdefault("OSAI_QEMU_X86_ACCEL", "tcg")
    env.setdefault("OSAI_QEMU_X86_CPU", "Skylake-Client")
    timeout = int(env.get("OSAI_QEMU_X86_SMOKE_TIMEOUT", "45"))

    proc = subprocess.Popen(
        ["./scripts/run-qemu-x86_64.sh"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=False,
        bufsize=0,
        env=env,
    )

    seen = []
    deadline = time.time() + timeout
    try:
        assert proc.stdout is not None
        fd = proc.stdout.fileno()
        while time.time() < deadline:
            ready, _, _ = select.select([fd], [], [], 0.2)
            if ready:
                chunk = os.read(fd, 4096).decode("utf-8", errors="replace")
                if not chunk:
                    break
                sys.stdout.write(chunk)
                sys.stdout.flush()
                seen.append(chunk)
                text = "".join(seen)
                if all(target in text for target in TARGETS):
                    print(
                        "qemu-x86_64-smoke: x86_64 boot reached all "
                        "Intel Desktop milestone 43 markers"
                    )
                    return 0
            elif proc.poll() is not None:
                break
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=3)

    text = "".join(seen)
    missing = [target for target in TARGETS if target not in text]
    print("\nqemu-x86_64-smoke: missing markers:")
    for marker in missing:
        print(f"  - {marker}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
