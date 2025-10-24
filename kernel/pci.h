#ifndef PCI_H
#define PCI_H

#include "kernel.h"


typedef struct pci_device {
	uint8_t bus;
	uint8_t slot;
	uint8_t func;
	uint16_t vendor_id;
	uint16_t device_id;
	uint8_t class_code;
	uint8_t subclass;
	uint8_t prog_if;
	uint32_t bar[6];
	uint8_t irq;
	int device_id_registered;
} pci_device_t;

void pci_init(void);
pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id);
pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass);
void pci_test_devices(void);
uint32_t pci_get_bar(pci_device_t *dev, int bar_num);

#endif // PCI_H
