#!/bin/sh
set -eu

dry_run=0
if [ "${1:-}" = "--dry-run" ]; then
  dry_run=1
  shift
fi

if [ "$#" -ne 0 ]; then
  printf '%s\n' "usage: $0 [--dry-run]" >&2
  exit 2
fi

find_tool() {
  tool_name="$1"
  shift

  for candidate in "$@"; do
    if [ -x "$candidate" ]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done

  if command -v "$tool_name" >/dev/null 2>&1; then
    command -v "$tool_name"
    return 0
  fi

  return 1
}

brew_prefix() {
  formula="$1"
  if command -v brew >/dev/null 2>&1; then
    brew --prefix "$formula" 2>/dev/null || true
  fi
}

find_ovmf_firmware() {
  if [ "${OSAI_OVMF_CODE:-}" != "" ]; then
    [ -f "$OSAI_OVMF_CODE" ] && printf '%s\n' "$OSAI_OVMF_CODE" && return 0
    return 1
  fi

  for candidate in \
    /opt/homebrew/share/qemu/edk2-x86_64-code.fd \
    /opt/homebrew/share/qemu/OVMF_CODE.fd \
    /opt/homebrew/share/edk2/x64/OVMF_CODE.fd \
    /usr/local/share/qemu/edk2-x86_64-code.fd \
    /usr/local/share/qemu/OVMF_CODE.fd \
    /usr/local/share/edk2/x64/OVMF_CODE.fd
  do
    if [ -f "$candidate" ]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done

  return 1
}

print_command() {
  printf 'QEMU x86_64 command:\n'
  printf '  '
  for arg in "$@"; do
    case "$arg" in
      *[!A-Za-z0-9_./:=,+-]*|'')
        printf "'%s' " "$(printf '%s' "$arg" | sed "s/'/'\\\\''/g")"
        ;;
      *)
        printf '%s ' "$arg"
        ;;
    esac
  done
  printf '\n'
}

QEMU_PREFIX="$(brew_prefix qemu)"
QEMU_BIN=""
if [ "$QEMU_PREFIX" != "" ]; then
  QEMU_BIN="$QEMU_PREFIX/bin"
fi

if ! qemu="$(find_tool qemu-system-x86_64 "$QEMU_BIN/qemu-system-x86_64")"; then
  printf '%s\n' "error: qemu-system-x86_64 not found. Install with: brew install qemu" >&2
  exit 1
fi

if ! firmware="$(find_ovmf_firmware)"; then
  printf '%s\n' "error: x86_64 OVMF firmware not found. Set OSAI_OVMF_CODE=/path/to/edk2-x86_64-code.fd." >&2
  exit 1
fi

accel="${OSAI_QEMU_X86_ACCEL:-tcg}"
machine="${OSAI_QEMU_X86_MACHINE:-q35}"
cpu="${OSAI_QEMU_X86_CPU:-max}"
memory="${OSAI_QEMU_X86_MEMORY:-2G}"
smp="${OSAI_QEMU_X86_SMP:-4}"
image="${OSAI_X86_64_IMAGE:-build/osai-x86_64.img}"

if [ "$dry_run" -eq 0 ] && [ ! -f "$image" ]; then
  printf '%s\n' "error: missing x86_64 boot image: $image" >&2
  printf '%s\n' "       Complete WP-003/WP-004 image creation first, or set OSAI_X86_64_IMAGE=/path/to/image.img." >&2
  exit 1
fi

set -- "$qemu" \
  -machine "$machine,accel=$accel" \
  -cpu "$cpu" \
  -m "$memory" \
  -smp "$smp" \
  -nographic \
  -serial mon:stdio \
  -drive "if=pflash,format=raw,readonly=on,file=$firmware" \
  -drive "if=virtio,format=raw,file=$image" \
  -netdev user,id=net0,hostfwd=tcp::2223-:22 \
  -device virtio-net-pci,netdev=net0

if [ "$dry_run" -eq 1 ]; then
  print_command "$@"
  exit 0
fi

exec "$@"
