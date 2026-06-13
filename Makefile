SHELL := /bin/sh

.PHONY: all bootstrap test image qemu qemu-aarch64 qemu-x86_64 qemu-dry-run clean

all: bootstrap

bootstrap:
	./scripts/macos-bootstrap.sh

test: bootstrap qemu-dry-run

image:
	@echo "OSAI image build is not implemented yet. Complete WP-003 first."
	@exit 1

qemu:
	./scripts/run-qemu-aarch64.sh

qemu-aarch64:
	./scripts/run-qemu-aarch64.sh

qemu-x86_64:
	./scripts/run-qemu-x86_64.sh

qemu-dry-run:
	./scripts/run-qemu-aarch64.sh --dry-run
	./scripts/run-qemu-x86_64.sh --dry-run

clean:
	rm -rf build out dist
