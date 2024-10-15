/* $Id: pcibios.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PCI BIOS support.
 */

/*
 * Copyright (C) 2004-2024 Oracle and/or its affiliates.
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

#if DEBUG_PCI
#  define BX_DEBUG_PCI(...) BX_DEBUG(__VA_ARGS__)
#else
#  define BX_DEBUG_PCI(...)
#endif

/* PCI function codes. */
enum pci_func {
    PCI_BIOS_PRESENT    = 0x01,     /* PCI BIOS presence check. */
    FIND_PCI_DEVICE     = 0x02,     /* Find PCI device by ID. */
    FIND_PCI_CLASS_CODE = 0x03,     /* Find PCI device by class. */
    GEN_SPECIAL_CYCLE   = 0x06,     /* Generate special cycle. */
    READ_CONFIG_BYTE    = 0x08,     /* Read a byte from PCI config space. */
    READ_CONFIG_WORD    = 0x09,     /* Read a word from PCI config space. */
    READ_CONFIG_DWORD   = 0x0A,     /* Read a dword from PCI config space. */
    WRITE_CONFIG_BYTE   = 0x0B,     /* Write a byte to PCI config space. */
    WRITE_CONFIG_WORD   = 0x0C,     /* Write a word to PCI config space. */
    WRITE_CONFIG_DWORD  = 0x0D,     /* Write a dword to PCI config space. */
    GET_IRQ_ROUTING     = 0x0E,     /* Get IRQ routing table. */
    SET_PCI_HW_INT      = 0x0F,     /* Set PCI hardware interrupt. */
};

enum pci_error {
    SUCCESSFUL          = 0x00,     /* Success. */
    FUNC_NOT_SUPPORTED  = 0x81,     /* Unsupported function. */
    BAD_VENDOR_ID       = 0x83,     /* Bad vendor ID (all bits set) passed. */
    DEVICE_NOT_FOUND    = 0x86,     /* No matching device found. */
    BAD_REGISTER_NUMBER = 0x87,     /* Register number out of range. */
    SET_FAILED          = 0x88,     /* Failed to set PCI interrupt. */
    BUFFER_TOO_SMALL    = 0x89      /* Routing table buffer insufficient. */
};

/// @todo merge with system.c
#define AX      r.gr.u.r16.ax
#define BX      r.gr.u.r16.bx
#define CX      r.gr.u.r16.cx
#define DX      r.gr.u.r16.dx
#define SI      r.gr.u.r16.si
#define DI      r.gr.u.r16.di
#define BP      r.gr.u.r16.bp
#define SP      r.gr.u.r16.sp
#define EAX     r.gr.u.r32.eax
#define EBX     r.gr.u.r32.ebx
#define ECX     r.gr.u.r32.ecx
#define EDX     r.gr.u.r32.edx
#define ES      r.es

/* The 16-bit PCI BIOS service must be callable from both real and protected
 * mode. In protected mode, the caller must set the CS selector base to F0000h
 * (but the CS selector value is not specified!). The caller does not always
 * provide a DS which covers the BIOS segment.
 *
 * Unlike APM, there are no provisions for the 32-bit PCI BIOS interface
 * calling the 16-bit implementation.
 *
 * The PCI Firmware Specification requires that the PCI BIOS service is called
 * with at least 1,024 bytes of stack space available, that interrupts are not
 * enabled during execution, and that the routines are re-entrant.
 *
 * Implementation notes:
 * - The PCI BIOS interface already uses certain 32-bit registers even in
 * 16-bit mode. To simplify matters, all 32-bit GPRs are saved/restored and
 * may be used by helper routines (notably for 32-bit port I/O).
 */

#define PCI_CFG_ADDR    0xCF8
#define PCI_CFG_DATA    0xCFC

#ifdef __386__

#define PCIxx(x)    pci32_##x

/* The stack layout is different in 32-bit mode. */
typedef struct {
    pushad_regs_t   gr;
    uint32_t        es;
    uint32_t        flags;
} pci_regs_t;

#define FLAGS   r.flags

/* In 32-bit mode, don't do any output; not technically impossible but needs
 * a lot of extra code.
 */
#undef  BX_INFO
#define BX_INFO(...)
#undef  BX_DEBUG_PCI
#define BX_DEBUG_PCI(...)

#else

#define PCIxx(x)    pci16_##x

typedef struct {
    pushad_regs_t   gr;
    uint16_t        ds;
    uint16_t        es;
    iret_addr_t     ra;
} pci_regs_t;

#define FLAGS   r.ra.flags.u.r16.flags

#endif

#ifdef __386__

/* 32-bit code can just use the compiler intrinsics. */
extern unsigned inpd(unsigned port);
extern unsigned outpd(unsigned port, unsigned value);
#pragma intrinsic(inpd,outpd)

#else

/// @todo merge with AHCI code

/* Warning: Destroys high bits of EAX. */
uint32_t inpd(uint16_t port);
#pragma aux inpd =      \
    ".386"              \
    "in     eax, dx"    \
    "mov    dx, ax"     \
    "shr    eax, 16"    \
    "xchg   ax, dx"     \
    parm [dx] value [dx ax] modify nomemory;

/* Warning: Destroys high bits of EAX. */
void outpd(uint16_t port, uint32_t val);
#pragma aux outpd =     \
    ".386"              \
    "xchg   ax, cx"     \
    "shl    eax, 16"    \
    "mov    ax, cx"     \
    "out    dx, eax"    \
    parm [dx] [cx ax] modify nomemory;

#endif

/* PCI IRQ routing expansion buffer descriptor. */
typedef struct {
    uint16_t        buf_size;
    uint8_t __far   *buf_ptr;
} pci_route_buf;

/* Defined in assembler module .*/
extern char     pci_routing_table[];
extern uint16_t pci_routing_table_size;

/* Write the CONFIG_ADDRESS register to prepare for data access. Requires
 * the register offset to be DWORD aligned (low two bits clear). Warning:
 * destroys high bits of EAX.
 */
void pci16_w_addr(uint16_t bus_dev_fn, uint16_t ofs, uint16_t cfg_addr);
#pragma aux pci16_w_addr =      \
    ".386"                      \
    "movzx  eax, ax"            \
    "shl    eax, 8"             \
    "or     eax, 80000000h"     \
    "mov    al, bl"             \
    "out    dx, eax"            \
    parm [ax] [bx] [dx] modify exact [ax] nomemory;


/* Select a PCI configuration register given its offset and bus/dev/fn.
 * This is largely a wrapper to avoid excessive inlining.
 */
void PCIxx(select_reg)(uint16_t bus_dev_fn, uint16_t ofs)
{
    pci16_w_addr(bus_dev_fn, ofs & ~3, PCI_CFG_ADDR);
}

/* Selected configuration space offsets. */
#define PCI_VEN_ID          0x00
#define PCI_DEV_ID          0x02
#define PCI_REV_ID          0x08
#define PCI_CLASS_CODE      0x09
#define PCI_HEADER_TYPE     0x0E
#define PCI_BRIDGE_SUBORD   0x1A

/* To avoid problems with 16-bit code, we reserve the last possible
 * bus/dev/fn combination (65,535). Upon reaching this location, the
 * probing will end.
 */
#define BUSDEVFN_NOT_FOUND  0xFFFF

/* In the search algorithm, we decrement the device index every time
 * a matching device is found. If the requested device is indeed found,
 * the index will have decremented down to -1/0xFFFF.
 */
#define INDEX_DEV_FOUND     0xFFFF

/* Find a specified PCI device, either by vendor+device ID or class.
 * If index is non-zero, the n-th device will be located. When searching
 * by class, the ignore_if flag only compares the base and sub-class code,
 * ignoring the programming interface code.
 *
 * Note: This function is somewhat performance critical; since it may
 * generate a high number of port I/O accesses, it can take a significant
 * amount of time in cases where the caller is looking for a number of
 * non-present devices.
 */
uint16_t PCIxx(find_device)(uint32_t search_item, uint16_t index, int search_class, int ignore_if)
{
    uint32_t    data;
    uint16_t    bus_dev_fn;
    uint8_t     max_bus;
    uint8_t     hdr_type;
    uint8_t     subordinate;
    int         step;
    int         found;

    if (search_class) {
        BX_DEBUG_PCI("PCI: Find class %08lX index %u\n",
                     search_item, index);
    } else
        BX_DEBUG_PCI("PCI: Find device %04X:%04X index %u\n",
                     (uint16_t)search_item, (uint16_t)(search_item >> 16), index);

    bus_dev_fn = 0;     /* Start at the beginning. */
    max_bus    = 0;     /* Initially assume primary bus only. */

    do {
        /* For the first function of a device, read the device's header type.
         * If the header type has all bits set, there's no device. A PCI
         * multi-function device must implement function 0 and the header type
         * will be something other than 0xFF. If the header type has the high
         * bit clear, there is a device but it's not multi-function, so we can
         * skip probing the next 7 sub-functions.
         */
        if ((bus_dev_fn & 7) == 0) {
            PCIxx(select_reg)(bus_dev_fn, PCI_HEADER_TYPE);
            hdr_type = inp(PCI_CFG_DATA + (PCI_HEADER_TYPE & 3));
            if (hdr_type == 0xFF) {
                bus_dev_fn += 8;    /* Skip to next device. */
                continue;
            }
            if (hdr_type & 0x80)
                step = 1;   /* MFD - try every sub-function. */
            else
                step = 8;   /* No MFD, go to next device after probing. */
        }

        /* If the header type indicates a bus, we're interested. The secondary
         * and subordinate bus numbers will indicate which buses are present;
         * thus we can determine the highest bus number. In the common case,
         * there will be only the primary bus (i.e. bus 0) and we can avoid
         * looking at the remaining 255 theoretically present buses. This check
         * only needs to be done on the primary bus, since bridges must report
         * all bridges potentially behind them.
         */
        if ((hdr_type & 7) == 1 && (bus_dev_fn >> 8) == 0) {
            /* Read the subordinate (last) bridge number. */
            PCIxx(select_reg)(bus_dev_fn, PCI_BRIDGE_SUBORD);
            subordinate = inp(PCI_CFG_DATA + (PCI_BRIDGE_SUBORD & 3));
            if (subordinate > max_bus)
                max_bus = subordinate;
        }

        /* Select the appropriate register. */
        PCIxx(select_reg)(bus_dev_fn, search_class ? PCI_REV_ID : PCI_VEN_ID);
        data  = inpd(PCI_CFG_DATA);
        found = 0;

        /* Only 3 or even just 2 bytes are compared for class searches. */
        if (search_class)
            if (ignore_if)
                data >>= 16;
            else
                data >>= 8;

#if 0
        BX_DEBUG_PCI("PCI: Data is %08lX @ %02X:%%02X:%01X\n", data,
                     bus_dev_fn >> 8, bus_dev_fn >> 3 & 31, bus_dev_fn & 7);
#endif

        if (data == search_item)
            found = 1;

        /* If device was found but index is non-zero, decrement index and
         * continue looking. If requested device was found, index will be -1!
         */
        if (found && !index--)
            break;

        bus_dev_fn += step;
    } while ((bus_dev_fn >> 8) <= max_bus);

    if (index == INDEX_DEV_FOUND)
        BX_DEBUG_PCI("PCI: Device found (%02X:%%02X:%01X)\n", bus_dev_fn >> 8,
                     bus_dev_fn >> 3 & 31, bus_dev_fn & 7);

    return index == INDEX_DEV_FOUND ? bus_dev_fn : BUSDEVFN_NOT_FOUND;
}

void BIOSCALL PCIxx(function)(volatile pci_regs_t r)
{
    pci_route_buf __far     *route_buf;
    uint16_t                device;

    BX_DEBUG_PCI("PCI: AX=%04X BX=%04X CX=%04X DI=%04X\n", AX, BX, CX, DI);

    SET_AH(SUCCESSFUL);     /* Assume success. */
    CLEAR_CF();

    switch (GET_AL()) {
    case PCI_BIOS_PRESENT:
        AX  = 0x0001;   /* Configuration mechanism #1 supported. */
        BX  = 0x0210;   /* Version 2.1. */
        /// @todo return true max bus # in CL
        CX  = 0;        /* Maximum bus number. */
        EDX = 'P' | ('C' << 8) | ((uint32_t)'I' << 16) | ((uint32_t)' ' << 24);
        break;
    case FIND_PCI_DEVICE:
        /* Vendor ID FFFFh is reserved so that non-present devices can
         * be easily detected.
         */
        if (DX == 0xFFFF) {
            SET_AH(BAD_VENDOR_ID);
            SET_CF();
        } else {
            device = PCIxx(find_device)(DX | (uint32_t)CX << 16, SI, 0, 0);
            if (device == BUSDEVFN_NOT_FOUND) {
                SET_AH(DEVICE_NOT_FOUND);
                SET_CF();
            } else {
                BX = device;
            }
        }
        break;
    case FIND_PCI_CLASS_CODE:
        device = PCIxx(find_device)(ECX, SI, 1, 0);
        if (device == BUSDEVFN_NOT_FOUND) {
            SET_AH(DEVICE_NOT_FOUND);
            SET_CF();
        } else {
            BX = device;
        }
        break;
    case READ_CONFIG_BYTE:
    case READ_CONFIG_WORD:
    case READ_CONFIG_DWORD:
    case WRITE_CONFIG_BYTE:
    case WRITE_CONFIG_WORD:
    case WRITE_CONFIG_DWORD:
        if (DI >= 256) {
            SET_AH(BAD_REGISTER_NUMBER);
            SET_CF();
        } else {
            PCIxx(select_reg)(BX, DI);
            switch (GET_AL()) {
            case READ_CONFIG_BYTE:
                SET_CL(inp(PCI_CFG_DATA + (DI & 3)));
                break;
            case READ_CONFIG_WORD:
                CX = inpw(PCI_CFG_DATA + (DI & 2));
                break;
            case READ_CONFIG_DWORD:
                ECX = inpd(PCI_CFG_DATA);
                break;
            case WRITE_CONFIG_BYTE:
                outp(PCI_CFG_DATA + (DI & 3), GET_CL());
                break;
            case WRITE_CONFIG_WORD:
                outpw(PCI_CFG_DATA + (DI & 2), CX);
                break;
            case WRITE_CONFIG_DWORD:
                outpd(PCI_CFG_DATA, ECX);
                break;
            }
        }
        break;
    case GET_IRQ_ROUTING:
        route_buf = ES :> (void *)DI;
        BX_DEBUG_PCI("PCI: Route Buf %04X:%04X size %04X, need %04X (at %04X:%04X)\n",
                     FP_SEG(route_buf->buf_ptr), FP_OFF(route_buf->buf_ptr),
                     route_buf->buf_size, pci_routing_table_size, ES, DI);
        if (pci_routing_table_size > route_buf->buf_size) {
            SET_AH(BUFFER_TOO_SMALL);
            SET_CF();
        } else {
            rep_movsb(route_buf->buf_ptr, pci_routing_table, pci_routing_table_size);
            /* IRQs 9 and 11 are PCI only. */
            BX = (1 << 9) | (1 << 11);
        }
        route_buf->buf_size = pci_routing_table_size;
        break;
    default:
        BX_INFO("PCI: Unsupported function AX=%04X BX=%04X called\n", AX, BX);
        SET_AH(FUNC_NOT_SUPPORTED);
        SET_CF();
    }
}
