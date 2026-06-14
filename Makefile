SHELL := /bin/sh

.PHONY: all bootstrap test image image-x86_64 qemu qemu-aarch64 qemu-x86_64 qemu-x86_64-smoke intel-desktop-gate qemu-dry-run qemu-smoke qemu-process-gate qemu-osctl-gate qemu-preview qemu-matrix qemu-cpu-matrix qemu-benchmark qemu-persistence-reboot qemu-fault-matrix qemu-regression-suite qemu-fault-injection qemu-abi-contract qemu-boot-loop qemu-userspace-suite qemu-network-suite qemu-cpu-ai-suite qemu-developer-ux qemu-post51-gate qemu-readiness-gate qemu-full-os-rc clean

all: bootstrap image

bootstrap:
	./scripts/macos-bootstrap.sh

test: bootstrap image qemu-dry-run

image:
	./scripts/build-image.sh

image-x86_64:
	./scripts/build-image-x86_64.sh

qemu:
	./scripts/run-qemu-aarch64.sh

qemu-aarch64:
	./scripts/run-qemu-aarch64.sh

qemu-x86_64: image-x86_64
	./scripts/run-qemu-x86_64.sh

qemu-dry-run:
	./scripts/run-qemu-aarch64.sh --dry-run
	./scripts/run-qemu-x86_64.sh --dry-run

qemu-smoke: image
	python3 ./scripts/qemu-smoke.py

qemu-process-gate: image
	python3 ./scripts/qemu-process-gate.py

qemu-osctl-gate: image
	python3 ./scripts/qemu-osctl-gate.py

qemu-x86_64-smoke: image-x86_64
	python3 ./scripts/qemu-x86_64-smoke.py

intel-desktop-gate:
	python3 ./scripts/intel-desktop-gate.py

qemu-preview: image
	python3 ./scripts/qemu-preview.py

qemu-matrix:
	python3 ./scripts/qemu-matrix.py

qemu-cpu-matrix: image image-x86_64
	python3 ./scripts/qemu-cpu-matrix.py

qemu-benchmark:
	python3 ./scripts/qemu-benchmark.py

qemu-persistence-reboot: image
	python3 ./scripts/qemu-persistence-reboot.py

qemu-fault-matrix:
	python3 ./scripts/qemu-fault-matrix.py

qemu-regression-suite: image
	python3 ./scripts/qemu-regression-suite.py

qemu-fault-injection: image
	python3 ./scripts/qemu-fault-injection.py

qemu-abi-contract:
	python3 ./scripts/qemu-abi-contract.py

qemu-boot-loop: image
	python3 ./scripts/qemu-boot-loop.py

qemu-userspace-suite: image
	python3 ./scripts/qemu-userspace-suite.py

qemu-network-suite: image
	python3 ./scripts/qemu-network-suite.py

qemu-cpu-ai-suite: image
	python3 ./scripts/qemu-cpu-ai-suite.py

qemu-developer-ux:
	python3 ./scripts/qemu-developer-ux.py

qemu-post51-gate: image image-x86_64
	python3 ./scripts/qemu-post51-gate.py

qemu-readiness-gate:
	python3 ./scripts/qemu-readiness-gate.py

qemu-full-os-rc:
	python3 ./scripts/qemu-full-os-rc.py

clean:
	rm -rf build out dist
