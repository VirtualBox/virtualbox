/* $Id: DevEFI.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * EFI for VirtualBox Common Definitions.
 *
 * WARNING: This header is used by both firmware and VBox device,
 * thus don't put anything here but numeric constants or helper
 * inline functions.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef VBOX_INCLUDED_SRC_EFI_DevEFI_h
#define VBOX_INCLUDED_SRC_EFI_DevEFI_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/** @defgroup grp_devefi    DevEFI <-> Firmware Interfaces
 * @{
 */

/** The base of the I/O ports used for interaction between the EFI firmware and DevEFI. */
#define EFI_PORT_BASE           0xEF10  /**< @todo r=klaus stupid choice which causes trouble with PCI resource allocation in complex bridge setups, change to 0x0400 with appropriate saved state and reset handling */
/** The number of ports. */
#define EFI_PORT_COUNT          0x0008


/** Information querying.
 * 32-bit write sets the info index and resets the reading, see EfiInfoIndex.
 * 32-bit read returns the size of the info (in bytes).
 * 8-bit reads returns the info as a byte sequence. */
#define EFI_INFO_PORT           (EFI_PORT_BASE+0x0)
/**
 * Information requests.
 */
typedef enum
{
    EFI_INFO_INDEX_INVALID = 0,
    EFI_INFO_INDEX_VOLUME_BASE,
    EFI_INFO_INDEX_VOLUME_SIZE,
    EFI_INFO_INDEX_TEMPMEM_BASE,
    EFI_INFO_INDEX_TEMPMEM_SIZE,
    EFI_INFO_INDEX_STACK_BASE,
    EFI_INFO_INDEX_STACK_SIZE,
    EFI_INFO_INDEX_BOOT_ARGS,
    EFI_INFO_INDEX_DEVICE_PROPS,
    EFI_INFO_INDEX_FSB_FREQUENCY,
    EFI_INFO_INDEX_CPU_FREQUENCY,
    EFI_INFO_INDEX_TSC_FREQUENCY,
    EFI_INFO_INDEX_GRAPHICS_MODE,
    EFI_INFO_INDEX_HORIZONTAL_RESOLUTION,
    EFI_INFO_INDEX_VERTICAL_RESOLUTION,
    EFI_INFO_INDEX_MCFG_BASE,
    EFI_INFO_INDEX_MCFG_SIZE,
    EFI_INFO_INDEX_APIC_MODE,
    EFI_INFO_INDEX_CPU_COUNT_CURRENT,
    EFI_INFO_INDEX_CPU_COUNT_MAX,
    EFI_INFO_INDEX_TPM_PPI_BASE,
    EFI_INFO_INDEX_END
} EfiInfoIndex;

/** @name APIC mode defines as returned by EFI_INFO_INDEX_APIC_MODE
 * @{ */
#define EFI_APIC_MODE_DISABLED          0
#define EFI_APIC_MODE_APIC              1
#define EFI_APIC_MODE_X2APIC            2
/** @} */

/** Panic port.
 * Write causes action to be taken according to the value written,
 * see the EFI_PANIC_CMD_* defines below.
 * Reading from the port has no effect. */
#define EFI_PANIC_PORT          (EFI_PORT_BASE+0x1)

/** @defgroup grp_devefi_panic_cmd  Panic Commands for EFI_PANIC_PORT
 * @{ */
/** Used by the EfiThunk.asm to signal ORG inconsistency. */
#define EFI_PANIC_CMD_BAD_ORG           1
/** Used by the EfiThunk.asm to signal unexpected trap or interrupt. */
#define EFI_PANIC_CMD_THUNK_TRAP        2
/** Starts a panic message.
 * Makes sure the panic message buffer is empty. */
#define EFI_PANIC_CMD_START_MSG         3
/** Ends a panic message and enters guru meditation state. */
#define EFI_PANIC_CMD_END_MSG           4
/** The first panic message command.
 * The low byte of the command is the char to be added to the panic message. */
#define EFI_PANIC_CMD_MSG_FIRST         0x4201
/** The last panic message command. */
#define EFI_PANIC_CMD_MSG_LAST          0x427f
/** Makes a panic message command from a char. */
#define EFI_PANIC_CMD_MSG_FROM_CHAR(ch) (0x4200 | ((ch) & 0x7f) )
/** Extracts the char from a panic message command. */
#define EFI_PANIC_CMD_MSG_GET_CHAR(u32) ((u32) & 0x7f)
/** @} */

/** EFI event signalling.
 * A 16-bit event identifier is written first.
 * It might follow a number of writes to convey additional data,
 * the size depends on the event identifier.
 */
#define EFI_PORT_EVENT          (EFI_PORT_BASE+0x2)

/**
 * Events.
 */
typedef enum EFI_EVENT_TYPE
{
    /** Invalid event id. */
    EFI_EVENT_TYPE_INVALID = 0,
    /** Booting any guest OS failed. */
    EFI_EVENT_TYPE_BOOT_FAILED,
    /** 32bit blow up hack. */
    EFI_EVENT_TYPE_16_BIT_HACK = 0x7fff
} EFI_EVENT_TYPE;


/** Debug logging.
 * The chars written goes to the log.
 * Reading has no effect.
 * @remarks The port number is the same as on of those used by the PC BIOS. */
#define EFI_DEBUG_PORT          (EFI_PORT_BASE+0x3)

#define VBOX_EFI_DEBUG_BUFFER   512
/** The top of the EFI stack.
 * The firmware expects a 128KB stack.
 * @todo Move this to 1MB + 128KB and drop the stack relocation the firmware
 *       does. It expects the stack to be within the temporary memory that
 *       SEC hands to PEI and the VBoxAutoScan PEIM reports. */
#define VBOX_EFI_TOP_OF_STACK   0x300000

#define EFI_PORT_VARIABLE_OP    (EFI_PORT_BASE+0x4)
#define EFI_PORT_VARIABLE_PARAM (EFI_PORT_BASE+0x5)

#define EFI_VARIABLE_OP_QUERY        0xdead0001
#define EFI_VARIABLE_OP_QUERY_NEXT   0xdead0002
#define EFI_VARIABLE_OP_QUERY_REWIND 0xdead0003
#define EFI_VARIABLE_OP_ADD          0xdead0010

#define EFI_VARIABLE_OP_STATUS_OK         0xcafe0000
#define EFI_VARIABLE_OP_STATUS_ERROR      0xcafe0001
#define EFI_VARIABLE_OP_STATUS_NOT_FOUND  0xcafe0002
#define EFI_VARIABLE_OP_STATUS_WP         0xcafe0003
#define EFI_VARIABLE_OP_STATUS_BSY        0xcafe0010

/** The max number of variables allowed. */
#define EFI_VARIABLE_MAX            128
/** The max variable name length (in bytes, including the zero terminator). */
#define EFI_VARIABLE_NAME_MAX       1024
/** The max value length (in bytes). */
#define EFI_VARIABLE_VALUE_MAX      1024

typedef enum
{
    EFI_VM_VARIABLE_OP_START = 0,
    EFI_VM_VARIABLE_OP_RESERVED_USED_TO_BE_END,
    EFI_VM_VARIABLE_OP_RESERVED_USED_TO_BE_INDEX,
    EFI_VM_VARIABLE_OP_GUID,
    EFI_VM_VARIABLE_OP_ATTRIBUTE,
    EFI_VM_VARIABLE_OP_NAME,
    EFI_VM_VARIABLE_OP_NAME_LENGTH,
    EFI_VM_VARIABLE_OP_VALUE,
    EFI_VM_VARIABLE_OP_VALUE_LENGTH,
    EFI_VM_VARIABLE_OP_ERROR,
    EFI_VM_VARIABLE_OP_NAME_UTF16,
    EFI_VM_VARIABLE_OP_NAME_LENGTH_UTF16,
    EFI_VM_VARIABLE_OP_MAX,
    EFI_VM_VARIABLE_OP_32BIT_HACK = 0x7fffffff
} EFIVAROP;


/** Debug point. */
#define EFI_PORT_DEBUG_POINT    (EFI_PORT_BASE + 0x6)

/**
 * EFI debug points.
 */
typedef enum EFIDBGPOINT
{
    /** Invalid. */
    EFIDBGPOINT_INVALID = 0,
    /** DEBUG_AGENT_INIT_PREMEM_SEC. */
    EFIDBGPOINT_SEC_PREMEM = 1,
    /** DEBUG_AGENT_INIT_POST_SEC. */
    EFIDBGPOINT_SEC_POSTMEM,
    /** DEBUG_AGENT_INIT_DXE_CORE. */
    EFIDBGPOINT_DXE_CORE,
    /** DEBUG_AGENT_INIT_. */
    EFIDBGPOINT_SMM,
    /** DEBUG_AGENT_INIT_ENTER_SMI. */
    EFIDBGPOINT_SMI_ENTER,
    /** DEBUG_AGENT_INIT_EXIT_SMI. */
    EFIDBGPOINT_SMI_EXIT,
    /** DEBUG_AGENT_INIT_S3. */
    EFIDBGPOINT_GRAPHICS,
    /** DEBUG_AGENT_INIT_DXE_AP. */
    EFIDBGPOINT_DXE_AP,
    /** End of valid points. */
    EFIDBGPOINT_END,
    /** Blow up the type to 32-bits. */
    EFIDBGPOINT_32BIT_HACK = 0x7fffffff
} EFIDBGPOINT;


/** EFI image load or unload event. All writes are 32-bit writes. */
#define EFI_PORT_IMAGE_EVENT    (EFI_PORT_BASE + 0x7)

/** @defgroup grp_devefi_image_evt  EFI Image Events (EFI_PORT_IMAGE_EVENT).
 *
 * The lower 8-bit of the values written to EFI_PORT_IMAGE_EVENT can be seen as
 * the command.  The start and complete commands does not have any additional
 * payload.  The other commands uses bit 8 thru 23 or 8 thru 15 to pass a value.
 *
 * @{ */

/** The command mask. */
#define EFI_IMAGE_EVT_CMD_MASK                  UINT32_C(0x000000ff)
/** Get the payload value. */
#define EFI_IMAGE_EVT_GET_PAYLOAD(a_u32)        ((a_u32) >> 8)
/** Get the payload value as unsigned 16-bit. */
#define EFI_IMAGE_EVT_GET_PAYLOAD_U16(a_u32)    ( EFI_IMAGE_EVT_GET_PAYLOAD(a_u32) & UINT16_MAX )
/** Get the payload value as unsigned 8-bit. */
#define EFI_IMAGE_EVT_GET_PAYLOAD_U8(a_u32)     ( EFI_IMAGE_EVT_GET_PAYLOAD(a_u32) &  UINT8_MAX )
/** Combines a command and a payload value. */
#define EFI_IMAGE_EVT_MAKE(a_uCmd, a_uPayload)  ( ((a_uCmd) & UINT32_C(0xff)) | (uint32_t)((a_uPayload) << 8) )

/** Invalid. */
#define EFI_IMAGE_EVT_CMD_INVALID               UINT32_C(0x00000000)
/** The event is complete. */
#define EFI_IMAGE_EVT_CMD_COMPLETE              UINT32_C(0x00000001)
/** Starts a 32-bit load event.  Requires name and address, size is optional. */
#define EFI_IMAGE_EVT_CMD_START_LOAD32          UINT32_C(0x00000002)
/** Starts a 64-bit load event.  Requires name and address, size is optional. */
#define EFI_IMAGE_EVT_CMD_START_LOAD64          UINT32_C(0x00000003)
/** Starts a 32-bit unload event. Requires name and address. */
#define EFI_IMAGE_EVT_CMD_START_UNLOAD32        UINT32_C(0x00000004)
/** Starts a 64-bit unload event. Requires name and address. */
#define EFI_IMAGE_EVT_CMD_START_UNLOAD64        UINT32_C(0x00000005)
/** Starts a 32-bit relocation event. RRequires new and old base address. */
#define EFI_IMAGE_EVT_CMD_START_RELOC32         UINT32_C(0x0000000A)
/** Starts a 64-bit relocation event. Requires new and old base address. */
#define EFI_IMAGE_EVT_CMD_START_RELOC64         UINT32_C(0x0000000B)

/** The command for writing to the second address register (64-bit).
 * Takes a 16-bit payload value.  The register value is shifted 16-bits
 * to the left and then the payload is ORed in. */
#define EFI_IMAGE_EVT_CMD_ADDR0                 UINT32_C(0x00000006)
/** The command for writing to the second address register (64-bit).
 * Takes a 16-bit payload value.  The register value is shifted 16-bits
 * to the left and then the payload is ORed in. */
#define EFI_IMAGE_EVT_CMD_ADDR1                 UINT32_C(0x00000007)
/** The command for writing to the first size register (64-bit).
 * Takes a 16-bit payload value.  The register value is shifted 16-bits
 * to the left and then the payload is ORed in. */
#define EFI_IMAGE_EVT_CMD_SIZE0                 UINT32_C(0x00000008)
/** The command for appending a character to the module name.
 * Takes a 7-bit payload value that.  The value is appended to the field if
 * there is room. */
#define EFI_IMAGE_EVT_CMD_NAME                  UINT32_C(0x00000009)

/** @} */


/** @} */

#endif /* !VBOX_INCLUDED_SRC_EFI_DevEFI_h */
