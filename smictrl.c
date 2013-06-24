/*
 * Copyright (C) 2006, 2010 Jan Kiszka <jan.kiszka@web.de>.
 *
 * Based on Xenomai's SMI workaround.
 *
 * smictrl is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/io.h>

#include <pci/pci.h>

#define LPC_DEV             31
#define LPC_FUNC            0

#define PMBASE_B0           0x40
#define PMBASE_B1           0x41

#define SMI_CTRL_ADDR       0x30
#define SMI_STATUS_ADDR     0x34
#define SMI_MON_ADDR        0x40

/* SMI_EN register: ICH[0](16 bits), ICH[2-5](32 bits) */
#define INTEL_USB2_EN_BIT   (0x01 << 18) /* ICH4, ... */
#define LEGACY_USB2_EN_BIT  (0x01 << 17) /* ICH4, ... */
#define PERIODIC_EN_BIT     (0x01 << 14) /* called 1MIN_ in ICH0 */
#define TCO_EN_BIT          (0x01 << 13)
#define MCSMI_EN_BIT        (0x01 << 11)
#define SWSMI_TMR_EN_BIT    (0x01 << 6)
#define APMC_EN_BIT         (0x01 << 5)
#define SLP_EN_BIT          (0x01 << 4)
#define LEGACY_USB_EN_BIT   (0x01 << 3)
#define BIOS_EN_BIT         (0x01 << 2)
#define GBL_SMI_EN_BIT      (0x01) /* This is reset by a PCI reset event! */

static uint16_t get_smi_en_addr(struct pci_dev *dev)
{
    uint8_t byte0, byte1;

    byte0 = pci_read_byte(dev, PMBASE_B0);
    byte1 = pci_read_byte(dev, PMBASE_B1);

    return SMI_CTRL_ADDR + (((byte1 << 1) | (byte0 >> 7)) << 7); // bits 7-15
}

int main(int argc, char *argv[])
{
    char vendor_name[128];
    char device_name[128];
    struct pci_access *pacc;
    struct pci_dev *dev;
    uint16_t smi_en_addr;
    uint32_t new_value = 0;
    int c, set_value = 0;

    while ((c = getopt(argc,argv,"s:")) != EOF)
        switch (c) {
            case 's':
                set_value = 1;
                new_value = strtol(optarg, NULL,
                    (strncmp(optarg, "0x", 2) == 0) ? 16 : 10);
                break;

            default:
                fprintf(stderr, "usage: smictrl [-s <value>]\n");
                exit(2);
        }

    if (iopl(3) < 0) {
        printf("root permissions required\n");
        exit(1);
    }

    pacc = pci_alloc();
    pci_init(pacc);
    pci_scan_bus(pacc);

    for (dev = pacc->devices; dev; dev = dev->next) {
        pci_fill_info(dev, PCI_FILL_IDENT | PCI_FILL_BASES);

        if (dev->vendor_id != PCI_VENDOR_ID_INTEL ||
            dev->device_class != PCI_CLASS_BRIDGE_ISA ||
            dev->dev != LPC_DEV || dev->func != LPC_FUNC)
            continue;

        pci_lookup_name(pacc, vendor_name, sizeof(vendor_name),
                        PCI_LOOKUP_VENDOR, dev->vendor_id);
        pci_lookup_name(pacc, device_name, sizeof(device_name),
                        PCI_LOOKUP_DEVICE, dev->vendor_id, dev->device_id);

        printf("SMI-enabled chipset found:\n %s %s (%04x:%04x)\n",
               vendor_name, device_name, dev->vendor_id, dev->device_id);

        smi_en_addr = get_smi_en_addr(dev);

        printf(" SMI_EN register:\t%08x\n", inl(smi_en_addr));

        if (set_value) {
            outl(new_value, smi_en_addr);
            printf(" new value:\t\t%08x\n", inl(smi_en_addr));
        }

        goto out;
    }

    printf("No SMI-enabled chipset found\n");

 out:
    pci_cleanup(pacc);
    return 0;
}
