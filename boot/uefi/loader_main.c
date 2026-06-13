#include "boot_info.h"
#include "include/uefi_min.h"

static void loader_puts(efi_system_table_t *system_table,
                        const efi_char16_t *message) {
  if (system_table == 0 || system_table->con_out == 0 ||
      system_table->con_out->output_string == 0) {
    return;
  }

  (void)system_table->con_out->output_string(system_table->con_out, message);
}

efi_status_t EFIAPI efi_main(efi_handle_t image_handle,
                             efi_system_table_t *system_table) {
  (void)image_handle;

  loader_puts(system_table, u"OSAI loader starting\r\n");
  loader_puts(system_table, u"OSAI loader target: AArch64 UEFI\r\n");
  loader_puts(system_table, u"OSAI kernel loading is not implemented yet\r\n");

  return EFI_SUCCESS;
}
