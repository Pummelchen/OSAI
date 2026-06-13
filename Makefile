SHELL := /bin/sh

.PHONY: all bootstrap test image qemu clean

all: bootstrap

bootstrap:
	./scripts/macos-bootstrap.sh

test: bootstrap

image:
	@echo "OSAI image build is not implemented yet. Complete WP-003 first."
	@exit 1

qemu:
	@echo "OSAI QEMU run script is not implemented yet. Complete WP-002 first."
	@exit 1

clean:
	rm -rf build out dist
