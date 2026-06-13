#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
EFI_BUILD_DIR="$BUILD_DIR/uefi"
KERNEL_BUILD_DIR="$BUILD_DIR/kernel"
IMAGE_PATH="${OSAI_AARCH64_IMAGE:-$BUILD_DIR/osai-aarch64.img}"
LOADER_OBJ="$EFI_BUILD_DIR/loader_main.obj"
LOADER_EFI="$EFI_BUILD_DIR/BOOTAA64.EFI"
KERNEL_ELF="$KERNEL_BUILD_DIR/kernel.elf"

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

require_tool() {
  label="$1"
  tool_name="$2"
  install_hint="$3"
  shift 3

  if tool_path="$(find_tool "$tool_name" "$@")"; then
    printf '%s\n' "$tool_path"
    return 0
  fi

  printf '%s\n' "error: $label not found. $install_hint" >&2
  exit 1
}

brew_prefix() {
  formula="$1"
  if command -v brew >/dev/null 2>&1; then
    brew --prefix "$formula" 2>/dev/null || true
  fi
}

LLVM_PREFIX="$(brew_prefix llvm)"
LLD_PREFIX="$(brew_prefix lld)"

LLVM_BIN=""
if [ "$LLVM_PREFIX" != "" ]; then
  LLVM_BIN="$LLVM_PREFIX/bin"
fi

LLD_BIN=""
if [ "$LLD_PREFIX" != "" ]; then
  LLD_BIN="$LLD_PREFIX/bin"
fi

CLANG="$(require_tool "Clang" clang "Install with: brew install llvm" "$LLVM_BIN/clang" /usr/bin/clang)"
LLD_LINK="$(require_tool "LLD COFF linker" lld-link "Install with: brew install lld" "$LLD_BIN/lld-link" "$LLVM_BIN/lld-link")"
LD_LLD="$(require_tool "LLD ELF linker" ld.lld "Install with: brew install lld" "$LLD_BIN/ld.lld" "$LLVM_BIN/ld.lld")"
MFORMAT="$(require_tool "mtools mformat" mformat "Install with: brew install mtools" /opt/homebrew/bin/mformat /usr/local/bin/mformat)"
MMD="$(require_tool "mtools mmd" mmd "Install with: brew install mtools" /opt/homebrew/bin/mmd /usr/local/bin/mmd)"
MCOPY="$(require_tool "mtools mcopy" mcopy "Install with: brew install mtools" /opt/homebrew/bin/mcopy /usr/local/bin/mcopy)"

mkdir -p "$EFI_BUILD_DIR" "$KERNEL_BUILD_DIR"

printf '%s\n' "Building AArch64 UEFI loader..."
"$CLANG" \
  --target=aarch64-unknown-windows \
  -ffreestanding \
  -fno-stack-protector \
  -fno-builtin \
  -fshort-wchar \
  -Wall \
  -Wextra \
  -Werror \
  -I"$ROOT_DIR/boot/uefi" \
  -c "$ROOT_DIR/boot/uefi/loader_main.c" \
  -o "$LOADER_OBJ"

"$LLD_LINK" \
  /nologo \
  /subsystem:efi_application \
  /entry:efi_main \
  /nodefaultlib \
  /machine:arm64 \
  "$LOADER_OBJ" \
  /out:"$LOADER_EFI"

printf '%s\n' "Building AArch64 kernel ELF..."
KERNEL_CFLAGS="
  --target=aarch64-none-elf
  -std=c99
  -ffreestanding
  -fno-stack-protector
  -fno-builtin
  -fno-pic
  -fno-pie
  -Wall
  -Wextra
  -Werror
  -I$ROOT_DIR/kernel/include
"

KERNEL_OBJECTS="
  $KERNEL_BUILD_DIR/entry.o
  $KERNEL_BUILD_DIR/kmain.o
  $KERNEL_BUILD_DIR/klog.o
  $KERNEL_BUILD_DIR/panic.o
  $KERNEL_BUILD_DIR/assert.o
  $KERNEL_BUILD_DIR/pmm.o
  $KERNEL_BUILD_DIR/mmu.o
"

"$CLANG" $KERNEL_CFLAGS -c "$ROOT_DIR/kernel/arch/aarch64/entry.S" -o "$KERNEL_BUILD_DIR/entry.o"
"$CLANG" $KERNEL_CFLAGS -c "$ROOT_DIR/kernel/core/kmain.c" -o "$KERNEL_BUILD_DIR/kmain.o"
"$CLANG" $KERNEL_CFLAGS -c "$ROOT_DIR/kernel/core/klog.c" -o "$KERNEL_BUILD_DIR/klog.o"
"$CLANG" $KERNEL_CFLAGS -c "$ROOT_DIR/kernel/core/panic.c" -o "$KERNEL_BUILD_DIR/panic.o"
"$CLANG" $KERNEL_CFLAGS -c "$ROOT_DIR/kernel/core/assert.c" -o "$KERNEL_BUILD_DIR/assert.o"
"$CLANG" $KERNEL_CFLAGS -c "$ROOT_DIR/kernel/mm/pmm.c" -o "$KERNEL_BUILD_DIR/pmm.o"
"$CLANG" $KERNEL_CFLAGS -c "$ROOT_DIR/kernel/arch/aarch64/mmu.c" -o "$KERNEL_BUILD_DIR/mmu.o"

"$LD_LLD" \
  -nostdlib \
  -T "$ROOT_DIR/kernel/arch/aarch64/linker.ld" \
  -o "$KERNEL_ELF" \
  $KERNEL_OBJECTS

rm -f "$IMAGE_PATH"
mkdir -p "$(dirname -- "$IMAGE_PATH")"

printf '%s\n' "Creating FAT boot image: $IMAGE_PATH"
dd if=/dev/zero of="$IMAGE_PATH" bs=1m count=64 status=none
"$MFORMAT" -i "$IMAGE_PATH" -F -v OSAI ::
"$MMD" -i "$IMAGE_PATH" ::/EFI
"$MMD" -i "$IMAGE_PATH" ::/EFI/BOOT
"$MMD" -i "$IMAGE_PATH" ::/EFI/OSAI
"$MCOPY" -i "$IMAGE_PATH" "$LOADER_EFI" ::/EFI/BOOT/BOOTAA64.EFI
"$MCOPY" -i "$IMAGE_PATH" "$KERNEL_ELF" ::/EFI/OSAI/kernel.elf

printf '%s\n' "Created $IMAGE_PATH"
