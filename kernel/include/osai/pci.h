#ifndef OSAI_PCI_H
#define OSAI_PCI_H

#include <osai/status.h>
#include <osai/types.h>

#define OSAI_PCI_ECAM_BASE UINT64_C(0x4010000000)
#define OSAI_PCI_ECAM_BUS0_SIZE UINT64_C(0x100000)
#define OSAI_PCI_MAX_DEVICES 64U
#define OSAI_PCI_MAX_BARS 6U

/* Standard PCI config space offsets */
#define OSAI_PCI_VENDOR_ID UINT16_C(0x00)
#define OSAI_PCI_DEVICE_ID UINT16_C(0x02)
#define OSAI_PCI_COMMAND UINT16_C(0x04)
#define OSAI_PCI_STATUS UINT16_C(0x06)
#define OSAI_PCI_CLASS_REV UINT16_C(0x08)
#define OSAI_PCI_HEADER_TYPE UINT16_C(0x0C)
#define OSAI_PCI_BAR0 UINT16_C(0x10)
#define OSAI_PCI_INTERRUPT_LINE UINT16_C(0x3C)
#define OSAI_PCI_INTERRUPT_PIN UINT16_C(0x3D)
#define OSAI_PCI_CAP_PTR UINT16_C(0x34)

/* Vendor IDs */
#define OSAI_PCI_VENDOR_VIRTIO UINT16_C(0x1AF4)
#define OSAI_PCI_VENDOR_REDHAT UINT16_C(0x1B36)
#define OSAI_PCI_VENDOR_INVALID UINT16_C(0xFFFF)

/* Class codes */
#define OSAI_PCI_CLASS_BRIDGE UINT8_C(0x06)
#define OSAI_PCI_CLASS_NETWORK UINT8_C(0x02)
#define OSAI_PCI_CLASS_STORAGE UINT8_C(0x01)

typedef struct osai_pci_device {
  uint8_t bus;
  uint8_t device;
  uint8_t function;
  uint16_t vendor_id;
  uint16_t device_id;
  uint8_t class_code;
  uint8_t subclass;
  uint8_t prog_if;
  uint8_t header_type;
  uint32_t bars[OSAI_PCI_MAX_BARS];
  uint8_t interrupt_line;
  uint8_t interrupt_pin;
  uint32_t is_pcie;
  uint32_t is_virtio;
} osai_pci_device_t;

void pci_init(void);
uint32_t pci_ecam_mapped(void);
uint32_t pci_device_count(void);
const osai_pci_device_t *pci_device(uint32_t index);
uint32_t pci_virtio_count(void);
uint32_t pci_network_count(void);
uint32_t pci_bridge_count(void);
uint32_t pci_find_device(uint16_t vendor_id, uint16_t device_id);
void pci_self_test(void);

#endif
