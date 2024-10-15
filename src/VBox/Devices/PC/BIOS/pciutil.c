/* $Id: pciutil.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Utility routines for calling the PCI BIOS.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <stdint.h>
#include <string.h>
#include "biosint.h"
#include "inlines.h"

/** PCI BIOS functions. */
#define PCIBIOS_ID                      0xb1
#define PCIBIOS_PCI_BIOS_PRESENT        0x01
#define PCIBIOS_FIND_PCI_DEVICE         0x02
#define PCIBIOS_FIND_CLASS_CODE         0x03
#define PCIBIOS_GENERATE_SPECIAL_CYCLE  0x06
#define PCIBIOS_READ_CONFIG_BYTE        0x08
#define PCIBIOS_READ_CONFIG_WORD        0x09
#define PCIBIOS_READ_CONFIG_DWORD       0x0a
#define PCIBIOS_WRITE_CONFIG_BYTE       0x0b
#define PCIBIOS_WRITE_CONFIG_WORD       0x0c
#define PCIBIOS_WRITE_CONFIG_DWORD      0x0d
#define PCIBIOS_GET_IRQ_ROUTING_OPTIONS 0x0e
#define PCIBIOS_SET_PCI_IRQ             0x0f

/** Status codes. */
#define SUCCESSFUL                      0x00
#define FUNC_NOT_SUPPORTED              0x81
#define BAD_VENDOR_ID                   0x83
#define DEVICE_NOT_FOUND                0x86
#define BAD_REGISTER_NUMBER             0x87
#define SET_FAILED                      0x88
#define BUFFER_TOO_SMALL                0x89


#if VBOX_BIOS_CPU >= 80386
/* Warning: Destroys high bits of ECX. */
uint16_t pci_find_class(uint16_t op, uint32_t dev_class, uint16_t index);
# pragma aux pci_find_class =    \
    ".386"                  \
    "shl    ecx, 16"        \
    "mov    cx, dx"         \
    "int    0x1a"           \
    "cmp    ah, 0"          \
    "je     found"          \
    "mov    bx, 0xffff"     \
    "found:"                \
    parm [ax] [cx dx] [si] value [bx];
#endif

uint16_t pci_find_dev(uint16_t op, uint16_t dev_id, uint16_t ven_id, uint16_t index);
#pragma aux pci_find_dev =  \
    "int    0x1a"           \
    "cmp    ah, 0"          \
    "je     found"          \
    "mov    bx, 0xffff"     \
    "found:"                \
    parm [ax] [cx] [dx] [si] value [bx];

uint8_t pci_read_cfgb(uint16_t op, uint16_t bus_dev_fn, uint16_t reg);
#pragma aux pci_read_cfgb = \
    "int    0x1a"           \
    parm [ax] [bx] [di] value [cl];

uint16_t pci_read_cfgw(uint16_t op, uint16_t bus_dev_fn, uint16_t reg);
#pragma aux pci_read_cfgw = \
    "int    0x1a"           \
    parm [ax] [bx] [di] value [cx];

#if VBOX_BIOS_CPU >= 80386
/* Warning: Destroys high bits of ECX. */
uint32_t pci_read_cfgd(uint16_t op, uint16_t bus_dev_fn, uint16_t reg);
# pragma aux pci_read_cfgd = \
    ".386"                  \
    "int    0x1a"           \
    "mov    ax, cx"         \
    "shr    ecx, 16"        \
    parm [ax] [bx] [di] value [cx ax];
#endif

uint8_t pci_write_cfgb(uint16_t op, uint16_t bus_dev_fn, uint16_t reg, uint8_t val);
#pragma aux pci_write_cfgb = \
    "int    0x1a"           \
    parm [ax] [bx] [di] [cl];

uint8_t pci_write_cfgw(uint16_t op, uint16_t bus_dev_fn, uint16_t reg, uint16_t val);
#pragma aux pci_write_cfgw = \
    "int    0x1a"           \
    parm [ax] [bx] [di] [cx];

#if VBOX_BIOS_CPU >= 80386
/* Warning: Destroys high bits of ECX. */
uint8_t pci_write_cfgd(uint16_t op, uint16_t bus_dev_fn, uint16_t reg, uint32_t val);
# pragma aux pci_write_cfgd = \
    ".386"                  \
    "shl    ecx, 16"        \
    "mov    cx, dx"         \
    "int    0x1a"           \
    parm [ax] [bx] [di] [cx dx];
#endif


/**
 * Returns the bus/device/function of a PCI device with
 * the given class code.
 *
 * @returns bus/device/fn in a 16-bit integer where
 *          where the upper byte contains the bus number
 *          and lower one the device and function number.
 *          0xffff if no device was found.
 * @param   dev_class   The PCI class code to search for.
 */
uint16_t pci_find_classcode(uint32_t dev_class)
{
#if VBOX_BIOS_CPU >= 80386
    return pci_find_class((PCIBIOS_ID << 8) | PCIBIOS_FIND_CLASS_CODE, dev_class, 0);
#else
    return UINT16_C(0xffff);
#endif
}

/**
 * Returns the bus/device/function of a PCI device with
 * the given base and sub-class code, ignoring the programming interface
 * code.
 *
 * @returns bus/device/fn in a 16-bit integer where
 *          where the upper byte contains the bus number
 *          and lower one the device and function number.
 *          0xffff if no device was found.
 * @param   dev_class   The PCI class code to search for.
 */
uint16_t pci_find_class_noif(uint16_t dev_class)
{
#if VBOX_BIOS_CPU >= 80386
    /* Internal call, not an interrupt service! */
    return pci16_find_device(dev_class, 0 /*index*/, 1 /*search class*/, 1 /*ignore prog if*/);
#else
    return UINT16_C(0xffff);
#endif
}

/**
 * Returns the bus/device/function of a PCI device with
 * the given vendor and device id.
 *
 * @returns bus/device/fn in one 16bit integer where
 *          where the upper byte contains the bus number
 *          and lower one the device and function number.
 *          0xffff if no device was found.
 * @param   v_id    The vendor ID.
 * @param   d_id    The device ID.
 */
uint16_t pci_find_device(uint16_t v_id, uint16_t d_id)
{
    return pci_find_dev((PCIBIOS_ID << 8) | PCIBIOS_FIND_PCI_DEVICE, d_id, v_id, 0);
}

uint32_t pci_read_config_byte(uint8_t bus, uint8_t dev_fn, uint8_t reg)
{
    return pci_read_cfgb((PCIBIOS_ID << 8) | PCIBIOS_READ_CONFIG_BYTE, (bus << 8) | dev_fn, reg);
}

uint32_t pci_read_config_word(uint8_t bus, uint8_t dev_fn, uint8_t reg)
{
    return pci_read_cfgw((PCIBIOS_ID << 8) | PCIBIOS_READ_CONFIG_WORD, (bus << 8) | dev_fn, reg);
}

uint32_t pci_read_config_dword(uint8_t bus, uint8_t dev_fn, uint8_t reg)
{
#if VBOX_BIOS_CPU >= 80386
    return pci_read_cfgd((PCIBIOS_ID << 8) | PCIBIOS_READ_CONFIG_DWORD, (bus << 8) | dev_fn, reg);
#else
    return pci_read_cfgw((PCIBIOS_ID << 8) | PCIBIOS_READ_CONFIG_WORD, (bus << 8) | dev_fn, reg)
        || ((uint32_t)pci_read_cfgw((PCIBIOS_ID << 8) | PCIBIOS_READ_CONFIG_WORD, (bus << 8) | dev_fn, reg + 2) << 16);
#endif
}

void pci_write_config_word(uint8_t bus, uint8_t dev_fn, uint8_t reg, uint16_t val)
{
    pci_write_cfgw((PCIBIOS_ID << 8) | PCIBIOS_WRITE_CONFIG_WORD, (bus << 8) | dev_fn, reg, val);
}

void pci_write_config_byte(uint8_t bus, uint8_t dev_fn, uint8_t reg, uint8_t val)
{
    pci_write_cfgb((PCIBIOS_ID << 8) | PCIBIOS_WRITE_CONFIG_BYTE, (bus << 8) | dev_fn, reg, val);
}

void pci_write_config_dword(uint8_t bus, uint8_t dev_fn, uint8_t reg, uint32_t val)
{
#if VBOX_BIOS_CPU >= 80386
    pci_write_cfgd((PCIBIOS_ID << 8) | PCIBIOS_WRITE_CONFIG_DWORD, (bus << 8) | dev_fn, reg, val);
#else
    pci_write_cfgw((PCIBIOS_ID << 8) | PCIBIOS_WRITE_CONFIG_WORD, (bus << 8) | dev_fn, reg, val & 0xffff);
    pci_write_cfgw((PCIBIOS_ID << 8) | PCIBIOS_WRITE_CONFIG_WORD, (bus << 8) | dev_fn, reg + 2, val >> 16);
#endif
}

