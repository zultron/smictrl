/* -*- c-basic-offset: 4 -*-
 *
 * Copyright (C) 2006, 2010 Jan Kiszka <jan.kiszka@web.de>.
 *
 * -g, -v, -d, -m, -c options by Marty Vona <martyvona@gmail.com>
 *
 * -g option based on https://bugzilla.kernel.org/attachment.cgi?id=23059
 *
 * Based on Xenomai's SMI workaround.
 *
 * If there is an accompanying Makefile, just run "make".  Otherwise the
 * suggested build commands on a Debian-like system are:
 *
 * sudo apt-get install libpci-dev
 * KSRC=/lib/modules/`uname -r`/source
 * cc -O2 -Wall -I $KSRC/include -lz -lpci smictrl.c -o smictrl
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

/* Intel chipset LPC (Low Pin Count) bus controller: PCI device=31 function=0 */
#define LPC_DEV             31
#define LPC_FUNC            0

#define PMBASE_B0           0x40
#define PMBASE_B1           0x41

#define SMI_CTRL_ADDR       0x30
#define SMI_STATUS_ADDR     0x34
#define SMI_ALT_GPIO_ADDR   0x38
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

static uint16_t get_smi_en_addr(struct pci_dev *dev, uint8_t gpio)
{
    uint8_t byte0, byte1;

    byte0 = pci_read_byte(dev, PMBASE_B0);
    byte1 = pci_read_byte(dev, PMBASE_B1);

    return ((gpio) ? SMI_ALT_GPIO_ADDR : SMI_CTRL_ADDR) + 
        (((byte1 << 1) | (byte0 >> 7)) << 7); // bits 7-15
}

static void print_bits(FILE *stream, uint32_t *val) {
  
#define PRINT_BIT(f) {                                              \
        fprintf(stream, "%20s (0x%08x)", #f, f);                    \
        if (val) fprintf(stream, " = %s", ((*val)&f) ? "1" : "0");  \
        fprintf(stream, "\n");                                      \
    }
    
    PRINT_BIT(INTEL_USB2_EN_BIT);
    PRINT_BIT(LEGACY_USB2_EN_BIT);
    PRINT_BIT(PERIODIC_EN_BIT);
    PRINT_BIT(TCO_EN_BIT);
    PRINT_BIT(MCSMI_EN_BIT);
    PRINT_BIT(SWSMI_TMR_EN_BIT);
    PRINT_BIT(APMC_EN_BIT);
    PRINT_BIT(SLP_EN_BIT);
    PRINT_BIT(LEGACY_USB_EN_BIT);
    PRINT_BIT(BIOS_EN_BIT);
    PRINT_BIT(GBL_SMI_EN_BIT);
    
#undef PRINT_BIT
}

int main(int argc, char *argv[])
{
    char vendor_name[128];
    char device_name[128];
    struct pci_access *pacc;
    struct pci_dev *dev;
    uint16_t smi_en_addr;
    uint32_t set_bits = 0, clr_bits = 0, opt_bits = 0;
    uint32_t orig_value, new_value, new_new_value;
    int c, set_value = 0, dry = 0, gpio = 0, verb = 0;
    const char *reg_name = "SMI_EN";
    int reg_width = 8; /* nybbles */

    while ((c = getopt(argc,argv,"hdgvs:m:c:")) != EOF)
        switch (c) {

            case 'd':
                dry = 1;
                break;

            case 'g':
                gpio = 1;
                reg_name = "alt GPIO SMI_EN";
                reg_width = 4;
                break;

            case 'v':
                verb = 1;
                break;

            case 's': case 'm': case 'c':
                set_value = 1;
                opt_bits = strtol(optarg, NULL,
                                  (strncmp(optarg, "0x", 2) == 0) ? 16 : 10);
                if (c == 'm') { set_bits |= opt_bits; clr_bits &= ~opt_bits; }
                if (c == 'c') { clr_bits |= opt_bits; set_bits &= ~opt_bits; }
                else          { set_bits  = opt_bits; clr_bits  = ~opt_bits; }
                break;

            case 'h': default:
                fprintf(stderr, "usage: smictrl [-h] [-d] [-s <bits>] "
                                               "[-m <bits>] [-c <bits>]\n");
                fprintf(stderr, "  <bits> are in decimal or 0xHEX\n");
                fprintf(stderr, "  -s sets all bits\n");
                fprintf(stderr, "  -m marks (sets) individual bits\n");
                fprintf(stderr, "  -c clears individual bits\n");
                fprintf(stderr, "  -g operate on alternate GPIO SMI_EN\n");
                fprintf(stderr, "  -v show individual bits\n");
                fprintf(stderr, "  -d dry run\n");
                fprintf(stderr, "  multiple options are combined in order\n");
                fprintf(stderr, "  common SMI_EN register bits "
                                "(not for alternate GPIO):\n");
                print_bits(stderr, 0);
                exit(2);
        }

    printf(" attemting to read %s - run with -h for help\n", reg_name);

    if (iopl(3) < 0) {
        printf(" root permissions required\n");
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

        printf(" SMI-enabled chipset found:\n %s %s (%04x:%04x)\n",
               vendor_name, device_name, dev->vendor_id, dev->device_id);

        smi_en_addr = get_smi_en_addr(dev, gpio);

        if (!gpio) orig_value = inl(smi_en_addr);
        else orig_value = inw(smi_en_addr);

        printf(" %s register current value:\t0x%0*x\n", 
               reg_name, reg_width, orig_value);

        if (!gpio && verb) print_bits(stdout, &orig_value);

        if (set_value) {

            new_value = orig_value;
            new_value |= set_bits;
            new_value &= ~clr_bits;

            printf(" %s set %s to value:\t0x%0*x\n",
                   (dry) ? "(dry run) would" : "attempting to",
                   reg_name, reg_width, new_value);

            if (!gpio && verb) print_bits(stdout, &new_value);

            if (!dry) {
                if (!gpio) outl(new_value, smi_en_addr);
                else outw(new_value, smi_en_addr);

                if (!gpio) new_new_value = inl(smi_en_addr);
                else new_new_value = inw(smi_en_addr);

                printf(" %s register new value:\t0x%0*x\n", 
                       reg_name, reg_width, new_new_value);

                if (!gpio && verb) print_bits(stdout, &new_new_value);
            }

        } else {
            printf(" %s register unchanged\n", reg_name);
        }

        goto out;
    }

    printf("No SMI-enabled chipset found\n");

 out:
    pci_cleanup(pacc);
    return 0;
}
