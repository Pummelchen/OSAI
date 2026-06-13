# OSAI Documentation

This folder contains design and implementation documentation for OSAI.

## Documents

- [`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md) — detailed 0-to-100% implementation plan for building OSAI with Codex, starting in QEMU on macOS and progressing through Intel Desktop, Intel Xeon, and ARM/NVIDIA N1X-class hardware.

## Current Priority

The first engineering milestone is a bootable QEMU prototype on macOS:

```text
UEFI loader
kernel.elf handoff
serial logging
memory map parsing
physical memory manager
virtual memory
exceptions
SMP
minimal user init
virtio-blk
virtio-net
admin over TCP/SSH
AI Cell lifecycle
shared model arena MVP
```
