#ifndef OSAI_UEFI_MIN_H
#define OSAI_UEFI_MIN_H

#include <stdint.h>

#define EFIAPI
#define EFI_SUCCESS ((efi_status_t)0)
#define EFI_ERROR_BIT UINT64_C(0x8000000000000000)
#define EFI_BUFFER_TOO_SMALL ((efi_status_t)(EFI_ERROR_BIT | 5))
#define EFI_LOAD_ERROR ((efi_status_t)(EFI_ERROR_BIT | 1))
#define EFI_NOT_FOUND ((efi_status_t)(EFI_ERROR_BIT | 14))
#define EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL 0x00000001U
#define EFI_FILE_MODE_READ UINT64_C(0x0000000000000001)
#define EFI_ALLOCATE_ANY_PAGES 0
#define EFI_ALLOCATE_ADDRESS 2
#define EFI_LOADER_DATA 2
#define EFI_SIZE_TO_PAGES(size) (((size) + 0xfffULL) >> 12)

typedef uint16_t efi_char16_t;
typedef uint64_t efi_status_t;
typedef uint64_t efi_physical_address_t;
typedef void *efi_handle_t;

typedef struct efi_guid {
  uint32_t data1;
  uint16_t data2;
  uint16_t data3;
  uint8_t data4[8];
} efi_guid_t;

typedef struct efi_table_header {
  uint64_t signature;
  uint32_t revision;
  uint32_t header_size;
  uint32_t crc32;
  uint32_t reserved;
} efi_table_header_t;

typedef struct efi_simple_text_output_protocol efi_simple_text_output_protocol_t;
typedef struct efi_boot_services efi_boot_services_t;
typedef struct efi_runtime_services efi_runtime_services_t;
typedef struct efi_loaded_image_protocol efi_loaded_image_protocol_t;
typedef struct efi_simple_file_system_protocol efi_simple_file_system_protocol_t;
typedef struct efi_file_protocol efi_file_protocol_t;
typedef struct efi_system_table efi_system_table_t;

struct efi_simple_text_output_protocol {
  void *reset;
  efi_status_t(EFIAPI *output_string)(
      efi_simple_text_output_protocol_t *self,
      const efi_char16_t *string);
};

struct efi_boot_services {
  efi_table_header_t hdr;
  void *raise_tpl;
  void *restore_tpl;
  efi_status_t(EFIAPI *allocate_pages)(int type, int memory_type,
                                       uint64_t pages,
                                       efi_physical_address_t *memory);
  efi_status_t(EFIAPI *free_pages)(efi_physical_address_t memory,
                                   uint64_t pages);
  efi_status_t(EFIAPI *get_memory_map)(uint64_t *memory_map_size,
                                       void *memory_map, uint64_t *map_key,
                                       uint64_t *descriptor_size,
                                       uint32_t *descriptor_version);
  efi_status_t(EFIAPI *allocate_pool)(int pool_type, uint64_t size,
                                      void **buffer);
  efi_status_t(EFIAPI *free_pool)(void *buffer);
  void *create_event;
  void *set_timer;
  void *wait_for_event;
  void *signal_event;
  void *close_event;
  void *check_event;
  void *install_protocol_interface;
  void *reinstall_protocol_interface;
  void *uninstall_protocol_interface;
  efi_status_t(EFIAPI *handle_protocol)(efi_handle_t handle,
                                        efi_guid_t *protocol,
                                        void **interface);
  void *reserved;
  void *register_protocol_notify;
  void *locate_handle;
  void *locate_device_path;
  void *install_configuration_table;
  void *load_image;
  void *start_image;
  void *exit;
  void *unload_image;
  efi_status_t(EFIAPI *exit_boot_services)(efi_handle_t image_handle,
                                           uint64_t map_key);
  void *get_next_monotonic_count;
  void *stall;
  void *set_watchdog_timer;
  void *connect_controller;
  void *disconnect_controller;
  efi_status_t(EFIAPI *open_protocol)(efi_handle_t handle,
                                      efi_guid_t *protocol,
                                      void **interface,
                                      efi_handle_t agent_handle,
                                      efi_handle_t controller_handle,
                                      uint32_t attributes);
};

struct efi_loaded_image_protocol {
  uint32_t revision;
  efi_handle_t parent_handle;
  efi_system_table_t *system_table;
  efi_handle_t device_handle;
};

struct efi_simple_file_system_protocol {
  uint64_t revision;
  efi_status_t(EFIAPI *open_volume)(
      efi_simple_file_system_protocol_t *self,
      efi_file_protocol_t **root);
};

struct efi_file_protocol {
  uint64_t revision;
  efi_status_t(EFIAPI *open)(efi_file_protocol_t *self,
                             efi_file_protocol_t **new_handle,
                             const efi_char16_t *file_name,
                             uint64_t open_mode,
                             uint64_t attributes);
  efi_status_t(EFIAPI *close)(efi_file_protocol_t *self);
  void *delete_file;
  efi_status_t(EFIAPI *read)(efi_file_protocol_t *self,
                             uint64_t *buffer_size,
                             void *buffer);
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
  efi_handle_t standard_error_handle;
  efi_simple_text_output_protocol_t *std_err;
  efi_runtime_services_t *runtime_services;
  efi_boot_services_t *boot_services;
};

#endif
