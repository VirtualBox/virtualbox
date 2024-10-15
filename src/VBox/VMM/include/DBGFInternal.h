/* $Id: DBGFInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Internal header file.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef VMM_INCLUDED_SRC_include_DBGFInternal_h
#define VMM_INCLUDED_SRC_include_DBGFInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#ifdef IN_RING3
# include <VBox/dis.h>
#endif
#include <VBox/types.h>
#include <iprt/semaphore.h>
#include <iprt/critsect.h>
#include <iprt/string.h>
#include <iprt/avl.h>
#include <iprt/dbg.h>
#include <iprt/tracelog.h>
#include <VBox/vmm/dbgf.h>



/** @defgroup grp_dbgf_int   Internals
 * @ingroup grp_dbgf
 * @internal
 * @{
 */

/** The maximum tracer instance (total) size, ring-0/raw-mode capable tracers. */
#define DBGF_MAX_TRACER_INSTANCE_SIZE    _512M
/** The maximum tracers instance (total) size, ring-3 only tracers. */
#define DBGF_MAX_TRACER_INSTANCE_SIZE_R3 _1G
/** Event ringbuffer header size. */
#define DBGF_TRACER_EVT_HDR_SZ           (32)
/** Event ringbuffer payload size. */
#define DBGF_TRACER_EVT_PAYLOAD_SZ       (32)
/** Event ringbuffer entry size. */
#define DBGF_TRACER_EVT_SZ               (DBGF_TRACER_EVT_HDR_SZ + DBGF_TRACER_EVT_PAYLOAD_SZ)


/** @name Global breakpoint table handling defines.
 * @{ */
/** Maximum number of breakpoint owners supported (power of two). */
#define DBGF_BP_OWNER_COUNT_MAX             _32K
/** Maximum number of breakpoints supported (power of two). */
#define DBGF_BP_COUNT_MAX                   _1M
/** Size of a single breakpoint structure in bytes. */
#define DBGF_BP_ENTRY_SZ                    64
/** Number of breakpoints handled in one chunk (power of two). */
#define DBGF_BP_COUNT_PER_CHUNK             _64K
/** Number of chunks required to support all breakpoints. */
#define DBGF_BP_CHUNK_COUNT                 (DBGF_BP_COUNT_MAX / DBGF_BP_COUNT_PER_CHUNK)
/** Maximum number of instruction bytes when executing breakpointed instructions. */
#define DBGF_BP_INSN_MAX                    16
/** @} */

/** @name L2 lookup table limit defines.
 * @{ */
/** Maximum number of entreis in the L2 lookup table. */
#define DBGF_BP_L2_TBL_ENTRY_COUNT_MAX      _512K
/** Number of L2 entries handled in one chunk. */
#define DBGF_BP_L2_TBL_ENTRIES_PER_CHUNK    _64K
/** Number of chunks required tp support all L2 lookup table entries. */
#define DBGF_BP_L2_TBL_CHUNK_COUNT          (DBGF_BP_L2_TBL_ENTRY_COUNT_MAX / DBGF_BP_L2_TBL_ENTRIES_PER_CHUNK)
/** @} */


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/

/**
 * Event entry types.
 */
typedef enum DBGFTRACEREVT
{
    /** Invalid type. */
    DBGFTRACEREVT_INVALID = 0,
    /** Register event source event. */
    DBGFTRACEREVT_SRC_REGISTER,
    /** Deregister event source event. */
    DBGFTRACEREVT_SRC_DEREGISTER,
    /** MMIO region create event. */
    DBGFTRACEREVT_MMIO_REGION_CREATE,
    /** MMIO map region event. */
    DBGFTRACEREVT_MMIO_MAP,
    /** MMIO unmap region event. */
    DBGFTRACEREVT_MMIO_UNMAP,
    /** MMIO read event. */
    DBGFTRACEREVT_MMIO_READ,
    /** MMIO write event. */
    DBGFTRACEREVT_MMIO_WRITE,
    /** MMIO fill event. */
    DBGFTRACEREVT_MMIO_FILL,
    /** I/O port region create event. */
    DBGFTRACEREVT_IOPORT_REGION_CREATE,
    /** I/O port map event. */
    DBGFTRACEREVT_IOPORT_MAP,
    /** I/O port unmap event. */
    DBGFTRACEREVT_IOPORT_UNMAP,
    /** I/O port read event. */
    DBGFTRACEREVT_IOPORT_READ,
    /** I/O port read string event. */
    DBGFTRACEREVT_IOPORT_READ_STR,
    /** I/O port write event. */
    DBGFTRACEREVT_IOPORT_WRITE,
    /** I/O port write string event. */
    DBGFTRACEREVT_IOPORT_WRITE_STR,
    /** IRQ event. */
    DBGFTRACEREVT_IRQ,
    /** I/O APIC MSI event. */
    DBGFTRACEREVT_IOAPIC_MSI,
    /** Read from guest physical memory. */
    DBGFTRACEREVT_GCPHYS_READ,
    /** Write to guest physical memory. */
    DBGFTRACEREVT_GCPHYS_WRITE,
    /** 32bit hack. */
    DBGFTRACEREVT_32BIT_HACK
} DBGFTRACEREVT;
/** Pointer to a trace event entry type. */
typedef DBGFTRACEREVT *PDBGFTRACEREVT;


/**
 * MMIO region create event.
 */
typedef struct DBGFTRACEREVTMMIOCREATE
{
    /** Unique region handle for the event source. */
    uint64_t                                hMmioRegion;
    /** Size of the region in bytes. */
    RTGCPHYS                                cbRegion;
    /** IOM flags passed to the region. */
    uint32_t                                fIomFlags;
    /** The PCI region for a PCI device. */
    uint32_t                                iPciRegion;
    /** Padding to 32byte. */
    uint64_t                                u64Pad0;
} DBGFTRACEREVTMMIOCREATE;
/** Pointer to a MMIO map event. */
typedef DBGFTRACEREVTMMIOCREATE *PDBGFTRACEREVTMMIOCREATE;
/** Pointer to a const MMIO map event. */
typedef const DBGFTRACEREVTMMIOCREATE *PCDBGFTRACEREVTMMIOCREATE;

AssertCompileSize(DBGFTRACEREVTMMIOCREATE, DBGF_TRACER_EVT_PAYLOAD_SZ);


/**
 * MMIO region map event.
 */
typedef struct DBGFTRACEREVTMMIOMAP
{
    /** Unique region handle for the event source. */
    uint64_t                                hMmioRegion;
    /** The base guest physical address of the MMIO region. */
    RTGCPHYS                                GCPhysMmioBase;
    /** Padding to 32byte. */
    uint64_t                                au64Pad0[2];
} DBGFTRACEREVTMMIOMAP;
/** Pointer to a MMIO map event. */
typedef DBGFTRACEREVTMMIOMAP *PDBGFTRACEREVTMMIOMAP;
/** Pointer to a const MMIO map event. */
typedef const DBGFTRACEREVTMMIOMAP *PCDBGFTRACEREVTMMIOMAP;

AssertCompileSize(DBGFTRACEREVTMMIOMAP, DBGF_TRACER_EVT_PAYLOAD_SZ);


/**
 * MMIO region unmap event.
 */
typedef struct DBGFTRACEREVTMMIOUNMAP
{
    /** Unique region handle for the event source. */
    uint64_t                                hMmioRegion;
    /** Padding to 32byte. */
    uint64_t                                au64Pad0[3];
} DBGFTRACEREVTMMIOUNMAP;
/** Pointer to a MMIO map event. */
typedef DBGFTRACEREVTMMIOUNMAP *PDBGFTRACEREVTMMIOUNMAP;
/** Pointer to a const MMIO map event. */
typedef const DBGFTRACEREVTMMIOUNMAP *PCDBGFTRACEREVTMMIOUNMAP;

AssertCompileSize(DBGFTRACEREVTMMIOUNMAP, DBGF_TRACER_EVT_PAYLOAD_SZ);


/**
 * MMIO event.
 */
typedef struct DBGFTRACEREVTMMIO
{
    /** Unique region handle for the event source. */
    uint64_t                                hMmioRegion;
    /** Offset into the region the access happened. */
    RTGCPHYS                                offMmio;
    /** Number of bytes transfered (the direction is in the event header). */
    uint64_t                                cbXfer;
    /** The value transfered. */
    uint64_t                                u64Val;
} DBGFTRACEREVTMMIO;
/** Pointer to a MMIO event. */
typedef DBGFTRACEREVTMMIO *PDBGFTRACEREVTMMIO;
/** Pointer to a const MMIO event. */
typedef const DBGFTRACEREVTMMIO *PCDBGFTRACEREVTMMIO;

AssertCompileSize(DBGFTRACEREVTMMIO, DBGF_TRACER_EVT_PAYLOAD_SZ);


/**
 * MMIO fill event.
 */
typedef struct DBGFTRACEREVTMMIOFILL
{
    /** Unique region handle for the event source. */
    uint64_t                                hMmioRegion;
    /** Offset into the region the access happened. */
    RTGCPHYS                                offMmio;
    /** Item size in bytes. */
    uint32_t                                cbItem;
    /** Amount of items being filled. */
    uint32_t                                cItems;
    /** The fill value. */
    uint32_t                                u32Item;
    /** Padding to 32bytes. */
    uint32_t                                u32Pad0;
} DBGFTRACEREVTMMIOFILL;
/** Pointer to a MMIO event. */
typedef DBGFTRACEREVTMMIOFILL *PDBGFTRACEREVTMMIOFILL;
/** Pointer to a const MMIO event. */
typedef const DBGFTRACEREVTMMIOFILL *PCDBGFTRACEREVTMMIOFILL;

AssertCompileSize(DBGFTRACEREVTMMIOFILL, DBGF_TRACER_EVT_PAYLOAD_SZ);


/**
 * I/O port region create event.
 */
typedef struct DBGFTRACEREVTIOPORTCREATE
{
    /** Unique I/O port region handle for the event source. */
    uint64_t                                hIoPorts;
    /** Number of ports. */
    RTIOPORT                                cPorts;
    /** Padding. */
    uint16_t                                u16Pad0;
    /** IOM flags passed to the region. */
    uint32_t                                fIomFlags;
    /** The PCI region for a PCI device. */
    uint32_t                                iPciRegion;
    /** Padding to 32byte. */
    uint32_t                                u32Pad0[3];
} DBGFTRACEREVTIOPORTCREATE;
/** Pointer to a MMIO map event. */
typedef DBGFTRACEREVTIOPORTCREATE *PDBGFTRACEREVTIOPORTCREATE;
/** Pointer to a const MMIO map event. */
typedef const DBGFTRACEREVTIOPORTCREATE *PCDBGFTRACEREVTIOPORTCREATE;

AssertCompileSize(DBGFTRACEREVTIOPORTCREATE, DBGF_TRACER_EVT_PAYLOAD_SZ);


/**
 * I/O port region map event.
 */
typedef struct DBGFTRACEREVTIOPORTMAP
{
    /** Unique I/O port region handle for the event source. */
    uint64_t                                hIoPorts;
    /** The base I/O port for the region. */
    RTIOPORT                                IoPortBase;
    /** Padding to 32byte. */
    uint16_t                                au16Pad0[11];
} DBGFTRACEREVTIOPORTMAP;
/** Pointer to a MMIO map event. */
typedef DBGFTRACEREVTIOPORTMAP *PDBGFTRACEREVTIOPORTMAP;
/** Pointer to a const MMIO map event. */
typedef const DBGFTRACEREVTIOPORTMAP *PCDBGFTRACEREVTIOPORTMAP;

AssertCompileSize(DBGFTRACEREVTIOPORTMAP, DBGF_TRACER_EVT_PAYLOAD_SZ);


/**
 * MMIO region unmap event.
 */
typedef struct DBGFTRACEREVTIOPORTUNMAP
{
    /** Unique region handle for the event source. */
    uint64_t                                hIoPorts;
    /** Padding to 32byte. */
    uint64_t                                au64Pad0[3];
} DBGFTRACEREVTIOPORTUNMAP;
/** Pointer to a MMIO map event. */
typedef DBGFTRACEREVTIOPORTUNMAP *PDBGFTRACEREVTIOPORTUNMAP;
/** Pointer to a const MMIO map event. */
typedef const DBGFTRACEREVTIOPORTUNMAP *PCDBGFTRACEREVTIOPORTUNMAP;

AssertCompileSize(DBGFTRACEREVTIOPORTUNMAP, DBGF_TRACER_EVT_PAYLOAD_SZ);


/**
 * I/O port event.
 */
typedef struct DBGFTRACEREVTIOPORT
{
    /** Unique region handle for the event source. */
    uint64_t                                hIoPorts;
    /** Offset into the I/O port region. */
    RTIOPORT                                offPort;
    /** 8 byte alignment. */
    uint8_t                                 abPad0[6];
    /** Number of bytes transfered (the direction is in the event header). */
    uint64_t                                cbXfer;
    /** The value transfered. */
    uint32_t                                u32Val;
    /** Padding to 32bytes. */
    uint8_t                                 abPad1[4];
} DBGFTRACEREVTIOPORT;
/** Pointer to a MMIO event. */
typedef DBGFTRACEREVTIOPORT *PDBGFTRACEREVTIOPORT;
/** Pointer to a const MMIO event. */
typedef const DBGFTRACEREVTIOPORT *PCDBGFTRACEREVTIOPORT;

AssertCompileSize(DBGFTRACEREVTIOPORT, DBGF_TRACER_EVT_PAYLOAD_SZ);


/**
 * I/O port string event.
 */
typedef struct DBGFTRACEREVTIOPORTSTR
{
    /** Unique region handle for the event source. */
    uint64_t                                hIoPorts;
    /** Item size in bytes. */
    uint32_t                                cbItem;
    /** Number of transfers requested - for writes this gives the amount of valid data following. */
    uint32_t                                cTransfersReq;
    /** Number of transfers done - for reads this gives the amount of valid data following. */
    uint32_t                                cTransfersRet;
    /** Offset into the I/O port region. */
    RTIOPORT                                offPort;
    /** Data being transfered. */
    uint8_t                                 abData[10];
} DBGFTRACEREVTIOPORTSTR;
/** Pointer to a MMIO event. */
typedef DBGFTRACEREVTIOPORTSTR *PDBGFTRACEREVTIOPORTSTR;
/** Pointer to a const MMIO event. */
typedef const DBGFTRACEREVTIOPORTSTR *PCDBGFTRACEREVTIOPORTSTR;

AssertCompileSize(DBGFTRACEREVTIOPORTSTR, DBGF_TRACER_EVT_PAYLOAD_SZ);


/**
 * IRQ event.
 */
typedef struct DBGFTRACEREVTIRQ
{
    /** The IRQ line. */
    int32_t                                 iIrq;
    /** IRQ level flags. */
    int32_t                                 fIrqLvl;
    /** Padding to 32bytes. */
    uint32_t                                au32Pad0[6];
} DBGFTRACEREVTIRQ;
/** Pointer to a MMIO event. */
typedef DBGFTRACEREVTIRQ *PDBGFTRACEREVTIRQ;
/** Pointer to a const MMIO event. */
typedef const DBGFTRACEREVTIRQ *PCDBGFTRACEREVTIRQ;

AssertCompileSize(DBGFTRACEREVTIRQ, DBGF_TRACER_EVT_PAYLOAD_SZ);


/**
 * I/O APIC MSI event.
 */
typedef struct DBGFTRACEREVTIOAPICMSI
{
    /** The guest physical address being written. */
    RTGCPHYS                                GCPhys;
    /** The value being written. */
    uint32_t                                u32Val;
    /** Padding to 32bytes. */
    uint32_t                                au32Pad0[5];
} DBGFTRACEREVTIOAPICMSI;
/** Pointer to a MMIO event. */
typedef DBGFTRACEREVTIOAPICMSI *PDBGFTRACEREVTIOAPICMSI;
/** Pointer to a const MMIO event. */
typedef const DBGFTRACEREVTIOAPICMSI *PCDBGFTRACEREVTIOAPICMSI;

AssertCompileSize(DBGFTRACEREVTIOAPICMSI, DBGF_TRACER_EVT_PAYLOAD_SZ);


/**
 * Guest physical memory transfer.
 */
typedef struct DBGFTRACEREVTGCPHYS
{
    /** Guest physical address of the access. */
    RTGCPHYS                                GCPhys;
    /** Number of bytes transfered (the direction is in the event header).
     * If the number is small enough to fit into the remaining space of the entry
     * it is stored here, otherwise it will be stored in the next entry (and following
     * entries). */
    uint64_t                                cbXfer;
    /** Guest data being transfered. */
    uint8_t                                 abData[16];
} DBGFTRACEREVTGCPHYS;
/** Pointer to a guest physical memory transfer event. */
typedef DBGFTRACEREVTGCPHYS *PDBGFTRACEREVTGCPHYS;
/** Pointer to a const uest physical memory transfer event. */
typedef const DBGFTRACEREVTGCPHYS *PCDBGFTRACEREVTGCPHYS;

AssertCompileSize(DBGFTRACEREVTGCPHYS, DBGF_TRACER_EVT_PAYLOAD_SZ);


/**
 * A trace event header in the shared ring buffer.
 */
typedef struct DBGFTRACEREVTHDR
{
    /** Event ID. */
    volatile uint64_t                       idEvt;
    /** The previous event ID this one links to,
     * DBGF_TRACER_EVT_HDR_ID_INVALID if it links to no other event. */
    uint64_t                                idEvtPrev;
    /** Event source. */
    DBGFTRACEREVTSRC                        hEvtSrc;
    /** The event entry type. */
    DBGFTRACEREVT                           enmEvt;
    /** Flags for this event. */
    uint32_t                                fFlags;
} DBGFTRACEREVTHDR;
/** Pointer to a trace event header. */
typedef DBGFTRACEREVTHDR *PDBGFTRACEREVTHDR;
/** Pointer to a const trace event header. */
typedef const DBGFTRACEREVTHDR *PCDBGFTRACEREVTHDR;

AssertCompileSize(DBGFTRACEREVTHDR, DBGF_TRACER_EVT_HDR_SZ);

/** Invalid event ID, this is always set by the flush thread after processing one entry
 * so the producers know when they are about to overwrite not yet processed entries in the ring buffer. */
#define DBGF_TRACER_EVT_HDR_ID_INVALID      UINT64_C(0xffffffffffffffff)

/** The event came from R0. */
#define DBGF_TRACER_EVT_HDR_F_R0            RT_BIT(0)

/** Default event header tracer flags. */
#ifdef IN_RING0
# define DBGF_TRACER_EVT_HDR_F_DEFAULT      DBGF_TRACER_EVT_HDR_F_R0
#else
# define DBGF_TRACER_EVT_HDR_F_DEFAULT      (0)
#endif


/**
 * Tracer instance data, shared structure.
 */
typedef struct DBGFTRACERSHARED
{
    /** The global event ID counter, monotonically increasing.
     * Accessed by all threads causing a trace event. */
    volatile uint64_t                       idEvt;
    /** The SUP event semaphore for poking the flush thread. */
    SUPSEMEVENT                             hSupSemEvtFlush;
    /** Ring buffer size. */
    size_t                                  cbRingBuf;
    /** Flag whether there are events in the ring buffer to get processed. */
    volatile bool                           fEvtsWaiting;
    /** Flag whether the flush thread is actively running or was kicked. */
    volatile bool                           fFlushThrdActive;
    /** Padding to a 64byte alignment. */
    uint8_t                                 abAlignment0[32];
} DBGFTRACERSHARED;
/** Pointer to the shared tarcer instance data. */
typedef DBGFTRACERSHARED *PDBGFTRACERSHARED;

AssertCompileSizeAlignment(DBGFTRACERSHARED, 64);


/**
 * Guest memory read/write data aggregation.
 */
typedef struct DBGFTRACERGCPHYSRWAGG
{
    /** The event ID which started the aggregation (used for the group ID when writing out the event). */
    uint64_t                                idEvtStart;
    /** The previous event ID used to link all the chunks together. */
    uint64_t                                idEvtPrev;
    /** Number of bytes being transfered. */
    size_t                                  cbXfer;
    /** Amount of data left to aggregate before it can be written. */
    size_t                                  cbLeft;
    /** Amount of bytes allocated. */
    size_t                                  cbBufMax;
    /** Offset into the buffer to write next. */
    size_t                                  offBuf;
    /** Pointer to the allocated buffer. */
    uint8_t                                 *pbBuf;
} DBGFTRACERGCPHYSRWAGG;
/** Pointer to a guest memory read/write data aggregation structure. */
typedef DBGFTRACERGCPHYSRWAGG *PDBGFTRACERGCPHYSRWAGG;


/**
 * Tracer instance data, ring-3
 */
typedef struct DBGFTRACERINSR3
{
    /** Pointer to the next instance.
     * (Head is pointed to by PDM::pTracerInstances.) */
    R3PTRTYPE(struct DBGFTRACERINSR3 *)     pNextR3;
    /** R3 pointer to the VM this instance was created for. */
    PVMR3                                   pVMR3;
    /** Tracer instance number. */
    uint32_t                                idTracer;
    /** Flag whether the tracer has the R0 part enabled. */
    bool                                    fR0Enabled;
    /** Flag whether the tracer flush thread should shut down. */
    volatile bool                           fShutdown;
    /** Padding. */
    bool                                    afPad0[6];
    /** Next event source ID to return for a source registration. */
    volatile DBGFTRACEREVTSRC               hEvtSrcNext;
    /** Pointer to the shared tracer instance data. */
    R3PTRTYPE(PDBGFTRACERSHARED)            pSharedR3;
    /** The I/O thread writing the log from the shared event ringbuffer. */
    RTTHREAD                                hThrdFlush;
    /** Pointer to the start of the ring buffer. */
    R3PTRTYPE(uint8_t *)                    pbRingBufR3;
    /** The last processed event ID. */
    uint64_t                                idEvtLast;
    /** The trace log writer handle. */
    RTTRACELOGWR                            hTraceLog;
    /** Guest memory data aggregation structures to track
     * currently pending guest memory reads/writes. */
    DBGFTRACERGCPHYSRWAGG                   aGstMemRwData[10];
} DBGFTRACERINSR3;
/** Pointer to a tarcer instance - Ring-3 Ptr. */
typedef R3PTRTYPE(DBGFTRACERINSR3 *) PDBGFTRACERINSR3;


/**
 * Private tracer instance data, ring-0
 */
typedef struct DBGFTRACERINSR0
{
    /** Pointer to the VM this instance was created for. */
    R0PTRTYPE(PGVM)                         pGVM;
    /** The tracer instance memory. */
    RTR0MEMOBJ                              hMemObj;
    /** The ring-3 mapping object. */
    RTR0MEMOBJ                              hMapObj;
    /** Pointer to the shared tracer instance data. */
    R0PTRTYPE(PDBGFTRACERSHARED)            pSharedR0;
    /** Size of the ring buffer in bytes, kept here so R3 can not manipulate the ring buffer
     * size afterwards to trick R0 into doing something harmful. */
    size_t                                  cbRingBuf;
    /** Pointer to the start of the ring buffer. */
    R0PTRTYPE(uint8_t *)                    pbRingBufR0;
} DBGFTRACERINSR0;
/** Pointer to a VM - Ring-0 Ptr. */
typedef R0PTRTYPE(DBGFTRACERINSR0 *) PDBGFTRACERINSR0;


/**
 * Private device instance data, raw-mode
 */
typedef struct DBGFTRACERINSRC
{
    /** Pointer to the VM this instance was created for. */
    RGPTRTYPE(PVM)                          pVMRC;
} DBGFTRACERINSRC;


#ifdef IN_RING3
DECLHIDDEN(int) dbgfTracerR3EvtPostSingle(PVMCC pVM, PDBGFTRACERINSCC pThisCC, DBGFTRACEREVTSRC hEvtSrc,
                                          DBGFTRACEREVT enmTraceEvt, const void *pvEvtDesc, size_t cbEvtDesc,
                                          uint64_t *pidEvt);
#endif

/** VMM Debugger Command. */
typedef enum DBGFCMD
{
    /** No command.
     * This is assigned to the field by the emulation thread after
     * a command has been completed. */
    DBGFCMD_NO_COMMAND = 0,
    /** Halt the VM. */
    DBGFCMD_HALT,
    /** Resume execution. */
    DBGFCMD_GO,
    /** Single step execution - stepping into calls. */
    DBGFCMD_SINGLE_STEP
} DBGFCMD;

/**
 * VMM Debugger Command.
 */
typedef union DBGFCMDDATA
{
    uint32_t    uDummy;
} DBGFCMDDATA;
/** Pointer to DBGF Command Data. */
typedef DBGFCMDDATA *PDBGFCMDDATA;

/**
 * Info type.
 */
typedef enum DBGFINFOTYPE
{
    /** Invalid. */
    DBGFINFOTYPE_INVALID = 0,
    /** Device owner. */
    DBGFINFOTYPE_DEV,
    /** Driver owner. */
    DBGFINFOTYPE_DRV,
    /** Internal owner. */
    DBGFINFOTYPE_INT,
    /** External owner. */
    DBGFINFOTYPE_EXT,
    /** Device owner. */
    DBGFINFOTYPE_DEV_ARGV,
    /** Driver owner. */
    DBGFINFOTYPE_DRV_ARGV,
    /** USB device  owner. */
    DBGFINFOTYPE_USB_ARGV,
    /** Internal owner, argv. */
    DBGFINFOTYPE_INT_ARGV,
    /** External owner. */
    DBGFINFOTYPE_EXT_ARGV
} DBGFINFOTYPE;


/** Pointer to info structure. */
typedef struct DBGFINFO *PDBGFINFO;

#ifdef IN_RING3
/**
 * Info structure.
 */
typedef struct DBGFINFO
{
    /** The flags. */
    uint32_t        fFlags;
    /** Owner type. */
    DBGFINFOTYPE    enmType;
    /** Per type data. */
    union
    {
        /** DBGFINFOTYPE_DEV */
        struct
        {
            /** Device info handler function. */
            PFNDBGFHANDLERDEV   pfnHandler;
            /** The device instance. */
            PPDMDEVINS          pDevIns;
        } Dev;

        /** DBGFINFOTYPE_DRV */
        struct
        {
            /** Driver info handler function. */
            PFNDBGFHANDLERDRV   pfnHandler;
            /** The driver instance. */
            PPDMDRVINS          pDrvIns;
        } Drv;

        /** DBGFINFOTYPE_INT */
        struct
        {
            /** Internal info handler function. */
            PFNDBGFHANDLERINT   pfnHandler;
        } Int;

        /** DBGFINFOTYPE_EXT */
        struct
        {
            /** External info handler function. */
            PFNDBGFHANDLEREXT   pfnHandler;
            /** The user argument. */
            void               *pvUser;
        } Ext;

        /** DBGFINFOTYPE_DEV_ARGV */
        struct
        {
            /** Device info handler function. */
            PFNDBGFINFOARGVDEV  pfnHandler;
            /** The device instance. */
            PPDMDEVINS          pDevIns;
        } DevArgv;

        /** DBGFINFOTYPE_DRV_ARGV */
        struct
        {
            /** Driver info handler function. */
            PFNDBGFINFOARGVDRV  pfnHandler;
            /** The driver instance. */
            PPDMDRVINS          pDrvIns;
        } DrvArgv;

        /** DBGFINFOTYPE_USB_ARGV */
        struct
        {
            /** Driver info handler function. */
            PFNDBGFINFOARGVUSB  pfnHandler;
            /** The driver instance. */
            PPDMUSBINS          pUsbIns;
        } UsbArgv;

        /** DBGFINFOTYPE_INT_ARGV */
        struct
        {
            /** Internal info handler function. */
            PFNDBGFINFOARGVINT  pfnHandler;
        } IntArgv;

        /** DBGFINFOTYPE_EXT_ARGV */
        struct
        {
            /** External info handler function. */
            PFNDBGFINFOARGVEXT  pfnHandler;
            /** The user argument. */
            void               *pvUser;
        } ExtArgv;
    } u;

    /** Pointer to the description. */
    const char     *pszDesc;
    /** Pointer to the next info structure. */
    PDBGFINFO       pNext;
    /** The identifier name length. */
    size_t          cchName;
    /** The identifier name. (Extends 'beyond' the struct as usual.) */
    char            szName[1];
} DBGFINFO;
#endif /* IN_RING3 */


#ifdef IN_RING3
/**
 * Guest OS digger instance.
 */
typedef struct DBGFOS
{
    /** Pointer to the registration record. */
    PCDBGFOSREG                 pReg;
    /** Pointer to the next OS we've registered. */
    struct DBGFOS              *pNext;
    /** List of EMT interface wrappers. */
    struct DBGFOSEMTWRAPPER    *pWrapperHead;
    /** The instance data (variable size). */
    uint8_t                     abData[16];
} DBGFOS;
#endif
/** Pointer to guest OS digger instance. */
typedef struct DBGFOS *PDBGFOS;
/** Pointer to const guest OS digger instance. */
typedef struct DBGFOS const *PCDBGFOS;


/** An invalid breakpoint chunk ID. */
#define DBGF_BP_CHUNK_ID_INVALID                    UINT32_MAX
/** Generates a unique breakpoint handle from the given chunk ID and entry inside the chunk. */
#define DBGF_BP_HND_CREATE(a_idChunk, a_idEntry)    RT_MAKE_U32(a_idEntry, a_idChunk);
/** Returns the chunk ID from the given breakpoint handle. */
#define DBGF_BP_HND_GET_CHUNK_ID(a_hBp)             ((uint32_t)RT_HI_U16(a_hBp))
/** Returns the entry index inside a chunk from the given breakpoint handle. */
#define DBGF_BP_HND_GET_ENTRY(a_hBp)                ((uint32_t)RT_LO_U16(a_hBp))


/** @name DBGF int3 L1 lookup table entry types.
 * @{ */
/** No breakpoint handle assigned for this entry - special value which can be used
 * for comparison with the whole entry. */
#define DBGF_BP_INT3_L1_ENTRY_TYPE_NULL                 UINT32_C(0)
/** Direct breakpoint handle. */
#define DBGF_BP_INT3_L1_ENTRY_TYPE_BP_HND               1
/** Index into the L2 tree denoting the root of a search tree. */
#define DBGF_BP_INT3_L1_ENTRY_TYPE_L2_IDX               2
/** @} */


/** Returns the entry type for the given L1 lookup table entry. */
#define DBGF_BP_INT3_L1_ENTRY_GET_TYPE(a_u32Entry)      ((a_u32Entry) >> 28)
/** Returns a DBGF breakpoint handle from the given L1 lookup table entry,
 * type needs to be DBGF_BP_INT3_L1_ENTRY_TYPE_BP_HND. */
#define DBGF_BP_INT3_L1_ENTRY_GET_BP_HND(a_u32Entry)    ((DBGFBP)((a_u32Entry) & UINT32_C(0x0fffffff)))
/** Returns a L2 index from the given L1 lookup table entry,
 * type needs to be DBGF_BP_INT3_L1_ENTRY_TYPE_L2_IDX. */
#define DBGF_BP_INT3_L1_ENTRY_GET_L2_IDX(a_u32Entry)    ((a_u32Entry) & UINT32_C(0x0fffffff))
/** Creates a L1 entry value from the given type and data. */
#define DBGF_BP_INT3_L1_ENTRY_CREATE(a_Type, a_u32Data) ((((uint32_t)(a_Type)) << 28) | ((a_u32Data) & UINT32_C(0x0fffffff)))
/** Creates a breakpoint handle type L1 lookup entry. */
#define DBGF_BP_INT3_L1_ENTRY_CREATE_BP_HND(a_hBp)      DBGF_BP_INT3_L1_ENTRY_CREATE(DBGF_BP_INT3_L1_ENTRY_TYPE_BP_HND, a_hBp)
/** Creates a L2 index type L1 lookup entry. */
#define DBGF_BP_INT3_L1_ENTRY_CREATE_L2_IDX(a_idxL2)    DBGF_BP_INT3_L1_ENTRY_CREATE(DBGF_BP_INT3_L1_ENTRY_TYPE_L2_IDX, a_idxL2)

/** Extracts the lowest bits from the given GC pointer used as an index into the L1 lookup table. */
#define DBGF_BP_INT3_L1_IDX_EXTRACT_FROM_ADDR(a_GCPtr)  ((uint16_t)((a_GCPtr) & UINT16_C(0xffff)))

/**
 * The internal breakpoint owner state, shared part.
 */
typedef struct DBGFBPOWNERINT
{
    /** Reference counter indicating how man breakpoints use this owner currently. */
    volatile uint32_t           cRefs;
    /** Padding. */
    uint32_t                    u32Pad0;
    /** Callback to call when a breakpoint has hit, Ring-3 Ptr. */
    R3PTRTYPE(PFNDBGFBPHIT)     pfnBpHitR3;
    /** Callback to call when a I/O breakpoint has hit, Ring-3 Ptr. */
    R3PTRTYPE(PFNDBGFBPIOHIT)   pfnBpIoHitR3;
    /** Padding. */
    uint64_t                    u64Pad1;
} DBGFBPOWNERINT;
AssertCompileSize(DBGFBPOWNERINT, 32);
/** Pointer to an internal breakpoint owner state, shared part. */
typedef DBGFBPOWNERINT *PDBGFBPOWNERINT;
/** Pointer to a constant internal breakpoint owner state, shared part. */
typedef const DBGFBPOWNERINT *PCDBGFBPOWNERINT;


/**
 * The internal breakpoint owner state, Ring-0 part.
 */
typedef struct DBGFBPOWNERINTR0
{
    /** Reference counter indicating how man breakpoints use this owner currently. */
    volatile uint32_t           cRefs;
    /** Padding. */
    uint32_t                    u32Pad0;
    /** Callback to call when a breakpoint has hit, Ring-0 Ptr. */
    R0PTRTYPE(PFNDBGFBPHIT)     pfnBpHitR0;
    /** Callback to call when a I/O breakpoint has hit, Ring-0 Ptr. */
    R0PTRTYPE(PFNDBGFBPIOHIT)   pfnBpIoHitR0;
    /** Padding. */
    uint64_t                    u64Pad1;
} DBGFBPOWNERINTR0;
AssertCompileSize(DBGFBPOWNERINTR0, 32);
/** Pointer to an internal breakpoint owner state, shared part. */
typedef DBGFBPOWNERINTR0 *PDBGFBPOWNERINTR0;
/** Pointer to a constant internal breakpoint owner state, shared part. */
typedef const DBGFBPOWNERINTR0 *PCDBGFBPOWNERINTR0;


/**
 * The internal breakpoint state, shared part.
 */
typedef struct DBGFBPINT
{
    /** The publicly visible part. */
    DBGFBPPUB                   Pub;
    /** The opaque user argument for the owner callback, Ring-3 Ptr. */
    R3PTRTYPE(void *)           pvUserR3;
} DBGFBPINT;
AssertCompileSize(DBGFBPINT, DBGF_BP_ENTRY_SZ);
/** Pointer to an internal breakpoint state. */
typedef DBGFBPINT *PDBGFBPINT;
/** Pointer to an const internal breakpoint state. */
typedef const DBGFBPINT *PCDBGFBPINT;


/**
 * The internal breakpoint state, R0 part.
 */
typedef struct DBGFBPINTR0
{
    /** The owner handle. */
    DBGFBPOWNER                 hOwner;
    /** Flag whether the breakpoint is in use. */
    bool                        fInUse;
    /** Padding to 8 byte alignment. */
    bool                        afPad[3];
    /** Opaque user data for the owner callback, Ring-0 Ptr. */
    R0PTRTYPE(void *)           pvUserR0;
} DBGFBPINTR0;
AssertCompileMemberAlignment(DBGFBPINTR0, pvUserR0, 8);
AssertCompileSize(DBGFBPINTR0, 16);
/** Pointer to an internal breakpoint state - Ring-0 Ptr. */
typedef R0PTRTYPE(DBGFBPINTR0 *) PDBGFBPINTR0;


/**
 * Hardware breakpoint state.
 */
typedef struct DBGFBPHW
{
    /** The flat GC address of the breakpoint. */
    RTGCUINTPTR                 GCPtr;
    /** The breakpoint handle if active, NIL_DBGFBP if not in use. */
    volatile DBGFBP             hBp;
    /** The access type (one of the X86_DR7_RW_* value). */
    uint8_t                     fType;
    /** The access size. */
    uint8_t                     cb;
    /** Flag whether the breakpoint is currently enabled. */
    volatile bool               fEnabled;
    /** Padding. */
    uint8_t                     bPad;
} DBGFBPHW;
AssertCompileSize(DBGFBPHW, 16);
/** Pointer to a hardware breakpoint state. */
typedef DBGFBPHW *PDBGFBPHW;
/** Pointer to a const hardware breakpoint state. */
typedef const DBGFBPHW *PCDBGFBPHW;


/**
 * A breakpoint table chunk, ring-3 state.
 */
typedef struct DBGFBPCHUNKR3
{
    /** Pointer to the R3 base of the chunk. */
    R3PTRTYPE(PDBGFBPINT)       pBpBaseR3;
    /** Bitmap of free/occupied breakpoint entries. */
    R3PTRTYPE(volatile void *)  pbmAlloc;
    /** Number of free breakpoints in the chunk. */
    volatile uint32_t           cBpsFree;
    /** The chunk index this tracking structure refers to. */
    uint32_t                    idChunk;
} DBGFBPCHUNKR3;
/** Pointer to a breakpoint table chunk - Ring-3 Ptr. */
typedef DBGFBPCHUNKR3 *PDBGFBPCHUNKR3;
/** Pointer to a const breakpoint table chunk - Ring-3 Ptr. */
typedef const DBGFBPCHUNKR3 *PCDBGFBPCHUNKR3;


/**
 * Breakpoint table chunk, ring-0 state.
 */
typedef struct DBGFBPCHUNKR0
{
    /** The chunks memory. */
    RTR0MEMOBJ                  hMemObj;
    /** The ring-3 mapping object. */
    RTR0MEMOBJ                  hMapObj;
    /** Pointer to the breakpoint entries base. */
    R0PTRTYPE(PDBGFBPINT)       paBpBaseSharedR0;
    /** Pointer to the Ring-0 only part of the breakpoints. */
    PDBGFBPINTR0                paBpBaseR0Only;
} DBGFBPCHUNKR0;
/** Pointer to a breakpoint table chunk - Ring-0 Ptr. */
typedef R0PTRTYPE(DBGFBPCHUNKR0 *) PDBGFBPCHUNKR0;


/**
 * L2 lookup table entry.
 *
 * @remark The order of the members matters to be able to atomically update
 *         the AVL left/right pointers and depth with a single 64bit atomic write.
 * @verbatim
 *         7         6        5        4        3        2        1        0
 *     +--------+--------+--------+--------+--------+--------+--------+--------+
 *     |    hBp[15:0]    |                   GCPtrKey[63:16]                   |
 *     +--------+--------+--------+--------+--------+--------+--------+--------+
 *     | hBp[27:16] | iDepth |     idxRight[21:0]     |      idxLeft[21:0]     |
 *     +--------+--------+--------+--------+--------+--------+--------+--------+
 *                  \_8 bits_/
 * @endverbatim
 */
typedef struct DBGFBPL2ENTRY
{
    /** The upper 6 bytes of the breakpoint address and the low 16 bits of the breakpoint handle. */
    volatile uint64_t           u64GCPtrKeyAndBpHnd1;
    /** Left/right lower index, tree depth and remaining 12 bits of the breakpoint handle. */
    volatile uint64_t           u64LeftRightIdxDepthBpHnd2;
} DBGFBPL2ENTRY;
AssertCompileSize(DBGFBPL2ENTRY, 16);
/** Pointer to a L2 lookup table entry. */
typedef DBGFBPL2ENTRY *PDBGFBPL2ENTRY;
/** Pointer to a const L2 lookup table entry. */
typedef const DBGFBPL2ENTRY *PCDBGFBPL2ENTRY;

/** Extracts the part from the given GC pointer used as the key in the L2 binary search tree. */
#define DBGF_BP_INT3_L2_KEY_EXTRACT_FROM_ADDR(a_GCPtr)  ((uint64_t)((a_GCPtr) >> 16))

/** An invalid breakpoint chunk ID. */
#define DBGF_BP_L2_IDX_CHUNK_ID_INVALID             UINT32_MAX
/** Generates a unique breakpoint handle from the given chunk ID and entry inside the chunk. */
#define DBGF_BP_L2_IDX_CREATE(a_idChunk, a_idEntry) RT_MAKE_U32(a_idEntry, a_idChunk);
/** Returns the chunk ID from the given breakpoint handle. */
#define DBGF_BP_L2_IDX_GET_CHUNK_ID(a_idxL2)        ((uint32_t)RT_HI_U16(a_idxL2))
/** Returns the entry index inside a chunk from the given breakpoint handle. */
#define DBGF_BP_L2_IDX_GET_ENTRY(a_idxL2)           ((uint32_t)RT_LO_U16(a_idxL2))

/** Number of bits for the left/right index pointers. */
#define DBGF_BP_L2_ENTRY_LEFT_RIGHT_IDX_BITS            22
/** Special index value marking the end of a tree. */
#define DBGF_BP_L2_ENTRY_IDX_END                        UINT32_C(0x3fffff)
/** Number of bits to shift the breakpoint handle in the first part. */
#define DBGF_BP_L2_ENTRY_BP_1ST_SHIFT                   48
/** Mask for the first part of the breakpoint handle. */
#define DBGF_BP_L2_ENTRY_BP_1ST_MASK                    UINT32_C(0x0000ffff)
/** Number of bits to shift the breakpoint handle in the second part. */
#define DBGF_BP_L2_ENTRY_BP_2ND_SHIFT                   52
/** Mask for the second part of the breakpoint handle. */
#define DBGF_BP_L2_ENTRY_BP_2ND_MASK                    UINT32_C(0x0fff0000)
/** Mask for the second part of the breakpoint handle stored in the L2 entry. */
#define DBGF_BP_L2_ENTRY_BP_2ND_L2_ENTRY_MASK           UINT64_C(0xfff0000000000000)
/** Number of bits to shift the depth in the second part. */
#define DBGF_BP_L2_ENTRY_DEPTH_SHIFT                    44
/** Mask for the depth. */
#define DBGF_BP_L2_ENTRY_DEPTH_MASK                     UINT8_MAX
/** Number of bits to shift the right L2 index in the second part. */
#define DBGF_BP_L2_ENTRY_RIGHT_IDX_SHIFT                22
/** Number of bits to shift the left L2 index in the second part. */
#define DBGF_BP_L2_ENTRY_LEFT_IDX_SHIFT                 0
/** Index mask. */
#define DBGF_BP_L2_ENTRY_LEFT_RIGHT_IDX_MASK            (RT_BIT_32(DBGF_BP_L2_ENTRY_LEFT_RIGHT_IDX_BITS) - 1)
/** Left index mask. */
#define DBGF_BP_L2_ENTRY_LEFT_IDX_MASK                  (DBGF_BP_L2_ENTRY_LEFT_RIGHT_IDX_MASK << DBGF_BP_L2_ENTRY_LEFT_IDX_SHIFT)
/** Right index mask. */
#define DBGF_BP_L2_ENTRY_RIGHT_IDX_MASK                 (DBGF_BP_L2_ENTRY_LEFT_RIGHT_IDX_MASK << DBGF_BP_L2_ENTRY_RIGHT_IDX_SHIFT)
/** Returns the upper 6 bytes of the GC pointer from the given breakpoint entry. */
#define DBGF_BP_L2_ENTRY_GET_GCPTR(a_u64GCPtrKeyAndBpHnd1) ((a_u64GCPtrKeyAndBpHnd1) & UINT64_C(0x0000ffffffffffff))
/** Returns the breakpoint handle from both L2 entry members. */
#define DBGF_BP_L2_ENTRY_GET_BP_HND(a_u64GCPtrKeyAndBpHnd1, a_u64LeftRightIdxDepthBpHnd2) \
    ((DBGFBP)(((a_u64GCPtrKeyAndBpHnd1) >> DBGF_BP_L2_ENTRY_BP_1ST_SHIFT) | (((a_u64LeftRightIdxDepthBpHnd2) >> DBGF_BP_L2_ENTRY_BP_2ND_SHIFT) << 16)))
/** Extracts the depth of the second 64bit L2 entry value. */
#define DBGF_BP_L2_ENTRY_GET_DEPTH(a_u64LeftRightIdxDepthBpHnd2) ((uint8_t)(((a_u64LeftRightIdxDepthBpHnd2) >> DBGF_BP_L2_ENTRY_DEPTH_SHIFT) & DBGF_BP_L2_ENTRY_DEPTH_MASK))
/** Extracts the lower right index value from the L2 entry value. */
#define DBGF_BP_L2_ENTRY_GET_IDX_RIGHT(a_u64LeftRightIdxDepthBpHnd2) \
    ((uint32_t)(((a_u64LeftRightIdxDepthBpHnd2) >> 22) & DBGF_BP_L2_ENTRY_LEFT_RIGHT_IDX_MASK))
/** Extracts the lower left index value from the L2 entry value. */
#define DBGF_BP_L2_ENTRY_GET_IDX_LEFT(a_u64LeftRightIdxDepthBpHnd2) \
    ((uint32_t)((a_u64LeftRightIdxDepthBpHnd2) & DBGF_BP_L2_ENTRY_LEFT_RIGHT_IDX_MASK))


/**
 * A breakpoint L2 lookup table chunk, ring-3 state.
 */
typedef struct DBGFBPL2TBLCHUNKR3
{
    /** Pointer to the R3 base of the chunk. */
    R3PTRTYPE(PDBGFBPL2ENTRY)   pL2BaseR3;
    /** Bitmap of free/occupied breakpoint entries. */
    R3PTRTYPE(volatile void *)  pbmAlloc;
    /** Number of free entries in the chunk. */
    volatile uint32_t           cFree;
    /** The chunk index this tracking structure refers to. */
    uint32_t                    idChunk;
} DBGFBPL2TBLCHUNKR3;
/** Pointer to a breakpoint L2 lookup table chunk - Ring-3 Ptr. */
typedef DBGFBPL2TBLCHUNKR3 *PDBGFBPL2TBLCHUNKR3;
/** Pointer to a const breakpoint L2 lookup table chunk - Ring-3 Ptr. */
typedef const DBGFBPL2TBLCHUNKR3 *PCDBGFBPL2TBLCHUNKR3;


/**
 * Breakpoint L2 lookup table chunk, ring-0 state.
 */
typedef struct DBGFBPL2TBLCHUNKR0
{
    /** The chunks memory. */
    RTR0MEMOBJ                  hMemObj;
    /** The ring-3 mapping object. */
    RTR0MEMOBJ                  hMapObj;
    /** Pointer to the breakpoint entries base. */
    R0PTRTYPE(PDBGFBPL2ENTRY)   paBpL2TblBaseSharedR0;
} DBGFBPL2TBLCHUNKR0;
/** Pointer to a breakpoint L2 lookup table chunk - Ring-0 Ptr. */
typedef R0PTRTYPE(DBGFBPL2TBLCHUNKR0 *) PDBGFBPL2TBLCHUNKR0;



/**
 * DBGF Data (part of VM)
 */
typedef struct DBGF
{
    /** Bitmap of enabled hardware interrupt breakpoints. */
    uint32_t                    bmHardIntBreakpoints[256 / 32];
    /** Bitmap of enabled software interrupt breakpoints. */
    uint32_t                    bmSoftIntBreakpoints[256 / 32];
    /** Bitmap of selected events.
     * This includes non-selectable events too for simplicity, we maintain the
     * state for some of these, as it may come in handy. */
    uint64_t                    bmSelectedEvents[(DBGFEVENT_END + 63) / 64];

    /** Enabled hardware interrupt breakpoints. */
    uint32_t                    cHardIntBreakpoints;
    /** Enabled software interrupt breakpoints. */
    uint32_t                    cSoftIntBreakpoints;

    /** The number of selected events. */
    uint32_t                    cSelectedEvents;

    /** The number of enabled hardware breakpoints. */
    uint8_t                     cEnabledHwBreakpoints;
    /** The number of enabled hardware I/O breakpoints. */
    uint8_t                     cEnabledHwIoBreakpoints;
    uint8_t                     au8Alignment1[2]; /**< Alignment padding. */
    /** The number of enabled INT3 breakpoints. */
    uint32_t volatile           cEnabledInt3Breakpoints;

    /** Debugger Attached flag.
     * Set if a debugger is attached, elsewise it's clear.
     */
    bool volatile               fAttached;

    /** Stepping filtering. */
    struct
    {
        /** The CPU doing the stepping.
         * Set to NIL_VMCPUID when filtering is inactive */
        VMCPUID                 idCpu;
        /** The specified flags. */
        uint32_t                fFlags;
        /** The effective PC address to stop at, if given. */
        RTGCPTR                 AddrPc;
        /** The lowest effective stack address to stop at.
         * Together with cbStackPop, this forms a range of effective stack pointer
         * addresses that we stop for.   */
        RTGCPTR                 AddrStackPop;
        /** The size of the stack stop area starting at AddrStackPop. */
        RTGCPTR                 cbStackPop;
        /** Maximum number of steps. */
        uint32_t                cMaxSteps;

        /** Number of steps made thus far. */
        uint32_t                cSteps;
        /** Current call counting balance for step-over handling. */
        uint32_t                uCallDepth;

        uint32_t                u32Padding; /**< Alignment padding. */

    } SteppingFilter;

    uint32_t                    au32Alignment2[2]; /**< Alignment padding. */

    /** @name Breakpoint handling related state.
     * @{ */
    /** Array of hardware breakpoints (0..3).
     * This is shared among all the CPUs because life is much simpler that way. */
    DBGFBPHW                    aHwBreakpoints[4];
    /** @} */

    /**
     * Bug check configuration and data.
     * @note The data will not be reset on reset.
     */
    struct
    {
        /** Whether to power off the VM on BSOD.   */
        bool                    fCfgPowerOffOnBsod;
        /** Whether to suspend the VM on BSOD.   */
        bool                    fCfgSuspendOnBsod;
        /** Explicit padding. */
        bool                    afReserved[2];

        /** The ID of the CPU reporting it. */
        VMCPUID                 idCpu;
        /** The event associated with the bug check (gives source).
         * This is set to DBGFEVENT_END if no BSOD data here. */
        DBGFEVENTTYPE           enmEvent;
        /** The total reset count at the time (VMGetResetCount). */
        uint32_t                uResetNo;
        /** When it was reported (TMVirtualGet). */
        uint64_t                uTimestamp;
        /** The bug check number.
         * @note This is really just 32-bit wide, see KeBugCheckEx.  */
        uint64_t                uBugCheck;
        /** The bug check parameters. */
        uint64_t                auParameters[4];
    } BugCheck;
} DBGF;
AssertCompileMemberAlignment(DBGF, aHwBreakpoints, 8);
AssertCompileMemberAlignment(DBGF, bmHardIntBreakpoints, 8);
/** Pointer to DBGF Data. */
typedef DBGF *PDBGF;


/**
 * Event state (for DBGFCPU::aEvents).
 */
typedef enum DBGFEVENTSTATE
{
    /** Invalid event stack entry. */
    DBGFEVENTSTATE_INVALID = 0,
    /** The current event stack entry. */
    DBGFEVENTSTATE_CURRENT,
    /** Event that should be ignored but hasn't yet actually been ignored. */
    DBGFEVENTSTATE_IGNORE,
    /** Event that has been ignored but may be restored to IGNORE should another
     * debug event fire before the instruction is completed. */
    DBGFEVENTSTATE_RESTORABLE,
    /** End of valid events.   */
    DBGFEVENTSTATE_END,
    /** Make sure we've got a 32-bit type. */
    DBGFEVENTSTATE_32BIT_HACK = 0x7fffffff
} DBGFEVENTSTATE;


/** Converts a DBGFCPU pointer into a VM pointer. */
#define DBGFCPU_2_VM(pDbgfCpu) ((PVM)((uint8_t *)(pDbgfCpu) + (pDbgfCpu)->offVM))

/**
 * The per CPU data for DBGF.
 */
typedef struct DBGFCPU
{
    /** The offset into the VM structure.
     * @see DBGFCPU_2_VM(). */
    uint32_t                offVM;

    /** Flag whether the to invoke any owner handlers in ring-3 before dropping into the debugger. */
    bool                    fBpInvokeOwnerCallback;
    /** Set if we're singlestepping in raw mode.
     * This is checked and cleared in the \#DB handler. */
    bool                    fSingleSteppingRaw;
    /** Flag whether an I/O breakpoint is pending. */
    bool                    fBpIoActive;
    /** Flagh whether the I/O breakpoint hit before the access or after. */
    bool                    fBpIoBefore;
    /** Current active breakpoint handle.
     * This is NIL_DBGFBP if not active. It is set when a execution engine
     * encounters a breakpoint and returns VINF_EM_DBG_BREAKPOINT.
     *
     * @todo drop this in favor of aEvents!  */
    DBGFBP                  hBpActive;
    /** The access mask for a pending I/O breakpoint. */
    uint32_t                fBpIoAccess;
    /** The address of the access. */
    uint64_t                uBpIoAddress;
    /** The value of the access. */
    uint64_t                uBpIoValue;

    /** The number of events on the stack (aEvents).
     * The pending event is the last one (aEvents[cEvents - 1]), but only when
     * enmState is DBGFEVENTSTATE_CURRENT. */
    uint32_t                cEvents;
    /** Events - current, ignoring and ignored.
     *
     * We maintain a stack of events in order to try avoid ending up in an infinit
     * loop when resuming after an event fired.  There are cases where we may end
     * generating additional events before the instruction can be executed
     * successfully.  Like for instance an XCHG on MMIO with separate read and write
     * breakpoints, or a MOVSB instruction working on breakpointed MMIO as both
     * source and destination.
     *
     * So, when resuming after dropping into the debugger for an event, we convert
     * the DBGFEVENTSTATE_CURRENT event into a DBGFEVENTSTATE_IGNORE event, leaving
     * cEvents unchanged.  If the event is reported again, we will ignore it and
     * tell the reporter to continue executing.  The event change to the
     * DBGFEVENTSTATE_RESTORABLE state.
     *
     * Currently, the event reporter has to figure out that it is a nested event and
     * tell DBGF to restore DBGFEVENTSTATE_RESTORABLE events (and keep
     * DBGFEVENTSTATE_IGNORE, should they happen out of order for some weird
     * reason).
     */
    struct
    {
        /** The event details. */
        DBGFEVENT           Event;
        /** The RIP at which this happend (for validating ignoring). */
        uint64_t            rip;
        /** The event state. */
        DBGFEVENTSTATE      enmState;
        /** Alignment padding. */
        uint32_t            u32Alignment;
    } aEvents[3];
} DBGFCPU;
AssertCompileMemberAlignment(DBGFCPU, aEvents, 8);
AssertCompileMemberSizeAlignment(DBGFCPU, aEvents[0], 8);
/** Pointer to DBGFCPU data. */
typedef DBGFCPU *PDBGFCPU;

struct DBGFOSEMTWRAPPER;

/**
 * DBGF data kept in the ring-0 GVM.
 */
typedef struct DBGFR0PERVM
{
    /** Pointer to the tracer instance if enabled. */
    R0PTRTYPE(struct DBGFTRACERINSR0 *) pTracerR0;

    /** @name Breakpoint handling related state, Ring-0 only part.
     * @{ */
    /** The breakpoint owner table memory object. */
    RTR0MEMOBJ                          hMemObjBpOwners;
    /** The breakpoint owner table mapping object. */
    RTR0MEMOBJ                          hMapObjBpOwners;
    /** Base pointer to the breakpoint owners table. */
    R0PTRTYPE(PDBGFBPOWNERINTR0)        paBpOwnersR0;

    /** Global breakpoint table chunk array. */
    DBGFBPCHUNKR0                       aBpChunks[DBGF_BP_CHUNK_COUNT];
    /** Breakpoint L2 lookup table chunk array. */
    DBGFBPL2TBLCHUNKR0                  aBpL2TblChunks[DBGF_BP_L2_TBL_CHUNK_COUNT];
    /** The L1 lookup tables memory object. */
    RTR0MEMOBJ                          hMemObjBpLocL1;
    /** The L1 lookup tables mapping object. */
    RTR0MEMOBJ                          hMapObjBpLocL1;
    /** The I/O port breakpoint lookup tables memory object. */
    RTR0MEMOBJ                          hMemObjBpLocPortIo;
    /** The I/O port breakpoint lookup tables mapping object. */
    RTR0MEMOBJ                          hMapObjBpLocPortIo;
    /** Base pointer to the L1 locator table. */
    R0PTRTYPE(volatile uint32_t *)      paBpLocL1R0;
    /** Base pointer to the L1 locator table. */
    R0PTRTYPE(volatile uint32_t *)      paBpLocPortIoR0;
    /** Flag whether the breakpoint manager was initialized (on demand). */
    bool                                fInit;
    /** @} */
} DBGFR0PERVM;

/**
 * The DBGF data kept in the UVM.
 */
typedef struct DBGFUSERPERVM
{
    /** The address space database lock. */
    RTSEMRW                     hAsDbLock;
    /** The address space handle database.      (Protected by hAsDbLock.) */
    R3PTRTYPE(AVLPVTREE)        AsHandleTree;
    /** The address space process id database.  (Protected by hAsDbLock.) */
    R3PTRTYPE(AVLU32TREE)       AsPidTree;
    /** The address space name database.        (Protected by hAsDbLock.) */
    R3PTRTYPE(RTSTRSPACE)       AsNameSpace;
    /** Special address space aliases.          (Protected by hAsDbLock.) */
    RTDBGAS volatile            ahAsAliases[DBGF_AS_COUNT];
    /** For lazily populating the aliased address spaces. */
    bool volatile               afAsAliasPopuplated[DBGF_AS_COUNT];
    /** Alignment padding. */
    bool                        afAlignment1[2];
    /** Debug configuration. */
    R3PTRTYPE(RTDBGCFG)         hDbgCfg;

    /** The register database lock. */
    RTSEMRW                     hRegDbLock;
    /** String space for looking up registers.  (Protected by hRegDbLock.) */
    R3PTRTYPE(RTSTRSPACE)       RegSpace;
    /** String space holding the register sets. (Protected by hRegDbLock.)  */
    R3PTRTYPE(RTSTRSPACE)       RegSetSpace;
    /** The number of registers (aliases, sub-fields and the special CPU
     * register aliases (eg AH) are not counted). */
    uint32_t                    cRegs;
    /** Number of registers per CPU. */
    uint16_t                    cPerCpuRegs;
    /** Number of hypervisor register per CPU. */
    uint8_t                     cPerCpuHyperRegs;
    /** For early initialization by . */
    bool volatile               fRegDbInitialized;

    /** Critical section protecting the Guest OS Digger data, the info handlers
     * and the plugins.  These share to give the best possible plugin unload
     * race protection. */
    RTCRITSECTRW                CritSect;
    /** Head of the LIFO of loaded DBGF plugins. */
    R3PTRTYPE(struct DBGFPLUGIN *) pPlugInHead;
    /** The current Guest OS digger. */
    R3PTRTYPE(PDBGFOS)          pCurOS;
    /** The head of the Guest OS digger instances. */
    R3PTRTYPE(PDBGFOS)          pOSHead;
    /** List of registered info handlers. */
    R3PTRTYPE(PDBGFINFO)        pInfoFirst;

    /** The configured tracer. */
    PDBGFTRACERINSR3            pTracerR3;

    /** @name VM -> Debugger event communication.
     * @{ */
    /** The event semaphore the debugger waits on for new events to arrive. */
    RTSEMEVENT                  hEvtWait;
    /** Multi event semaphore the vCPUs wait on in case the debug event ringbuffer is
     * full and require growing (done from the thread waiting for events). */
    RTSEMEVENTMULTI             hEvtRingBufFull;
    /** Fast mutex protecting the event ring from concurrent write accesses by multiple vCPUs. */
    RTSEMFASTMUTEX              hMtxDbgEvtWr;
    /** Ringbuffer of events, dynamically allocated based on the number of available vCPUs
     * (+ some safety entries). */
    PDBGFEVENT                  paDbgEvts;
    /** Number of entries in the event ring buffer. */
    uint32_t                    cDbgEvtMax;
    /** Next free entry to write to (vCPU thread). */
    volatile uint32_t           idxDbgEvtWrite;
    /** Next event entry to from (debugger thread). */
    volatile uint32_t           idxDbgEvtRead;
    /** @} */

    /** @name Breakpoint handling related state.
     * @{ */
    /** Base pointer to the breakpoint owners table. */
    R3PTRTYPE(PDBGFBPOWNERINT)      paBpOwnersR3;
    /** Pointer to the bitmap denoting occupied owner entries. */
    R3PTRTYPE(volatile void *)      pbmBpOwnersAllocR3;

    /** Global breakpoint table chunk array. */
    DBGFBPCHUNKR3                   aBpChunks[DBGF_BP_CHUNK_COUNT];
    /** Breakpoint L2 lookup table chunk array. */
    DBGFBPL2TBLCHUNKR3              aBpL2TblChunks[DBGF_BP_L2_TBL_CHUNK_COUNT];
    /** Base pointer to the L1 locator table. */
    R3PTRTYPE(volatile uint32_t *)  paBpLocL1R3;
    /** Base pointer to the Port I/O breakpoint locator table. */
    R3PTRTYPE(volatile uint32_t *)  paBpLocPortIoR3;
    /** Fast mutex protecting the L2 table from concurrent write accesses (EMTs
     * can still do read accesses without holding it while traversing the trees). */
    RTSEMFASTMUTEX                  hMtxBpL2Wr;
    /** Number of armed port I/O breakpoints. */
    volatile uint32_t               cPortIoBps;
    /** @} */

    /** The type database lock. */
    RTSEMRW                     hTypeDbLock;
    /** String space for looking up types.  (Protected by hTypeDbLock.) */
    R3PTRTYPE(RTSTRSPACE)       TypeSpace;
    /** For early initialization by . */
    bool volatile               fTypeDbInitialized;
    /** Alignment padding. */
    bool                        afAlignment3[3];

} DBGFUSERPERVM;
typedef DBGFUSERPERVM *PDBGFUSERPERVM;
typedef DBGFUSERPERVM const *PCDBGFUSERPERVM;

/**
 * The per-CPU DBGF data kept in the UVM.
 */
typedef struct DBGFUSERPERVMCPU
{
    /** The guest register set for this CPU.  Can be NULL. */
    R3PTRTYPE(struct DBGFREGSET *)  pGuestRegSet;
    /** The hypervisor register set for this CPU.  Can be NULL. */
    R3PTRTYPE(struct DBGFREGSET *)  pHyperRegSet;

    /** @name Debugger -> vCPU command communication.
     * @{ */
    /** Flag whether this vCPU is currently stopped waiting in the debugger. */
    bool volatile                   fStopped;
    /** The Command to the vCPU.
     * Operated in an atomic fashion since the vCPU will poll on this.
     * This means that a the command data must be written before this member
     * is set. The VMM will reset this member to the no-command state
     * when it have processed it.
     */
    DBGFCMD volatile                enmDbgfCmd;
    /** The Command data.
     * Not all commands take data. */
    DBGFCMDDATA                     DbgfCmdData;
    /** @} */

} DBGFUSERPERVMCPU;


#ifdef IN_RING3
int  dbgfR3AsInit(PUVM pUVM);
void dbgfR3AsTerm(PUVM pUVM);
void dbgfR3AsRelocate(PUVM pUVM, RTGCUINTPTR offDelta);
DECLHIDDEN(int) dbgfR3BpInit(PUVM pUVM);
DECLHIDDEN(int) dbgfR3BpTerm(PUVM pUVM);
int  dbgfR3InfoInit(PUVM pUVM);
int  dbgfR3InfoTerm(PUVM pUVM);
int  dbgfR3OSInit(PUVM pUVM);
void dbgfR3OSTermPart1(PUVM pUVM);
void dbgfR3OSTermPart2(PUVM pUVM);
int  dbgfR3OSStackUnwindAssist(PUVM pUVM, VMCPUID idCpu, PDBGFSTACKFRAME pFrame, PRTDBGUNWINDSTATE pState,
                               PCCPUMCTX pInitialCtx, RTDBGAS hAs, uint64_t *puScratch);
int  dbgfR3RegInit(PUVM pUVM);
void dbgfR3RegTerm(PUVM pUVM);
int  dbgfR3TraceInit(PVM pVM);
void dbgfR3TraceRelocate(PVM pVM);
void dbgfR3TraceTerm(PVM pVM);
DECLHIDDEN(int)  dbgfR3TypeInit(PUVM pUVM);
DECLHIDDEN(void) dbgfR3TypeTerm(PUVM pUVM);
int  dbgfR3PlugInInit(PUVM pUVM);
void dbgfR3PlugInTerm(PUVM pUVM);
int  dbgfR3BugCheckInit(PVM pVM);
DECLHIDDEN(int) dbgfR3TracerInit(PVM pVM);
DECLHIDDEN(void) dbgfR3TracerTerm(PVM pVM);

/**
 * DBGF disassembler state (substate of DISSTATE).
 */
typedef struct DBGFDISSTATE
{
    /** Pointer to the current instruction. */
    PCDISOPCODE     pCurInstr;
    /** Size of the instruction in bytes. */
    uint32_t        cbInstr;
    /** Parameters.  */
    DISOPPARAM      Param1;
    DISOPPARAM      Param2;
    DISOPPARAM      Param3;
    DISOPPARAM      Param4;
} DBGFDISSTATE;
/** Pointer to a DBGF disassembler state. */
typedef DBGFDISSTATE *PDBGFDISSTATE;

DECLHIDDEN(int) dbgfR3DisasInstrStateEx(PUVM pUVM, VMCPUID idCpu, PDBGFADDRESS pAddr, uint32_t fFlags,
                                        char *pszOutput, uint32_t cbOutput, PDBGFDISSTATE pDisState);

#endif /* IN_RING3 */

#ifdef IN_RING0
DECLHIDDEN(void) dbgfR0TracerDestroy(PGVM pGVM, PDBGFTRACERINSR0 pTracer);
DECLHIDDEN(void) dbgfR0BpInit(PGVM pGVM);
DECLHIDDEN(void) dbgfR0BpDestroy(PGVM pGVM);
#endif /* !IN_RING0 */

/** @} */

#endif /* !VMM_INCLUDED_SRC_include_DBGFInternal_h */
