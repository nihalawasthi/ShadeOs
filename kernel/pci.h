#ifndef PCI_H
#define PCI_H

#include "kernel.h"

typedef struct pci_device pci_device_t;

void pci_init(void);
pci_device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id);
pci_device_t *pci_find_class(uint8_t class_code, uint8_t subclass);
void pci_test_devices(void);

#endif // PCI_H
