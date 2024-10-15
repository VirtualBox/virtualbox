/* $Id: lsilogic.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * LsiLogic SCSI host adapter driver to boot from disks.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
#include "ebda.h"
#include "inlines.h"
#include "pciutil.h"
#include "vds.h"
#include "scsi.h"

#define USE_VDS

//#define DEBUG_LSILOGIC 1
#if DEBUG_LSILOGIC
# define DBG_LSILOGIC(...)        BX_INFO(__VA_ARGS__)
#else
# define DBG_LSILOGIC(...)
#endif

#define RT_BIT(bit) (1L << (bit))


#ifdef USE_VDS
/* The disk BIOS won't transfer more than 64K at a time. The extended
 * INT 13h interface is limited to 127 sectors (Phoenix EDD 3.0).
 * We need at most enough for 64K + 1 page for alignment.
 */
#define NUM_SG_BUFFERS      17
#endif


/**
 * A simple SG element for a 32bit address.
 */
typedef struct MptSGEntrySimple32
{
    /** Length of the buffer this entry describes. */
    uint32_t u24Length:          24;
    /** Flag whether this element is the end of the list. */
    uint32_t fEndOfList:          1;
    /** Flag whether the address is 32bit or 64bits wide. */
    uint32_t f64BitAddress:       1;
    /** Flag whether this buffer contains data to be transferred or is the destination. */
    uint32_t fBufferContainsData: 1;
    /** Flag whether this is a local address or a system address. */
    uint32_t fLocalAddress:       1;
    /** Element type. */
    uint32_t u2ElementType:       2;
    /** Flag whether this is the last element of the buffer. */
    uint32_t fEndOfBuffer:        1;
    /** Flag whether this is the last element of the current segment. */
    uint32_t fLastElement:        1;
    /** Lower 32bits of the address of the data buffer. */
    uint32_t u32DataBufferAddressLow: 32;
} MptSGEntrySimple32, *PMptSGEntrySimple32;

/** Defined function codes found in the message header. */
#define MPT_MESSAGE_HDR_FUNCTION_SCSI_IO_REQUEST        (0x00)
#define MPT_MESSAGE_HDR_FUNCTION_IOC_INIT               (0x02)

/**
 * SCSI IO Request
 */
typedef struct MptSCSIIORequest
{
    /** Target ID */
    uint8_t     u8TargetID;
    /** Bus number */
    uint8_t     u8Bus;
    /** Chain offset */
    uint8_t     u8ChainOffset;
    /** Function number. */
    uint8_t     u8Function;
    /** CDB length. */
    uint8_t     u8CDBLength;
    /** Sense buffer length. */
    uint8_t     u8SenseBufferLength;
    /** Reserved */
    uint8_t     u8Reserved;
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context ID. */
    uint32_t    u32MessageContext;
    /** LUN */
    uint8_t     au8LUN[8];
    /** Control values. */
    uint32_t    u32Control;
    /** The CDB. */
    uint8_t     au8CDB[16];
    /** Data length. */
    uint32_t    u32DataLength;
    /** Sense buffer low 32bit address. */
    uint32_t    u32SenseBufferLowAddress;
} MptSCSIIORequest, *PMptSCSIIORequest;

#define MPT_SCSIIO_REQUEST_CONTROL_TXDIR_NONE  (0x0L)
#define MPT_SCSIIO_REQUEST_CONTROL_TXDIR_WRITE (0x1L)
#define MPT_SCSIIO_REQUEST_CONTROL_TXDIR_READ  (0x2L)

/**
 * SCSI IO error reply.
 */
typedef struct MptSCSIIOErrorReply
{
    /** Target ID */
    uint8_t     u8TargetID;
    /** Bus number */
    uint8_t     u8Bus;
    /** Message length. */
    uint8_t     u8MessageLength;
    /** Function number. */
    uint8_t     u8Function;
    /** CDB length */
    uint8_t     u8CDBLength;
    /** Sense buffer length */
    uint8_t     u8SenseBufferLength;
    /** Reserved */
    uint8_t     u8Reserved;
    /** Message flags */
    uint8_t     u8MessageFlags;
    /** Message context ID */
    uint32_t    u32MessageContext;
    /** SCSI status. */
    uint8_t     u8SCSIStatus;
    /** SCSI state */
    uint8_t     u8SCSIState;
    /** IO controller status */
    uint16_t    u16IOCStatus;
    /** IO controller log information */
    uint32_t    u32IOCLogInfo;
    /** Transfer count */
    uint32_t    u32TransferCount;
    /** Sense count */
    uint32_t    u32SenseCount;
    /** Response information */
    uint32_t    u32ResponseInfo;
} MptSCSIIOErrorReply, *PMptSCSIIOErrorReply;

/**
 * IO controller init request.
 */
typedef struct MptIOCInitRequest
{
    /** Which system send this init request. */
    uint8_t     u8WhoInit;
    /** Reserved */
    uint8_t     u8Reserved;
    /** Chain offset in the SG list. */
    uint8_t     u8ChainOffset;
    /** Function to execute. */
    uint8_t     u8Function;
    /** Flags */
    uint8_t     u8Flags;
    /** Maximum number of devices the driver can handle. */
    uint8_t     u8MaxDevices;
    /** Maximum number of buses the driver can handle. */
    uint8_t     u8MaxBuses;
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context ID. */
    uint32_t    u32MessageContext;
    /** Reply frame size. */
    uint16_t    u16ReplyFrameSize;
    /** Reserved */
    uint16_t    u16Reserved;
    /** Upper 32bit part of the 64bit address the message frames are in.
     *  That means all frames must be in the same 4GB segment. */
    uint32_t    u32HostMfaHighAddr;
    /** Upper 32bit of the sense buffer. */
    uint32_t    u32SenseBufferHighAddr;
} MptIOCInitRequest, *PMptIOCInitRequest;

#define LSILOGICWHOINIT_SYSTEM_BIOS 0x01


/**
 * IO controller init reply.
 */
typedef struct MptIOCInitReply
{
    /** Which subsystem send this init request. */
    uint8_t     u8WhoInit;
    /** Reserved */
    uint8_t     u8Reserved;
    /** Message length */
    uint8_t     u8MessageLength;
    /** Function. */
    uint8_t     u8Function;
    /** Flags */
    uint8_t     u8Flags;
    /** Maximum number of devices the driver can handle. */
    uint8_t     u8MaxDevices;
    /** Maximum number of busses the driver can handle. */
    uint8_t     u8MaxBuses;
    /** Message flags. */
    uint8_t     u8MessageFlags;
    /** Message context ID */
    uint32_t    u32MessageContext;
    /** Reserved */
    uint16_t    u16Reserved;
    /** IO controller status. */
    uint16_t    u16IOCStatus;
    /** IO controller log information. */
    uint32_t    u32IOCLogInfo;
} MptIOCInitReply, *PMptIOCInitReply;

/**
 * Doorbell register - Used to get the status of the controller and
 * initialise it.
 */
#define LSILOGIC_REG_DOORBELL 0x00
# define LSILOGIC_REG_DOORBELL_SET_STATE(enmState)     (((enmState) & 0x0f) << 28)
# define LSILOGIC_REG_DOORBELL_SET_USED(enmDoorbell)   (((enmDoorbell != LSILOGICDOORBELLSTATE_NOT_IN_USE) ? 1 : 0) << 27)
# define LSILOGIC_REG_DOORBELL_SET_WHOINIT(enmWhoInit) (((enmWhoInit) & 0x07) << 24)
# define LSILOGIC_REG_DOORBELL_SET_FAULT_CODE(u16Code) (u16Code)
# define LSILOGIC_REG_DOORBELL_GET_FUNCTION(x)         (((x) & 0xff000000) >> 24)
# define LSILOGIC_REG_DOORBELL_GET_SIZE(x)             (((x) & 0x00ff0000) >> 16)

/**
 * Functions which can be passed through the system doorbell.
 */
#define LSILOGIC_DOORBELL_FUNCTION_IOC_MSG_UNIT_RESET  0x40L
#define LSILOGIC_DOORBELL_FUNCTION_IO_UNIT_RESET       0x41L
#define LSILOGIC_DOORBELL_FUNCTION_HANDSHAKE           0x42L
#define LSILOGIC_DOORBELL_FUNCTION_REPLY_FRAME_REMOVAL 0x43L

/**
 * Write sequence register for the diagnostic register.
 */
#define LSILOGIC_REG_WRITE_SEQUENCE    0x04

/**
 * Diagnostic register - used to reset the controller.
 */
#define LSILOGIC_REG_HOST_DIAGNOSTIC   0x08
# define LSILOGIC_REG_HOST_DIAGNOSTIC_DIAG_MEM_ENABLE     (RT_BIT(0))
# define LSILOGIC_REG_HOST_DIAGNOSTIC_DISABLE_ARM         (RT_BIT(1))
# define LSILOGIC_REG_HOST_DIAGNOSTIC_RESET_ADAPTER       (RT_BIT(2))
# define LSILOGIC_REG_HOST_DIAGNOSTIC_DIAG_RW_ENABLE      (RT_BIT(4))
# define LSILOGIC_REG_HOST_DIAGNOSTIC_RESET_HISTORY       (RT_BIT(5))
# define LSILOGIC_REG_HOST_DIAGNOSTIC_FLASH_BAD_SIG       (RT_BIT(6))
# define LSILOGIC_REG_HOST_DIAGNOSTIC_DRWE                (RT_BIT(7))
# define LSILOGIC_REG_HOST_DIAGNOSTIC_PREVENT_IOC_BOOT    (RT_BIT(9))
# define LSILOGIC_REG_HOST_DIAGNOSTIC_CLEAR_FLASH_BAD_SIG (RT_BIT(10))

#define LSILOGIC_REG_TEST_BASE_ADDRESS 0x0c
#define LSILOGIC_REG_DIAG_RW_DATA      0x10
#define LSILOGIC_REG_DIAG_RW_ADDRESS   0x14

/**
 * Interrupt status register.
 */
#define LSILOGIC_REG_HOST_INTR_STATUS  0x30
# define LSILOGIC_REG_HOST_INTR_STATUS_W_MASK (RT_BIT(3))
# define LSILOGIC_REG_HOST_INTR_STATUS_DOORBELL_STS    (RT_BIT(31))
# define LSILOGIC_REG_HOST_INTR_STATUS_REPLY_INTR      (RT_BIT(3))
# define LSILOGIC_REG_HOST_INTR_STATUS_SYSTEM_DOORBELL (RT_BIT(0))

/**
 * Interrupt mask register.
 */
#define LSILOGIC_REG_HOST_INTR_MASK    0x34
# define LSILOGIC_REG_HOST_INTR_MASK_W_MASK (RT_BIT(0) | RT_BIT(3) | RT_BIT(8) | RT_BIT(9))
# define LSILOGIC_REG_HOST_INTR_MASK_IRQ_ROUTING (RT_BIT(8) | RT_BIT(9))
# define LSILOGIC_REG_HOST_INTR_MASK_DOORBELL RT_BIT(0)
# define LSILOGIC_REG_HOST_INTR_MASK_REPLY    RT_BIT(3)

/**
 * Queue registers.
 */
#define LSILOGIC_REG_REQUEST_QUEUE     0x40
#define LSILOGIC_REG_REPLY_QUEUE       0x44

/**
 * LsiLogic-SCSI controller data.
 */
typedef struct
{
    /** The SCSI I/O request structure. */
    MptSCSIIORequest    ScsiIoReq;
    /** S/G elements being used, must come after the I/O request structure. */
    MptSGEntrySimple32  Sge[NUM_SG_BUFFERS];
    /** The reply frame used for address replies. */
    uint8_t             abReply[128];
    /** I/O base of device. */
    uint16_t            u16IoBase;
#ifdef USE_VDS
    /** VDS EDDS structure for the I/O request structure. */
    vds_edds            edds_req;
    /** VDS EDDS DMA buffer descriptor structure. */
    vds_edds            edds;
    vds_sg              edds_more_sg[NUM_SG_BUFFERS - 1];
#endif
} lsilogic_t;

/* The BusLogic specific data must fit into 1KB (statically allocated). */
ct_assert(sizeof(lsilogic_t) <= 1024);

#define VBOX_LSILOGIC_NO_DEVICE 0xffff

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

/**
 * Converts a segment:offset pair into a 32bit physical address.
 */
static uint32_t lsilogic_addr_to_phys(void __far *ptr)
{
    return ((uint32_t)FP_SEG(ptr) << 4) + FP_OFF(ptr);
}

static int lsilogic_cmd(lsilogic_t __far *lsilogic, const void __far *pvReq, uint16_t cbReq,
                        void __far *pvReply, uint16_t cbReply)
{
    uint16_t i;
    const uint32_t __far *pu32Req = (const uint32_t __far *)pvReq;
    uint16_t __far *pu16Reply = (uint16_t *)pvReply;
    uint32_t cMsg = cbReq / sizeof(uint32_t);
    uint16_t cReply = cbReply / sizeof(uint16_t);
    uint32_t u32Fn = (LSILOGIC_DOORBELL_FUNCTION_HANDSHAKE << 24) | (cMsg << 16);

    if (   cbReq % sizeof(uint32_t)
        || cbReply % sizeof(uint16_t))
        return 1;

    outpd(lsilogic->u16IoBase + LSILOGIC_REG_DOORBELL, u32Fn);
    for (i = 0; i < cMsg; i++)
        outpd(lsilogic->u16IoBase + LSILOGIC_REG_DOORBELL, pu32Req[i]);

    for (i = 0; i < cReply; i++)
    {
        /* Wait for the system doorbell interrupt status to be set. */
        while (!(inpd(lsilogic->u16IoBase + LSILOGIC_REG_HOST_INTR_STATUS) & LSILOGIC_REG_HOST_INTR_STATUS_SYSTEM_DOORBELL));

        pu16Reply[i] = (uint16_t)inpd(lsilogic->u16IoBase + LSILOGIC_REG_DOORBELL); /* The lower 16bits contain the reply data. */
        outpd(lsilogic->u16IoBase + LSILOGIC_REG_HOST_INTR_STATUS, 1);
    }

    return 0;
}

static int lsilogic_scsi_cmd_exec(lsilogic_t __far *lsilogic)
{
    uint32_t    u32Reply = 0;
    uint32_t    u32ReplyDummy = 0;

#ifdef USE_VDS
    /* Lock request memory. */
    lsilogic->edds_req.num_avail = 1;
    vds_build_sg_list(&lsilogic->edds_req, &lsilogic->ScsiIoReq, sizeof(lsilogic->ScsiIoReq));

    /* Send it off. */
    outpd(lsilogic->u16IoBase + LSILOGIC_REG_REQUEST_QUEUE, lsilogic->edds_req.u.sg[0].phys_addr);
#else
    /* Send it off. */
    outpd(lsilogic->u16IoBase + LSILOGIC_REG_REQUEST_QUEUE, lsilogic_addr_to_phys(&lsilogic->ScsiIoReq));
#endif

    /* Wait for it to finish. */
    while (!(inpd(lsilogic->u16IoBase + LSILOGIC_REG_HOST_INTR_STATUS) & LSILOGIC_REG_HOST_INTR_STATUS_REPLY_INTR));

    outpd(lsilogic->u16IoBase + LSILOGIC_REG_HOST_INTR_STATUS, 1);

#ifdef USE_VDS
    /* Unlock the request again. */
    vds_free_sg_list(&lsilogic->edds_req);
#endif
    /* Read the reply queue. */
    u32Reply = inpd(lsilogic->u16IoBase + LSILOGIC_REG_REPLY_QUEUE);
    u32ReplyDummy = inpd(lsilogic->u16IoBase + LSILOGIC_REG_REPLY_QUEUE);
    if (u32ReplyDummy != 0xffffffff)
        return 5;
    if (u32Reply & RT_BIT(31))
    {
        /*
         * This is an address reply indicating a failed transaction, so just return an error without
         * bothering to check the exact failure reason for now.
         *
         * Just provide the reply frame to the reply queue again.
         */
        outpd(lsilogic->u16IoBase + LSILOGIC_REG_REPLY_QUEUE, lsilogic_addr_to_phys(&lsilogic->abReply));
        return 4;
    }

    if (u32Reply != 0xcafe) /* Getting a different context ID should never ever happen. */
        return 3;

    return 0;
}

int lsilogic_scsi_cmd_data_out(void __far *pvHba, uint8_t idTgt, uint8_t __far *aCDB,
                               uint8_t cbCDB, unsigned char __far *buffer, uint32_t length)
{
    lsilogic_t __far    *lsilogic = (lsilogic_t __far *)pvHba;
    int                 i;
    int                 rc;

    _fmemset(&lsilogic->ScsiIoReq, 0, sizeof(lsilogic->ScsiIoReq));

#ifdef USE_VDS
    lsilogic->edds.num_avail = NUM_SG_BUFFERS;
    vds_build_sg_list(&lsilogic->edds, buffer, length);
#endif

    lsilogic->ScsiIoReq.u8TargetID          = idTgt;
    lsilogic->ScsiIoReq.u8Bus               = 0;
    lsilogic->ScsiIoReq.u8ChainOffset       = 0;
    lsilogic->ScsiIoReq.u8Function          = MPT_MESSAGE_HDR_FUNCTION_SCSI_IO_REQUEST;
    lsilogic->ScsiIoReq.u8CDBLength         = cbCDB;
    lsilogic->ScsiIoReq.u8SenseBufferLength = 0;
    lsilogic->ScsiIoReq.u32MessageContext   = 0xcafe;
    lsilogic->ScsiIoReq.u32Control          = MPT_SCSIIO_REQUEST_CONTROL_TXDIR_WRITE << 24;
    lsilogic->ScsiIoReq.u32DataLength       = length;
    for (i = 0; i < cbCDB; i++)
        lsilogic->ScsiIoReq.au8CDB[i] = aCDB[i];

#ifdef USE_VDS
    for (i = 0; i < lsilogic->edds.num_used; ++i)
    {
        lsilogic->Sge[i].u24Length                  = lsilogic->edds.u.sg[i].size;
        lsilogic->Sge[i].fEndOfList                 = 0;
        lsilogic->Sge[i].f64BitAddress              = 0;
        lsilogic->Sge[i].fBufferContainsData        = 0;
        lsilogic->Sge[i].fLocalAddress              = 0;
        lsilogic->Sge[i].u2ElementType              = 0x01; /* Simple type */
        lsilogic->Sge[i].fEndOfBuffer               = 0;
        lsilogic->Sge[i].fLastElement               = 0;
        lsilogic->Sge[i].u32DataBufferAddressLow    = lsilogic->edds.u.sg[i].phys_addr;
    }
    --i;
    lsilogic->Sge[i].fEndOfList = lsilogic->Sge[i].fEndOfBuffer = lsilogic->Sge[i].fLastElement = 1;
#else
    lsilogic->Sge[0].u24Length               = length;
    lsilogic->Sge[0].fEndOfList              = 1;
    lsilogic->Sge[0].f64BitAddress           = 0;
    lsilogic->Sge[0].fBufferContainsData     = 0;
    lsilogic->Sge[0].fLocalAddress           = 0;
    lsilogic->Sge[0].u2ElementType           = 0x01; /* Simple type */
    lsilogic->Sge[0].fEndOfBuffer            = 1;
    lsilogic->Sge[0].fLastElement            = 1;
    lsilogic->Sge[0].u32DataBufferAddressLow = phys_buf;
#endif

    rc = lsilogic_scsi_cmd_exec(lsilogic);

#ifdef USE_VDS
    /* Unlock the buffer again. */
    vds_free_sg_list(&lsilogic->edds);
#endif

    return rc;
}

int lsilogic_scsi_cmd_data_in(void __far *pvHba, uint8_t idTgt, uint8_t __far *aCDB,
                              uint8_t cbCDB, unsigned char __far *buffer, uint32_t length)
{
    lsilogic_t __far    *lsilogic = (lsilogic_t __far *)pvHba;
    int                 i;
    int                 rc;

    _fmemset(&lsilogic->ScsiIoReq, 0, sizeof(lsilogic->ScsiIoReq));

#ifdef USE_VDS
    lsilogic->edds.num_avail = NUM_SG_BUFFERS;
    vds_build_sg_list(&lsilogic->edds, buffer, length);
#endif

    lsilogic->ScsiIoReq.u8TargetID          = idTgt;
    lsilogic->ScsiIoReq.u8Bus               = 0;
    lsilogic->ScsiIoReq.u8ChainOffset       = 0;
    lsilogic->ScsiIoReq.u8Function          = MPT_MESSAGE_HDR_FUNCTION_SCSI_IO_REQUEST;
    lsilogic->ScsiIoReq.u8CDBLength         = cbCDB;
    lsilogic->ScsiIoReq.u8SenseBufferLength = 0;
    lsilogic->ScsiIoReq.u32MessageContext   = 0xcafe;
    lsilogic->ScsiIoReq.u32Control          = MPT_SCSIIO_REQUEST_CONTROL_TXDIR_READ << 24;
    lsilogic->ScsiIoReq.u32DataLength       = length;
    for (i = 0; i < cbCDB; i++)
        lsilogic->ScsiIoReq.au8CDB[i] = aCDB[i];

#ifdef USE_VDS
    for (i = 0; i < lsilogic->edds.num_used; ++i)
    {
        lsilogic->Sge[i].u24Length                  = lsilogic->edds.u.sg[i].size;
        lsilogic->Sge[i].fEndOfList                 = 0;
        lsilogic->Sge[i].f64BitAddress              = 0;
        lsilogic->Sge[i].fBufferContainsData        = 0;
        lsilogic->Sge[i].fLocalAddress              = 0;
        lsilogic->Sge[i].u2ElementType              = 0x01; /* Simple type */
        lsilogic->Sge[i].fEndOfBuffer               = 0;
        lsilogic->Sge[i].fLastElement               = 0;
        lsilogic->Sge[i].u32DataBufferAddressLow    = lsilogic->edds.u.sg[i].phys_addr;
    }
    --i;
    lsilogic->Sge[i].fEndOfList = lsilogic->Sge[i].fEndOfBuffer = lsilogic->Sge[i].fLastElement = 1;

#else
    lsilogic->Sge[0].u24Length                 = length;
    lsilogic->Sge[0].fEndOfList                = 1;
    lsilogic->Sge[0].f64BitAddress             = 0;
    lsilogic->Sge[0].fBufferContainsData       = 0;
    lsilogic->Sge[0].fLocalAddress             = 0;
    lsilogic->Sge[0].u2ElementType             = 0x01; /* Simple type */
    lsilogic->Sge[0].fEndOfBuffer              = 1;
    lsilogic->Sge[0].fLastElement              = 1;
    lsilogic->Sge[0].u32DataBufferAddressLow   = lsilogic_addr_to_phys(buffer);
#endif

    rc = lsilogic_scsi_cmd_exec(lsilogic);

#ifdef USE_VDS
    /* Unlock the buffer again. */
    vds_free_sg_list(&lsilogic->edds);
#endif

    return rc;
}

/**
 * Initializes the LsiLogic SCSI HBA and detects attached devices.
 */
static int lsilogic_scsi_hba_init(lsilogic_t __far *lsilogic)
{
    int                 rc;
    MptIOCInitRequest   IocInitReq;
    MptIOCInitReply     IocInitReply;

    /*
     * The following initialization sequence is stripped down to the point to work with
     * our emulated LsiLogic controller, it will most certainly fail on real hardware.
     */

    /* Hard reset, write the sequence to enable the diagnostic access. */
    outpd(lsilogic->u16IoBase + LSILOGIC_REG_WRITE_SEQUENCE, 0x04);
    outpd(lsilogic->u16IoBase + LSILOGIC_REG_WRITE_SEQUENCE, 0x02);
    outpd(lsilogic->u16IoBase + LSILOGIC_REG_WRITE_SEQUENCE, 0x07);
    outpd(lsilogic->u16IoBase + LSILOGIC_REG_WRITE_SEQUENCE, 0x0d);
    outpd(lsilogic->u16IoBase + LSILOGIC_REG_HOST_DIAGNOSTIC, LSILOGIC_REG_HOST_DIAGNOSTIC_RESET_ADAPTER);

    IocInitReq.u8WhoInit              = LSILOGICWHOINIT_SYSTEM_BIOS;
    IocInitReq.u8Function             = MPT_MESSAGE_HDR_FUNCTION_IOC_INIT;
    IocInitReq.u32HostMfaHighAddr     = 0;
    IocInitReq.u32SenseBufferHighAddr = 0;
    IocInitReq.u8MaxBuses             = 1;
    IocInitReq.u8MaxDevices           = 4;
    IocInitReq.u16ReplyFrameSize      = sizeof(lsilogic->abReply);
    rc = lsilogic_cmd(lsilogic, &IocInitReq, sizeof(IocInitReq), &IocInitReply, sizeof(IocInitReply));
    if (!rc)
    {
        /* Provide a single reply frame for SCSI I/O errors. */
        outpd(lsilogic->u16IoBase + LSILOGIC_REG_REPLY_QUEUE, lsilogic_addr_to_phys(&lsilogic->abReply));
        return 0;
    }

    return 1;
}

/**
 * Init the LsiLogic SCSI driver and detect attached disks.
 */
int lsilogic_scsi_init(void __far *pvHba, uint8_t u8Bus, uint8_t u8DevFn)
{
    lsilogic_t __far *lsilogic = (lsilogic_t __far *)pvHba;
    uint32_t u32Bar;

    DBG_LSILOGIC("LsiLogic SCSI HBA at Bus %u DevFn 0x%x (raw 0x%x)\n", u8Bus, u8DevFn);

    u32Bar = pci_read_config_dword(u8Bus, u8DevFn, 0x10);

    DBG_LSILOGIC("BAR at 0x10 : 0x%x\n", u32Bar);

    if ((u32Bar & 0x01) != 0)
    {
        uint16_t u16IoBase = (u32Bar & 0xfff0);

        /* Enable PCI memory, I/O, bus mastering access in command register. */
        pci_write_config_word(u8Bus, u8DevFn, 4, 0x7);

        DBG_LSILOGIC("I/O base: 0x%x\n", u16IoBase);
        lsilogic->u16IoBase = u16IoBase;
        return lsilogic_scsi_hba_init(lsilogic);
    }
    else
        DBG_LSILOGIC("BAR is MMIO\n");

    return 1;
}

