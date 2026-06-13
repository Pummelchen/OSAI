#ifndef OSAI_UEFI_MIN_H
#define OSAI_UEFI_MIN_H

#include <stdint.h>

#define EFIAPI
#define EFI_SUCCESS ((efi_status_t)0)

typedef uint16_t efi_char16_t;
typedef uint64_t efi_status_t;
typedef void *efi_handle_t;

typedef struct efi_table_header {
  uint64_t signature;
  uint32_t revision;
  uint32_t header_size;
  uint32_t crc32;
  uint32_t reserved;
} efi_table_header_t;

typedef struct efi_simple_text_output_protocol efi_simple_text_output_protocol_t;
typedef struct efi_system_table efi_system_table_t;

struct efi_simple_text_output_protocol {
  void *reset;
  efi_status_t(EFIAPI *output_string)(
      efi_simple_text_output_protocol_t *self,
      const efi_char16_t *string);
};

struct efi_system_table {
  efi_table_header_t hdr;
  efi_char16_t *firmware_vendor;
  uint32_t firmware_revision;
  uint32_t _pad0;
  efi_handle_t console_in_handle;
  void *con_in;
  efi_handle_t console_out_handle;
  efi_simple_text_output_protocol_t *con_out;
};

#endif
