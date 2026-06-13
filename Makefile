SHELL := /bin/sh

.PHONY: all bootstrap test image qemu qemu-aarch64 qemu-x86_64 qemu-dry-run qemu-smoke qemu-preview qemu-matrix qemu-benchmark clean

all: bootstrap image

bootstrap:
	./scripts/macos-bootstrap.sh

test: bootstrap image qemu-dry-run

image:
	./scripts/build-image.sh

qemu:
	./scripts/run-qemu-aarch64.sh

qemu-aarch64:
	./scripts/run-qemu-aarch64.sh

qemu-x86_64:
	./scripts/run-qemu-x86_64.sh

qemu-dry-run:
	./scripts/run-qemu-aarch64.sh --dry-run
	./scripts/run-qemu-x86_64.sh --dry-run

qemu-smoke: image
	python3 ./scripts/qemu-smoke.py

qemu-preview: qemu-smoke

qemu-matrix:
	python3 ./scripts/qemu-matrix.py

qemu-benchmark:
	python3 ./scripts/qemu-benchmark.py

clean:
	rm -rf build out dist
