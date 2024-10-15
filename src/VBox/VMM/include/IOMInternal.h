/* $Id: IOMInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IOM - Internal header file.
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

#ifndef VMM_INCLUDED_SRC_include_IOMInternal_h
#define VMM_INCLUDED_SRC_include_IOMInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#define IOM_WITH_CRIT_SECT_RW

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/pdmcritsect.h>
#ifdef IOM_WITH_CRIT_SECT_RW
# include <VBox/vmm/pdmcritsectrw.h>
#endif
#include <VBox/param.h>
#include <iprt/assert.h>
#include <iprt/avl.h>



/** @defgroup grp_iom_int   Internals
 * @ingroup grp_iom
 * @internal
 * @{
 */

/**
 * I/O port lookup table entry.
 */
typedef struct IOMIOPORTLOOKUPENTRY
{
    /** The first port in the range. */
    RTIOPORT                    uFirstPort;
    /** The last port in the range (inclusive). */
    RTIOPORT                    uLastPort;
    /** The registration handle/index. */
    uint16_t                    idx;
} IOMIOPORTLOOKUPENTRY;
/** Pointer to an I/O port lookup table entry. */
typedef IOMIOPORTLOOKUPENTRY *PIOMIOPORTLOOKUPENTRY;
/** Pointer to a const I/O port lookup table entry. */
typedef IOMIOPORTLOOKUPENTRY const *PCIOMIOPORTLOOKUPENTRY;

/**
 * Ring-0 I/O port handle table entry.
 */
typedef struct IOMIOPORTENTRYR0
{
    /** Pointer to user argument. */
    RTR0PTR                             pvUser;
    /** Pointer to the associated device instance, NULL if entry not used. */
    R0PTRTYPE(PPDMDEVINS)               pDevIns;
    /** Pointer to OUT callback function. */
    R0PTRTYPE(PFNIOMIOPORTNEWOUT)       pfnOutCallback;
    /** Pointer to IN callback function. */
    R0PTRTYPE(PFNIOMIOPORTNEWIN)        pfnInCallback;
    /** Pointer to string OUT callback function. */
    R0PTRTYPE(PFNIOMIOPORTNEWOUTSTRING) pfnOutStrCallback;
    /** Pointer to string IN callback function. */
    R0PTRTYPE(PFNIOMIOPORTNEWINSTRING)  pfnInStrCallback;
    /** The entry of the first statistics entry, UINT16_MAX if no stats. */
    uint16_t                            idxStats;
    /** The number of ports covered by this entry, 0 if entry not used. */
    RTIOPORT                            cPorts;
    /** Same as the handle index. */
    uint16_t                            idxSelf;
    /** IOM_IOPORT_F_XXX (copied from ring-3). */
    uint16_t                            fFlags;
} IOMIOPORTENTRYR0;
/** Pointer to a ring-0 I/O port handle table entry. */
typedef IOMIOPORTENTRYR0 *PIOMIOPORTENTRYR0;
/** Pointer to a const ring-0 I/O port handle table entry. */
typedef IOMIOPORTENTRYR0 const *PCIOMIOPORTENTRYR0;

/**
 * Ring-3 I/O port handle table entry.
 */
typedef struct IOMIOPORTENTRYR3
{
    /** Pointer to user argument. */
    RTR3PTR                             pvUser;
    /** Pointer to the associated device instance. */
    R3PTRTYPE(PPDMDEVINS)               pDevIns;
    /** Pointer to OUT callback function. */
    R3PTRTYPE(PFNIOMIOPORTNEWOUT)       pfnOutCallback;
    /** Pointer to IN callback function. */
    R3PTRTYPE(PFNIOMIOPORTNEWIN)        pfnInCallback;
    /** Pointer to string OUT callback function. */
    R3PTRTYPE(PFNIOMIOPORTNEWOUTSTRING) pfnOutStrCallback;
    /** Pointer to string IN callback function. */
    R3PTRTYPE(PFNIOMIOPORTNEWINSTRING)  pfnInStrCallback;
    /** Description / Name. For easing debugging. */
    R3PTRTYPE(const char *)             pszDesc;
    /** Extended port description table, optional. */
    R3PTRTYPE(PCIOMIOPORTDESC)          paExtDescs;
    /** PCI device the registration is associated with. */
    R3PTRTYPE(PPDMPCIDEV)               pPciDev;
    /** The PCI device region (high 16-bit word) and subregion (low word),
     *  UINT32_MAX if not applicable. */
    uint32_t                            iPciRegion;
    /** The number of ports covered by this entry. */
    RTIOPORT                            cPorts;
    /** The current port mapping (duplicates lookup table). */
    RTIOPORT                            uPort;
    /** The entry of the first statistics entry, UINT16_MAX if no stats. */
    uint16_t                            idxStats;
    /** Set if mapped, clear if not.
     * Only updated when critsect is held exclusively.   */
    bool                                fMapped;
    /** Set if there is an ring-0 entry too. */
    bool                                fRing0;
    /** Set if there is an raw-mode entry too. */
    bool                                fRawMode;
    /** IOM_IOPORT_F_XXX */
    uint8_t                             fFlags;
    /** Same as the handle index. */
    uint16_t                            idxSelf;
} IOMIOPORTENTRYR3;
AssertCompileSize(IOMIOPORTENTRYR3, 9 * sizeof(RTR3PTR) + 16);
/** Pointer to a ring-3 I/O port handle table entry. */
typedef IOMIOPORTENTRYR3 *PIOMIOPORTENTRYR3;
/** Pointer to a const ring-3 I/O port handle table entry. */
typedef IOMIOPORTENTRYR3 const *PCIOMIOPORTENTRYR3;

/**
 * I/O port statistics entry (one I/O port).
 */
typedef struct IOMIOPORTSTATSENTRY
{
    /** All accesses (only updated for the first port in a range). */
    STAMCOUNTER                 Total;

    /** Number of INs to this port from R3. */
    STAMCOUNTER                 InR3;
    /** Profiling IN handler overhead in R3. */
    STAMPROFILE                 ProfInR3;
    /** Number of OUTs to this port from R3. */
    STAMCOUNTER                 OutR3;
    /** Profiling OUT handler overhead in R3. */
    STAMPROFILE                 ProfOutR3;

    /** Number of INs to this port from R0/RC. */
    STAMCOUNTER                 InRZ;
    /** Profiling IN handler overhead in R0/RC. */
    STAMPROFILE                 ProfInRZ;
    /** Number of INs to this port from R0/RC which was serviced in R3. */
    STAMCOUNTER                 InRZToR3;

    /** Number of OUTs to this port from R0/RC. */
    STAMCOUNTER                 OutRZ;
    /** Profiling OUT handler overhead in R0/RC. */
    STAMPROFILE                 ProfOutRZ;
    /** Number of OUTs to this port from R0/RC which was serviced in R3. */
    STAMCOUNTER                 OutRZToR3;
} IOMIOPORTSTATSENTRY;
/** Pointer to I/O port statistics entry. */
typedef IOMIOPORTSTATSENTRY *PIOMIOPORTSTATSENTRY;



/**
 * MMIO lookup table entry.
 */
typedef struct IOMMMIOLOOKUPENTRY
{
    /** The first port in the range. */
    RTGCPHYS                    GCPhysFirst;
    /** The last port in the range (inclusive). */
    RTGCPHYS                    GCPhysLast;
    /** The registration handle/index.
     * @todo bake this into the lower/upper bits of GCPhysFirst & GCPhysLast. */
    uint16_t                    idx;
    uint16_t                    abPadding[3];
} IOMMMIOLOOKUPENTRY;
/** Pointer to an MMIO lookup table entry. */
typedef IOMMMIOLOOKUPENTRY *PIOMMMIOLOOKUPENTRY;
/** Pointer to a const MMIO lookup table entry. */
typedef IOMMMIOLOOKUPENTRY const *PCIOMMMIOLOOKUPENTRY;

/**
 * Ring-0 MMIO handle table entry.
 */
typedef struct IOMMMIOENTRYR0
{
    /** The number of bytes covered by this entry, 0 if entry not used. */
    RTGCPHYS                            cbRegion;
    /** Pointer to user argument. */
    RTR0PTR                             pvUser;
    /** Pointer to the associated device instance, NULL if entry not used. */
    R0PTRTYPE(PPDMDEVINS)               pDevIns;
    /** Pointer to the write callback function. */
    R0PTRTYPE(PFNIOMMMIONEWWRITE)       pfnWriteCallback;
    /** Pointer to the read callback function. */
    R0PTRTYPE(PFNIOMMMIONEWREAD)        pfnReadCallback;
    /** Pointer to the fill callback function. */
    R0PTRTYPE(PFNIOMMMIONEWFILL)        pfnFillCallback;
    /** The entry of the first statistics entry, UINT16_MAX if no stats.
     * @note For simplicity, this is always copied from ring-3 for all entries at
     *       the end of VM creation. */
    uint16_t                            idxStats;
    /** Same as the handle index. */
    uint16_t                            idxSelf;
    /** IOM_MMIO_F_XXX (copied from ring-3). */
    uint32_t                            fFlags;
} IOMMMIOENTRYR0;
/** Pointer to a ring-0 MMIO handle table entry. */
typedef IOMMMIOENTRYR0 *PIOMMMIOENTRYR0;
/** Pointer to a const ring-0 MMIO handle table entry. */
typedef IOMMMIOENTRYR0 const *PCIOMMMIOENTRYR0;

/**
 * Ring-3 MMIO handle table entry.
 */
typedef struct IOMMMIOENTRYR3
{
    /** The number of bytes covered by this entry. */
    RTGCPHYS                            cbRegion;
    /** The current mapping address (duplicates lookup table).
     * This is set to NIL_RTGCPHYS if not mapped (exclusive lock + atomic). */
    RTGCPHYS volatile                   GCPhysMapping;
    /** Pointer to user argument. */
    RTR3PTR                             pvUser;
    /** Pointer to the associated device instance. */
    R3PTRTYPE(PPDMDEVINS)               pDevIns;
    /** Pointer to the write callback function. */
    R3PTRTYPE(PFNIOMMMIONEWWRITE)       pfnWriteCallback;
    /** Pointer to the read callback function. */
    R3PTRTYPE(PFNIOMMMIONEWREAD)        pfnReadCallback;
    /** Pointer to the fill callback function. */
    R3PTRTYPE(PFNIOMMMIONEWFILL)        pfnFillCallback;
    /** Description / Name. For easing debugging. */
    R3PTRTYPE(const char *)             pszDesc;
    /** PCI device the registration is associated with. */
    R3PTRTYPE(PPDMPCIDEV)               pPciDev;
    /** The PCI device region (high 16-bit word) and subregion (low word),
     *  UINT32_MAX if not applicable. */
    uint32_t                            iPciRegion;
    /** IOM_MMIO_F_XXX */
    uint32_t                            fFlags;
    /** The entry of the first statistics entry, UINT16_MAX if no stats. */
    uint16_t                            idxStats;
    /** Set if mapped, clear if not.
     * Only updated when critsect is held exclusively.
     * @todo remove as GCPhysMapping != NIL_RTGCPHYS serves the same purpose. */
    bool volatile                       fMapped;
    /** Set if there is an ring-0 entry too. */
    bool                                fRing0 : 1;
    /** Set if there is an raw-mode entry too. */
    bool                                fRawMode : 1;
    bool                                fPadding : 6;
    /** Pre-registered ad-hoc RAM range ID. */
    uint16_t                            idRamRange;
    /** Same as the handle index. */
    uint16_t                            idxSelf;
} IOMMMIOENTRYR3;
AssertCompileSize(IOMMMIOENTRYR3, sizeof(RTGCPHYS) * 2 + 7 * sizeof(RTR3PTR) + 16);
/** Pointer to a ring-3 MMIO handle table entry. */
typedef IOMMMIOENTRYR3 *PIOMMMIOENTRYR3;
/** Pointer to a const ring-3 MMIO handle table entry. */
typedef IOMMMIOENTRYR3 const *PCIOMMMIOENTRYR3;

/**
 * MMIO statistics entry (one MMIO).
 */
typedef struct IOMMMIOSTATSENTRY
{
    /** Counting and profiling reads in R0/RC. */
    STAMPROFILE                 ProfReadRZ;
    /** Number of successful read accesses. */
    STAMCOUNTER                 Reads;
    /** Number of reads to this address from R0/RC which was serviced in R3. */
    STAMCOUNTER                 ReadRZToR3;
    /** Number of complicated reads. */
    STAMCOUNTER                 ComplicatedReads;
    /** Number of reads of 0xff or 0x00. */
    STAMCOUNTER                 FFor00Reads;
    /** Profiling read handler overhead in R3. */
    STAMPROFILE                 ProfReadR3;

    /** Counting and profiling writes in R0/RC. */
    STAMPROFILE                 ProfWriteRZ;
    /** Number of successful read accesses. */
    STAMCOUNTER                 Writes;
    /** Number of writes to this address from R0/RC which was serviced in R3. */
    STAMCOUNTER                 WriteRZToR3;
    /** Number of writes to this address from R0/RC which was committed in R3. */
    STAMCOUNTER                 CommitRZToR3;
    /** Number of complicated writes. */
    STAMCOUNTER                 ComplicatedWrites;
    /** Profiling write handler overhead in R3. */
    STAMPROFILE                 ProfWriteR3;
} IOMMMIOSTATSENTRY;
/** Pointer to MMIO statistics entry. */
typedef IOMMMIOSTATSENTRY *PIOMMMIOSTATSENTRY;


/**
 * IOM per virtual CPU instance data.
 */
typedef struct IOMCPU
{
    /**
     * Pending I/O port write commit (VINF_IOM_R3_IOPORT_COMMIT_WRITE).
     *
     * This is a converted VINF_IOM_R3_IOPORT_WRITE handler return that lets the
     * execution engine commit the instruction and then return to ring-3 to complete
     * the I/O port write there.  This avoids having to decode the instruction again
     * in ring-3.
     */
    struct
    {
        /** The value size (0 if not pending). */
        uint16_t                        cbValue;
        /** The I/O port. */
        RTIOPORT                        IOPort;
        /** The value. */
        uint32_t                        u32Value;
    } PendingIOPortWrite;

    /**
     * Pending MMIO write commit (VINF_IOM_R3_MMIO_COMMIT_WRITE).
     *
     * This is a converted VINF_IOM_R3_MMIO_WRITE handler return that lets the
     * execution engine commit the instruction, stop any more REPs, and return to
     * ring-3 to complete the MMIO write there.  The avoid the tedious decoding of
     * the instruction again once we're in ring-3, more importantly it allows us to
     * correctly deal with read-modify-write instructions like XCHG, OR, and XOR.
     */
    struct
    {
        /** Guest physical MMIO address. */
        RTGCPHYS                        GCPhys;
        /** The number of bytes to write (0 if nothing pending). */
        uint32_t                        cbValue;
        /** Hint. */
        uint32_t                        idxMmioRegionHint;
        /** The value to write. */
        uint8_t                         abValue[128];
    } PendingMmioWrite;

    /** @name Caching of I/O Port and MMIO ranges and statistics.
     * (Saves quite some time in rep outs/ins instruction emulation.)
     * @{ */
    /** I/O port registration index for the last read operation. */
    uint16_t                            idxIoPortLastRead;
    /** I/O port registration index for the last write operation. */
    uint16_t                            idxIoPortLastWrite;
    /** I/O port registration index for the last read string operation. */
    uint16_t                            idxIoPortLastReadStr;
    /** I/O port registration index for the last write string operation. */
    uint16_t                            idxIoPortLastWriteStr;

    /** MMIO port registration index for the last IOMR3MmioPhysHandler call.
     * @note pretty static as only used by APIC on AMD-V.  */
    uint16_t                            idxMmioLastPhysHandler;
    uint16_t                            au16Padding[2];
    /** @} */

    /** MMIO recursion guard (see @bugref{10315}). */
    uint8_t                             cMmioRecursionDepth;
    uint8_t                             bPadding;
    /** The MMIO recursion stack (ring-3 version). */
    PPDMDEVINSR3                        apMmioRecursionStack[2];
} IOMCPU;
/** Pointer to IOM per virtual CPU instance data. */
typedef IOMCPU *PIOMCPU;


/**
 * IOM Data (part of VM)
 */
typedef struct IOM
{
    /** Lock serializing EMT access to IOM. */
#ifdef IOM_WITH_CRIT_SECT_RW
    PDMCRITSECTRW                   CritSect;
#else
    PDMCRITSECT                     CritSect;
#endif

    /** @name I/O ports
     * @note The updating of these variables is done exclusively from EMT(0).
     * @{ */
    /** Number of I/O port registrations. */
    uint32_t                        cIoPortRegs;
    /** The size of the paIoPortRegs allocation (in entries). */
    uint32_t                        cIoPortAlloc;
    /** I/O port registration table for ring-3.
     * There is a parallel table in ring-0, IOMR0PERVM::paIoPortRegs. */
    R3PTRTYPE(PIOMIOPORTENTRYR3)    paIoPortRegs;
    /** I/O port lookup table. */
    R3PTRTYPE(PIOMIOPORTLOOKUPENTRY) paIoPortLookup;
    /** Number of entries in the lookup table. */
    uint32_t                        cIoPortLookupEntries;
    /** Set if I/O port registrations are frozen. */
    bool                            fIoPortsFrozen;
    bool                            afPadding1[3];

    /** The number of valid entries in paioPortStats. */
    uint32_t                        cIoPortStats;
    /** The size of the paIoPortStats allocation (in entries). */
    uint32_t                        cIoPortStatsAllocation;
    /** I/O port lookup table.   */
    R3PTRTYPE(PIOMIOPORTSTATSENTRY) paIoPortStats;
    /** Dummy stats entry so we don't need to check for NULL pointers so much. */
    IOMIOPORTSTATSENTRY             IoPortDummyStats;
    /** @} */

    /** @name MMIO ports
     * @note The updating of these variables is done exclusively from EMT(0).
     * @{ */
    /** MMIO physical access handler type, new style.   */
    PGMPHYSHANDLERTYPE              hNewMmioHandlerType;
    /** Number of MMIO registrations. */
    uint32_t                        cMmioRegs;
    /** The size of the paMmioRegs allocation (in entries). */
    uint32_t                        cMmioAlloc;
    /** MMIO registration table for ring-3.
     * There is a parallel table in ring-0, IOMR0PERVM::paMmioRegs. */
    R3PTRTYPE(PIOMMMIOENTRYR3)      paMmioRegs;
    /** MMIO lookup table. */
    R3PTRTYPE(PIOMMMIOLOOKUPENTRY)  paMmioLookup;
    /** Number of entries in the lookup table. */
    uint32_t                        cMmioLookupEntries;
    /** Set if MMIO registrations are frozen. */
    bool                            fMmioFrozen;
    bool                            afPadding2[3];

    /** The number of valid entries in paioPortStats. */
    uint32_t                        cMmioStats;
    /** The size of the paMmioStats allocation (in entries). */
    uint32_t                        cMmioStatsAllocation;
    /** MMIO lookup table.   */
    R3PTRTYPE(PIOMMMIOSTATSENTRY)   paMmioStats;
    /** Dummy stats entry so we don't need to check for NULL pointers so much. */
    IOMMMIOSTATSENTRY               MmioDummyStats;
    /** @} */

    /** @name I/O Port statistics.
     * @{ */
    STAMCOUNTER                     StatIoPortIn;
    STAMCOUNTER                     StatIoPortOut;
    STAMCOUNTER                     StatIoPortInS;
    STAMCOUNTER                     StatIoPortOutS;
    STAMCOUNTER                     StatIoPortCommits;
    /** @} */

    /** @name MMIO statistics.
     * @{ */
    STAMPROFILE                     StatMmioPfHandler;
    STAMPROFILE                     StatMmioPhysHandler;
    STAMCOUNTER                     StatMmioHandlerR3;
    STAMCOUNTER                     StatMmioHandlerR0;
    STAMCOUNTER                     StatMmioReadsR0ToR3;
    STAMCOUNTER                     StatMmioWritesR0ToR3;
    STAMCOUNTER                     StatMmioCommitsR0ToR3;
    STAMCOUNTER                     StatMmioCommitsDirect;
    STAMCOUNTER                     StatMmioCommitsPgm;
    STAMCOUNTER                     StatMmioStaleMappings;
    STAMCOUNTER                     StatMmioDevLockContentionR0;
    STAMCOUNTER                     StatMmioTooDeepRecursion;
    /** @} */
} IOM;
#ifdef IOM_WITH_CRIT_SECT_RW
AssertCompileMemberAlignment(IOM, CritSect, 64);
#endif
/** Pointer to IOM instance data. */
typedef IOM *PIOM;


/**
 * IOM data kept in the ring-0 GVM.
 */
typedef struct IOMR0PERVM
{
    /** @name I/O ports
     * @{ */
    /** The higest ring-0 I/O port registration plus one. */
    uint32_t                        cIoPortMax;
    /** The size of the paIoPortRegs allocation (in entries). */
    uint32_t                        cIoPortAlloc;
    /** I/O port registration table for ring-0.
     * There is a parallel table for ring-3, paIoPortRing3Regs. */
    R0PTRTYPE(PIOMIOPORTENTRYR0)    paIoPortRegs;
    /** I/O port lookup table. */
    R0PTRTYPE(PIOMIOPORTLOOKUPENTRY) paIoPortLookup;
    /** I/O port registration table for ring-3.
     * Also mapped to ring-3 as IOM::paIoPortRegs. */
    R0PTRTYPE(PIOMIOPORTENTRYR3)    paIoPortRing3Regs;
    /** Handle to the allocation backing both the ring-0 and ring-3 registration
     * tables as well as the lookup table. */
    RTR0MEMOBJ                      hIoPortMemObj;
    /** Handle to the ring-3 mapping of the lookup and ring-3 registration table. */
    RTR0MEMOBJ                      hIoPortMapObj;
#ifdef VBOX_WITH_STATISTICS
    /** The size of the paIoPortStats allocation (in entries). */
    uint32_t                        cIoPortStatsAllocation;
    /** Prevents paIoPortStats from growing, set by IOMR0IoPortSyncStatisticsIndices(). */
    bool                            fIoPortStatsFrozen;
    /** I/O port lookup table.   */
    R0PTRTYPE(PIOMIOPORTSTATSENTRY) paIoPortStats;
    /** Handle to the allocation backing the I/O port statistics. */
    RTR0MEMOBJ                      hIoPortStatsMemObj;
    /** Handle to the ring-3 mapping of the I/O port statistics. */
    RTR0MEMOBJ                      hIoPortStatsMapObj;
#endif
    /** @} */

    /** @name MMIO
     * @{ */
    /** The higest ring-0 MMIO registration plus one. */
    uint32_t                        cMmioMax;
    /** The size of the paMmioRegs allocation (in entries). */
    uint32_t                        cMmioAlloc;
    /** MMIO registration table for ring-0.
     * There is a parallel table for ring-3, paMmioRing3Regs. */
    R0PTRTYPE(PIOMMMIOENTRYR0)      paMmioRegs;
    /** MMIO lookup table. */
    R0PTRTYPE(PIOMMMIOLOOKUPENTRY)  paMmioLookup;
    /** MMIO registration table for ring-3.
     * Also mapped to ring-3 as IOM::paMmioRegs. */
    R0PTRTYPE(PIOMMMIOENTRYR3)      paMmioRing3Regs;
    /** Handle to the allocation backing both the ring-0 and ring-3 registration
     * tables as well as the lookup table. */
    RTR0MEMOBJ                      hMmioMemObj;
    /** Handle to the ring-3 mapping of the lookup and ring-3 registration table. */
    RTR0MEMOBJ                      hMmioMapObj;
#ifdef VBOX_WITH_STATISTICS
    /** The size of the paMmioStats allocation (in entries). */
    uint32_t                        cMmioStatsAllocation;
    /* Prevents paMmioStats from growing, set by IOMR0MmioSyncStatisticsIndices(). */
    bool                            fMmioStatsFrozen;
    /** MMIO lookup table.   */
    R0PTRTYPE(PIOMMMIOSTATSENTRY)   paMmioStats;
    /** Handle to the allocation backing the MMIO statistics. */
    RTR0MEMOBJ                      hMmioStatsMemObj;
    /** Handle to the ring-3 mapping of the MMIO statistics. */
    RTR0MEMOBJ                      hMmioStatsMapObj;
#endif
    /** @} */

} IOMR0PERVM;


RT_C_DECLS_BEGIN

#ifdef IN_RING3
DECLCALLBACK(void)  iomR3IoPortInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
void                iomR3IoPortRegStats(PVM pVM, PIOMIOPORTENTRYR3 pRegEntry);
DECLCALLBACK(void)  iomR3MmioInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);
void                iomR3MmioRegStats(PVM pVM, PIOMMMIOENTRYR3 pRegEntry);
VBOXSTRICTRC        iomR3MmioCommitWorker(PVM pVM, PVMCPU pVCpu, PIOMMMIOENTRYR3 pRegEntry, RTGCPHYS offRegion); /* IOMAllMmioNew.cpp */
#endif /* IN_RING3 */
#ifdef IN_RING0
void                iomR0IoPortCleanupVM(PGVM pGVM);
void                iomR0IoPortInitPerVMData(PGVM pGVM);
void                iomR0MmioCleanupVM(PGVM pGVM);
void                iomR0MmioInitPerVMData(PGVM pGVM);
#endif

#ifndef IN_RING3
DECLCALLBACK(FNPGMRZPHYSPFHANDLER)  iomMmioPfHandlerNew;
#endif
DECLCALLBACK(FNPGMPHYSHANDLER)      iomMmioHandlerNew;

/* IOM locking helpers. */
#ifdef IOM_WITH_CRIT_SECT_RW
# define IOM_LOCK_EXCL(a_pVM)                   PDMCritSectRwEnterExcl((a_pVM), &(a_pVM)->iom.s.CritSect, VERR_SEM_BUSY)
# define IOM_UNLOCK_EXCL(a_pVM)                 do { PDMCritSectRwLeaveExcl((a_pVM), &(a_pVM)->iom.s.CritSect); } while (0)
# if 0 /* (in case needed for debugging) */
# define IOM_LOCK_SHARED_EX(a_pVM, a_rcBusy)    PDMCritSectRwEnterExcl(&(a_pVM)->iom.s.CritSect, (a_rcBusy))
# define IOM_UNLOCK_SHARED(a_pVM)               do { PDMCritSectRwLeaveExcl(&(a_pVM)->iom.s.CritSect); } while (0)
# define IOM_IS_SHARED_LOCK_OWNER(a_pVM)        PDMCritSectRwIsWriteOwner(&(a_pVM)->iom.s.CritSect)
# else
# define IOM_LOCK_SHARED_EX(a_pVM, a_rcBusy)    PDMCritSectRwEnterShared((a_pVM), &(a_pVM)->iom.s.CritSect, (a_rcBusy))
# define IOM_UNLOCK_SHARED(a_pVM)               do { PDMCritSectRwLeaveShared((a_pVM), &(a_pVM)->iom.s.CritSect); } while (0)
# define IOM_IS_SHARED_LOCK_OWNER(a_pVM)        PDMCritSectRwIsReadOwner((a_pVM), &(a_pVM)->iom.s.CritSect, true)
# endif
# define IOM_IS_EXCL_LOCK_OWNER(a_pVM)          PDMCritSectRwIsWriteOwner((a_pVM), &(a_pVM)->iom.s.CritSect)
#else
# define IOM_LOCK_EXCL(a_pVM)                   PDMCritSectEnter((a_pVM), &(a_pVM)->iom.s.CritSect, VERR_SEM_BUSY)
# define IOM_UNLOCK_EXCL(a_pVM)                 do { PDMCritSectLeave((a_pVM), &(a_pVM)->iom.s.CritSect); } while (0)
# define IOM_LOCK_SHARED_EX(a_pVM, a_rcBusy)    PDMCritSectEnter((a_pVM), &(a_pVM)->iom.s.CritSect, (a_rcBusy))
# define IOM_UNLOCK_SHARED(a_pVM)               do { PDMCritSectLeave((a_pVM), &(a_pVM)->iom.s.CritSect); } while (0)
# define IOM_IS_SHARED_LOCK_OWNER(a_pVM)        PDMCritSectIsOwner((a_pVM), &(a_pVM)->iom.s.CritSect)
# define IOM_IS_EXCL_LOCK_OWNER(a_pVM)          PDMCritSectIsOwner((a_pVM), &(a_pVM)->iom.s.CritSect)
#endif
#define IOM_LOCK_SHARED(a_pVM)                  IOM_LOCK_SHARED_EX(a_pVM, VERR_SEM_BUSY)


RT_C_DECLS_END


#ifdef IN_RING3

#endif

/** @} */

#endif /* !VMM_INCLUDED_SRC_include_IOMInternal_h */

