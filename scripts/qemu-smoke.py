#!/usr/bin/env python3
import os
import select
import subprocess
import sys
import time


TARGETS = [
    "exceptions: self-test",
    "timer: monotonic self-test passed",
    "smp: per-core registry self-test passed",
    "virtio-blk: read self-test passed",
    "virtio-net: rx/tx self-test passed",
    "initramfs: lookup self-test passed",
    "service: supervisor self-test passed",
    "model-arena: shared read-only arena self-test passed",
    "core-lease: owner=0 mask=0x2 acquired",
    "ai-cell: lifecycle self-test passed",
    "kheap: self-test passed",
    "VMM translation test passed",
    "gic: discovery self-test passed",
    "PMM 1024 page allocate/free test passed",
    "telemetry: boot_summary",
    "/init: hello from EL0",
    "user: /init exited status=0",
]


def main() -> int:
    proc = subprocess.Popen(
        ["make", "qemu-aarch64"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    seen = []
    deadline = time.time() + int(os.environ.get("OSAI_QEMU_SMOKE_TIMEOUT", "60"))
    try:
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
                    print("\nQEMU smoke boot reached all Phase 8 MVP markers")
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
    print("\nmissing targets:", missing)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
