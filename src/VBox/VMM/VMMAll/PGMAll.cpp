/* $Id: PGMAll.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PGM - Page Manager and Monitor - All context code.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_PGM
#define VBOX_WITHOUT_PAGING_BIT_FIELDS /* 64-bit bitfields are just asking for trouble. See @bugref{9841} and others. */
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/iom.h>
#include <VBox/sup.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/hm_vmx.h>
#include "PGMInternal.h"
#include <VBox/vmm/vmcc.h>
#include "PGMInline.h"
#include <iprt/assert.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/string.h>
#include <VBox/log.h>
#include <VBox/param.h>
#include <VBox/err.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
DECLINLINE(int) pgmShwGetLongModePDPtr(PVMCPUCC pVCpu, RTGCPTR64 GCPtr, PX86PML4E *ppPml4e, PX86PDPT *ppPdpt, PX86PDPAE *ppPD);
DECLINLINE(int) pgmShwGetPaePoolPagePD(PVMCPUCC pVCpu, RTGCPTR GCPtr, PPGMPOOLPAGE *ppShwPde);
DECLINLINE(int) pgmGstMapCr3(PVMCPUCC pVCpu, RTGCPHYS GCPhysCr3, PRTHCPTR pHCPtrGuestCr3);
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
static int pgmGstSlatWalk(PVMCPUCC pVCpu, RTGCPHYS GCPhysNested, bool fIsLinearAddrValid, RTGCPTR GCPtrNested, PPGMPTWALK pWalk,
                          PPGMPTWALKGST pGstWalk);
static int pgmGstSlatTranslateCr3(PVMCPUCC pVCpu, uint64_t uCr3, PRTGCPHYS pGCPhysCr3);
static int pgmShwGetNestedEPTPDPtr(PVMCPUCC pVCpu, RTGCPTR64 GCPhysNested, PEPTPDPT *ppPdpt, PEPTPD *ppPD,
                                   PPGMPTWALKGST pGstWalkAll);
#endif
static int pgmShwSyncLongModePDPtr(PVMCPUCC pVCpu, RTGCPTR64 GCPtr, X86PGPAEUINT uGstPml4e, X86PGPAEUINT uGstPdpe, PX86PDPAE *ppPD);
static int pgmShwGetEPTPDPtr(PVMCPUCC pVCpu, RTGCPTR64 GCPtr, PEPTPDPT *ppPdpt, PEPTPD *ppPD);
#ifdef PGM_WITH_PAGE_ZEROING_DETECTION
static bool pgmHandlePageZeroingCode(PVMCPUCC pVCpu, PCPUMCTX pCtx);
#endif


/*
 * Second level transation - EPT.
 */
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
# define PGM_SLAT_TYPE               PGM_SLAT_TYPE_EPT
# include "PGMSlatDefs.h"
# include "PGMAllGstSlatEpt.cpp.h"
# undef PGM_SLAT_TYPE
#endif


/*
 * Shadow - 32-bit mode
 */
#define PGM_SHW_TYPE                PGM_TYPE_32BIT
#define PGM_SHW_NAME(name)          PGM_SHW_NAME_32BIT(name)
#include "PGMAllShw.h"

/* Guest - real mode */
#define PGM_GST_TYPE                PGM_TYPE_REAL
#define PGM_GST_NAME(name)          PGM_GST_NAME_REAL(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_32BIT_REAL(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_32BIT_PT_FOR_PHYS
#define BTH_PGMPOOLKIND_ROOT        PGMPOOLKIND_32BIT_PD_PHYS
#include "PGMGstDefs.h"
#include "PGMAllGst.h"
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef BTH_PGMPOOLKIND_ROOT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - protected mode */
#define PGM_GST_TYPE                PGM_TYPE_PROT
#define PGM_GST_NAME(name)          PGM_GST_NAME_PROT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_32BIT_PROT(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_32BIT_PT_FOR_PHYS
#define BTH_PGMPOOLKIND_ROOT        PGMPOOLKIND_32BIT_PD_PHYS
#include "PGMGstDefs.h"
#include "PGMAllGst.h"
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef BTH_PGMPOOLKIND_ROOT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - 32-bit mode */
#define PGM_GST_TYPE                PGM_TYPE_32BIT
#define PGM_GST_NAME(name)          PGM_GST_NAME_32BIT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_32BIT_32BIT(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_32BIT_PT_FOR_32BIT_PT
#define BTH_PGMPOOLKIND_PT_FOR_BIG  PGMPOOLKIND_32BIT_PT_FOR_32BIT_4MB
#define BTH_PGMPOOLKIND_ROOT        PGMPOOLKIND_32BIT_PD
#include "PGMGstDefs.h"
#include "PGMAllGst.h"
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_BIG
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef BTH_PGMPOOLKIND_ROOT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

#undef PGM_SHW_TYPE
#undef PGM_SHW_NAME


/*
 * Shadow - PAE mode
 */
#define PGM_SHW_TYPE                PGM_TYPE_PAE
#define PGM_SHW_NAME(name)          PGM_SHW_NAME_PAE(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_PAE_REAL(name)
#include "PGMAllShw.h"

/* Guest - real mode */
#define PGM_GST_TYPE                PGM_TYPE_REAL
#define PGM_GST_NAME(name)          PGM_GST_NAME_REAL(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_PAE_REAL(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_PAE_PT_FOR_PHYS
#define BTH_PGMPOOLKIND_ROOT        PGMPOOLKIND_PAE_PDPT_PHYS
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef BTH_PGMPOOLKIND_ROOT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - protected mode */
#define PGM_GST_TYPE                PGM_TYPE_PROT
#define PGM_GST_NAME(name)          PGM_GST_NAME_PROT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_PAE_PROT(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_PAE_PT_FOR_PHYS
#define BTH_PGMPOOLKIND_ROOT        PGMPOOLKIND_PAE_PDPT_PHYS
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef BTH_PGMPOOLKIND_ROOT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - 32-bit mode */
#define PGM_GST_TYPE                PGM_TYPE_32BIT
#define PGM_GST_NAME(name)          PGM_GST_NAME_32BIT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_PAE_32BIT(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_PAE_PT_FOR_32BIT_PT
#define BTH_PGMPOOLKIND_PT_FOR_BIG  PGMPOOLKIND_PAE_PT_FOR_32BIT_4MB
#define BTH_PGMPOOLKIND_ROOT        PGMPOOLKIND_PAE_PDPT_FOR_32BIT
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_BIG
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef BTH_PGMPOOLKIND_ROOT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME


/* Guest - PAE mode */
#define PGM_GST_TYPE                PGM_TYPE_PAE
#define PGM_GST_NAME(name)          PGM_GST_NAME_PAE(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_PAE_PAE(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_PAE_PT_FOR_PAE_PT
#define BTH_PGMPOOLKIND_PT_FOR_BIG  PGMPOOLKIND_PAE_PT_FOR_PAE_2MB
#define BTH_PGMPOOLKIND_ROOT        PGMPOOLKIND_PAE_PDPT
#include "PGMGstDefs.h"
#include "PGMAllGst.h"
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_BIG
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef BTH_PGMPOOLKIND_ROOT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

#undef PGM_SHW_TYPE
#undef PGM_SHW_NAME


/*
 * Shadow - AMD64 mode
 */
#define PGM_SHW_TYPE               PGM_TYPE_AMD64
#define PGM_SHW_NAME(name)         PGM_SHW_NAME_AMD64(name)
#include "PGMAllShw.h"

/* Guest - protected mode (only used for AMD-V nested paging in 64 bits mode) */
/** @todo retire this hack. */
#define PGM_GST_TYPE               PGM_TYPE_PROT
#define PGM_GST_NAME(name)         PGM_GST_NAME_PROT(name)
#define PGM_BTH_NAME(name)         PGM_BTH_NAME_AMD64_PROT(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT  PGMPOOLKIND_PAE_PT_FOR_PHYS
#define BTH_PGMPOOLKIND_ROOT       PGMPOOLKIND_PAE_PD_PHYS
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef BTH_PGMPOOLKIND_ROOT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

#ifdef VBOX_WITH_64_BITS_GUESTS
/* Guest - AMD64 mode */
# define PGM_GST_TYPE              PGM_TYPE_AMD64
# define PGM_GST_NAME(name)        PGM_GST_NAME_AMD64(name)
# define PGM_BTH_NAME(name)        PGM_BTH_NAME_AMD64_AMD64(name)
# define BTH_PGMPOOLKIND_PT_FOR_PT PGMPOOLKIND_PAE_PT_FOR_PAE_PT
# define BTH_PGMPOOLKIND_PT_FOR_BIG PGMPOOLKIND_PAE_PT_FOR_PAE_2MB
# define BTH_PGMPOOLKIND_ROOT      PGMPOOLKIND_64BIT_PML4
# include "PGMGstDefs.h"
# include "PGMAllGst.h"
# include "PGMAllBth.h"
# undef BTH_PGMPOOLKIND_PT_FOR_BIG
# undef BTH_PGMPOOLKIND_PT_FOR_PT
# undef BTH_PGMPOOLKIND_ROOT
# undef PGM_BTH_NAME
# undef PGM_GST_TYPE
# undef PGM_GST_NAME
#endif /* VBOX_WITH_64_BITS_GUESTS */

#undef PGM_SHW_TYPE
#undef PGM_SHW_NAME


/*
 * Shadow - 32-bit nested paging mode.
 */
#define PGM_SHW_TYPE                PGM_TYPE_NESTED_32BIT
#define PGM_SHW_NAME(name)          PGM_SHW_NAME_NESTED_32BIT(name)
#include "PGMAllShw.h"

/* Guest - real mode */
#define PGM_GST_TYPE                PGM_TYPE_REAL
#define PGM_GST_NAME(name)          PGM_GST_NAME_REAL(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NESTED_32BIT_REAL(name)
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - protected mode */
#define PGM_GST_TYPE                PGM_TYPE_PROT
#define PGM_GST_NAME(name)          PGM_GST_NAME_PROT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NESTED_32BIT_PROT(name)
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - 32-bit mode */
#define PGM_GST_TYPE                PGM_TYPE_32BIT
#define PGM_GST_NAME(name)          PGM_GST_NAME_32BIT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NESTED_32BIT_32BIT(name)
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - PAE mode */
#define PGM_GST_TYPE                PGM_TYPE_PAE
#define PGM_GST_NAME(name)          PGM_GST_NAME_PAE(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NESTED_32BIT_PAE(name)
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

#ifdef VBOX_WITH_64_BITS_GUESTS
/* Guest - AMD64 mode */
# define PGM_GST_TYPE               PGM_TYPE_AMD64
# define PGM_GST_NAME(name)         PGM_GST_NAME_AMD64(name)
# define PGM_BTH_NAME(name)         PGM_BTH_NAME_NESTED_32BIT_AMD64(name)
# include "PGMGstDefs.h"
# include "PGMAllBth.h"
# undef PGM_BTH_NAME
# undef PGM_GST_TYPE
# undef PGM_GST_NAME
#endif /* VBOX_WITH_64_BITS_GUESTS */

#undef PGM_SHW_TYPE
#undef PGM_SHW_NAME


/*
 * Shadow - PAE nested paging mode.
 */
#define PGM_SHW_TYPE                PGM_TYPE_NESTED_PAE
#define PGM_SHW_NAME(name)          PGM_SHW_NAME_NESTED_PAE(name)
#include "PGMAllShw.h"

/* Guest - real mode */
#define PGM_GST_TYPE                PGM_TYPE_REAL
#define PGM_GST_NAME(name)          PGM_GST_NAME_REAL(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NESTED_PAE_REAL(name)
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - protected mode */
#define PGM_GST_TYPE                PGM_TYPE_PROT
#define PGM_GST_NAME(name)          PGM_GST_NAME_PROT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NESTED_PAE_PROT(name)
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - 32-bit mode */
#define PGM_GST_TYPE                PGM_TYPE_32BIT
#define PGM_GST_NAME(name)          PGM_GST_NAME_32BIT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NESTED_PAE_32BIT(name)
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - PAE mode */
#define PGM_GST_TYPE                PGM_TYPE_PAE
#define PGM_GST_NAME(name)          PGM_GST_NAME_PAE(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NESTED_PAE_PAE(name)
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

#ifdef VBOX_WITH_64_BITS_GUESTS
/* Guest - AMD64 mode */
# define PGM_GST_TYPE               PGM_TYPE_AMD64
# define PGM_GST_NAME(name)         PGM_GST_NAME_AMD64(name)
# define PGM_BTH_NAME(name)         PGM_BTH_NAME_NESTED_PAE_AMD64(name)
# include "PGMGstDefs.h"
# include "PGMAllBth.h"
# undef PGM_BTH_NAME
# undef PGM_GST_TYPE
# undef PGM_GST_NAME
#endif /* VBOX_WITH_64_BITS_GUESTS */

#undef PGM_SHW_TYPE
#undef PGM_SHW_NAME


/*
 * Shadow - AMD64 nested paging mode.
 */
#define PGM_SHW_TYPE                PGM_TYPE_NESTED_AMD64
#define PGM_SHW_NAME(name)          PGM_SHW_NAME_NESTED_AMD64(name)
#include "PGMAllShw.h"

/* Guest - real mode */
#define PGM_GST_TYPE                PGM_TYPE_REAL
#define PGM_GST_NAME(name)          PGM_GST_NAME_REAL(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NESTED_AMD64_REAL(name)
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - protected mode */
#define PGM_GST_TYPE                PGM_TYPE_PROT
#define PGM_GST_NAME(name)          PGM_GST_NAME_PROT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NESTED_AMD64_PROT(name)
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - 32-bit mode */
#define PGM_GST_TYPE                PGM_TYPE_32BIT
#define PGM_GST_NAME(name)          PGM_GST_NAME_32BIT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NESTED_AMD64_32BIT(name)
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - PAE mode */
#define PGM_GST_TYPE                PGM_TYPE_PAE
#define PGM_GST_NAME(name)          PGM_GST_NAME_PAE(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NESTED_AMD64_PAE(name)
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

#ifdef VBOX_WITH_64_BITS_GUESTS
/* Guest - AMD64 mode */
# define PGM_GST_TYPE               PGM_TYPE_AMD64
# define PGM_GST_NAME(name)         PGM_GST_NAME_AMD64(name)
# define PGM_BTH_NAME(name)         PGM_BTH_NAME_NESTED_AMD64_AMD64(name)
# include "PGMGstDefs.h"
# include "PGMAllBth.h"
# undef PGM_BTH_NAME
# undef PGM_GST_TYPE
# undef PGM_GST_NAME
#endif /* VBOX_WITH_64_BITS_GUESTS */

#undef PGM_SHW_TYPE
#undef PGM_SHW_NAME


/*
 * Shadow - EPT.
 */
#define PGM_SHW_TYPE                PGM_TYPE_EPT
#define PGM_SHW_NAME(name)          PGM_SHW_NAME_EPT(name)
#include "PGMAllShw.h"

/* Guest - real mode */
#define PGM_GST_TYPE                PGM_TYPE_REAL
#define PGM_GST_NAME(name)          PGM_GST_NAME_REAL(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_EPT_REAL(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_EPT_PT_FOR_PHYS
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - protected mode */
#define PGM_GST_TYPE                PGM_TYPE_PROT
#define PGM_GST_NAME(name)          PGM_GST_NAME_PROT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_EPT_PROT(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_EPT_PT_FOR_PHYS
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - 32-bit mode */
#define PGM_GST_TYPE                PGM_TYPE_32BIT
#define PGM_GST_NAME(name)          PGM_GST_NAME_32BIT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_EPT_32BIT(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_EPT_PT_FOR_PHYS
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - PAE mode */
#define PGM_GST_TYPE                PGM_TYPE_PAE
#define PGM_GST_NAME(name)          PGM_GST_NAME_PAE(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_EPT_PAE(name)
#define BTH_PGMPOOLKIND_PT_FOR_PT   PGMPOOLKIND_EPT_PT_FOR_PHYS
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef BTH_PGMPOOLKIND_PT_FOR_PT
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

#ifdef VBOX_WITH_64_BITS_GUESTS
/* Guest - AMD64 mode */
# define PGM_GST_TYPE               PGM_TYPE_AMD64
# define PGM_GST_NAME(name)         PGM_GST_NAME_AMD64(name)
# define PGM_BTH_NAME(name)         PGM_BTH_NAME_EPT_AMD64(name)
# define BTH_PGMPOOLKIND_PT_FOR_PT  PGMPOOLKIND_EPT_PT_FOR_PHYS
# include "PGMGstDefs.h"
# include "PGMAllBth.h"
# undef BTH_PGMPOOLKIND_PT_FOR_PT
# undef PGM_BTH_NAME
# undef PGM_GST_TYPE
# undef PGM_GST_NAME
#endif /* VBOX_WITH_64_BITS_GUESTS */

#undef PGM_SHW_TYPE
#undef PGM_SHW_NAME


/*
 * Shadow - NEM / None.
 */
#define PGM_SHW_TYPE                PGM_TYPE_NONE
#define PGM_SHW_NAME(name)          PGM_SHW_NAME_NONE(name)
#include "PGMAllShw.h"

/* Guest - real mode */
#define PGM_GST_TYPE                PGM_TYPE_REAL
#define PGM_GST_NAME(name)          PGM_GST_NAME_REAL(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NONE_REAL(name)
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - protected mode */
#define PGM_GST_TYPE                PGM_TYPE_PROT
#define PGM_GST_NAME(name)          PGM_GST_NAME_PROT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NONE_PROT(name)
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - 32-bit mode */
#define PGM_GST_TYPE                PGM_TYPE_32BIT
#define PGM_GST_NAME(name)          PGM_GST_NAME_32BIT(name)
#define PGM_BTH_NAME(name)          PGM_BTH_NAME_NONE_32BIT(name)
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

/* Guest - PAE mode */
#define PGM_GST_TYPE               PGM_TYPE_PAE
#define PGM_GST_NAME(name)         PGM_GST_NAME_PAE(name)
#define PGM_BTH_NAME(name)         PGM_BTH_NAME_NONE_PAE(name)
#include "PGMGstDefs.h"
#include "PGMAllBth.h"
#undef PGM_BTH_NAME
#undef PGM_GST_TYPE
#undef PGM_GST_NAME

#ifdef VBOX_WITH_64_BITS_GUESTS
/* Guest - AMD64 mode */
# define PGM_GST_TYPE               PGM_TYPE_AMD64
# define PGM_GST_NAME(name)         PGM_GST_NAME_AMD64(name)
# define PGM_BTH_NAME(name)         PGM_BTH_NAME_NONE_AMD64(name)
# include "PGMGstDefs.h"
# include "PGMAllBth.h"
# undef PGM_BTH_NAME
# undef PGM_GST_TYPE
# undef PGM_GST_NAME
#endif /* VBOX_WITH_64_BITS_GUESTS */

#undef PGM_SHW_TYPE
#undef PGM_SHW_NAME



/**
 * Guest mode data array.
 */
PGMMODEDATAGST const g_aPgmGuestModeData[PGM_GUEST_MODE_DATA_ARRAY_SIZE] =
{
    { UINT32_MAX, NULL, NULL, NULL, NULL }, /* 0 */
    {
        PGM_TYPE_REAL,
        PGM_GST_NAME_REAL(GetPage),
        PGM_GST_NAME_REAL(QueryPageFast),
        PGM_GST_NAME_REAL(ModifyPage),
        PGM_GST_NAME_REAL(Enter),
        PGM_GST_NAME_REAL(Exit),
#ifdef IN_RING3
        PGM_GST_NAME_REAL(Relocate),
#endif
    },
    {
        PGM_TYPE_PROT,
        PGM_GST_NAME_PROT(GetPage),
        PGM_GST_NAME_PROT(QueryPageFast),
        PGM_GST_NAME_PROT(ModifyPage),
        PGM_GST_NAME_PROT(Enter),
        PGM_GST_NAME_PROT(Exit),
#ifdef IN_RING3
        PGM_GST_NAME_PROT(Relocate),
#endif
    },
    {
        PGM_TYPE_32BIT,
        PGM_GST_NAME_32BIT(GetPage),
        PGM_GST_NAME_32BIT(QueryPageFast),
        PGM_GST_NAME_32BIT(ModifyPage),
        PGM_GST_NAME_32BIT(Enter),
        PGM_GST_NAME_32BIT(Exit),
#ifdef IN_RING3
        PGM_GST_NAME_32BIT(Relocate),
#endif
    },
    {
        PGM_TYPE_PAE,
        PGM_GST_NAME_PAE(GetPage),
        PGM_GST_NAME_PAE(QueryPageFast),
        PGM_GST_NAME_PAE(ModifyPage),
        PGM_GST_NAME_PAE(Enter),
        PGM_GST_NAME_PAE(Exit),
#ifdef IN_RING3
        PGM_GST_NAME_PAE(Relocate),
#endif
    },
#ifdef VBOX_WITH_64_BITS_GUESTS
    {
        PGM_TYPE_AMD64,
        PGM_GST_NAME_AMD64(GetPage),
        PGM_GST_NAME_AMD64(QueryPageFast),
        PGM_GST_NAME_AMD64(ModifyPage),
        PGM_GST_NAME_AMD64(Enter),
        PGM_GST_NAME_AMD64(Exit),
# ifdef IN_RING3
        PGM_GST_NAME_AMD64(Relocate),
# endif
    },
#endif
};


/**
 * The shadow mode data array.
 */
PGMMODEDATASHW const g_aPgmShadowModeData[PGM_SHADOW_MODE_DATA_ARRAY_SIZE] =
{
    { UINT8_MAX, NULL, NULL, NULL, NULL }, /* 0 */
    { UINT8_MAX, NULL, NULL, NULL, NULL }, /* PGM_TYPE_REAL */
    { UINT8_MAX, NULL, NULL, NULL, NULL }, /* PGM_TYPE_PROT */
    {
        PGM_TYPE_32BIT,
        PGM_SHW_NAME_32BIT(GetPage),
        PGM_SHW_NAME_32BIT(ModifyPage),
        PGM_SHW_NAME_32BIT(Enter),
        PGM_SHW_NAME_32BIT(Exit),
#ifdef IN_RING3
        PGM_SHW_NAME_32BIT(Relocate),
#endif
    },
    {
        PGM_TYPE_PAE,
        PGM_SHW_NAME_PAE(GetPage),
        PGM_SHW_NAME_PAE(ModifyPage),
        PGM_SHW_NAME_PAE(Enter),
        PGM_SHW_NAME_PAE(Exit),
#ifdef IN_RING3
        PGM_SHW_NAME_PAE(Relocate),
#endif
    },
    {
        PGM_TYPE_AMD64,
        PGM_SHW_NAME_AMD64(GetPage),
        PGM_SHW_NAME_AMD64(ModifyPage),
        PGM_SHW_NAME_AMD64(Enter),
        PGM_SHW_NAME_AMD64(Exit),
#ifdef IN_RING3
        PGM_SHW_NAME_AMD64(Relocate),
#endif
    },
    {
        PGM_TYPE_NESTED_32BIT,
        PGM_SHW_NAME_NESTED_32BIT(GetPage),
        PGM_SHW_NAME_NESTED_32BIT(ModifyPage),
        PGM_SHW_NAME_NESTED_32BIT(Enter),
        PGM_SHW_NAME_NESTED_32BIT(Exit),
#ifdef IN_RING3
        PGM_SHW_NAME_NESTED_32BIT(Relocate),
#endif
    },
    {
        PGM_TYPE_NESTED_PAE,
        PGM_SHW_NAME_NESTED_PAE(GetPage),
        PGM_SHW_NAME_NESTED_PAE(ModifyPage),
        PGM_SHW_NAME_NESTED_PAE(Enter),
        PGM_SHW_NAME_NESTED_PAE(Exit),
#ifdef IN_RING3
        PGM_SHW_NAME_NESTED_PAE(Relocate),
#endif
    },
    {
        PGM_TYPE_NESTED_AMD64,
        PGM_SHW_NAME_NESTED_AMD64(GetPage),
        PGM_SHW_NAME_NESTED_AMD64(ModifyPage),
        PGM_SHW_NAME_NESTED_AMD64(Enter),
        PGM_SHW_NAME_NESTED_AMD64(Exit),
#ifdef IN_RING3
        PGM_SHW_NAME_NESTED_AMD64(Relocate),
#endif
    },
    {
        PGM_TYPE_EPT,
        PGM_SHW_NAME_EPT(GetPage),
        PGM_SHW_NAME_EPT(ModifyPage),
        PGM_SHW_NAME_EPT(Enter),
        PGM_SHW_NAME_EPT(Exit),
#ifdef IN_RING3
        PGM_SHW_NAME_EPT(Relocate),
#endif
    },
    {
        PGM_TYPE_NONE,
        PGM_SHW_NAME_NONE(GetPage),
        PGM_SHW_NAME_NONE(ModifyPage),
        PGM_SHW_NAME_NONE(Enter),
        PGM_SHW_NAME_NONE(Exit),
#ifdef IN_RING3
        PGM_SHW_NAME_NONE(Relocate),
#endif
    },
};


/**
 * The guest+shadow mode data array.
 */
PGMMODEDATABTH const g_aPgmBothModeData[PGM_BOTH_MODE_DATA_ARRAY_SIZE] =
{
#if   !defined(IN_RING3) && !defined(VBOX_STRICT)
# define PGMMODEDATABTH_NULL_ENTRY()    { UINT32_MAX, UINT32_MAX, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
# define PGMMODEDATABTH_ENTRY(uShwT, uGstT, Nm) \
    { uShwT, uGstT, Nm(InvalidatePage), Nm(SyncCR3), Nm(PrefetchPage), Nm(VerifyAccessSyncPage), Nm(MapCR3), Nm(UnmapCR3), Nm(Enter), Nm(Trap0eHandler), Nm(NestedTrap0eHandler) }

#elif !defined(IN_RING3) && defined(VBOX_STRICT)
# define PGMMODEDATABTH_NULL_ENTRY()    { UINT32_MAX, UINT32_MAX, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
# define PGMMODEDATABTH_ENTRY(uShwT, uGstT, Nm) \
    { uShwT, uGstT, Nm(InvalidatePage), Nm(SyncCR3), Nm(PrefetchPage), Nm(VerifyAccessSyncPage), Nm(MapCR3), Nm(UnmapCR3), Nm(Enter), Nm(Trap0eHandler), Nm(NestedTrap0eHandler), Nm(AssertCR3) }

#elif defined(IN_RING3) && !defined(VBOX_STRICT)
# define PGMMODEDATABTH_NULL_ENTRY()    { UINT32_MAX, UINT32_MAX, NULL, NULL, NULL, NULL, NULL, NULL }
# define PGMMODEDATABTH_ENTRY(uShwT, uGstT, Nm) \
    { uShwT, uGstT, Nm(InvalidatePage), Nm(SyncCR3), Nm(PrefetchPage), Nm(VerifyAccessSyncPage), Nm(MapCR3), Nm(UnmapCR3), Nm(Enter), }

#elif defined(IN_RING3) && defined(VBOX_STRICT)
# define PGMMODEDATABTH_NULL_ENTRY()    { UINT32_MAX, UINT32_MAX, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
# define PGMMODEDATABTH_ENTRY(uShwT, uGstT, Nm) \
    { uShwT, uGstT, Nm(InvalidatePage), Nm(SyncCR3), Nm(PrefetchPage), Nm(VerifyAccessSyncPage), Nm(MapCR3), Nm(UnmapCR3), Nm(Enter), Nm(AssertCR3) }

#else
# error "Misconfig."
#endif

    /* 32-bit shadow paging mode: */
    PGMMODEDATABTH_NULL_ENTRY(), /* 0 */
    PGMMODEDATABTH_ENTRY(PGM_TYPE_32BIT, PGM_TYPE_REAL,  PGM_BTH_NAME_32BIT_REAL),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_32BIT, PGM_TYPE_PROT,  PGM_BTH_NAME_32BIT_PROT),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_32BIT, PGM_TYPE_32BIT, PGM_BTH_NAME_32BIT_32BIT),
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_32BIT, PGM_TYPE_PAE          - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_32BIT, PGM_TYPE_AMD64        - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_32BIT, PGM_TYPE_NESTED_32BIT - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_32BIT, PGM_TYPE_NESTED_PAE   - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_32BIT, PGM_TYPE_NESTED_AMD64 - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_32BIT, PGM_TYPE_EPT          - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_32BIT, PGM_TYPE_NONE         - illegal */

    /* PAE shadow paging mode: */
    PGMMODEDATABTH_NULL_ENTRY(), /* 0 */
    PGMMODEDATABTH_ENTRY(PGM_TYPE_PAE, PGM_TYPE_REAL,  PGM_BTH_NAME_PAE_REAL),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_PAE, PGM_TYPE_PROT,  PGM_BTH_NAME_PAE_PROT),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_PAE, PGM_TYPE_32BIT, PGM_BTH_NAME_PAE_32BIT),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_PAE, PGM_TYPE_PAE,   PGM_BTH_NAME_PAE_PAE),
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_PAE, PGM_TYPE_AMD64        - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_PAE, PGM_TYPE_NESTED_32BIT - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_PAE, PGM_TYPE_NESTED_PAE   - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_PAE, PGM_TYPE_NESTED_AMD64 - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_PAE, PGM_TYPE_EPT          - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_PAE, PGM_TYPE_NONE         - illegal */

    /* AMD64 shadow paging mode: */
    PGMMODEDATABTH_NULL_ENTRY(), /* 0 */
    PGMMODEDATABTH_NULL_ENTRY(), //PGMMODEDATABTH_ENTRY(PGM_TYPE_AMD64, PGM_TYPE_REAL,  PGM_BTH_NAME_AMD64_REAL),
    PGMMODEDATABTH_NULL_ENTRY(), //PGMMODEDATABTH_ENTRY(PGM_TYPE_AMD64, PGM_TYPE_PROT,  PGM_BTH_NAME_AMD64_PROT),
    PGMMODEDATABTH_NULL_ENTRY(), //PGMMODEDATABTH_ENTRY(PGM_TYPE_AMD64, PGM_TYPE_32BIT, PGM_BTH_NAME_AMD64_32BIT),
    PGMMODEDATABTH_NULL_ENTRY(), //PGMMODEDATABTH_ENTRY(PGM_TYPE_AMD64, PGM_TYPE_PAE,   PGM_BTH_NAME_AMD64_PAE),
#ifdef VBOX_WITH_64_BITS_GUESTS
    PGMMODEDATABTH_ENTRY(PGM_TYPE_AMD64, PGM_TYPE_AMD64, PGM_BTH_NAME_AMD64_AMD64),
#else
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_AMD64, PGM_TYPE_AMD64        - illegal */
#endif
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_AMD64, PGM_TYPE_NESTED_32BIT - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_AMD64, PGM_TYPE_NESTED_PAE   - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_AMD64, PGM_TYPE_NESTED_AMD64 - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_AMD64, PGM_TYPE_EPT          - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_AMD64, PGM_TYPE_NONE         - illegal */

    /* 32-bit nested paging mode: */
    PGMMODEDATABTH_NULL_ENTRY(), /* 0 */
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NESTED_32BIT, PGM_TYPE_REAL,  PGM_BTH_NAME_NESTED_32BIT_REAL),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NESTED_32BIT, PGM_TYPE_PROT,  PGM_BTH_NAME_NESTED_32BIT_PROT),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NESTED_32BIT, PGM_TYPE_32BIT, PGM_BTH_NAME_NESTED_32BIT_32BIT),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NESTED_32BIT, PGM_TYPE_PAE,   PGM_BTH_NAME_NESTED_32BIT_PAE),
#ifdef VBOX_WITH_64_BITS_GUESTS
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NESTED_32BIT, PGM_TYPE_AMD64, PGM_BTH_NAME_NESTED_32BIT_AMD64),
#else
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NESTED_32BIT, PGM_TYPE_AMD64        - illegal */
#endif
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NESTED_32BIT, PGM_TYPE_NESTED_32BIT - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NESTED_32BIT, PGM_TYPE_NESTED_PAE   - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NESTED_32BIT, PGM_TYPE_NESTED_AMD64 - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NESTED_32BIT, PGM_TYPE_EPT          - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NESTED_32BIT, PGM_TYPE_NONE         - illegal */

    /* PAE nested paging mode: */
    PGMMODEDATABTH_NULL_ENTRY(), /* 0 */
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NESTED_PAE, PGM_TYPE_REAL,  PGM_BTH_NAME_NESTED_PAE_REAL),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NESTED_PAE, PGM_TYPE_PROT,  PGM_BTH_NAME_NESTED_PAE_PROT),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NESTED_PAE, PGM_TYPE_32BIT, PGM_BTH_NAME_NESTED_PAE_32BIT),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NESTED_PAE, PGM_TYPE_PAE,   PGM_BTH_NAME_NESTED_PAE_PAE),
#ifdef VBOX_WITH_64_BITS_GUESTS
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NESTED_PAE, PGM_TYPE_AMD64, PGM_BTH_NAME_NESTED_PAE_AMD64),
#else
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NESTED_PAE, PGM_TYPE_AMD64        - illegal */
#endif
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NESTED_PAE, PGM_TYPE_NESTED_32BIT - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NESTED_PAE, PGM_TYPE_NESTED_PAE   - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NESTED_PAE, PGM_TYPE_NESTED_AMD64 - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NESTED_PAE, PGM_TYPE_EPT          - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NESTED_PAE, PGM_TYPE_NONE         - illegal */

    /* AMD64 nested paging mode: */
    PGMMODEDATABTH_NULL_ENTRY(), /* 0 */
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NESTED_AMD64, PGM_TYPE_REAL,  PGM_BTH_NAME_NESTED_AMD64_REAL),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NESTED_AMD64, PGM_TYPE_PROT,  PGM_BTH_NAME_NESTED_AMD64_PROT),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NESTED_AMD64, PGM_TYPE_32BIT, PGM_BTH_NAME_NESTED_AMD64_32BIT),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NESTED_AMD64, PGM_TYPE_PAE,   PGM_BTH_NAME_NESTED_AMD64_PAE),
#ifdef VBOX_WITH_64_BITS_GUESTS
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NESTED_AMD64, PGM_TYPE_AMD64, PGM_BTH_NAME_NESTED_AMD64_AMD64),
#else
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NESTED_AMD64, PGM_TYPE_AMD64        - illegal */
#endif
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NESTED_AMD64, PGM_TYPE_NESTED_32BIT - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NESTED_AMD64, PGM_TYPE_NESTED_PAE   - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NESTED_AMD64, PGM_TYPE_NESTED_AMD64 - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NESTED_AMD64, PGM_TYPE_EPT          - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NESTED_AMD64, PGM_TYPE_NONE         - illegal */

    /* EPT nested paging mode: */
    PGMMODEDATABTH_NULL_ENTRY(), /* 0 */
    PGMMODEDATABTH_ENTRY(PGM_TYPE_EPT, PGM_TYPE_REAL,  PGM_BTH_NAME_EPT_REAL),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_EPT, PGM_TYPE_PROT,  PGM_BTH_NAME_EPT_PROT),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_EPT, PGM_TYPE_32BIT, PGM_BTH_NAME_EPT_32BIT),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_EPT, PGM_TYPE_PAE,   PGM_BTH_NAME_EPT_PAE),
#ifdef VBOX_WITH_64_BITS_GUESTS
    PGMMODEDATABTH_ENTRY(PGM_TYPE_EPT, PGM_TYPE_AMD64, PGM_BTH_NAME_EPT_AMD64),
#else
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_EPT, PGM_TYPE_AMD64        - illegal */
#endif
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_EPT, PGM_TYPE_NESTED_32BIT - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_EPT, PGM_TYPE_NESTED_PAE   - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_EPT, PGM_TYPE_NESTED_AMD64 - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_EPT, PGM_TYPE_EPT          - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_EPT, PGM_TYPE_NONE         - illegal */

    /* NONE / NEM: */
    PGMMODEDATABTH_NULL_ENTRY(), /* 0 */
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NONE, PGM_TYPE_REAL,  PGM_BTH_NAME_EPT_REAL),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NONE, PGM_TYPE_PROT,  PGM_BTH_NAME_EPT_PROT),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NONE, PGM_TYPE_32BIT, PGM_BTH_NAME_EPT_32BIT),
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NONE, PGM_TYPE_PAE,   PGM_BTH_NAME_EPT_PAE),
#ifdef VBOX_WITH_64_BITS_GUESTS
    PGMMODEDATABTH_ENTRY(PGM_TYPE_NONE, PGM_TYPE_AMD64, PGM_BTH_NAME_EPT_AMD64),
#else
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NONE, PGM_TYPE_AMD64        - illegal */
#endif
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NONE, PGM_TYPE_NESTED_32BIT - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NONE, PGM_TYPE_NESTED_PAE   - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NONE, PGM_TYPE_NESTED_AMD64 - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NONE, PGM_TYPE_EPT          - illegal */
    PGMMODEDATABTH_NULL_ENTRY(), /* PGM_TYPE_NONE, PGM_TYPE_NONE         - illegal */


#undef PGMMODEDATABTH_ENTRY
#undef PGMMODEDATABTH_NULL_ENTRY
};


/** Mask array used by pgmGetCr3MaskForMode.
 * X86_CR3_AMD64_PAGE_MASK is used for modes that doesn't have a CR3 or EPTP. */
static uint64_t const g_auCr3MaskForMode[PGMMODE_MAX] =
{
    /* [PGMMODE_INVALID] = */           X86_CR3_AMD64_PAGE_MASK,
    /* [PGMMODE_REAL] = */              X86_CR3_AMD64_PAGE_MASK,
    /* [PGMMODE_PROTECTED] = */         X86_CR3_AMD64_PAGE_MASK,
    /* [PGMMODE_32_BIT] = */            X86_CR3_PAGE_MASK,
    /* [PGMMODE_PAE] = */               X86_CR3_PAE_PAGE_MASK,
    /* [PGMMODE_PAE_NX] = */            X86_CR3_PAE_PAGE_MASK,
    /* [PGMMODE_AMD64] = */             X86_CR3_AMD64_PAGE_MASK,
    /* [PGMMODE_AMD64_NX] = */          X86_CR3_AMD64_PAGE_MASK,
    /* [PGMMODE_NESTED_32BIT = */       X86_CR3_PAGE_MASK,
    /* [PGMMODE_NESTED_PAE] = */        X86_CR3_PAE_PAGE_MASK,
    /* [PGMMODE_NESTED_AMD64] = */      X86_CR3_AMD64_PAGE_MASK,
    /* [PGMMODE_EPT] = */               X86_CR3_EPT_PAGE_MASK,
    /* [PGMMODE_NONE] = */              X86_CR3_AMD64_PAGE_MASK,
};


/**
 * Gets the physical address mask for CR3 in the given paging mode.
 *
 * The mask is for eliminating flags and other stuff in CR3/EPTP when
 * extracting the physical address.  It is not for validating whether there are
 * reserved bits set.  PGM ASSUMES that whoever loaded the CR3 value and passed
 * it to PGM checked for reserved bits, including reserved physical address
 * bits.
 *
 * @returns The CR3 mask.
 * @param   enmMode         The paging mode.
 * @param   enmSlatMode     The second-level address translation mode.
 */
DECLINLINE(uint64_t) pgmGetCr3MaskForMode(PGMMODE enmMode, PGMSLAT enmSlatMode)
{
    if (enmSlatMode == PGMSLAT_DIRECT)
    {
        Assert(enmMode != PGMMODE_EPT);
        return g_auCr3MaskForMode[(unsigned)enmMode < (unsigned)PGMMODE_MAX ? enmMode : 0];
    }
    Assert(enmSlatMode == PGMSLAT_EPT);
    return X86_CR3_EPT_PAGE_MASK;
}


/**
 * Gets the masked CR3 value according to the current guest paging mode.
 *
 * See disclaimer in pgmGetCr3MaskForMode.
 *
 * @returns The masked PGM CR3 value.
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   uCr3    The raw guest CR3 value.
 */
DECLINLINE(RTGCPHYS) pgmGetGuestMaskedCr3(PVMCPUCC pVCpu, uint64_t uCr3)
{
    uint64_t const fCr3Mask  = pgmGetCr3MaskForMode(pVCpu->pgm.s.enmGuestMode, pVCpu->pgm.s.enmGuestSlatMode);
    RTGCPHYS       GCPhysCR3 = (RTGCPHYS)(uCr3 & fCr3Mask);
    PGM_A20_APPLY_TO_VAR(pVCpu, GCPhysCR3);
    return GCPhysCR3;
}


#ifdef IN_RING0
/**
 * #PF Handler.
 *
 * @returns VBox status code (appropriate for trap handling and GC return).
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uErr        The trap error code.
 * @param   pCtx        Pointer to the register context for the CPU.
 * @param   pvFault     The fault address.
 */
VMMDECL(int) PGMTrap0eHandler(PVMCPUCC pVCpu, RTGCUINT uErr, PCPUMCTX pCtx, RTGCPTR pvFault)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);

    Log(("PGMTrap0eHandler: uErr=%RGx pvFault=%RGv eip=%04x:%RGv cr3=%RGp\n", uErr, pvFault, pCtx->cs.Sel, (RTGCPTR)pCtx->rip, (RTGCPHYS)CPUMGetGuestCR3(pVCpu)));
    STAM_PROFILE_START(&pVCpu->pgm.s.Stats.StatRZTrap0e, a);
    STAM_STATS({ pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = NULL; } );


# ifdef VBOX_WITH_STATISTICS
    /*
     * Error code stats.
     */
    if (uErr & X86_TRAP_PF_US)
    {
        if (!(uErr & X86_TRAP_PF_P))
        {
            if (uErr & X86_TRAP_PF_RW)
                STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eUSNotPresentWrite);
            else
                STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eUSNotPresentRead);
        }
        else if (uErr & X86_TRAP_PF_RW)
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eUSWrite);
        else if (uErr & X86_TRAP_PF_RSVD)
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eUSReserved);
        else if (uErr & X86_TRAP_PF_ID)
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eUSNXE);
        else
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eUSRead);
    }
    else
    {   /* Supervisor */
        if (!(uErr & X86_TRAP_PF_P))
        {
            if (uErr & X86_TRAP_PF_RW)
                STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eSVNotPresentWrite);
            else
                STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eSVNotPresentRead);
        }
        else if (uErr & X86_TRAP_PF_RW)
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eSVWrite);
        else if (uErr & X86_TRAP_PF_ID)
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eSNXE);
        else if (uErr & X86_TRAP_PF_RSVD)
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eSVReserved);
    }
# endif /* VBOX_WITH_STATISTICS */

    /*
     * Call the worker.
     */
    uintptr_t const idxBth = pVCpu->pgm.s.idxBothModeData;
    AssertReturn(idxBth < RT_ELEMENTS(g_aPgmBothModeData), VERR_PGM_MODE_IPE);
    AssertReturn(g_aPgmBothModeData[idxBth].pfnTrap0eHandler, VERR_PGM_MODE_IPE);
    bool fLockTaken = false;
    int rc = g_aPgmBothModeData[idxBth].pfnTrap0eHandler(pVCpu, uErr, pCtx, pvFault, &fLockTaken);
    if (fLockTaken)
    {
        PGM_LOCK_ASSERT_OWNER(pVM);
        PGM_UNLOCK(pVM);
    }
    LogFlow(("PGMTrap0eHandler: uErr=%RGx pvFault=%RGv rc=%Rrc\n", uErr, pvFault, rc));

    /*
     * Return code tweaks.
     */
    if (rc != VINF_SUCCESS)
    {
        if (rc == VINF_PGM_SYNCPAGE_MODIFIED_PDE)
            rc = VINF_SUCCESS;

        /* Note: hack alert for difficult to reproduce problem. */
        if (    rc == VERR_PAGE_NOT_PRESENT                 /* SMP only ; disassembly might fail. */
            ||  rc == VERR_PAGE_TABLE_NOT_PRESENT           /* seen with UNI & SMP */
            ||  rc == VERR_PAGE_DIRECTORY_PTR_NOT_PRESENT   /* seen with SMP */
            ||  rc == VERR_PAGE_MAP_LEVEL4_NOT_PRESENT)     /* precaution */
        {
            Log(("WARNING: Unexpected VERR_PAGE_TABLE_NOT_PRESENT (%d) for page fault at %RGv error code %x (rip=%RGv)\n", rc, pvFault, uErr, pCtx->rip));
            /* Some kind of inconsistency in the SMP case; it's safe to just execute the instruction again; not sure about single VCPU VMs though. */
            rc = VINF_SUCCESS;
        }
    }

    STAM_STATS({ if (rc == VINF_EM_RAW_GUEST_TRAP) STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZTrap0eGuestPF); });
    STAM_STATS({ if (!pVCpu->pgmr0.s.pStatTrap0eAttributionR0)
                    pVCpu->pgmr0.s.pStatTrap0eAttributionR0 = &pVCpu->pgm.s.Stats.StatRZTrap0eTime2Misc; });
    STAM_PROFILE_STOP_EX(&pVCpu->pgm.s.Stats.StatRZTrap0e, pVCpu->pgmr0.s.pStatTrap0eAttributionR0, a);
    return rc;
}
#endif /* IN_RING0 */


/**
 * Prefetch a page
 *
 * Typically used to sync commonly used pages before entering raw mode
 * after a CR3 reload.
 *
 * @returns VBox status code suitable for scheduling.
 * @retval  VINF_SUCCESS on success.
 * @retval  VINF_PGM_SYNC_CR3 if we're out of shadow pages or something like that.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtrPage   Page to invalidate.
 */
VMMDECL(int) PGMPrefetchPage(PVMCPUCC pVCpu, RTGCPTR GCPtrPage)
{
    STAM_PROFILE_START(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,Prefetch), a);

    uintptr_t const idxBth = pVCpu->pgm.s.idxBothModeData;
    AssertReturn(idxBth < RT_ELEMENTS(g_aPgmBothModeData), VERR_PGM_MODE_IPE);
    AssertReturn(g_aPgmBothModeData[idxBth].pfnPrefetchPage, VERR_PGM_MODE_IPE);
    int rc = g_aPgmBothModeData[idxBth].pfnPrefetchPage(pVCpu, GCPtrPage);

    STAM_PROFILE_STOP(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,Prefetch), a);
    AssertMsg(rc == VINF_SUCCESS || rc == VINF_PGM_SYNC_CR3 || RT_FAILURE(rc), ("rc=%Rrc\n", rc));
    return rc;
}


/**
 * Emulation of the invlpg instruction (HC only actually).
 *
 * @returns Strict VBox status code, special care required.
 * @retval  VINF_PGM_SYNC_CR3 - handled.
 * @retval  VINF_EM_RAW_EMULATE_INSTR - not handled (RC only).
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtrPage   Page to invalidate.
 *
 * @remark  ASSUMES the page table entry or page directory is valid. Fairly
 *          safe, but there could be edge cases!
 *
 * @todo    Flush page or page directory only if necessary!
 * @todo    VBOXSTRICTRC
 */
VMMDECL(int) PGMInvalidatePage(PVMCPUCC pVCpu, RTGCPTR GCPtrPage)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    int rc;
    Log3(("PGMInvalidatePage: GCPtrPage=%RGv\n", GCPtrPage));

    IEMTlbInvalidatePage(pVCpu, GCPtrPage);

    /*
     * Call paging mode specific worker.
     */
    STAM_PROFILE_START(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,InvalidatePage), a);
    PGM_LOCK_VOID(pVM);

    uintptr_t const idxBth = pVCpu->pgm.s.idxBothModeData;
    AssertReturnStmt(idxBth < RT_ELEMENTS(g_aPgmBothModeData), PGM_UNLOCK(pVM), VERR_PGM_MODE_IPE);
    AssertReturnStmt(g_aPgmBothModeData[idxBth].pfnInvalidatePage, PGM_UNLOCK(pVM), VERR_PGM_MODE_IPE);
    rc = g_aPgmBothModeData[idxBth].pfnInvalidatePage(pVCpu, GCPtrPage);

    PGM_UNLOCK(pVM);
    STAM_PROFILE_STOP(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,InvalidatePage), a);

    /* Ignore all irrelevant error codes. */
    if (    rc == VERR_PAGE_NOT_PRESENT
        ||  rc == VERR_PAGE_TABLE_NOT_PRESENT
        ||  rc == VERR_PAGE_DIRECTORY_PTR_NOT_PRESENT
        ||  rc == VERR_PAGE_MAP_LEVEL4_NOT_PRESENT)
        rc = VINF_SUCCESS;

    return rc;
}


/**
 * Executes an instruction using the interpreter.
 *
 * @returns VBox status code (appropriate for trap handling and GC return).
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pvFault     Fault address.
 */
VMMDECL(VBOXSTRICTRC) PGMInterpretInstruction(PVMCPUCC pVCpu, RTGCPTR pvFault)
{
    RT_NOREF(pvFault);
    VBOXSTRICTRC rc = EMInterpretInstruction(pVCpu);
    if (rc == VERR_EM_INTERPRETER)
        rc = VINF_EM_RAW_EMULATE_INSTR;
    if (rc != VINF_SUCCESS)
        Log(("PGMInterpretInstruction: returns %Rrc (pvFault=%RGv)\n", VBOXSTRICTRC_VAL(rc), pvFault));
    return rc;
}


/**
 * Gets effective page information (from the VMM page directory).
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       Guest Context virtual address of the page.
 * @param   pfFlags     Where to store the flags. These are X86_PTE_*.
 * @param   pHCPhys     Where to store the HC physical address of the page.
 *                      This is page aligned.
 * @remark  You should use PGMMapGetPage() for pages in a mapping.
 */
VMMDECL(int) PGMShwGetPage(PVMCPUCC pVCpu, RTGCPTR GCPtr, uint64_t *pfFlags, PRTHCPHYS pHCPhys)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    PGM_LOCK_VOID(pVM);

    uintptr_t idxShw = pVCpu->pgm.s.idxShadowModeData;
    AssertReturn(idxShw < RT_ELEMENTS(g_aPgmShadowModeData), VERR_PGM_MODE_IPE);
    AssertReturn(g_aPgmShadowModeData[idxShw].pfnGetPage, VERR_PGM_MODE_IPE);
    int rc = g_aPgmShadowModeData[idxShw].pfnGetPage(pVCpu, GCPtr, pfFlags, pHCPhys);

    PGM_UNLOCK(pVM);
    return rc;
}


/**
 * Modify page flags for a range of pages in the shadow context.
 *
 * The existing flags are ANDed with the fMask and ORed with the fFlags.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       Virtual address of the first page in the range.
 * @param   fFlags      The OR  mask - page flags X86_PTE_*, excluding the page mask of course.
 * @param   fMask       The AND mask - page flags X86_PTE_*.
 *                      Be very CAREFUL when ~'ing constants which could be 32-bit!
 * @param   fOpFlags    A combination of the PGM_MK_PK_XXX flags.
 * @remark  You must use PGMMapModifyPage() for pages in a mapping.
 */
DECLINLINE(int) pdmShwModifyPage(PVMCPUCC pVCpu, RTGCPTR GCPtr, uint64_t fFlags, uint64_t fMask, uint32_t fOpFlags)
{
    AssertMsg(!(fFlags & X86_PTE_PAE_PG_MASK), ("fFlags=%#llx\n", fFlags));
    Assert(!(fOpFlags & ~(PGM_MK_PG_IS_MMIO2 | PGM_MK_PG_IS_WRITE_FAULT)));

    GCPtr &= ~(RTGCPTR)GUEST_PAGE_OFFSET_MASK; /** @todo this ain't necessary, right... */

    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    PGM_LOCK_VOID(pVM);

    uintptr_t idxShw = pVCpu->pgm.s.idxShadowModeData;
    AssertReturn(idxShw < RT_ELEMENTS(g_aPgmShadowModeData), VERR_PGM_MODE_IPE);
    AssertReturn(g_aPgmShadowModeData[idxShw].pfnModifyPage, VERR_PGM_MODE_IPE);
    int rc = g_aPgmShadowModeData[idxShw].pfnModifyPage(pVCpu, GCPtr, GUEST_PAGE_SIZE, fFlags, fMask, fOpFlags);

    PGM_UNLOCK(pVM);
    return rc;
}


/**
 * Changing the page flags for a single page in the shadow page tables so as to
 * make it read-only.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       Virtual address of the first page in the range.
 * @param   fOpFlags    A combination of the PGM_MK_PK_XXX flags.
 */
VMMDECL(int) PGMShwMakePageReadonly(PVMCPUCC pVCpu, RTGCPTR GCPtr, uint32_t fOpFlags)
{
    return pdmShwModifyPage(pVCpu, GCPtr, 0, ~(uint64_t)X86_PTE_RW, fOpFlags);
}


/**
 * Changing the page flags for a single page in the shadow page tables so as to
 * make it writable.
 *
 * The call must know with 101% certainty that the guest page tables maps this
 * as writable too.  This function will deal shared, zero and write monitored
 * pages.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       Virtual address of the first page in the range.
 * @param   fOpFlags    A combination of the PGM_MK_PK_XXX flags.
 */
VMMDECL(int) PGMShwMakePageWritable(PVMCPUCC pVCpu, RTGCPTR GCPtr, uint32_t fOpFlags)
{
    if (pVCpu->pgm.s.enmShadowMode != PGMMODE_NONE) /* avoid assertions */
        return pdmShwModifyPage(pVCpu, GCPtr, X86_PTE_RW, ~(uint64_t)0, fOpFlags);
    return VINF_SUCCESS;
}


/**
 * Changing the page flags for a single page in the shadow page tables so as to
 * make it not present.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       Virtual address of the first page in the range.
 * @param   fOpFlags    A combination of the PGM_MK_PG_XXX flags.
 */
VMMDECL(int) PGMShwMakePageNotPresent(PVMCPUCC pVCpu, RTGCPTR GCPtr, uint32_t fOpFlags)
{
    return pdmShwModifyPage(pVCpu, GCPtr, 0, 0, fOpFlags);
}


/**
 * Changing the page flags for a single page in the shadow page tables so as to
 * make it supervisor and writable.
 *
 * This if for dealing with CR0.WP=0 and readonly user pages.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       Virtual address of the first page in the range.
 * @param   fBigPage    Whether or not this is a big page. If it is, we have to
 *                      change the shadow PDE as well.  If it isn't, the caller
 *                      has checked that the shadow PDE doesn't need changing.
 *                      We ASSUME 4KB pages backing the big page here!
 * @param   fOpFlags    A combination of the PGM_MK_PG_XXX flags.
 */
int pgmShwMakePageSupervisorAndWritable(PVMCPUCC pVCpu, RTGCPTR GCPtr, bool fBigPage, uint32_t fOpFlags)
{
    int rc = pdmShwModifyPage(pVCpu, GCPtr, X86_PTE_RW, ~(uint64_t)X86_PTE_US, fOpFlags);
    if (rc == VINF_SUCCESS && fBigPage)
    {
        /* this is a bit ugly... */
        switch (pVCpu->pgm.s.enmShadowMode)
        {
            case PGMMODE_32_BIT:
            {
                PX86PDE pPde = pgmShwGet32BitPDEPtr(pVCpu, GCPtr);
                AssertReturn(pPde, VERR_INTERNAL_ERROR_3);
                Log(("pgmShwMakePageSupervisorAndWritable: PDE=%#llx", pPde->u));
                pPde->u |= X86_PDE_RW;
                Log(("-> PDE=%#llx (32)\n", pPde->u));
                break;
            }
            case PGMMODE_PAE:
            case PGMMODE_PAE_NX:
            {
                PX86PDEPAE pPde = pgmShwGetPaePDEPtr(pVCpu, GCPtr);
                AssertReturn(pPde, VERR_INTERNAL_ERROR_3);
                Log(("pgmShwMakePageSupervisorAndWritable: PDE=%#llx", pPde->u));
                pPde->u |= X86_PDE_RW;
                Log(("-> PDE=%#llx (PAE)\n", pPde->u));
                break;
            }
            default:
                AssertFailedReturn(VERR_INTERNAL_ERROR_4);
        }
    }
    return rc;
}


/**
 * Gets the shadow page directory for the specified address, PAE.
 *
 * @returns Pointer to the shadow PD.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       The address.
 * @param   uGstPdpe    Guest PDPT entry. Valid.
 * @param   ppPD        Receives address of page directory
 */
int pgmShwSyncPaePDPtr(PVMCPUCC pVCpu, RTGCPTR GCPtr, X86PGPAEUINT uGstPdpe, PX86PDPAE *ppPD)
{
    PVMCC          pVM      = pVCpu->CTX_SUFF(pVM);
    PPGMPOOL       pPool    = pVM->pgm.s.CTX_SUFF(pPool);
    PPGMPOOLPAGE   pShwPage;
    int            rc;
    PGM_LOCK_ASSERT_OWNER(pVM);


    /* Allocate page directory if not present. */
    const unsigned iPdPt     = (GCPtr >> X86_PDPT_SHIFT) & X86_PDPT_MASK_PAE;
    PX86PDPT       pPdpt     = pgmShwGetPaePDPTPtr(pVCpu);
    PX86PDPE       pPdpe     = &pPdpt->a[iPdPt];
    X86PGPAEUINT const uPdpe = pPdpe->u;
    if (uPdpe & (X86_PDPE_P | X86_PDPE_PG_MASK))
    {
        pShwPage = pgmPoolGetPage(pPool, uPdpe & X86_PDPE_PG_MASK);
        AssertReturn(pShwPage, VERR_PGM_POOL_GET_PAGE_FAILED);
        Assert((pPdpe->u & X86_PDPE_PG_MASK) == pShwPage->Core.Key);

        pgmPoolCacheUsed(pPool, pShwPage);

        /* Update the entry if necessary. */
        X86PGPAEUINT const uPdpeNew = pShwPage->Core.Key | (uGstPdpe & (X86_PDPE_P | X86_PDPE_A)) | (uPdpe & PGM_PDPT_FLAGS);
        if (uPdpeNew == uPdpe)
        { /* likely */ }
        else
            ASMAtomicWriteU64(&pPdpe->u, uPdpeNew);
    }
    else
    {
        RTGCPTR64   GCPdPt;
        PGMPOOLKIND enmKind;
        if (pVM->pgm.s.fNestedPaging || !CPUMIsGuestPagingEnabled(pVCpu))
        {
            /* AMD-V nested paging or real/protected mode without paging. */
            GCPdPt  = GCPtr & ~(RT_BIT_64(X86_PDPT_SHIFT) - 1);
            enmKind = PGMPOOLKIND_PAE_PD_PHYS;
        }
        else if (CPUMGetGuestCR4(pVCpu) & X86_CR4_PAE)
        {
            if (uGstPdpe & X86_PDPE_P)
            {
                GCPdPt  = uGstPdpe & X86_PDPE_PG_MASK;
                enmKind = PGMPOOLKIND_PAE_PD_FOR_PAE_PD;
            }
            else
            {
                /* PD not present; guest must reload CR3 to change it.
                 * No need to monitor anything in this case. */
                /** @todo r=bird: WTF is hit?!?   */
                /*Assert(VM_IS_RAW_MODE_ENABLED(pVM)); - ??? */
                GCPdPt  = uGstPdpe & X86_PDPE_PG_MASK;
                enmKind = PGMPOOLKIND_PAE_PD_PHYS;
                Assert(uGstPdpe & X86_PDPE_P); /* caller should do this already */
            }
        }
        else
        {
            GCPdPt  = CPUMGetGuestCR3(pVCpu);
            enmKind = (PGMPOOLKIND)(PGMPOOLKIND_PAE_PD0_FOR_32BIT_PD + iPdPt);
        }

        /* Create a reference back to the PDPT by using the index in its shadow page. */
        rc = pgmPoolAlloc(pVM, GCPdPt, enmKind, PGMPOOLACCESS_DONTCARE, PGM_A20_IS_ENABLED(pVCpu),
                          pVCpu->pgm.s.CTX_SUFF(pShwPageCR3)->idx, iPdPt, false /*fLockPage*/,
                          &pShwPage);
        AssertRCReturn(rc, rc);

        /* Hook it up. */
        ASMAtomicWriteU64(&pPdpe->u, pShwPage->Core.Key | (uGstPdpe & (X86_PDPE_P | X86_PDPE_A)) | (uPdpe & PGM_PDPT_FLAGS));
    }
    PGM_DYNMAP_UNUSED_HINT(pVCpu, pPdpe);

    *ppPD = (PX86PDPAE)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPage);
    return VINF_SUCCESS;
}


/**
 * Gets the pointer to the shadow page directory entry for an address, PAE.
 *
 * @returns Pointer to the PDE.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   GCPtr       The address.
 * @param   ppShwPde    Receives the address of the pgm pool page for the shadow page directory
 */
DECLINLINE(int) pgmShwGetPaePoolPagePD(PVMCPUCC pVCpu, RTGCPTR GCPtr, PPGMPOOLPAGE *ppShwPde)
{
    PVMCC pVM   = pVCpu->CTX_SUFF(pVM);
    PGM_LOCK_ASSERT_OWNER(pVM);

    PX86PDPT        pPdpt = pgmShwGetPaePDPTPtr(pVCpu);
    AssertReturn(pPdpt, VERR_PAGE_DIRECTORY_PTR_NOT_PRESENT);    /* can't happen */
    const unsigned  iPdPt = (GCPtr >> X86_PDPT_SHIFT) & X86_PDPT_MASK_PAE;
    X86PGPAEUINT const uPdpe = pPdpt->a[iPdPt].u;
    if (!(uPdpe & X86_PDPE_P))
    {
        LogFlow(("pgmShwGetPaePoolPagePD: PD %d not present (%RX64)\n", iPdPt, uPdpe));
        return VERR_PAGE_DIRECTORY_PTR_NOT_PRESENT;
    }
    AssertMsg(uPdpe & X86_PDPE_PG_MASK, ("GCPtr=%RGv\n", GCPtr));

    /* Fetch the pgm pool shadow descriptor. */
    PPGMPOOLPAGE pShwPde = pgmPoolGetPage(pVM->pgm.s.CTX_SUFF(pPool), uPdpe & X86_PDPE_PG_MASK);
    AssertReturn(pShwPde, VERR_PGM_POOL_GET_PAGE_FAILED);

    *ppShwPde = pShwPde;
    return VINF_SUCCESS;
}


/**
 * Syncs the SHADOW page directory pointer for the specified address.
 *
 * Allocates backing pages in case the PDPT or PML4 entry is missing.
 *
 * The caller is responsible for making sure the guest has a valid PD before
 * calling this function.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       The address.
 * @param   uGstPml4e   Guest PML4 entry (valid).
 * @param   uGstPdpe    Guest PDPT entry (valid).
 * @param   ppPD        Receives address of page directory
 */
static int pgmShwSyncLongModePDPtr(PVMCPUCC pVCpu, RTGCPTR64 GCPtr, X86PGPAEUINT uGstPml4e, X86PGPAEUINT uGstPdpe, PX86PDPAE *ppPD)
{
    PVMCC          pVM           = pVCpu->CTX_SUFF(pVM);
    PPGMPOOL       pPool         = pVM->pgm.s.CTX_SUFF(pPool);
    bool const     fNestedPagingOrNoGstPaging = pVM->pgm.s.fNestedPaging || !CPUMIsGuestPagingEnabled(pVCpu);
    int            rc;

    PGM_LOCK_ASSERT_OWNER(pVM);

    /*
     * PML4.
     */
    PPGMPOOLPAGE pShwPage;
    {
        const unsigned     iPml4  = (GCPtr >> X86_PML4_SHIFT) & X86_PML4_MASK;
        PX86PML4E          pPml4e = pgmShwGetLongModePML4EPtr(pVCpu, iPml4);
        AssertReturn(pPml4e, VERR_PGM_PML4_MAPPING);
        X86PGPAEUINT const uPml4e = pPml4e->u;

        /* Allocate page directory pointer table if not present. */
        if (uPml4e & (X86_PML4E_P | X86_PML4E_PG_MASK))
        {
            pShwPage = pgmPoolGetPage(pPool, uPml4e & X86_PML4E_PG_MASK);
            AssertReturn(pShwPage, VERR_PGM_POOL_GET_PAGE_FAILED);

            pgmPoolCacheUsed(pPool, pShwPage);

            /* Update the entry if needed. */
            X86PGPAEUINT const uPml4eNew = pShwPage->Core.Key | (uGstPml4e & pVCpu->pgm.s.fGstAmd64ShadowedPml4eMask)
                                         | (uPml4e & PGM_PML4_FLAGS);
            if (uPml4e == uPml4eNew)
            { /* likely */ }
            else
                ASMAtomicWriteU64(&pPml4e->u, uPml4eNew);
        }
        else
        {
            Assert(pVCpu->pgm.s.CTX_SUFF(pShwPageCR3));

            RTGCPTR64   GCPml4;
            PGMPOOLKIND enmKind;
            if (fNestedPagingOrNoGstPaging)
            {
                /* AMD-V nested paging or real/protected mode without paging */
                GCPml4  = (RTGCPTR64)iPml4 << X86_PML4_SHIFT; /** @todo bogus calculation for PML5 */
                enmKind = PGMPOOLKIND_64BIT_PDPT_FOR_PHYS;
            }
            else
            {
                GCPml4  = uGstPml4e & X86_PML4E_PG_MASK;
                enmKind = PGMPOOLKIND_64BIT_PDPT_FOR_64BIT_PDPT;
            }

            /* Create a reference back to the PDPT by using the index in its shadow page. */
            rc = pgmPoolAlloc(pVM, GCPml4, enmKind, PGMPOOLACCESS_DONTCARE, PGM_A20_IS_ENABLED(pVCpu),
                              pVCpu->pgm.s.CTX_SUFF(pShwPageCR3)->idx, iPml4, false /*fLockPage*/,
                              &pShwPage);
            AssertRCReturn(rc, rc);

            /* Hook it up. */
            ASMAtomicWriteU64(&pPml4e->u, pShwPage->Core.Key | (uGstPml4e & pVCpu->pgm.s.fGstAmd64ShadowedPml4eMask)
                                        | (uPml4e & PGM_PML4_FLAGS));
        }
    }

    /*
     * PDPT.
     */
    const unsigned     iPdPt = (GCPtr >> X86_PDPT_SHIFT) & X86_PDPT_MASK_AMD64;
    PX86PDPT           pPdpt = (PX86PDPT)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPage);
    PX86PDPE           pPdpe = &pPdpt->a[iPdPt];
    X86PGPAEUINT const uPdpe = pPdpe->u;

    /* Allocate page directory if not present. */
    if (uPdpe & (X86_PDPE_P | X86_PDPE_PG_MASK))
    {
        pShwPage = pgmPoolGetPage(pPool, uPdpe & X86_PDPE_PG_MASK);
        AssertReturn(pShwPage, VERR_PGM_POOL_GET_PAGE_FAILED);

        pgmPoolCacheUsed(pPool, pShwPage);

        /* Update the entry if needed. */
        X86PGPAEUINT const uPdpeNew = pShwPage->Core.Key | (uGstPdpe & pVCpu->pgm.s.fGstAmd64ShadowedPdpeMask)
                                     | (uPdpe & PGM_PDPT_FLAGS);
        if (uPdpe == uPdpeNew)
        { /* likely */ }
        else
            ASMAtomicWriteU64(&pPdpe->u, uPdpeNew);
    }
    else
    {
        RTGCPTR64   GCPdPt;
        PGMPOOLKIND enmKind;
        if (fNestedPagingOrNoGstPaging)
        {
            /* AMD-V nested paging or real/protected mode without paging */
            GCPdPt  = GCPtr & ~(RT_BIT_64(iPdPt << X86_PDPT_SHIFT) - 1);
            enmKind = PGMPOOLKIND_64BIT_PD_FOR_PHYS;
        }
        else
        {
            GCPdPt  = uGstPdpe & X86_PDPE_PG_MASK;
            enmKind = PGMPOOLKIND_64BIT_PD_FOR_64BIT_PD;
        }

        /* Create a reference back to the PDPT by using the index in its shadow page. */
        rc = pgmPoolAlloc(pVM, GCPdPt, enmKind, PGMPOOLACCESS_DONTCARE, PGM_A20_IS_ENABLED(pVCpu),
                          pShwPage->idx, iPdPt, false /*fLockPage*/,
                          &pShwPage);
        AssertRCReturn(rc, rc);

        /* Hook it up. */
        ASMAtomicWriteU64(&pPdpe->u,
                          pShwPage->Core.Key | (uGstPdpe & pVCpu->pgm.s.fGstAmd64ShadowedPdpeMask) | (uPdpe & PGM_PDPT_FLAGS));
    }

    *ppPD = (PX86PDPAE)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPage);
    return VINF_SUCCESS;
}


/**
 * Gets the SHADOW page directory pointer for the specified address (long mode).
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       The address.
 * @param   ppPml4e     Receives the address of the page map level 4 entry.
 * @param   ppPdpt      Receives the address of the page directory pointer table.
 * @param   ppPD        Receives the address of the page directory.
 */
DECLINLINE(int) pgmShwGetLongModePDPtr(PVMCPUCC pVCpu, RTGCPTR64 GCPtr, PX86PML4E *ppPml4e, PX86PDPT *ppPdpt, PX86PDPAE *ppPD)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    PGM_LOCK_ASSERT_OWNER(pVM);

    /*
     * PML4
     */
    const unsigned  iPml4 = (GCPtr >> X86_PML4_SHIFT) & X86_PML4_MASK;
    PCX86PML4E      pPml4e = pgmShwGetLongModePML4EPtr(pVCpu, iPml4);
    AssertReturn(pPml4e, VERR_PGM_PML4_MAPPING);
    if (ppPml4e)
        *ppPml4e = (PX86PML4E)pPml4e;
    X86PGPAEUINT const uPml4e = pPml4e->u;
    Log4(("pgmShwGetLongModePDPtr %RGv (%RHv) %RX64\n", GCPtr, pPml4e, uPml4e));
    if (!(uPml4e & X86_PML4E_P)) /** @todo other code is check for NULL page frame number! */
        return VERR_PAGE_MAP_LEVEL4_NOT_PRESENT;

    PPGMPOOL        pPool    = pVM->pgm.s.CTX_SUFF(pPool);
    PPGMPOOLPAGE    pShwPage = pgmPoolGetPage(pPool, uPml4e & X86_PML4E_PG_MASK);
    AssertReturn(pShwPage, VERR_PGM_POOL_GET_PAGE_FAILED);

    /*
     * PDPT
     */
    const unsigned      iPdPt = (GCPtr >> X86_PDPT_SHIFT) & X86_PDPT_MASK_AMD64;
    PCX86PDPT           pPdpt = *ppPdpt = (PX86PDPT)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPage);
    X86PGPAEUINT const  uPdpe = pPdpt->a[iPdPt].u;
    if (!(uPdpe & X86_PDPE_P)) /** @todo other code is check for NULL page frame number! */
        return VERR_PAGE_DIRECTORY_PTR_NOT_PRESENT;

    pShwPage = pgmPoolGetPage(pPool, uPdpe & X86_PDPE_PG_MASK);
    AssertReturn(pShwPage, VERR_PGM_POOL_GET_PAGE_FAILED);

    *ppPD = (PX86PDPAE)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPage);
    Log4(("pgmShwGetLongModePDPtr %RGv -> *ppPD=%p PDE=%p/%RX64\n", GCPtr, *ppPD, &(*ppPD)->a[(GCPtr >> X86_PD_PAE_SHIFT) & X86_PD_PAE_MASK], (*ppPD)->a[(GCPtr >> X86_PD_PAE_SHIFT) & X86_PD_PAE_MASK].u));
    return VINF_SUCCESS;
}


/**
 * Syncs the SHADOW EPT page directory pointer for the specified address. Allocates
 * backing pages in case the PDPT or PML4 entry is missing.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       The address.
 * @param   ppPdpt      Receives address of pdpt
 * @param   ppPD        Receives address of page directory
 */
static int pgmShwGetEPTPDPtr(PVMCPUCC pVCpu, RTGCPTR64 GCPtr, PEPTPDPT *ppPdpt, PEPTPD *ppPD)
{
    PVMCC          pVM   = pVCpu->CTX_SUFF(pVM);
    PPGMPOOL       pPool = pVM->pgm.s.CTX_SUFF(pPool);
    int            rc;

    Assert(pVM->pgm.s.fNestedPaging);
    PGM_LOCK_ASSERT_OWNER(pVM);

    /*
     * PML4 level.
     */
    PEPTPML4 pPml4 = (PEPTPML4)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pVCpu->pgm.s.CTX_SUFF(pShwPageCR3));
    Assert(pPml4);

    /* Allocate page directory pointer table if not present. */
    PPGMPOOLPAGE pShwPage;
    {
        const unsigned iPml4 = (GCPtr >> EPT_PML4_SHIFT) & EPT_PML4_MASK;
        PEPTPML4E pPml4e = &pPml4->a[iPml4];
        EPTPML4E Pml4e;
        Pml4e.u = pPml4e->u;
        if (!(Pml4e.u & (EPT_E_PG_MASK | EPT_E_READ)))
        {
            RTGCPTR64 GCPml4 = (RTGCPTR64)iPml4 << EPT_PML4_SHIFT;
            rc = pgmPoolAlloc(pVM, GCPml4, PGMPOOLKIND_EPT_PDPT_FOR_PHYS, PGMPOOLACCESS_DONTCARE, PGM_A20_IS_ENABLED(pVCpu),
                              pVCpu->pgm.s.CTX_SUFF(pShwPageCR3)->idx, iPml4, false /*fLockPage*/,
                              &pShwPage);
            AssertRCReturn(rc, rc);

            /* Hook up the new PDPT now. */
            ASMAtomicWriteU64(&pPml4e->u, pShwPage->Core.Key | EPT_E_READ | EPT_E_WRITE | EPT_E_EXECUTE);
        }
        else
        {
            pShwPage = pgmPoolGetPage(pPool, pPml4e->u & EPT_PML4E_PG_MASK);
            AssertReturn(pShwPage, VERR_PGM_POOL_GET_PAGE_FAILED);

            pgmPoolCacheUsed(pPool, pShwPage);

            /* Hook up the cached PDPT if needed (probably not given 512*512 PTs to sync). */
            if (Pml4e.u == (pShwPage->Core.Key | EPT_E_READ | EPT_E_WRITE | EPT_E_EXECUTE))
            { }
            else
                ASMAtomicWriteU64(&pPml4e->u, pShwPage->Core.Key | EPT_E_READ | EPT_E_WRITE | EPT_E_EXECUTE);
        }
    }

    /*
     * PDPT level.
     */
    const unsigned iPdPt = (GCPtr >> EPT_PDPT_SHIFT) & EPT_PDPT_MASK;
    PEPTPDPT  pPdpt = (PEPTPDPT)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPage);
    PEPTPDPTE pPdpe = &pPdpt->a[iPdPt];

    if (ppPdpt)
        *ppPdpt = pPdpt;

    /* Allocate page directory if not present. */
    EPTPDPTE Pdpe;
    Pdpe.u = pPdpe->u;
    if (!(Pdpe.u & (EPT_E_PG_MASK | EPT_E_READ)))
    {
        RTGCPTR64 const GCPdPt = GCPtr & ~(RT_BIT_64(EPT_PDPT_SHIFT) - 1);
        rc = pgmPoolAlloc(pVM, GCPdPt, PGMPOOLKIND_EPT_PD_FOR_PHYS, PGMPOOLACCESS_DONTCARE, PGM_A20_IS_ENABLED(pVCpu),
                          pShwPage->idx, iPdPt, false /*fLockPage*/,
                          &pShwPage);
        AssertRCReturn(rc, rc);

        /* Hook up the new PD now. */
        ASMAtomicWriteU64(&pPdpe->u, pShwPage->Core.Key | EPT_E_READ | EPT_E_WRITE | EPT_E_EXECUTE);
    }
    else
    {
        pShwPage = pgmPoolGetPage(pPool, pPdpe->u & EPT_PDPTE_PG_MASK);
        AssertReturn(pShwPage, VERR_PGM_POOL_GET_PAGE_FAILED);

        pgmPoolCacheUsed(pPool, pShwPage);

        /* Hook up the cached PD if needed (probably not given there are 512 PTs we may need sync). */
        if (Pdpe.u == (pShwPage->Core.Key | EPT_E_READ | EPT_E_WRITE | EPT_E_EXECUTE))
        { }
        else
            ASMAtomicWriteU64(&pPdpe->u, pShwPage->Core.Key | EPT_E_READ | EPT_E_WRITE | EPT_E_EXECUTE);
    }

    *ppPD = (PEPTPD)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPage);
    return VINF_SUCCESS;
}


#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
/**
 * Syncs the SHADOW nested-guest page directory pointer for the specified address.
 * Allocates backing pages in case the PDPT or PML4 entry is missing.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   GCPhysNested    The nested-guest physical address.
 * @param   ppPdpt          Where to store the PDPT. Optional, can be NULL.
 * @param   ppPD            Where to store the PD. Optional, can be NULL.
 * @param   pGstWalkAll     The guest walk info.
 */
static int pgmShwGetNestedEPTPDPtr(PVMCPUCC pVCpu, RTGCPTR64 GCPhysNested, PEPTPDPT *ppPdpt, PEPTPD *ppPD,
                                   PPGMPTWALKGST pGstWalkAll)
{
    PVMCC    pVM   = pVCpu->CTX_SUFF(pVM);
    PPGMPOOL pPool = pVM->pgm.s.CTX_SUFF(pPool);
    int      rc;

    PPGMPOOLPAGE pShwPage;
    Assert(pVM->pgm.s.fNestedPaging);
    Assert(pVCpu->pgm.s.enmGuestSlatMode == PGMSLAT_EPT);
    PGM_LOCK_ASSERT_OWNER(pVM);

    /*
     * PML4 level.
     */
    {
        PEPTPML4 pPml4 = (PEPTPML4)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pVCpu->pgm.s.CTX_SUFF(pShwPageCR3));
        Assert(pPml4);

        /* Allocate page directory pointer table if not present. */
        {
            uint64_t const fShwFlags = pGstWalkAll->u.Ept.Pml4e.u & pVCpu->pgm.s.fGstEptShadowedPml4eMask;
            const unsigned iPml4e    = (GCPhysNested >> EPT_PML4_SHIFT) & EPT_PML4_MASK;
            PEPTPML4E      pPml4e    = &pPml4->a[iPml4e];

            if (!(pPml4e->u & (EPT_E_PG_MASK | EPT_PRESENT_MASK)))
            {
                RTGCPHYS const GCPhysPdpt = pGstWalkAll->u.Ept.Pml4e.u & EPT_PML4E_PG_MASK;
                rc = pgmPoolAlloc(pVM, GCPhysPdpt, PGMPOOLKIND_EPT_PDPT_FOR_EPT_PDPT, PGMPOOLACCESS_DONTCARE,
                                  PGM_A20_IS_ENABLED(pVCpu), pVCpu->pgm.s.CTX_SUFF(pShwPageCR3)->idx, iPml4e, false /*fLockPage*/,
                                  &pShwPage);
                AssertRCReturn(rc, rc);

                /* Hook up the new PDPT now. */
                ASMAtomicWriteU64(&pPml4e->u, pShwPage->Core.Key | fShwFlags);
            }
            else
            {
                pShwPage = pgmPoolGetPage(pPool, pPml4e->u & EPT_PML4E_PG_MASK);
                AssertReturn(pShwPage, VERR_PGM_POOL_GET_PAGE_FAILED);

                pgmPoolCacheUsed(pPool, pShwPage);

                /* Hook up the cached PDPT if needed (probably not given 512*512 PTs to sync). */
                if (pPml4e->u != (pShwPage->Core.Key | fShwFlags))
                    ASMAtomicWriteU64(&pPml4e->u, pShwPage->Core.Key | fShwFlags);
            }
            Assert(PGMPOOL_PAGE_IS_NESTED(pShwPage));
            Log7Func(("GstPml4e=%RX64 ShwPml4e=%RX64 iPml4e=%u\n", pGstWalkAll->u.Ept.Pml4e.u, pPml4e->u, iPml4e));
        }
    }

    /*
     * PDPT level.
     */
    {
        AssertReturn(!(pGstWalkAll->u.Ept.Pdpte.u & EPT_E_LEAF), VERR_NOT_SUPPORTED); /* shadowing 1GB pages not supported yet. */

        PEPTPDPT pPdpt = (PEPTPDPT)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPage);
        if (ppPdpt)
            *ppPdpt = pPdpt;

        uint64_t const fShwFlags = pGstWalkAll->u.Ept.Pdpte.u & pVCpu->pgm.s.fGstEptShadowedPdpteMask;
        const unsigned iPdPte    = (GCPhysNested >> EPT_PDPT_SHIFT) & EPT_PDPT_MASK;
        PEPTPDPTE      pPdpte    = &pPdpt->a[iPdPte];

        if (!(pPdpte->u & (EPT_E_PG_MASK | EPT_PRESENT_MASK)))
        {
            RTGCPHYS const GCPhysPd = pGstWalkAll->u.Ept.Pdpte.u & EPT_PDPTE_PG_MASK;
            rc = pgmPoolAlloc(pVM, GCPhysPd, PGMPOOLKIND_EPT_PD_FOR_EPT_PD, PGMPOOLACCESS_DONTCARE, PGM_A20_IS_ENABLED(pVCpu),
                              pShwPage->idx, iPdPte, false /*fLockPage*/, &pShwPage);
            AssertRCReturn(rc, rc);

            /* Hook up the new PD now. */
            ASMAtomicWriteU64(&pPdpte->u, pShwPage->Core.Key | fShwFlags);
        }
        else
        {
            pShwPage = pgmPoolGetPage(pPool, pPdpte->u & EPT_PDPTE_PG_MASK);
            AssertReturn(pShwPage, VERR_PGM_POOL_GET_PAGE_FAILED);

            pgmPoolCacheUsed(pPool, pShwPage);

            /* Hook up the cached PD if needed (probably not given there are 512 PTs we may need sync). */
            if (pPdpte->u != (pShwPage->Core.Key | fShwFlags))
                ASMAtomicWriteU64(&pPdpte->u, pShwPage->Core.Key | fShwFlags);
        }
        Assert(PGMPOOL_PAGE_IS_NESTED(pShwPage));
        Log7Func(("GstPdpte=%RX64 ShwPdpte=%RX64 iPdPte=%u \n", pGstWalkAll->u.Ept.Pdpte.u, pPdpte->u, iPdPte));

        *ppPD = (PEPTPD)PGMPOOL_PAGE_2_PTR_V2(pVM, pVCpu, pShwPage);
    }

    return VINF_SUCCESS;
}
#endif /* VBOX_WITH_NESTED_HWVIRT_VMX_EPT */


#ifdef IN_RING0
/**
 * Synchronizes a range of nested page table entries.
 *
 * The caller must own the PGM lock.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 * @param   GCPhys              Where to start.
 * @param   cPages              How many pages which entries should be synced.
 * @param   enmShwPagingMode    The shadow paging mode (PGMMODE_EPT for VT-x,
 *                              host paging mode for AMD-V).
 */
int pgmShwSyncNestedPageLocked(PVMCPUCC pVCpu, RTGCPHYS GCPhys, uint32_t cPages, PGMMODE enmShwPagingMode)
{
    PGM_LOCK_ASSERT_OWNER(pVCpu->CTX_SUFF(pVM));

/** @todo r=bird: Gotta love this nested paging hacking we're still carrying with us... (Split PGM_TYPE_NESTED.) */
    int rc;
    switch (enmShwPagingMode)
    {
        case PGMMODE_32_BIT:
        {
            X86PDE PdeDummy = { X86_PDE_P | X86_PDE_US | X86_PDE_RW | X86_PDE_A };
            rc = PGM_BTH_NAME_32BIT_PROT(SyncPage)(pVCpu, PdeDummy, GCPhys, cPages, ~0U /*uErr*/);
            break;
        }

        case PGMMODE_PAE:
        case PGMMODE_PAE_NX:
        {
            X86PDEPAE PdeDummy = { X86_PDE_P | X86_PDE_US | X86_PDE_RW | X86_PDE_A };
            rc = PGM_BTH_NAME_PAE_PROT(SyncPage)(pVCpu, PdeDummy, GCPhys, cPages, ~0U /*uErr*/);
            break;
        }

        case PGMMODE_AMD64:
        case PGMMODE_AMD64_NX:
        {
            X86PDEPAE PdeDummy = { X86_PDE_P | X86_PDE_US | X86_PDE_RW | X86_PDE_A };
            rc = PGM_BTH_NAME_AMD64_PROT(SyncPage)(pVCpu, PdeDummy, GCPhys, cPages, ~0U /*uErr*/);
            break;
        }

        case PGMMODE_EPT:
        {
            X86PDEPAE PdeDummy = { X86_PDE_P | X86_PDE_US | X86_PDE_RW | X86_PDE_A };
            rc = PGM_BTH_NAME_EPT_PROT(SyncPage)(pVCpu, PdeDummy, GCPhys, cPages, ~0U /*uErr*/);
            break;
        }

        default:
            AssertMsgFailedReturn(("%d\n", enmShwPagingMode), VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }
    return rc;
}
#endif /* IN_RING0 */


/**
 * Gets effective Guest OS page information.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   GCPtr       Guest Context virtual address of the page.
 * @param   pWalk       Where to store the page walk information.
 * @thread  EMT(pVCpu)
 */
VMMDECL(int) PGMGstGetPage(PVMCPUCC pVCpu, RTGCPTR GCPtr, PPGMPTWALK pWalk)
{
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(pWalk);
    uintptr_t idx = pVCpu->pgm.s.idxGuestModeData;
    AssertReturn(idx < RT_ELEMENTS(g_aPgmGuestModeData), VERR_PGM_MODE_IPE);
    AssertReturn(g_aPgmGuestModeData[idx].pfnGetPage, VERR_PGM_MODE_IPE);
    return g_aPgmGuestModeData[idx].pfnGetPage(pVCpu, GCPtr, pWalk);
}


/**
 * Gets effective Guest OS page information.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   GCPtr       Guest Context virtual address of the page.
 * @param   fFlags      PGMQPAGE_F_XXX. If zero, no accessed or dirty bits will
 *                      be set.
 * @param   pWalk       Where to store the page walk information.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(int) PGMGstQueryPageFast(PVMCPUCC pVCpu, RTGCPTR GCPtr, uint32_t fFlags, PPGMPTWALKFAST pWalk)
{
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(pWalk);
    Assert(!(fFlags & ~(PGMQPAGE_F_VALID_MASK)));
    Assert(!(fFlags & PGMQPAGE_F_EXECUTE) || !(fFlags & PGMQPAGE_F_WRITE));
    uintptr_t idx = pVCpu->pgm.s.idxGuestModeData;
    AssertReturn(idx < RT_ELEMENTS(g_aPgmGuestModeData), VERR_PGM_MODE_IPE);
    AssertReturn(g_aPgmGuestModeData[idx].pfnGetPage, VERR_PGM_MODE_IPE);
    return g_aPgmGuestModeData[idx].pfnQueryPageFast(pVCpu, GCPtr, fFlags, pWalk);
}


/**
 * Maps the guest CR3.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   GCPhysCr3       The guest CR3 value.
 * @param   pHCPtrGuestCr3  Where to store the mapped memory.
 */
DECLINLINE(int) pgmGstMapCr3(PVMCPUCC pVCpu, RTGCPHYS GCPhysCr3, PRTHCPTR pHCPtrGuestCr3)
{
    /** @todo this needs some reworking wrt. locking?  */
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    PGM_LOCK_VOID(pVM);
    PPGMPAGE pPageCr3 = pgmPhysGetPage(pVM, GCPhysCr3);
    AssertReturnStmt(pPageCr3, PGM_UNLOCK(pVM), VERR_PGM_INVALID_CR3_ADDR);

    RTHCPTR HCPtrGuestCr3;
    int rc = pgmPhysGCPhys2CCPtrInternalDepr(pVM, pPageCr3, GCPhysCr3, (void **)&HCPtrGuestCr3);
    PGM_UNLOCK(pVM);

    *pHCPtrGuestCr3 = HCPtrGuestCr3;
    return rc;
}


#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
/**
 * Unmaps the guest CR3.
 *
 * @returns VBox status code.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
DECLINLINE(int) pgmGstUnmapCr3(PVMCPUCC pVCpu)
{
    uintptr_t const idxBth = pVCpu->pgm.s.idxBothModeData;
    AssertReturn(idxBth < RT_ELEMENTS(g_aPgmBothModeData), VERR_PGM_MODE_IPE);
    AssertReturn(g_aPgmBothModeData[idxBth].pfnUnmapCR3, VERR_PGM_MODE_IPE);
    return g_aPgmBothModeData[idxBth].pfnUnmapCR3(pVCpu);
}
#endif


/**
 * Performs a guest page table walk.
 *
 * The guest should be in paged protect mode or long mode when making a call to
 * this function.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PAGE_TABLE_NOT_PRESENT on failure.  Check pWalk for details.
 * @retval  VERR_PGM_NOT_USED_IN_MODE if not paging isn't enabled. @a pWalk is
 *          not valid, except enmType is PGMPTWALKGSTTYPE_INVALID.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   GCPtr       The guest virtual address to walk by.
 * @param   pWalk       Where to return the walk result. This is valid for some
 *                      error codes as well.
 * @param   pGstWalk    The guest mode specific page walk information.
 */
int pgmGstPtWalk(PVMCPUCC pVCpu, RTGCPTR GCPtr, PPGMPTWALK pWalk, PPGMPTWALKGST pGstWalk)
{
    VMCPU_ASSERT_EMT(pVCpu);
    switch (pVCpu->pgm.s.enmGuestMode)
    {
        case PGMMODE_32_BIT:
            pGstWalk->enmType = PGMPTWALKGSTTYPE_32BIT;
            return PGM_GST_NAME_32BIT(Walk)(pVCpu, GCPtr, pWalk, &pGstWalk->u.Legacy);

        case PGMMODE_PAE:
        case PGMMODE_PAE_NX:
            pGstWalk->enmType = PGMPTWALKGSTTYPE_PAE;
            return PGM_GST_NAME_PAE(Walk)(pVCpu, GCPtr, pWalk, &pGstWalk->u.Pae);

        case PGMMODE_AMD64:
        case PGMMODE_AMD64_NX:
            pGstWalk->enmType = PGMPTWALKGSTTYPE_AMD64;
            return PGM_GST_NAME_AMD64(Walk)(pVCpu, GCPtr, pWalk, &pGstWalk->u.Amd64);

        case PGMMODE_REAL:
        case PGMMODE_PROTECTED:
            pGstWalk->enmType = PGMPTWALKGSTTYPE_INVALID;
            return VERR_PGM_NOT_USED_IN_MODE;

        case PGMMODE_EPT:
        case PGMMODE_NESTED_32BIT:
        case PGMMODE_NESTED_PAE:
        case PGMMODE_NESTED_AMD64:
        default:
            AssertFailed();
            pGstWalk->enmType = PGMPTWALKGSTTYPE_INVALID;
            return VERR_PGM_NOT_USED_IN_MODE;
    }
}


#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
/**
 * Performs a guest second-level address translation (SLAT).
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PAGE_TABLE_NOT_PRESENT on failure.  Check pWalk for details.
 * @retval  VERR_PGM_NOT_USED_IN_MODE if not paging isn't enabled. @a pWalk is
 *          not valid, except enmType is PGMPTWALKGSTTYPE_INVALID.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 * @param   GCPhysNested        The nested-guest physical address being translated.
 * @param   fIsLinearAddrValid  Whether the linear address in @a GCPtrNested is the
 *                              cause for this translation.
 * @param   GCPtrNested         The nested-guest virtual address that initiated the
 *                              SLAT. If none, pass 0 (and not NIL_RTGCPTR).
 * @param   pWalk               Where to return the walk result. This is updated for
 *                              all error codes other than
 *                              VERR_PGM_NOT_USED_IN_MODE.
 * @param   pGstWalk            Where to store the second-level paging-mode specific
 *                              walk info.
 */
static int pgmGstSlatWalk(PVMCPUCC pVCpu, RTGCPHYS GCPhysNested, bool fIsLinearAddrValid, RTGCPTR GCPtrNested,
                          PPGMPTWALK pWalk, PPGMPTWALKGST pGstWalk)
{
    /* SLAT mode must be valid at this point as this should only be used -after- we have determined SLAT mode. */
    Assert(   pVCpu->pgm.s.enmGuestSlatMode != PGMSLAT_DIRECT
           && pVCpu->pgm.s.enmGuestSlatMode != PGMSLAT_INVALID);
    AssertPtr(pWalk);
    AssertPtr(pGstWalk);
    switch (pVCpu->pgm.s.enmGuestSlatMode)
    {
        case PGMSLAT_EPT:
            pGstWalk->enmType = PGMPTWALKGSTTYPE_EPT;
            return PGM_GST_SLAT_NAME_EPT(Walk)(pVCpu, GCPhysNested, fIsLinearAddrValid, GCPtrNested, pWalk, &pGstWalk->u.Ept);

        default:
            AssertFailed();
            pGstWalk->enmType = PGMPTWALKGSTTYPE_INVALID;
            return VERR_PGM_NOT_USED_IN_MODE;
    }
}
#endif /* VBOX_WITH_NESTED_HWVIRT_VMX_EPT */


/**
 * Tries to continue the previous walk.
 *
 * @note    Requires the caller to hold the PGM lock from the first
 *          pgmGstPtWalk() call to the last pgmGstPtWalkNext() call.  Otherwise
 *          we cannot use the pointers.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PAGE_TABLE_NOT_PRESENT on failure.  Check pWalk for details.
 * @retval  VERR_PGM_NOT_USED_IN_MODE if not paging isn't enabled. @a pWalk is
 *          not valid, except enmType is PGMPTWALKGSTTYPE_INVALID.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   GCPtr       The guest virtual address to walk by.
 * @param   pWalk       Pointer to the previous walk result and where to return
 *                      the result of this walk.  This is valid for some error
 *                      codes as well.
 * @param   pGstWalk    The guest-mode specific walk information.
 */
int pgmGstPtWalkNext(PVMCPUCC pVCpu, RTGCPTR GCPtr, PPGMPTWALK pWalk, PPGMPTWALKGST pGstWalk)
{
    /*
     * We can only handle successfully walks.
     * We also limit ourselves to the next page.
     */
    if (   pWalk->fSucceeded
        && GCPtr - pWalk->GCPtr == GUEST_PAGE_SIZE)
    {
        Assert(pWalk->uLevel == 0);
        if (pGstWalk->enmType == PGMPTWALKGSTTYPE_AMD64)
        {
            /*
             * AMD64
             */
            if (!pWalk->fGigantPage && !pWalk->fBigPage)
            {
                /*
                 * We fall back to full walk if the PDE table changes, if any
                 * reserved bits are set, or if the effective page access changes.
                 */
                const uint64_t fPteSame = X86_PTE_P   | X86_PTE_RW | X86_PTE_US     | X86_PTE_PWT
                                        | X86_PTE_PCD | X86_PTE_A  | X86_PTE_PAE_NX;
                const uint64_t fPdeSame = X86_PDE_P   | X86_PDE_RW | X86_PDE_US     | X86_PDE_PWT
                                        | X86_PDE_PCD | X86_PDE_A  | X86_PDE_PAE_NX | X86_PDE_PS;

                if ((GCPtr >> X86_PD_PAE_SHIFT) == (pWalk->GCPtr >> X86_PD_PAE_SHIFT))
                {
                    if (pGstWalk->u.Amd64.pPte)
                    {
                        X86PTEPAE Pte;
                        Pte.u = pGstWalk->u.Amd64.pPte[1].u;
                        if (   (Pte.u & fPteSame) == (pGstWalk->u.Amd64.Pte.u & fPteSame)
                            && !(Pte.u & (pVCpu)->pgm.s.fGstAmd64MbzPteMask))
                        {
                            pWalk->GCPtr  = GCPtr;
                            pWalk->GCPhys = Pte.u & X86_PTE_PAE_PG_MASK;
                            pGstWalk->u.Amd64.Pte.u = Pte.u;
                            pGstWalk->u.Amd64.pPte++;
                            return VINF_SUCCESS;
                        }
                    }
                }
                else if ((GCPtr >> X86_PDPT_SHIFT) == (pWalk->GCPtr >> X86_PDPT_SHIFT))
                {
                    Assert(!((GCPtr >> X86_PT_PAE_SHIFT) & X86_PT_PAE_MASK)); /* Must be first PT entry. */
                    if (pGstWalk->u.Amd64.pPde)
                    {
                        X86PDEPAE Pde;
                        Pde.u = pGstWalk->u.Amd64.pPde[1].u;
                        if (   (Pde.u & fPdeSame) == (pGstWalk->u.Amd64.Pde.u & fPdeSame)
                            && !(Pde.u & (pVCpu)->pgm.s.fGstAmd64MbzPdeMask))
                        {
                            /* Get the new PTE and check out the first entry. */
                            int rc = PGM_GCPHYS_2_PTR_BY_VMCPU(pVCpu, PGM_A20_APPLY(pVCpu, (Pde.u & X86_PDE_PAE_PG_MASK)),
                                                               &pGstWalk->u.Amd64.pPt);
                            if (RT_SUCCESS(rc))
                            {
                                pGstWalk->u.Amd64.pPte = &pGstWalk->u.Amd64.pPt->a[0];
                                X86PTEPAE Pte;
                                Pte.u = pGstWalk->u.Amd64.pPte->u;
                                if (   (Pte.u & fPteSame) == (pGstWalk->u.Amd64.Pte.u & fPteSame)
                                    && !(Pte.u & (pVCpu)->pgm.s.fGstAmd64MbzPteMask))
                                {
                                    pWalk->GCPtr  = GCPtr;
                                    pWalk->GCPhys = Pte.u & X86_PTE_PAE_PG_MASK;
                                    pGstWalk->u.Amd64.Pte.u = Pte.u;
                                    pGstWalk->u.Amd64.Pde.u = Pde.u;
                                    pGstWalk->u.Amd64.pPde++;
                                    return VINF_SUCCESS;
                                }
                            }
                        }
                    }
                }
            }
            else if (!pWalk->fGigantPage)
            {
                if ((GCPtr & X86_PAGE_2M_BASE_MASK) == (pWalk->GCPtr & X86_PAGE_2M_BASE_MASK))
                {
                    pWalk->GCPtr   = GCPtr;
                    pWalk->GCPhys += GUEST_PAGE_SIZE;
                    return VINF_SUCCESS;
                }
            }
            else
            {
                if ((GCPtr & X86_PAGE_1G_BASE_MASK) == (pWalk->GCPtr & X86_PAGE_1G_BASE_MASK))
                {
                    pWalk->GCPtr   = GCPtr;
                    pWalk->GCPhys += GUEST_PAGE_SIZE;
                    return VINF_SUCCESS;
                }
            }
        }
    }
    /* Case we don't handle.  Do full walk. */
    return pgmGstPtWalk(pVCpu, GCPtr, pWalk, pGstWalk);
}


/**
 * Modify page flags for a range of pages in the guest's tables
 *
 * The existing flags are ANDed with the fMask and ORed with the fFlags.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       Virtual address of the first page in the range.
 * @param   cb          Size (in bytes) of the range to apply the modification to.
 * @param   fFlags      The OR  mask - page flags X86_PTE_*, excluding the page mask of course.
 * @param   fMask       The AND mask - page flags X86_PTE_*, excluding the page mask of course.
 *                      Be very CAREFUL when ~'ing constants which could be 32-bit!
 */
VMMDECL(int)  PGMGstModifyPage(PVMCPUCC pVCpu, RTGCPTR GCPtr, size_t cb, uint64_t fFlags, uint64_t fMask)
{
    STAM_PROFILE_START(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,GstModifyPage), a);
    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * Validate input.
     */
    AssertMsg(!(fFlags & X86_PTE_PAE_PG_MASK), ("fFlags=%#llx\n", fFlags));
    Assert(cb);

    LogFlow(("PGMGstModifyPage %RGv %d bytes fFlags=%08llx fMask=%08llx\n", GCPtr, cb, fFlags, fMask));

    /*
     * Adjust input.
     */
    cb     += GCPtr & GUEST_PAGE_OFFSET_MASK;
    cb      = RT_ALIGN_Z(cb, GUEST_PAGE_SIZE);
    GCPtr  &= ~(RTGCPTR)GUEST_PAGE_OFFSET_MASK;

    /*
     * Call worker.
     */
    uintptr_t idx = pVCpu->pgm.s.idxGuestModeData;
    AssertReturn(idx < RT_ELEMENTS(g_aPgmGuestModeData), VERR_PGM_MODE_IPE);
    AssertReturn(g_aPgmGuestModeData[idx].pfnModifyPage, VERR_PGM_MODE_IPE);
    int rc = g_aPgmGuestModeData[idx].pfnModifyPage(pVCpu, GCPtr, cb, fFlags, fMask);

    STAM_PROFILE_STOP(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,GstModifyPage), a);
    return rc;
}


/**
 * Checks whether the given PAE PDPEs are potentially valid for the guest.
 *
 * @returns @c true if the PDPE is valid, @c false otherwise.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   paPaePdpes  The PAE PDPEs to validate.
 *
 * @remarks This function -only- checks the reserved bits in the PDPE entries.
 */
VMM_INT_DECL(bool) PGMGstArePaePdpesValid(PVMCPUCC pVCpu, PCX86PDPE paPaePdpes)
{
    Assert(paPaePdpes);
    for (unsigned i = 0; i < X86_PG_PAE_PDPE_ENTRIES; i++)
    {
        X86PDPE const PaePdpe = paPaePdpes[i];
        if (   !(PaePdpe.u & X86_PDPE_P)
            || !(PaePdpe.u & pVCpu->pgm.s.fGstPaeMbzPdpeMask))
        { /* likely */ }
        else
            return false;
    }
    return true;
}


/**
 * Performs the lazy mapping of the 32-bit guest PD.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   ppPd        Where to return the pointer to the mapping.  This is
 *                      always set.
 */
int pgmGstLazyMap32BitPD(PVMCPUCC pVCpu, PX86PD *ppPd)
{
    PVMCC       pVM = pVCpu->CTX_SUFF(pVM);
    PGM_LOCK_VOID(pVM);

    Assert(!pVCpu->pgm.s.CTX_SUFF(pGst32BitPd));

    RTGCPHYS    GCPhysCR3 = pgmGetGuestMaskedCr3(pVCpu, pVCpu->pgm.s.GCPhysCR3);
    PPGMPAGE    pPage;
    int rc = pgmPhysGetPageEx(pVM, GCPhysCR3, &pPage);
    if (RT_SUCCESS(rc))
    {
        rc = pgmPhysGCPhys2CCPtrInternalDepr(pVM, pPage, GCPhysCR3, (void **)ppPd);
        if (RT_SUCCESS(rc))
        {
# ifdef IN_RING3
            pVCpu->pgm.s.pGst32BitPdR0 = NIL_RTR0PTR;
            pVCpu->pgm.s.pGst32BitPdR3 = *ppPd;
# else
            pVCpu->pgm.s.pGst32BitPdR3 = NIL_RTR0PTR;
            pVCpu->pgm.s.pGst32BitPdR0 = *ppPd;
# endif
            PGM_UNLOCK(pVM);
            return VINF_SUCCESS;
        }
        AssertRC(rc);
    }
    PGM_UNLOCK(pVM);

    *ppPd = NULL;
    return rc;
}


/**
 * Performs the lazy mapping of the PAE guest PDPT.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   ppPdpt      Where to return the pointer to the mapping.  This is
 *                      always set.
 */
int pgmGstLazyMapPaePDPT(PVMCPUCC pVCpu, PX86PDPT *ppPdpt)
{
    Assert(!pVCpu->pgm.s.CTX_SUFF(pGstPaePdpt));
    PVMCC       pVM = pVCpu->CTX_SUFF(pVM);
    PGM_LOCK_VOID(pVM);

    RTGCPHYS    GCPhysCR3 = pgmGetGuestMaskedCr3(pVCpu, pVCpu->pgm.s.GCPhysCR3);
    PPGMPAGE    pPage;
    int rc = pgmPhysGetPageEx(pVM, GCPhysCR3, &pPage);
    if (RT_SUCCESS(rc))
    {
        rc = pgmPhysGCPhys2CCPtrInternalDepr(pVM, pPage, GCPhysCR3, (void **)ppPdpt);
        if (RT_SUCCESS(rc))
        {
# ifdef IN_RING3
            pVCpu->pgm.s.pGstPaePdptR0 = NIL_RTR0PTR;
            pVCpu->pgm.s.pGstPaePdptR3 = *ppPdpt;
# else
            pVCpu->pgm.s.pGstPaePdptR3 = NIL_RTR3PTR;
            pVCpu->pgm.s.pGstPaePdptR0 = *ppPdpt;
# endif
            PGM_UNLOCK(pVM);
            return VINF_SUCCESS;
        }
        AssertRC(rc);
    }

    PGM_UNLOCK(pVM);
    *ppPdpt = NULL;
    return rc;
}


/**
 * Performs the lazy mapping / updating of a PAE guest PD.
 *
 * @returns Pointer to the mapping.
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   iPdpt       Which PD entry to map (0..3).
 * @param   ppPd        Where to return the pointer to the mapping.  This is
 *                      always set.
 */
int pgmGstLazyMapPaePD(PVMCPUCC pVCpu, uint32_t iPdpt, PX86PDPAE *ppPd)
{
    PVMCC           pVM         = pVCpu->CTX_SUFF(pVM);
    PGM_LOCK_VOID(pVM);

    PX86PDPT        pGuestPDPT  = pVCpu->pgm.s.CTX_SUFF(pGstPaePdpt);
    Assert(pGuestPDPT);
    Assert(pGuestPDPT->a[iPdpt].u & X86_PDPE_P);
    RTGCPHYS        GCPhys      = pGuestPDPT->a[iPdpt].u & X86_PDPE_PG_MASK;
    bool const      fChanged    = pVCpu->pgm.s.aGCPhysGstPaePDs[iPdpt] != GCPhys;

    PPGMPAGE        pPage;
    int rc = pgmPhysGetPageEx(pVM, GCPhys, &pPage);
    if (RT_SUCCESS(rc))
    {
        rc = pgmPhysGCPhys2CCPtrInternalDepr(pVM, pPage, GCPhys, (void **)ppPd);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
# ifdef IN_RING3
            pVCpu->pgm.s.apGstPaePDsR0[iPdpt]          = NIL_RTR0PTR;
            pVCpu->pgm.s.apGstPaePDsR3[iPdpt]          = *ppPd;
# else
            pVCpu->pgm.s.apGstPaePDsR3[iPdpt]          = NIL_RTR3PTR;
            pVCpu->pgm.s.apGstPaePDsR0[iPdpt]          = *ppPd;
# endif
            if (fChanged)
                pVCpu->pgm.s.aGCPhysGstPaePDs[iPdpt]   = GCPhys;
            PGM_UNLOCK(pVM);
            return VINF_SUCCESS;
        }
    }

    /* Invalid page or some failure, invalidate the entry. */
    pVCpu->pgm.s.aGCPhysGstPaePDs[iPdpt]   = NIL_RTGCPHYS;
    pVCpu->pgm.s.apGstPaePDsR3[iPdpt]      = NIL_RTR3PTR;
    pVCpu->pgm.s.apGstPaePDsR0[iPdpt]      = NIL_RTR0PTR;

    PGM_UNLOCK(pVM);
    return rc;
}


/**
 * Performs the lazy mapping of the 32-bit guest PD.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   ppPml4      Where to return the pointer to the mapping.  This will
 *                      always be set.
 */
int pgmGstLazyMapPml4(PVMCPUCC pVCpu, PX86PML4 *ppPml4)
{
    Assert(!pVCpu->pgm.s.CTX_SUFF(pGstAmd64Pml4));
    PVMCC       pVM = pVCpu->CTX_SUFF(pVM);
    PGM_LOCK_VOID(pVM);

    RTGCPHYS    GCPhysCR3 = pgmGetGuestMaskedCr3(pVCpu, pVCpu->pgm.s.GCPhysCR3);
    PPGMPAGE    pPage;
    int rc = pgmPhysGetPageEx(pVM, GCPhysCR3, &pPage);
    if (RT_SUCCESS(rc))
    {
        rc = pgmPhysGCPhys2CCPtrInternalDepr(pVM, pPage, GCPhysCR3, (void **)ppPml4);
        if (RT_SUCCESS(rc))
        {
# ifdef IN_RING3
            pVCpu->pgm.s.pGstAmd64Pml4R0 = NIL_RTR0PTR;
            pVCpu->pgm.s.pGstAmd64Pml4R3 = *ppPml4;
# else
            pVCpu->pgm.s.pGstAmd64Pml4R3 = NIL_RTR3PTR;
            pVCpu->pgm.s.pGstAmd64Pml4R0 = *ppPml4;
# endif
            PGM_UNLOCK(pVM);
            return VINF_SUCCESS;
        }
    }

    PGM_UNLOCK(pVM);
    *ppPml4 = NULL;
    return rc;
}


#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
 /**
 * Performs the lazy mapping of the guest PML4 table when using EPT paging.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   ppEptPml4   Where to return the pointer to the mapping.  This will
 *                      always be set.
 */
int pgmGstLazyMapEptPml4(PVMCPUCC pVCpu, PEPTPML4 *ppEptPml4)
{
    Assert(!pVCpu->pgm.s.CTX_SUFF(pGstEptPml4));
    PVMCC       pVM = pVCpu->CTX_SUFF(pVM);
    PGM_LOCK_VOID(pVM);

    RTGCPHYS const GCPhysEpt = pVCpu->pgm.s.uEptPtr & EPT_EPTP_PG_MASK;
    PPGMPAGE       pPage;
    int rc = pgmPhysGetPageEx(pVM, GCPhysEpt, &pPage);
    if (RT_SUCCESS(rc))
    {
        rc = pgmPhysGCPhys2CCPtrInternalDepr(pVM, pPage, GCPhysEpt, (void **)ppEptPml4);
        if (RT_SUCCESS(rc))
        {
# ifdef IN_RING3
            pVCpu->pgm.s.pGstEptPml4R0 = NIL_RTR0PTR;
            pVCpu->pgm.s.pGstEptPml4R3 = *ppEptPml4;
# else
            pVCpu->pgm.s.pGstEptPml4R3 = NIL_RTR3PTR;
            pVCpu->pgm.s.pGstEptPml4R0 = *ppEptPml4;
# endif
            PGM_UNLOCK(pVM);
            return VINF_SUCCESS;
        }
    }

    PGM_UNLOCK(pVM);
    *ppEptPml4 = NULL;
    return rc;
}
#endif


/**
 * Gets the current CR3 register value for the shadow memory context.
 * @returns CR3 value.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(RTHCPHYS) PGMGetHyperCR3(PVMCPU pVCpu)
{
    PPGMPOOLPAGE pPoolPage = pVCpu->pgm.s.CTX_SUFF(pShwPageCR3);
    AssertPtrReturn(pPoolPage, NIL_RTHCPHYS);
    return pPoolPage->Core.Key;
}


/**
 * Forces lazy remapping of the guest's PAE page-directory structures.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static void pgmGstFlushPaePdpes(PVMCPU pVCpu)
{
    for (unsigned i = 0; i < RT_ELEMENTS(pVCpu->pgm.s.aGCPhysGstPaePDs); i++)
    {
        pVCpu->pgm.s.apGstPaePDsR3[i]     = 0;
        pVCpu->pgm.s.apGstPaePDsR0[i]     = 0;
        pVCpu->pgm.s.aGCPhysGstPaePDs[i]  = NIL_RTGCPHYS;
    }
}


#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
/**
 * Performs second-level address translation for the given CR3 and updates the
 * nested-guest CR3 when successful.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uCr3        The masked nested-guest CR3 value.
 * @param   pGCPhysCR3  Where to store the translated CR3.
 *
 * @warning This updates PGMCPU::GCPhysNstGstCR3 when the translation succeeds. Be
 *          mindful of this in code that's hyper sensitive to the order of
 *          operations.
 */
static int pgmGstSlatTranslateCr3(PVMCPUCC pVCpu, uint64_t uCr3, PRTGCPHYS pGCPhysCr3)
{
    if (uCr3 != pVCpu->pgm.s.GCPhysNstGstCR3)
    {
        PGMPTWALK    Walk;
        PGMPTWALKGST GstWalk;
        int const rc = pgmGstSlatWalk(pVCpu, uCr3,  false /* fIsLinearAddrValid */,  0 /* GCPtrNested */, &Walk, &GstWalk);
        if (RT_SUCCESS(rc))
        {
            /* Update nested-guest CR3. */
            pVCpu->pgm.s.GCPhysNstGstCR3 = uCr3;

            /* Pass back the translated result. */
            *pGCPhysCr3 = Walk.GCPhys;
            return VINF_SUCCESS;
        }

        /* Translation failed. */
        *pGCPhysCr3 = NIL_RTGCPHYS;
        return rc;
    }

    /*
     * If the nested-guest CR3 has not changed, then the previously
     * translated CR3 result (i.e. GCPhysCR3) is passed back.
     */
    *pGCPhysCr3 = pVCpu->pgm.s.GCPhysCR3;
    return VINF_SUCCESS;
}
#endif


/**
 * Performs and schedules necessary updates following a CR3 load or reload.
 *
 * This will normally involve mapping the guest PD or nPDPT
 *
 * @returns VBox status code.
 * @retval  VINF_PGM_SYNC_CR3 if monitoring requires a CR3 sync. This can
 *          safely be ignored and overridden since the FF will be set too then.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   cr3             The new cr3.
 * @param   fGlobal         Indicates whether this is a global flush or not.
 */
VMMDECL(int) PGMFlushTLB(PVMCPUCC pVCpu, uint64_t cr3, bool fGlobal)
{
    STAM_PROFILE_START(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,FlushTLB), a);
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);

    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * Always flag the necessary updates; necessary for hardware acceleration
     */
    /** @todo optimize this, it shouldn't always be necessary. */
    VMCPU_FF_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL);
    if (fGlobal)
        VMCPU_FF_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3);

    /*
     * Remap the CR3 content and adjust the monitoring if CR3 was actually changed.
     */
    RTGCPHYS const GCPhysOldCR3 = pVCpu->pgm.s.GCPhysCR3;
    RTGCPHYS       GCPhysCR3    = pgmGetGuestMaskedCr3(pVCpu, cr3);
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
    if (   pVCpu->pgm.s.enmGuestSlatMode == PGMSLAT_EPT
        && PGMMODE_WITH_PAGING(pVCpu->pgm.s.enmGuestMode))
    {
        RTGCPHYS GCPhysOut;
        int const rc = pgmGstSlatTranslateCr3(pVCpu, GCPhysCR3, &GCPhysOut);
        if (RT_SUCCESS(rc))
            GCPhysCR3 = GCPhysOut;
        else
        {
            /* CR3 SLAT translation failed but we try to pretend it
               succeeded for the reasons mentioned in PGMHCChangeMode(). */
            AssertMsgFailed(("SLAT failed for CR3 %#RX64 rc=%Rrc\n", cr3, rc));
            int const rc2 = pgmGstUnmapCr3(pVCpu);
            pVCpu->pgm.s.GCPhysCR3       = NIL_RTGCPHYS;
            pVCpu->pgm.s.GCPhysNstGstCR3 = NIL_RTGCPHYS;
            return rc2;
        }
    }
#endif

    LogFlowFunc(("cr3=%RX64 old=%RX64 fGlobal=%d\n", cr3, GCPhysOldCR3, fGlobal));
    int rc = VINF_SUCCESS;
    if (GCPhysOldCR3 != GCPhysCR3)
    {
        uintptr_t const idxBth = pVCpu->pgm.s.idxBothModeData;
        AssertReturn(idxBth < RT_ELEMENTS(g_aPgmBothModeData), VERR_PGM_MODE_IPE);
        AssertReturn(g_aPgmBothModeData[idxBth].pfnMapCR3, VERR_PGM_MODE_IPE);

        pVCpu->pgm.s.GCPhysCR3 = GCPhysCR3;
        rc = g_aPgmBothModeData[idxBth].pfnMapCR3(pVCpu, GCPhysCR3);
        if (RT_LIKELY(rc == VINF_SUCCESS))
        { }
        else
        {
            AssertMsg(rc == VINF_PGM_SYNC_CR3, ("%Rrc\n", rc));
            Assert(VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL | VMCPU_FF_PGM_SYNC_CR3));
            pVCpu->pgm.s.CTX_SUFF(fPaePdpesAndCr3Mapped) = false;
            pVCpu->pgm.s.GCPhysPaeCR3 = NIL_RTGCPHYS;
            pVCpu->pgm.s.GCPhysCR3 = GCPhysOldCR3;
            pVCpu->pgm.s.fSyncFlags |= PGM_SYNC_MAP_CR3;
        }

        if (fGlobal)
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,FlushTLBNewCR3Global));
        else
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,FlushTLBNewCR3));
    }
    else
    {
#ifdef PGMPOOL_WITH_OPTIMIZED_DIRTY_PT
        PPGMPOOL pPool = pVM->pgm.s.CTX_SUFF(pPool);
        if (pPool->cDirtyPages)
        {
            PGM_LOCK_VOID(pVM);
            pgmPoolResetDirtyPages(pVM);
            PGM_UNLOCK(pVM);
        }
#endif
        if (fGlobal)
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,FlushTLBSameCR3Global));
        else
            STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,FlushTLBSameCR3));

        /*
         * Flush PAE PDPTEs.
         */
        if (PGMMODE_IS_PAE(pVCpu->pgm.s.enmGuestMode))
            pgmGstFlushPaePdpes(pVCpu);
    }

    if (!fGlobal)
        IEMTlbInvalidateAll(pVCpu);
    else
        IEMTlbInvalidateAllGlobal(pVCpu);
    STAM_PROFILE_STOP(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,FlushTLB), a);
    return rc;
}


/**
 * Performs and schedules necessary updates following a CR3 load or reload when
 * using nested or extended paging.
 *
 * This API is an alternative to PGMFlushTLB that avoids actually flushing the
 * TLB and triggering a SyncCR3.
 *
 * This will normally involve mapping the guest PD or nPDPT
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS.
 * @retval  VINF_PGM_SYNC_CR3 if monitoring requires a CR3 sync (not for nested
 *          paging modes).  This can safely be ignored and overridden since the
 *          FF will be set too then.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   cr3             The new CR3.
 */
VMMDECL(int) PGMUpdateCR3(PVMCPUCC pVCpu, uint64_t cr3)
{
    VMCPU_ASSERT_EMT(pVCpu);

    /* We assume we're only called in nested paging mode. */
    Assert(pVCpu->CTX_SUFF(pVM)->pgm.s.fNestedPaging || pVCpu->pgm.s.enmShadowMode == PGMMODE_EPT);

    /*
     * Remap the CR3 content and adjust the monitoring if CR3 was actually changed.
     */
    RTGCPHYS const GCPhysOldCR3 = pVCpu->pgm.s.GCPhysCR3;
    RTGCPHYS       GCPhysCR3    = pgmGetGuestMaskedCr3(pVCpu, cr3);
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
    if (pVCpu->pgm.s.enmGuestSlatMode == PGMSLAT_EPT)
    {
        RTGCPHYS GCPhysOut;
        int const rc = pgmGstSlatTranslateCr3(pVCpu, GCPhysCR3, &GCPhysOut);
        if (RT_SUCCESS(rc))
            GCPhysCR3 = GCPhysOut;
        else
        {
            /* CR3 SLAT translation failed but we try to pretend it
               succeeded for the reasons mentioned in PGMHCChangeMode(). */
            Log(("SLAT failed for CR3 %#RX64 rc=%Rrc\n", cr3, rc));
            int const rc2 = pgmGstUnmapCr3(pVCpu);
            pVCpu->pgm.s.GCPhysCR3       = NIL_RTGCPHYS;
            pVCpu->pgm.s.GCPhysNstGstCR3 = NIL_RTGCPHYS;
            VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_HM_UPDATE_CR3);
            return rc2;
        }
    }
#endif

    LogFlowFunc(("cr3=%RX64 old=%RX64\n", cr3, GCPhysOldCR3));
    int rc = VINF_SUCCESS;
    if (GCPhysOldCR3 != GCPhysCR3)
    {
        uintptr_t const idxBth = pVCpu->pgm.s.idxBothModeData;
        AssertReturn(idxBth < RT_ELEMENTS(g_aPgmBothModeData), VERR_PGM_MODE_IPE);
        AssertReturn(g_aPgmBothModeData[idxBth].pfnMapCR3, VERR_PGM_MODE_IPE);

        pVCpu->pgm.s.GCPhysCR3 = GCPhysCR3;
        rc = g_aPgmBothModeData[idxBth].pfnMapCR3(pVCpu, GCPhysCR3);

        AssertRCSuccess(rc); /* Assumes VINF_PGM_SYNC_CR3 doesn't apply to nested paging. */ /** @todo this isn't true for the mac, but we need hw to test/fix this. */
    }
    /*
     * Flush PAE PDPTEs.
     */
    else if (PGMMODE_IS_PAE(pVCpu->pgm.s.enmGuestMode))
        pgmGstFlushPaePdpes(pVCpu);

    VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_HM_UPDATE_CR3);
    return rc;
}


/**
 * Synchronize the paging structures.
 *
 * This function is called in response to the VM_FF_PGM_SYNC_CR3 and
 * VM_FF_PGM_SYNC_CR3_NONGLOBAL. Those two force action flags are set
 * in several places, most importantly whenever the CR3 is loaded.
 *
 * @returns VBox status code. May return VINF_PGM_SYNC_CR3 in RC/R0.
 * @retval  VERR_PGM_NO_HYPERVISOR_ADDRESS in raw-mode when we're unable to map
 *          the VMM into guest context.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cr0         Guest context CR0 register
 * @param   cr3         Guest context CR3 register
 * @param   cr4         Guest context CR4 register
 * @param   fGlobal     Including global page directories or not
 */
VMMDECL(int) PGMSyncCR3(PVMCPUCC pVCpu, uint64_t cr0, uint64_t cr3, uint64_t cr4, bool fGlobal)
{
    int rc;

    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * The pool may have pending stuff and even require a return to ring-3 to
     * clear the whole thing.
     */
    rc = pgmPoolSyncCR3(pVCpu);
    if (rc != VINF_SUCCESS)
        return rc;

    /*
     * We might be called when we shouldn't.
     *
     * The mode switching will ensure that the PD is resynced after every mode
     * switch.  So, if we find ourselves here when in protected or real mode
     * we can safely clear the FF and return immediately.
     */
    if (pVCpu->pgm.s.enmGuestMode <= PGMMODE_PROTECTED)
    {
        Assert((cr0 & (X86_CR0_PG | X86_CR0_PE)) != (X86_CR0_PG | X86_CR0_PE));
        Assert(!(pVCpu->pgm.s.fSyncFlags & PGM_SYNC_CLEAR_PGM_POOL));
        VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_PGM_SYNC_CR3);
        VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL);
        return VINF_SUCCESS;
    }

    /* If global pages are not supported, then all flushes are global. */
    if (!(cr4 & X86_CR4_PGE))
        fGlobal = true;
    LogFlow(("PGMSyncCR3: cr0=%RX64 cr3=%RX64 cr4=%RX64 fGlobal=%d[%d,%d]\n", cr0, cr3, cr4, fGlobal,
             VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3), VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL)));

    /*
     * Check if we need to finish an aborted MapCR3 call (see PGMFlushTLB).
     * This should be done before SyncCR3.
     */
    if (pVCpu->pgm.s.fSyncFlags & PGM_SYNC_MAP_CR3)
    {
        pVCpu->pgm.s.fSyncFlags &= ~PGM_SYNC_MAP_CR3;

        RTGCPHYS const GCPhysOldCR3 = pVCpu->pgm.s.GCPhysCR3;
        RTGCPHYS       GCPhysCR3    = pgmGetGuestMaskedCr3(pVCpu, cr3);
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
        if (pVCpu->pgm.s.enmGuestSlatMode == PGMSLAT_EPT)
        {
            RTGCPHYS GCPhysOut;
            int rc2 = pgmGstSlatTranslateCr3(pVCpu, GCPhysCR3, &GCPhysOut);
            if (RT_SUCCESS(rc2))
                GCPhysCR3 = GCPhysOut;
            else
            {
                /* CR3 SLAT translation failed but we try to pretend it
                   succeeded for the reasons mentioned in PGMHCChangeMode(). */
                AssertMsgFailed(("Failed to translate CR3 %#RX64. rc=%Rrc\n", cr3, rc2));
                pVCpu->pgm.s.GCPhysCR3       = NIL_RTGCPHYS;
                pVCpu->pgm.s.GCPhysNstGstCR3 = NIL_RTGCPHYS;
                return rc2;
            }
        }
#endif
        Assert(!pVCpu->pgm.s.CTX_SUFF(fPaePdpesAndCr3Mapped));
        if (GCPhysOldCR3 != GCPhysCR3)
        {
            uintptr_t const idxBth = pVCpu->pgm.s.idxBothModeData;
            AssertReturn(idxBth < RT_ELEMENTS(g_aPgmBothModeData), VERR_PGM_MODE_IPE);
            AssertReturn(g_aPgmBothModeData[idxBth].pfnMapCR3, VERR_PGM_MODE_IPE);
            pVCpu->pgm.s.GCPhysCR3 = GCPhysCR3;
            rc = g_aPgmBothModeData[idxBth].pfnMapCR3(pVCpu, GCPhysCR3);
        }

        /* Make sure we check for pending pgm pool syncs as we clear VMCPU_FF_PGM_SYNC_CR3 later on! */
        if (    rc == VINF_PGM_SYNC_CR3
            ||  (pVCpu->pgm.s.fSyncFlags & PGM_SYNC_CLEAR_PGM_POOL))
        {
            Log(("PGMSyncCR3: pending pgm pool sync after MapCR3!\n"));
#ifdef IN_RING3
            rc = pgmPoolSyncCR3(pVCpu);
#else
            if (rc == VINF_PGM_SYNC_CR3)
                pVCpu->pgm.s.GCPhysCR3 = GCPhysOldCR3;
            return VINF_PGM_SYNC_CR3;
#endif
        }
        AssertRCReturn(rc, rc);
        AssertRCSuccessReturn(rc, VERR_IPE_UNEXPECTED_INFO_STATUS);
    }

    /*
     * Let the 'Bth' function do the work and we'll just keep track of the flags.
     */
    STAM_PROFILE_START(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncCR3), a);

    uintptr_t const idxBth = pVCpu->pgm.s.idxBothModeData;
    AssertReturn(idxBth < RT_ELEMENTS(g_aPgmBothModeData), VERR_PGM_MODE_IPE);
    AssertReturn(g_aPgmBothModeData[idxBth].pfnSyncCR3, VERR_PGM_MODE_IPE);
    rc = g_aPgmBothModeData[idxBth].pfnSyncCR3(pVCpu, cr0, cr3, cr4, fGlobal);

    STAM_PROFILE_STOP(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncCR3), a);
    AssertMsg(rc == VINF_SUCCESS || rc == VINF_PGM_SYNC_CR3 || RT_FAILURE(rc), ("rc=%Rrc\n", rc));
    if (rc == VINF_SUCCESS)
    {
        if (pVCpu->pgm.s.fSyncFlags & PGM_SYNC_CLEAR_PGM_POOL)
        {
            /* Go back to ring 3 if a pgm pool sync is again pending. */
            return VINF_PGM_SYNC_CR3;
        }

        if (!(pVCpu->pgm.s.fSyncFlags & PGM_SYNC_ALWAYS))
        {
            Assert(!(pVCpu->pgm.s.fSyncFlags & PGM_SYNC_CLEAR_PGM_POOL));
            VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_PGM_SYNC_CR3);
            VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL);
        }
    }

    /*
     * Now flush the CR3 (guest context).
     */
    if (rc == VINF_SUCCESS)
        PGM_INVL_VCPU_TLBS(pVCpu);
    return rc;
}


/**
 * Maps all the PAE PDPE entries.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   paPaePdpes  The new PAE PDPE values.
 *
 * @remarks This function may be invoked during the process of changing the guest
 *          paging mode to PAE, hence the guest state (CR0, CR4 etc.) may not
 *          reflect PAE paging just yet.
 */
VMM_INT_DECL(int) PGMGstMapPaePdpes(PVMCPUCC pVCpu, PCX86PDPE paPaePdpes)
{
    Assert(paPaePdpes);
    for (unsigned i = 0; i < X86_PG_PAE_PDPE_ENTRIES; i++)
    {
        X86PDPE const PaePdpe = paPaePdpes[i];

        /*
         * In some cases (e.g. in SVM with nested paging) the validation of the PAE PDPEs
         * are deferred.[1] Also, different situations require different handling of invalid
         * PDPE entries. Here we assume the caller has already validated or doesn't require
         * validation of the PDPEs.
         *
         * In the case of nested EPT (i.e. for nested-guests), the PAE PDPEs have been
         * validated by the VMX transition.
         *
         * [1] -- See AMD spec. 15.25.10 "Legacy PAE Mode".
         */
        if ((PaePdpe.u & (pVCpu->pgm.s.fGstPaeMbzPdpeMask | X86_PDPE_P)) == X86_PDPE_P)
        {
            PVMCC   pVM = pVCpu->CTX_SUFF(pVM);
            RTHCPTR HCPtr;

            RTGCPHYS GCPhys;
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
            if (pVCpu->pgm.s.enmGuestSlatMode == PGMSLAT_EPT)
            {
                PGMPTWALK      Walk;
                PGMPTWALKGST   GstWalk;
                RTGCPHYS const GCPhysNested = PaePdpe.u & X86_PDPE_PG_MASK;
                int const rc = pgmGstSlatWalk(pVCpu, GCPhysNested, false /* fIsLinearAddrValid */, 0 /* GCPtrNested */,
                                              &Walk, &GstWalk);
                if (RT_SUCCESS(rc))
                    GCPhys = Walk.GCPhys;
                else
                {
                    /*
                     * Second-level address translation of the PAE PDPE has failed but we must -NOT-
                     * abort and return a failure now. This is because we're called from a Mov CRx
                     * instruction (or similar operation). Let's just pretend success but flag that
                     * we need to map this PDPE lazily later.
                     *
                     * See Intel spec. 25.3 "Changes to instruction behavior in VMX non-root operation".
                     * See Intel spec. 28.3.1 "EPT Overview".
                     */
                    pVCpu->pgm.s.apGstPaePDsR3[i]    = 0;
                    pVCpu->pgm.s.apGstPaePDsR0[i]    = 0;
                    pVCpu->pgm.s.aGCPhysGstPaePDs[i] = NIL_RTGCPHYS;
                    continue;
                }
            }
            else
#endif
            {
                GCPhys = PGM_A20_APPLY(pVCpu, PaePdpe.u & X86_PDPE_PG_MASK);
            }

            PGM_LOCK_VOID(pVM);
            PPGMPAGE pPage = pgmPhysGetPage(pVM, GCPhys);
            AssertReturnStmt(pPage, PGM_UNLOCK(pVM), VERR_PGM_INVALID_PDPE_ADDR);
            int const rc = pgmPhysGCPhys2CCPtrInternalDepr(pVM, pPage, GCPhys, (void **)&HCPtr);
            PGM_UNLOCK(pVM);
            if (RT_SUCCESS(rc))
            {
#ifdef IN_RING3
                pVCpu->pgm.s.apGstPaePDsR3[i]    = (PX86PDPAE)HCPtr;
                pVCpu->pgm.s.apGstPaePDsR0[i]    = NIL_RTR0PTR;
#else
                pVCpu->pgm.s.apGstPaePDsR3[i]    = NIL_RTR3PTR;
                pVCpu->pgm.s.apGstPaePDsR0[i]    = (PX86PDPAE)HCPtr;
#endif
                pVCpu->pgm.s.aGCPhysGstPaePDs[i] = GCPhys;
                continue;
            }
            AssertMsgFailed(("PGMPhysMapPaePdpes: rc2=%d GCPhys=%RGp i=%d\n", rc, GCPhys, i));
        }
        pVCpu->pgm.s.apGstPaePDsR3[i]    = 0;
        pVCpu->pgm.s.apGstPaePDsR0[i]    = 0;
        pVCpu->pgm.s.aGCPhysGstPaePDs[i] = NIL_RTGCPHYS;
    }
    return VINF_SUCCESS;
}


/**
 * Validates and maps the PDPT and PAE PDPEs referenced by the given CR3.
 *
 * @returns VBox status code.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   cr3     The guest CR3 value.
 *
 * @remarks This function may be invoked during the process of changing the guest
 *          paging mode to PAE but the guest state (CR0, CR4 etc.) may not reflect
 *          PAE paging just yet.
 */
VMM_INT_DECL(int) PGMGstMapPaePdpesAtCr3(PVMCPUCC pVCpu, uint64_t cr3)
{
    /*
     * Read the page-directory-pointer table (PDPT) at CR3.
     */
    RTGCPHYS GCPhysCR3 = (cr3 & X86_CR3_PAE_PAGE_MASK);
    PGM_A20_APPLY_TO_VAR(pVCpu, GCPhysCR3);

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
    if (pVCpu->pgm.s.enmGuestSlatMode == PGMSLAT_EPT)
    {
        RTGCPHYS GCPhysOut;
        int const rc = pgmGstSlatTranslateCr3(pVCpu, GCPhysCR3, &GCPhysOut);
        if (RT_SUCCESS(rc))
            GCPhysCR3 = GCPhysOut;
        else
        {
            Log(("Failed to load CR3 at %#RX64. rc=%Rrc\n", GCPhysCR3, rc));
            return rc;
        }
    }
#endif

    RTHCPTR HCPtrGuestCr3;
    int rc = pgmGstMapCr3(pVCpu, GCPhysCR3, &HCPtrGuestCr3);
    if (RT_SUCCESS(rc))
    {
        /*
         * Validate the page-directory-pointer table entries (PDPE).
         */
        X86PDPE aPaePdpes[X86_PG_PAE_PDPE_ENTRIES];
        memcpy(&aPaePdpes[0], HCPtrGuestCr3, sizeof(aPaePdpes));
        if (PGMGstArePaePdpesValid(pVCpu, &aPaePdpes[0]))
        {
            /*
             * Map the PDPT.
             * We deliberately don't update PGM's GCPhysCR3 here as it's expected
             * that PGMFlushTLB will be called soon and only a change to CR3 then
             * will cause the shadow page tables to be updated.
             */
#ifdef IN_RING3
            pVCpu->pgm.s.pGstPaePdptR3 = (PX86PDPT)HCPtrGuestCr3;
            pVCpu->pgm.s.pGstPaePdptR0 = NIL_RTR0PTR;
#else
            pVCpu->pgm.s.pGstPaePdptR3 = NIL_RTR3PTR;
            pVCpu->pgm.s.pGstPaePdptR0 = (PX86PDPT)HCPtrGuestCr3;
#endif

            /*
             * Update CPUM and map the 4 PAE PDPEs.
             */
            CPUMSetGuestPaePdpes(pVCpu, &aPaePdpes[0]);
            rc = PGMGstMapPaePdpes(pVCpu, &aPaePdpes[0]);
            if (RT_SUCCESS(rc))
            {
#ifdef IN_RING3
                pVCpu->pgm.s.fPaePdpesAndCr3MappedR3 = true;
                pVCpu->pgm.s.fPaePdpesAndCr3MappedR0 = false;
#else
                pVCpu->pgm.s.fPaePdpesAndCr3MappedR3 = false;
                pVCpu->pgm.s.fPaePdpesAndCr3MappedR0 = true;
#endif
                pVCpu->pgm.s.GCPhysPaeCR3 = GCPhysCR3;
            }
        }
        else
            rc = VERR_PGM_PAE_PDPE_RSVD;
    }
    return rc;
}


/**
 * Called whenever CR0 or CR4 in a way which may affect the paging mode.
 *
 * @returns VBox status code, with the following informational code for
 *          VM scheduling.
 * @retval  VINF_SUCCESS if the was no change, or it was successfully dealt with.
 * @retval  VINF_EM_SUSPEND or VINF_EM_OFF on a fatal runtime error. (R3 only)
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cr0         The new cr0.
 * @param   cr4         The new cr4.
 * @param   efer        The new extended feature enable register.
 * @param   fForce      Whether to force a mode change.
 */
VMMDECL(int) PGMChangeMode(PVMCPUCC pVCpu, uint64_t cr0, uint64_t cr4, uint64_t efer, bool fForce)
{
    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * Calc the new guest mode.
     *
     * Note! We check PG before PE and without requiring PE because of the
     *       special AMD-V paged real mode (APM vol 2, rev 3.28, 15.9).
     */
    PGMMODE enmGuestMode;
    if (cr0 & X86_CR0_PG)
    {
        if (!(cr4 & X86_CR4_PAE))
        {
            bool const fPse = !!(cr4 & X86_CR4_PSE);
            if (pVCpu->pgm.s.fGst32BitPageSizeExtension != fPse)
                Log(("PGMChangeMode: CR4.PSE %d -> %d\n", pVCpu->pgm.s.fGst32BitPageSizeExtension, fPse));
            pVCpu->pgm.s.fGst32BitPageSizeExtension = fPse;
            enmGuestMode = PGMMODE_32_BIT;
        }
        else if (!(efer & MSR_K6_EFER_LME))
        {
            if (!(efer & MSR_K6_EFER_NXE))
                enmGuestMode = PGMMODE_PAE;
            else
                enmGuestMode = PGMMODE_PAE_NX;
        }
        else
        {
            if (!(efer & MSR_K6_EFER_NXE))
                enmGuestMode = PGMMODE_AMD64;
            else
                enmGuestMode = PGMMODE_AMD64_NX;
        }
    }
    else if (!(cr0 & X86_CR0_PE))
        enmGuestMode = PGMMODE_REAL;
    else
        enmGuestMode = PGMMODE_PROTECTED;

    /*
     * Did it change?
     */
    if (   !fForce
        && pVCpu->pgm.s.enmGuestMode == enmGuestMode)
        return VINF_SUCCESS;

    /* Flush the TLB */
    PGM_INVL_VCPU_TLBS(pVCpu);
    return PGMHCChangeMode(pVCpu->CTX_SUFF(pVM), pVCpu, enmGuestMode, fForce);
}


/**
 * Converts a PGMMODE value to a PGM_TYPE_* \#define.
 *
 * @returns PGM_TYPE_*.
 * @param   pgmMode     The mode value to convert.
 */
DECLINLINE(unsigned) pgmModeToType(PGMMODE pgmMode)
{
    switch (pgmMode)
    {
        case PGMMODE_REAL:          return PGM_TYPE_REAL;
        case PGMMODE_PROTECTED:     return PGM_TYPE_PROT;
        case PGMMODE_32_BIT:        return PGM_TYPE_32BIT;
        case PGMMODE_PAE:
        case PGMMODE_PAE_NX:        return PGM_TYPE_PAE;
        case PGMMODE_AMD64:
        case PGMMODE_AMD64_NX:      return PGM_TYPE_AMD64;
        case PGMMODE_NESTED_32BIT:  return PGM_TYPE_NESTED_32BIT;
        case PGMMODE_NESTED_PAE:    return PGM_TYPE_NESTED_PAE;
        case PGMMODE_NESTED_AMD64:  return PGM_TYPE_NESTED_AMD64;
        case PGMMODE_EPT:           return PGM_TYPE_EPT;
        case PGMMODE_NONE:          return PGM_TYPE_NONE;
        default:
            AssertFatalMsgFailed(("pgmMode=%d\n", pgmMode));
    }
}


/**
 * Calculates the shadow paging mode.
 *
 * @returns The shadow paging mode.
 * @param   pVM             The cross context VM structure.
 * @param   enmGuestMode    The guest mode.
 * @param   enmHostMode     The host mode.
 * @param   enmShadowMode   The current shadow mode.
 */
static PGMMODE pgmCalcShadowMode(PVMCC pVM, PGMMODE enmGuestMode, SUPPAGINGMODE enmHostMode, PGMMODE enmShadowMode)
{
    switch (enmGuestMode)
    {
        case PGMMODE_REAL:
        case PGMMODE_PROTECTED:
            switch (enmHostMode)
            {
                case SUPPAGINGMODE_32_BIT:
                case SUPPAGINGMODE_32_BIT_GLOBAL:
                    enmShadowMode = PGMMODE_32_BIT;
                    break;

                case SUPPAGINGMODE_PAE:
                case SUPPAGINGMODE_PAE_NX:
                case SUPPAGINGMODE_PAE_GLOBAL:
                case SUPPAGINGMODE_PAE_GLOBAL_NX:
                    enmShadowMode = PGMMODE_PAE;
                    break;

                case SUPPAGINGMODE_AMD64:
                case SUPPAGINGMODE_AMD64_GLOBAL:
                case SUPPAGINGMODE_AMD64_NX:
                case SUPPAGINGMODE_AMD64_GLOBAL_NX:
                    enmShadowMode = PGMMODE_PAE;
                    break;

                default:
                    AssertLogRelMsgFailedReturn(("enmHostMode=%d\n", enmHostMode), PGMMODE_INVALID);
            }
            break;

        case PGMMODE_32_BIT:
            switch (enmHostMode)
            {
                case SUPPAGINGMODE_32_BIT:
                case SUPPAGINGMODE_32_BIT_GLOBAL:
                    enmShadowMode = PGMMODE_32_BIT;
                    break;

                case SUPPAGINGMODE_PAE:
                case SUPPAGINGMODE_PAE_NX:
                case SUPPAGINGMODE_PAE_GLOBAL:
                case SUPPAGINGMODE_PAE_GLOBAL_NX:
                    enmShadowMode = PGMMODE_PAE;
                    break;

                case SUPPAGINGMODE_AMD64:
                case SUPPAGINGMODE_AMD64_GLOBAL:
                case SUPPAGINGMODE_AMD64_NX:
                case SUPPAGINGMODE_AMD64_GLOBAL_NX:
                    enmShadowMode = PGMMODE_PAE;
                    break;

                default:
                    AssertLogRelMsgFailedReturn(("enmHostMode=%d\n", enmHostMode), PGMMODE_INVALID);
            }
            break;

        case PGMMODE_PAE:
        case PGMMODE_PAE_NX: /** @todo This might require more switchers and guest+both modes. */
            switch (enmHostMode)
            {
                case SUPPAGINGMODE_32_BIT:
                case SUPPAGINGMODE_32_BIT_GLOBAL:
                    enmShadowMode = PGMMODE_PAE;
                    break;

                case SUPPAGINGMODE_PAE:
                case SUPPAGINGMODE_PAE_NX:
                case SUPPAGINGMODE_PAE_GLOBAL:
                case SUPPAGINGMODE_PAE_GLOBAL_NX:
                    enmShadowMode = PGMMODE_PAE;
                    break;

                case SUPPAGINGMODE_AMD64:
                case SUPPAGINGMODE_AMD64_GLOBAL:
                case SUPPAGINGMODE_AMD64_NX:
                case SUPPAGINGMODE_AMD64_GLOBAL_NX:
                    enmShadowMode = PGMMODE_PAE;
                    break;

                default:
                    AssertLogRelMsgFailedReturn(("enmHostMode=%d\n", enmHostMode), PGMMODE_INVALID);
            }
            break;

        case PGMMODE_AMD64:
        case PGMMODE_AMD64_NX:
            switch (enmHostMode)
            {
                case SUPPAGINGMODE_32_BIT:
                case SUPPAGINGMODE_32_BIT_GLOBAL:
                    enmShadowMode = PGMMODE_AMD64;
                    break;

                case SUPPAGINGMODE_PAE:
                case SUPPAGINGMODE_PAE_NX:
                case SUPPAGINGMODE_PAE_GLOBAL:
                case SUPPAGINGMODE_PAE_GLOBAL_NX:
                    enmShadowMode = PGMMODE_AMD64;
                    break;

                case SUPPAGINGMODE_AMD64:
                case SUPPAGINGMODE_AMD64_GLOBAL:
                case SUPPAGINGMODE_AMD64_NX:
                case SUPPAGINGMODE_AMD64_GLOBAL_NX:
                    enmShadowMode = PGMMODE_AMD64;
                    break;

                default:
                    AssertLogRelMsgFailedReturn(("enmHostMode=%d\n", enmHostMode), PGMMODE_INVALID);
            }
            break;

        default:
            AssertLogRelMsgFailedReturn(("enmGuestMode=%d\n", enmGuestMode), PGMMODE_INVALID);
    }

    /*
     * Override the shadow mode when NEM, IEM or nested paging is active.
     */
    if (!VM_IS_HM_ENABLED(pVM))
    {
        Assert(VM_IS_NEM_ENABLED(pVM) || VM_IS_EXEC_ENGINE_IEM(pVM));
        pVM->pgm.s.fNestedPaging = true;
        enmShadowMode = PGMMODE_NONE;
    }
    else
    {
        bool fNestedPaging = HMIsNestedPagingActive(pVM);
        pVM->pgm.s.fNestedPaging = fNestedPaging;
        if (fNestedPaging)
        {
            if (HMIsVmxActive(pVM))
                enmShadowMode = PGMMODE_EPT;
            else
            {
                /* The nested SVM paging depends on the host one. */
                Assert(HMIsSvmActive(pVM));
                if (   enmGuestMode == PGMMODE_AMD64
                    || enmGuestMode == PGMMODE_AMD64_NX)
                    enmShadowMode = PGMMODE_NESTED_AMD64;
                else
                    switch (pVM->pgm.s.enmHostMode)
                    {
                        case SUPPAGINGMODE_32_BIT:
                        case SUPPAGINGMODE_32_BIT_GLOBAL:
                            enmShadowMode = PGMMODE_NESTED_32BIT;
                            break;

                        case SUPPAGINGMODE_PAE:
                        case SUPPAGINGMODE_PAE_GLOBAL:
                        case SUPPAGINGMODE_PAE_NX:
                        case SUPPAGINGMODE_PAE_GLOBAL_NX:
                            enmShadowMode = PGMMODE_NESTED_PAE;
                            break;

                        case SUPPAGINGMODE_AMD64:
                        case SUPPAGINGMODE_AMD64_GLOBAL:
                        case SUPPAGINGMODE_AMD64_NX:
                        case SUPPAGINGMODE_AMD64_GLOBAL_NX:
                            enmShadowMode = PGMMODE_NESTED_AMD64;
                            break;

                        default:
                            AssertLogRelMsgFailedReturn(("enmHostMode=%d\n", pVM->pgm.s.enmHostMode), PGMMODE_INVALID);
                    }
            }
        }
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
        else
        {
            /* Nested paging is a requirement for nested VT-x. */
            AssertLogRelMsgReturn(enmGuestMode != PGMMODE_EPT, ("enmHostMode=%d\n", pVM->pgm.s.enmHostMode), PGMMODE_INVALID);
        }
#endif
    }

    return enmShadowMode;
}


/**
 * Performs the actual mode change.
 * This is called by PGMChangeMode and pgmR3InitPaging().
 *
 * @returns VBox status code. May suspend or power off the VM on error, but this
 *          will trigger using FFs and not informational status codes.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   enmGuestMode    The new guest mode. This is assumed to be different from
 *                          the current mode.
 * @param   fForce          Whether to force a shadow paging mode change.
 */
VMM_INT_DECL(int) PGMHCChangeMode(PVMCC pVM, PVMCPUCC pVCpu, PGMMODE enmGuestMode, bool fForce)
{
    Log(("PGMHCChangeMode: Guest mode: %s -> %s\n", PGMGetModeName(pVCpu->pgm.s.enmGuestMode), PGMGetModeName(enmGuestMode)));
    STAM_REL_COUNTER_INC(&pVCpu->pgm.s.cGuestModeChanges);

    /*
     * Calc the shadow mode and switcher.
     */
    PGMMODE const enmShadowMode = pgmCalcShadowMode(pVM, enmGuestMode, pVM->pgm.s.enmHostMode, pVCpu->pgm.s.enmShadowMode);
    bool const fShadowModeChanged = enmShadowMode != pVCpu->pgm.s.enmShadowMode || fForce;

    /*
     * Exit old mode(s).
     */
    /* shadow */
    if (fShadowModeChanged)
    {
        LogFlow(("PGMHCChangeMode: Shadow mode: %s -> %s\n", PGMGetModeName(pVCpu->pgm.s.enmShadowMode), PGMGetModeName(enmShadowMode)));
        uintptr_t idxOldShw = pVCpu->pgm.s.idxShadowModeData;
        if (   idxOldShw < RT_ELEMENTS(g_aPgmShadowModeData)
            && g_aPgmShadowModeData[idxOldShw].pfnExit)
        {
            int rc = g_aPgmShadowModeData[idxOldShw].pfnExit(pVCpu);
            AssertMsgRCReturn(rc, ("Exit failed for shadow mode %d: %Rrc\n", pVCpu->pgm.s.enmShadowMode, rc), rc);
        }
    }
    else
        LogFlow(("PGMHCChangeMode: Shadow mode remains: %s\n",  PGMGetModeName(pVCpu->pgm.s.enmShadowMode)));

    /* guest */
    uintptr_t const idxOldGst = pVCpu->pgm.s.idxGuestModeData;
    if (   idxOldGst < RT_ELEMENTS(g_aPgmGuestModeData)
        && g_aPgmGuestModeData[idxOldGst].pfnExit)
    {
        int rc = g_aPgmGuestModeData[idxOldGst].pfnExit(pVCpu);
        AssertMsgReturn(RT_SUCCESS(rc), ("Exit failed for guest mode %d: %Rrc\n", pVCpu->pgm.s.enmGuestMode, rc), rc);
    }
    pVCpu->pgm.s.GCPhysCR3 = NIL_RTGCPHYS;
    pVCpu->pgm.s.GCPhysNstGstCR3 = NIL_RTGCPHYS;
    pVCpu->pgm.s.GCPhysPaeCR3 = NIL_RTGCPHYS;
    Assert(!pVCpu->pgm.s.CTX_SUFF(fPaePdpesAndCr3Mapped));

    /*
     * Change the paging mode data indexes.
     */
    uintptr_t idxNewGst = pVCpu->pgm.s.idxGuestModeData = pgmModeToType(enmGuestMode);
    AssertReturn(idxNewGst < RT_ELEMENTS(g_aPgmGuestModeData), VERR_PGM_MODE_IPE);
    AssertReturn(g_aPgmGuestModeData[idxNewGst].uType == idxNewGst, VERR_PGM_MODE_IPE);
    AssertPtrReturn(g_aPgmGuestModeData[idxNewGst].pfnGetPage, VERR_PGM_MODE_IPE);
    AssertPtrReturn(g_aPgmGuestModeData[idxNewGst].pfnModifyPage, VERR_PGM_MODE_IPE);
    AssertPtrReturn(g_aPgmGuestModeData[idxNewGst].pfnExit, VERR_PGM_MODE_IPE);
    AssertPtrReturn(g_aPgmGuestModeData[idxNewGst].pfnEnter, VERR_PGM_MODE_IPE);
#ifdef IN_RING3
    AssertPtrReturn(g_aPgmGuestModeData[idxNewGst].pfnRelocate, VERR_PGM_MODE_IPE);
#endif

    uintptr_t const idxNewShw = pVCpu->pgm.s.idxShadowModeData = pgmModeToType(enmShadowMode);
    AssertReturn(idxNewShw < RT_ELEMENTS(g_aPgmShadowModeData), VERR_PGM_MODE_IPE);
    AssertReturn(g_aPgmShadowModeData[idxNewShw].uType == idxNewShw, VERR_PGM_MODE_IPE);
    AssertPtrReturn(g_aPgmShadowModeData[idxNewShw].pfnGetPage, VERR_PGM_MODE_IPE);
    AssertPtrReturn(g_aPgmShadowModeData[idxNewShw].pfnModifyPage, VERR_PGM_MODE_IPE);
    AssertPtrReturn(g_aPgmShadowModeData[idxNewShw].pfnExit, VERR_PGM_MODE_IPE);
    AssertPtrReturn(g_aPgmShadowModeData[idxNewShw].pfnEnter, VERR_PGM_MODE_IPE);
#ifdef IN_RING3
    AssertPtrReturn(g_aPgmShadowModeData[idxNewShw].pfnRelocate, VERR_PGM_MODE_IPE);
#endif

    uintptr_t const idxNewBth = pVCpu->pgm.s.idxBothModeData = (idxNewShw - PGM_TYPE_FIRST_SHADOW) * PGM_TYPE_END + idxNewGst;
    AssertReturn(g_aPgmBothModeData[idxNewBth].uShwType == idxNewShw, VERR_PGM_MODE_IPE);
    AssertReturn(g_aPgmBothModeData[idxNewBth].uGstType == idxNewGst, VERR_PGM_MODE_IPE);
    AssertPtrReturn(g_aPgmBothModeData[idxNewBth].pfnInvalidatePage, VERR_PGM_MODE_IPE);
    AssertPtrReturn(g_aPgmBothModeData[idxNewBth].pfnSyncCR3, VERR_PGM_MODE_IPE);
    AssertPtrReturn(g_aPgmBothModeData[idxNewBth].pfnPrefetchPage, VERR_PGM_MODE_IPE);
    AssertPtrReturn(g_aPgmBothModeData[idxNewBth].pfnVerifyAccessSyncPage, VERR_PGM_MODE_IPE);
    AssertPtrReturn(g_aPgmBothModeData[idxNewBth].pfnMapCR3, VERR_PGM_MODE_IPE);
    AssertPtrReturn(g_aPgmBothModeData[idxNewBth].pfnUnmapCR3, VERR_PGM_MODE_IPE);
    AssertPtrReturn(g_aPgmBothModeData[idxNewBth].pfnEnter, VERR_PGM_MODE_IPE);
#ifdef VBOX_STRICT
    AssertPtrReturn(g_aPgmBothModeData[idxNewBth].pfnAssertCR3, VERR_PGM_MODE_IPE);
#endif

    /*
     * Determine SLAT mode -before- entering the new shadow mode!
     */
    pVCpu->pgm.s.enmGuestSlatMode = !CPUMIsGuestVmxEptPagingEnabled(pVCpu) ? PGMSLAT_DIRECT : PGMSLAT_EPT;

    /*
     * Enter new shadow mode (if changed).
     */
    if (fShadowModeChanged)
    {
        pVCpu->pgm.s.enmShadowMode = enmShadowMode;
        int rc = g_aPgmShadowModeData[idxNewShw].pfnEnter(pVCpu);
        AssertLogRelMsgRCReturnStmt(rc, ("Entering enmShadowMode=%s failed: %Rrc\n", PGMGetModeName(enmShadowMode), rc),
                                    pVCpu->pgm.s.enmShadowMode = PGMMODE_INVALID, rc);
    }

    /*
     * Always flag the necessary updates
     */
    VMCPU_FF_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3);

    /*
     * Enter the new guest and shadow+guest modes.
     */
    /* Calc the new CR3 value. */
    RTGCPHYS GCPhysCR3;
    switch (enmGuestMode)
    {
        case PGMMODE_REAL:
        case PGMMODE_PROTECTED:
            GCPhysCR3 = NIL_RTGCPHYS;
            break;

        case PGMMODE_32_BIT:
            GCPhysCR3 = CPUMGetGuestCR3(pVCpu) & X86_CR3_PAGE_MASK;
            break;

        case PGMMODE_PAE_NX:
        case PGMMODE_PAE:
            if (!pVM->cpum.ro.GuestFeatures.fPae)
#ifdef IN_RING3 /** @todo r=bird: wrong place, probably hasn't really worked for a while. */
                return VMSetRuntimeError(pVM, VMSETRTERR_FLAGS_FATAL, "PAEmode",
                                         N_("The guest is trying to switch to the PAE mode which is currently disabled by default in VirtualBox. PAE support can be enabled using the VM settings (System/Processor)"));
#else
                AssertLogRelMsgFailedReturn(("enmGuestMode=%s - Try enable PAE for the guest!\n", PGMGetModeName(enmGuestMode)), VERR_PGM_MODE_IPE);

#endif
            GCPhysCR3 = CPUMGetGuestCR3(pVCpu) & X86_CR3_PAE_PAGE_MASK;
            break;

#ifdef VBOX_WITH_64_BITS_GUESTS
        case PGMMODE_AMD64_NX:
        case PGMMODE_AMD64:
            GCPhysCR3 = CPUMGetGuestCR3(pVCpu) & X86_CR3_AMD64_PAGE_MASK;
            break;
#endif
        default:
            AssertLogRelMsgFailedReturn(("enmGuestMode=%d\n", enmGuestMode), VERR_PGM_MODE_IPE);
    }

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
    /*
     * If a nested-guest is using EPT paging:
     *   - Update the second-level address translation (SLAT) mode.
     *   - Indicate that the CR3 is nested-guest physical address.
     */
    if (pVCpu->pgm.s.enmGuestSlatMode == PGMSLAT_EPT)
    {
        if (PGMMODE_WITH_PAGING(enmGuestMode))
        {
            /*
             * Translate CR3 to its guest-physical address.
             * We don't use pgmGstSlatTranslateCr3() here as we want to update GCPhysNstGstCR3 -after-
             * switching modes to keep it consistent with how GCPhysCR3 is updated.
             */
            PGMPTWALK    Walk;
            PGMPTWALKGST GstWalk;
            int const rc = pgmGstSlatWalk(pVCpu, GCPhysCR3, false /* fIsLinearAddrValid */, 0 /* GCPtrNested */, &Walk,
                                          &GstWalk);
            if (RT_SUCCESS(rc))
            { /* likely */ }
            else
            {
                /*
                 * SLAT failed but we avoid reporting this to the caller because the caller
                 * is not supposed to fail. The only time the caller needs to indicate a
                 * failure to software is when PAE paging is used by the nested-guest, but
                 * we handle the PAE case separately (e.g., see VMX transition in IEM).
                 * In all other cases, the failure will be indicated when CR3 tries to be
                 * translated on the next linear-address memory access.
                 * See Intel spec. 27.2.1 "EPT Overview".
                 */
                Log(("SLAT failed for CR3 %#RX64 rc=%Rrc\n", GCPhysCR3, rc));

                /* Trying to coax PGM to succeed for the time being... */
                Assert(pVCpu->pgm.s.GCPhysCR3 == NIL_RTGCPHYS);
                pVCpu->pgm.s.GCPhysNstGstCR3  = GCPhysCR3;
                pVCpu->pgm.s.enmGuestMode     = enmGuestMode;
                HMHCChangedPagingMode(pVM, pVCpu, pVCpu->pgm.s.enmShadowMode, pVCpu->pgm.s.enmGuestMode);
                return VINF_SUCCESS;
            }
            pVCpu->pgm.s.GCPhysNstGstCR3  = GCPhysCR3;
            GCPhysCR3 = Walk.GCPhys & X86_CR3_EPT_PAGE_MASK;
        }
    }
    else
        Assert(pVCpu->pgm.s.GCPhysNstGstCR3 == NIL_RTGCPHYS);
#endif

    /*
     * Enter the new guest mode.
     */
    pVCpu->pgm.s.enmGuestMode = enmGuestMode;
    int rc = g_aPgmGuestModeData[idxNewGst].pfnEnter(pVCpu, GCPhysCR3);
    int rc2 = g_aPgmBothModeData[idxNewBth].pfnEnter(pVCpu, GCPhysCR3);

    /* Set the new guest CR3 (and nested-guest CR3). */
    pVCpu->pgm.s.GCPhysCR3 = GCPhysCR3;

    /* status codes. */
    AssertRC(rc);
    AssertRC(rc2);
    if (RT_SUCCESS(rc))
    {
        rc = rc2;
        if (RT_SUCCESS(rc)) /* no informational status codes. */
            rc = VINF_SUCCESS;
    }

    /*
     * Notify HM.
     */
    HMHCChangedPagingMode(pVM, pVCpu, pVCpu->pgm.s.enmShadowMode, pVCpu->pgm.s.enmGuestMode);
    return rc;
}


/**
 * Called by CPUM or REM when CR0.WP changes to 1.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @thread  EMT
 */
VMMDECL(void) PGMCr0WpEnabled(PVMCPUCC pVCpu)
{
    /*
     * Netware WP0+RO+US hack cleanup when WP0 -> WP1.
     *
     * Use the counter to judge whether there might be pool pages with active
     * hacks in them.  If there are, we will be running the risk of messing up
     * the guest by allowing it to write to read-only pages.  Thus, we have to
     * clear the page pool ASAP if there is the slightest chance.
     */
    if (pVCpu->pgm.s.cNetwareWp0Hacks > 0)
    {
        Assert(pVCpu->CTX_SUFF(pVM)->cCpus == 1);

        Log(("PGMCr0WpEnabled: %llu WP0 hacks active - clearing page pool\n", pVCpu->pgm.s.cNetwareWp0Hacks));
        pVCpu->pgm.s.cNetwareWp0Hacks = 0;
        pVCpu->pgm.s.fSyncFlags |= PGM_SYNC_CLEAR_PGM_POOL;
        VMCPU_FF_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3);
    }
}


/**
 * Gets the current guest paging mode.
 *
 * If you just need the CPU mode (real/protected/long), use CPUMGetGuestMode().
 *
 * @returns The current paging mode.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(PGMMODE) PGMGetGuestMode(PVMCPU pVCpu)
{
    return pVCpu->pgm.s.enmGuestMode;
}


/**
 * Gets the current shadow paging mode.
 *
 * @returns The current paging mode.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMMDECL(PGMMODE) PGMGetShadowMode(PVMCPU pVCpu)
{
    return pVCpu->pgm.s.enmShadowMode;
}


/**
 * Gets the current host paging mode.
 *
 * @returns The current paging mode.
 * @param   pVM             The cross context VM structure.
 */
VMMDECL(PGMMODE) PGMGetHostMode(PVM pVM)
{
    switch (pVM->pgm.s.enmHostMode)
    {
        case SUPPAGINGMODE_32_BIT:
        case SUPPAGINGMODE_32_BIT_GLOBAL:
            return PGMMODE_32_BIT;

        case SUPPAGINGMODE_PAE:
        case SUPPAGINGMODE_PAE_GLOBAL:
            return PGMMODE_PAE;

        case SUPPAGINGMODE_PAE_NX:
        case SUPPAGINGMODE_PAE_GLOBAL_NX:
            return PGMMODE_PAE_NX;

        case SUPPAGINGMODE_AMD64:
        case SUPPAGINGMODE_AMD64_GLOBAL:
            return PGMMODE_AMD64;

        case SUPPAGINGMODE_AMD64_NX:
        case SUPPAGINGMODE_AMD64_GLOBAL_NX:
            return PGMMODE_AMD64_NX;

        default: AssertMsgFailed(("enmHostMode=%d\n", pVM->pgm.s.enmHostMode)); break;
    }

    return PGMMODE_INVALID;
}


/**
 * Get mode name.
 *
 * @returns read-only name string.
 * @param   enmMode     The mode which name is desired.
 */
VMMDECL(const char *) PGMGetModeName(PGMMODE enmMode)
{
    switch (enmMode)
    {
        case PGMMODE_REAL:          return "Real";
        case PGMMODE_PROTECTED:     return "Protected";
        case PGMMODE_32_BIT:        return "32-bit";
        case PGMMODE_PAE:           return "PAE";
        case PGMMODE_PAE_NX:        return "PAE+NX";
        case PGMMODE_AMD64:         return "AMD64";
        case PGMMODE_AMD64_NX:      return "AMD64+NX";
        case PGMMODE_NESTED_32BIT:  return "Nested-32";
        case PGMMODE_NESTED_PAE:    return "Nested-PAE";
        case PGMMODE_NESTED_AMD64:  return "Nested-AMD64";
        case PGMMODE_EPT:           return "EPT";
        case PGMMODE_NONE:          return "None";
        default:                    return "unknown mode value";
    }
}


#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
/**
 * Gets the SLAT mode name.
 *
 * @returns The read-only SLAT mode descriptive string.
 * @param   enmSlatMode     The SLAT mode value.
 */
VMM_INT_DECL(const char *) PGMGetSlatModeName(PGMSLAT enmSlatMode)
{
    switch (enmSlatMode)
    {
        case PGMSLAT_DIRECT:        return "Direct";
        case PGMSLAT_EPT:           return "EPT";
        case PGMSLAT_32BIT:         return "32-bit";
        case PGMSLAT_PAE:           return "PAE";
        case PGMSLAT_AMD64:         return "AMD64";
        default:                    return "Unknown";
    }
}
#endif  /* VBOX_WITH_NESTED_HWVIRT_VMX_EPT */


/**
 * Gets the physical address represented in the guest CR3 as PGM sees it.
 *
 * This is mainly for logging and debugging.
 *
 * @returns PGM's guest CR3 value.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
VMM_INT_DECL(RTGCPHYS) PGMGetGuestCR3Phys(PVMCPU pVCpu)
{
    return pVCpu->pgm.s.GCPhysCR3;
}



/**
 * Notification from CPUM that the EFER.NXE bit has changed.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the CPU for
 *                      which EFER changed.
 * @param   fNxe        The new NXE state.
 */
VMM_INT_DECL(void) PGMNotifyNxeChanged(PVMCPU pVCpu, bool fNxe)
{
/** @todo VMCPU_ASSERT_EMT_OR_NOT_RUNNING(pVCpu); */
    Log(("PGMNotifyNxeChanged: fNxe=%RTbool\n", fNxe));

    pVCpu->pgm.s.fNoExecuteEnabled = fNxe;
    if (fNxe)
    {
        /*pVCpu->pgm.s.fGst32BitMbzBigPdeMask - N/A */
        pVCpu->pgm.s.fGstPaeMbzPteMask       &= ~X86_PTE_PAE_NX;
        pVCpu->pgm.s.fGstPaeMbzPdeMask       &= ~X86_PDE_PAE_NX;
        pVCpu->pgm.s.fGstPaeMbzBigPdeMask    &= ~X86_PDE2M_PAE_NX;
        /*pVCpu->pgm.s.fGstPaeMbzPdpeMask - N/A */
        pVCpu->pgm.s.fGstAmd64MbzPteMask     &= ~X86_PTE_PAE_NX;
        pVCpu->pgm.s.fGstAmd64MbzPdeMask     &= ~X86_PDE_PAE_NX;
        pVCpu->pgm.s.fGstAmd64MbzBigPdeMask  &= ~X86_PDE2M_PAE_NX;
        pVCpu->pgm.s.fGstAmd64MbzPdpeMask    &= ~X86_PDPE_LM_NX;
        pVCpu->pgm.s.fGstAmd64MbzBigPdpeMask &= ~X86_PDPE_LM_NX;
        pVCpu->pgm.s.fGstAmd64MbzPml4eMask   &= ~X86_PML4E_NX;

        pVCpu->pgm.s.fGst64ShadowedPteMask        |= X86_PTE_PAE_NX;
        pVCpu->pgm.s.fGst64ShadowedPdeMask        |= X86_PDE_PAE_NX;
        pVCpu->pgm.s.fGst64ShadowedBigPdeMask     |= X86_PDE2M_PAE_NX;
        pVCpu->pgm.s.fGst64ShadowedBigPde4PteMask |= X86_PDE2M_PAE_NX;
        pVCpu->pgm.s.fGstAmd64ShadowedPdpeMask    |= X86_PDPE_LM_NX;
        pVCpu->pgm.s.fGstAmd64ShadowedPml4eMask   |= X86_PML4E_NX;
    }
    else
    {
        /*pVCpu->pgm.s.fGst32BitMbzBigPdeMask - N/A */
        pVCpu->pgm.s.fGstPaeMbzPteMask       |= X86_PTE_PAE_NX;
        pVCpu->pgm.s.fGstPaeMbzPdeMask       |= X86_PDE_PAE_NX;
        pVCpu->pgm.s.fGstPaeMbzBigPdeMask    |= X86_PDE2M_PAE_NX;
        /*pVCpu->pgm.s.fGstPaeMbzPdpeMask -N/A */
        pVCpu->pgm.s.fGstAmd64MbzPteMask     |= X86_PTE_PAE_NX;
        pVCpu->pgm.s.fGstAmd64MbzPdeMask     |= X86_PDE_PAE_NX;
        pVCpu->pgm.s.fGstAmd64MbzBigPdeMask  |= X86_PDE2M_PAE_NX;
        pVCpu->pgm.s.fGstAmd64MbzPdpeMask    |= X86_PDPE_LM_NX;
        pVCpu->pgm.s.fGstAmd64MbzBigPdpeMask |= X86_PDPE_LM_NX;
        pVCpu->pgm.s.fGstAmd64MbzPml4eMask   |= X86_PML4E_NX;

        pVCpu->pgm.s.fGst64ShadowedPteMask        &= ~X86_PTE_PAE_NX;
        pVCpu->pgm.s.fGst64ShadowedPdeMask        &= ~X86_PDE_PAE_NX;
        pVCpu->pgm.s.fGst64ShadowedBigPdeMask     &= ~X86_PDE2M_PAE_NX;
        pVCpu->pgm.s.fGst64ShadowedBigPde4PteMask &= ~X86_PDE2M_PAE_NX;
        pVCpu->pgm.s.fGstAmd64ShadowedPdpeMask    &= ~X86_PDPE_LM_NX;
        pVCpu->pgm.s.fGstAmd64ShadowedPml4eMask   &= ~X86_PML4E_NX;
    }
}


/**
 * Check if any pgm pool pages are marked dirty (not monitored)
 *
 * @returns bool locked/not locked
 * @param   pVM         The cross context VM structure.
 */
VMMDECL(bool) PGMHasDirtyPages(PVM pVM)
{
    return pVM->pgm.s.CTX_SUFF(pPool)->cDirtyPages != 0;
}


/**
 * Check if this VCPU currently owns the PGM lock.
 *
 * @returns bool owner/not owner
 * @param   pVM         The cross context VM structure.
 */
VMMDECL(bool) PGMIsLockOwner(PVMCC pVM)
{
    return PDMCritSectIsOwner(pVM, &pVM->pgm.s.CritSectX);
}


/**
 * Enable or disable large page usage
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   fUseLargePages  Use/not use large pages
 */
VMMDECL(int) PGMSetLargePageUsage(PVMCC pVM, bool fUseLargePages)
{
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    pVM->pgm.s.fUseLargePages = fUseLargePages;
    return VINF_SUCCESS;
}


/**
 * Acquire the PGM lock.
 *
 * @returns VBox status code
 * @param   pVM         The cross context VM structure.
 * @param   fVoid       Set if the caller cannot handle failure returns.
 * @param   SRC_POS     The source position of the caller (RT_SRC_POS).
 */
#if defined(VBOX_STRICT) || defined(DOXYGEN_RUNNING)
int pgmLockDebug(PVMCC pVM, bool fVoid, RT_SRC_POS_DECL)
#else
int pgmLock(PVMCC pVM, bool fVoid)
#endif
{
#if defined(VBOX_STRICT)
    int rc = PDMCritSectEnterDebug(pVM, &pVM->pgm.s.CritSectX, VINF_SUCCESS, (uintptr_t)ASMReturnAddress(), RT_SRC_POS_ARGS);
#else
    int rc = PDMCritSectEnter(pVM, &pVM->pgm.s.CritSectX, VINF_SUCCESS);
#endif
    if (RT_SUCCESS(rc))
        return rc;
    if (fVoid)
        PDM_CRITSECT_RELEASE_ASSERT_RC(pVM, &pVM->pgm.s.CritSectX, rc);
    else
        AssertRC(rc);
    return rc;
}


/**
 * Release the PGM lock.
 *
 * @param   pVM         The cross context VM structure.
 */
void pgmUnlock(PVMCC pVM)
{
    uint32_t cDeprecatedPageLocks = pVM->pgm.s.cDeprecatedPageLocks;
    pVM->pgm.s.cDeprecatedPageLocks = 0;
    int rc = PDMCritSectLeave(pVM, &pVM->pgm.s.CritSectX);
    if (rc == VINF_SEM_NESTED)
        pVM->pgm.s.cDeprecatedPageLocks = cDeprecatedPageLocks;
}


#if !defined(IN_R0) || defined(LOG_ENABLED)

/** Format handler for PGMPAGE.
 * @copydoc FNRTSTRFORMATTYPE */
static DECLCALLBACK(size_t) pgmFormatTypeHandlerPage(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                                                     const char *pszType, void const *pvValue,
                                                     int cchWidth, int cchPrecision, unsigned fFlags,
                                                     void *pvUser)
{
    size_t    cch;
    PCPGMPAGE pPage = (PCPGMPAGE)pvValue;
    if (RT_VALID_PTR(pPage))
    {
        char szTmp[64+80];

        cch = 0;

        /* The single char state stuff. */
        static const char s_achPageStates[4]    = { 'Z', 'A', 'W', 'S' };
        szTmp[cch++] = s_achPageStates[PGM_PAGE_GET_STATE_NA(pPage)];

# define IS_PART_INCLUDED(lvl) ( !(fFlags & RTSTR_F_PRECISION) || cchPrecision == (lvl) || cchPrecision >= (lvl)+10 )
        if (IS_PART_INCLUDED(5))
        {
            static const char s_achHandlerStates[4*2] = { '-', 't', 'w', 'a' , '_', 'T', 'W', 'A' };
            szTmp[cch++] = s_achHandlerStates[  PGM_PAGE_GET_HNDL_PHYS_STATE(pPage)
                                              | ((uint8_t)PGM_PAGE_IS_HNDL_PHYS_NOT_IN_HM(pPage) << 2)];
        }

        /* The type. */
        if (IS_PART_INCLUDED(4))
        {
            szTmp[cch++] = ':';
            static const char s_achPageTypes[8][4]  = { "INV", "RAM", "MI2", "M2A", "SHA", "ROM", "MIO", "BAD" };
            szTmp[cch++] = s_achPageTypes[PGM_PAGE_GET_TYPE_NA(pPage)][0];
            szTmp[cch++] = s_achPageTypes[PGM_PAGE_GET_TYPE_NA(pPage)][1];
            szTmp[cch++] = s_achPageTypes[PGM_PAGE_GET_TYPE_NA(pPage)][2];
        }

        /* The numbers. */
        if (IS_PART_INCLUDED(3))
        {
            szTmp[cch++] = ':';
            cch += RTStrFormatNumber(&szTmp[cch], PGM_PAGE_GET_HCPHYS_NA(pPage), 16, 12, 0, RTSTR_F_ZEROPAD | RTSTR_F_64BIT);
        }

        if (IS_PART_INCLUDED(2))
        {
            szTmp[cch++] = ':';
            cch += RTStrFormatNumber(&szTmp[cch], PGM_PAGE_GET_PAGEID(pPage), 16, 7, 0, RTSTR_F_ZEROPAD | RTSTR_F_32BIT);
        }

        if (IS_PART_INCLUDED(6))
        {
            szTmp[cch++] = ':';
            static const char s_achRefs[4] = { '-', 'U', '!', 'L' };
            szTmp[cch++] = s_achRefs[PGM_PAGE_GET_TD_CREFS_NA(pPage)];
            cch += RTStrFormatNumber(&szTmp[cch], PGM_PAGE_GET_TD_IDX_NA(pPage), 16, 4, 0, RTSTR_F_ZEROPAD | RTSTR_F_16BIT);
        }
# undef IS_PART_INCLUDED

        cch = pfnOutput(pvArgOutput, szTmp, cch);
#if 0
        size_t cch2 = 0;
        szTmp[cch2++] = '(';
        cch2 += RTStrFormatNumber(&szTmp[cch2], (uintptr_t)pPage, 16, 18, 0, RTSTR_F_SPECIAL | RTSTR_F_ZEROPAD | RTSTR_F_64BIT);
        szTmp[cch2++] = ')';
        szTmp[cch2] = '\0';
        cch += pfnOutput(pvArgOutput, szTmp, cch2);
#endif
    }
    else
        cch = pfnOutput(pvArgOutput, RT_STR_TUPLE("<bad-pgmpage-ptr>"));
    NOREF(pszType); NOREF(cchWidth); NOREF(pvUser);
    return cch;
}


/** Format handler for PGMRAMRANGE.
 * @copydoc FNRTSTRFORMATTYPE */
static DECLCALLBACK(size_t) pgmFormatTypeHandlerRamRange(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                                                         const char *pszType, void const *pvValue,
                                                         int cchWidth, int cchPrecision, unsigned fFlags,
                                                         void *pvUser)
{
    size_t              cch;
    PGMRAMRANGE const  *pRam = (PGMRAMRANGE const *)pvValue;
    if (RT_VALID_PTR(pRam))
    {
        char szTmp[80];
        cch = RTStrPrintf(szTmp, sizeof(szTmp), "%RGp-%RGp", pRam->GCPhys, pRam->GCPhysLast);
        cch = pfnOutput(pvArgOutput, szTmp, cch);
    }
    else
        cch = pfnOutput(pvArgOutput, RT_STR_TUPLE("<bad-pgmramrange-ptr>"));
    NOREF(pszType); NOREF(cchWidth); NOREF(cchPrecision); NOREF(pvUser); NOREF(fFlags);
    return cch;
}

/** Format type andlers to be registered/deregistered. */
static const struct
{
    char                szType[24];
    PFNRTSTRFORMATTYPE  pfnHandler;
} g_aPgmFormatTypes[] =
{
    { "pgmpage",        pgmFormatTypeHandlerPage },
    { "pgmramrange",    pgmFormatTypeHandlerRamRange }
};

#endif /* !IN_R0 || LOG_ENABLED */

/**
 * Registers the global string format types.
 *
 * This should be called at module load time or in some other manner that ensure
 * that it's called exactly one time.
 *
 * @returns IPRT status code on RTStrFormatTypeRegister failure.
 */
VMMDECL(int) PGMRegisterStringFormatTypes(void)
{
#if !defined(IN_R0) || defined(LOG_ENABLED)
    int         rc = VINF_SUCCESS;
    unsigned    i;
    for (i = 0; RT_SUCCESS(rc) && i < RT_ELEMENTS(g_aPgmFormatTypes); i++)
    {
        rc = RTStrFormatTypeRegister(g_aPgmFormatTypes[i].szType, g_aPgmFormatTypes[i].pfnHandler, NULL);
# ifdef IN_RING0
        if (rc == VERR_ALREADY_EXISTS)
        {
            /* in case of cleanup failure in ring-0 */
            RTStrFormatTypeDeregister(g_aPgmFormatTypes[i].szType);
            rc = RTStrFormatTypeRegister(g_aPgmFormatTypes[i].szType, g_aPgmFormatTypes[i].pfnHandler, NULL);
        }
# endif
    }
    if (RT_FAILURE(rc))
        while (i-- > 0)
            RTStrFormatTypeDeregister(g_aPgmFormatTypes[i].szType);

    return rc;
#else
    return VINF_SUCCESS;
#endif
}


/**
 * Deregisters the global string format types.
 *
 * This should be called at module unload time or in some other manner that
 * ensure that it's called exactly one time.
 */
VMMDECL(void) PGMDeregisterStringFormatTypes(void)
{
#if !defined(IN_R0) || defined(LOG_ENABLED)
    for (unsigned i = 0; i < RT_ELEMENTS(g_aPgmFormatTypes); i++)
        RTStrFormatTypeDeregister(g_aPgmFormatTypes[i].szType);
#endif
}


#ifdef VBOX_STRICT
/**
 * Asserts that everything related to the guest CR3 is correctly shadowed.
 *
 * This will call PGMAssertNoMappingConflicts() and PGMAssertHandlerAndFlagsInSync(),
 * and assert the correctness of the guest CR3 mapping before asserting that the
 * shadow page tables is in sync with the guest page tables.
 *
 * @returns Number of conflicts.
 * @param   pVM     The cross context VM structure.
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   cr3     The current guest CR3 register value.
 * @param   cr4     The current guest CR4 register value.
 */
VMMDECL(unsigned) PGMAssertCR3(PVMCC pVM, PVMCPUCC pVCpu, uint64_t cr3, uint64_t cr4)
{
    STAM_PROFILE_START(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncCR3), a);

    uintptr_t const idxBth = pVCpu->pgm.s.idxBothModeData;
    AssertReturn(idxBth < RT_ELEMENTS(g_aPgmBothModeData), -VERR_PGM_MODE_IPE);
    AssertReturn(g_aPgmBothModeData[idxBth].pfnAssertCR3, -VERR_PGM_MODE_IPE);

    PGM_LOCK_VOID(pVM);
    unsigned cErrors = g_aPgmBothModeData[idxBth].pfnAssertCR3(pVCpu, cr3, cr4, 0, ~(RTGCPTR)0);
    PGM_UNLOCK(pVM);

    STAM_PROFILE_STOP(&pVCpu->pgm.s.Stats.CTX_MID_Z(Stat,SyncCR3), a);
    return cErrors;
}
#endif /* VBOX_STRICT */


/**
 * Updates PGM's copy of the guest's EPT pointer.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uEptPtr     The EPT pointer.
 *
 * @remarks This can be called as part of VM-entry so we might be in the midst of
 *          switching to VMX non-root mode.
 */
VMM_INT_DECL(void) PGMSetGuestEptPtr(PVMCPUCC pVCpu, uint64_t uEptPtr)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    PGM_LOCK_VOID(pVM);
    pVCpu->pgm.s.uEptPtr = uEptPtr;
    pVCpu->pgm.s.pGstEptPml4R3 = 0;
    pVCpu->pgm.s.pGstEptPml4R0 = 0;
    PGM_UNLOCK(pVM);
}

#ifdef PGM_WITH_PAGE_ZEROING_DETECTION

/**
 * Helper for checking whether XMM0 is zero, possibly retriving external state.
 */
static bool pgmHandlePageZeroingIsXmm0Zero(PVMCPUCC pVCpu, PCPUMCTX pCtx)
{
    if (pCtx->fExtrn & CPUMCTX_EXTRN_SSE_AVX)
    {
        int rc = CPUMImportGuestStateOnDemand(pVCpu, CPUMCTX_EXTRN_SSE_AVX);
        AssertRCReturn(rc, false);
    }
    return pCtx->XState.x87.aXMM[0].au64[0] == 0
        && pCtx->XState.x87.aXMM[0].au64[1] == 0
        && pCtx->XState.x87.aXMM[0].au64[2] == 0
        && pCtx->XState.x87.aXMM[0].au64[3] == 0;
}


/**
 * Helper for comparing opcode bytes.
 */
static bool pgmHandlePageZeroingMatchOpcodes(PVMCPUCC pVCpu, PCPUMCTX pCtx, uint8_t const *pbOpcodes, uint32_t cbOpcodes)
{
    uint8_t abTmp[64];
    AssertMsgReturn(cbOpcodes <= sizeof(abTmp), ("cbOpcodes=%#x\n", cbOpcodes), false);
    int rc = PGMPhysSimpleReadGCPtr(pVCpu, abTmp, pCtx->rip + pCtx->cs.u64Base, cbOpcodes);
    if (RT_SUCCESS(rc))
        return memcmp(abTmp, pbOpcodes, cbOpcodes) == 0;
    return false;
}


/**
 * Called on faults on ZERO pages to check if the guest is trying to zero it.
 *
 * Since it's a waste of time to zero a ZERO page and it will cause an
 * unnecessary page allocation, we'd like to detect and avoid this.
 * If any known page zeroing code is detected, this function will update the CPU
 * state to pretend the page was zeroed by the code.
 *
 * @returns true if page zeroing code was detected and CPU state updated to skip
 *          the code.
 * @param   pVCpu   The cross context virtual CPU structure of the calling EMT.
 * @param   pCtx    The guest register context.
 */
static bool pgmHandlePageZeroingCode(PVMCPUCC pVCpu, PCPUMCTX pCtx)
{
    CPUMCTX_ASSERT_NOT_EXTRN(pCtx, CPUMCTX_EXTRN_CR0 | CPUMCTX_EXTRN_CR3 | CPUMCTX_EXTRN_CR4 | CPUMCTX_EXTRN_EFER);

    /*
     * Sort by mode first.
     */
    if (CPUMIsGuestInLongModeEx(pCtx))
    {
        if (CPUMIsGuestIn64BitCodeEx(pCtx))
        {
            /*
             * 64-bit code.
             */
            Log9(("pgmHandlePageZeroingCode: not page zeroing - 64-bit\n"));
        }
        else if (pCtx->cs.Attr.n.u1DefBig)
            Log9(("pgmHandlePageZeroingCode: not page zeroing - 32-bit lm\n"));
        else
            Log9(("pgmHandlePageZeroingCode: not page zeroing - 16-bit lm\n"));
    }
    else if (CPUMIsGuestInPagedProtectedModeEx(pCtx))
    {
        if (pCtx->cs.Attr.n.u1DefBig)
        {
            /*
             * 32-bit paged protected mode code.
             */
            CPUMCTX_ASSERT_NOT_EXTRN(pCtx, CPUMCTX_EXTRN_RAX | CPUMCTX_EXTRN_RCX | CPUMCTX_EXTRN_RDX | CPUMCTX_EXTRN_RBX
                                         | CPUMCTX_EXTRN_RSP | CPUMCTX_EXTRN_RBP | CPUMCTX_EXTRN_RSI | CPUMCTX_EXTRN_RDI
                                         | CPUMCTX_EXTRN_RIP | CPUMCTX_EXTRN_RFLAGS);

            /* 1. Generic 'rep stosd' detection. */
            static uint8_t const s_abRepStosD[] = { 0xf3, 0xab };
            if (   pCtx->eax == 0
                && pCtx->ecx == X86_PAGE_SIZE / 4
                && !(pCtx->edi & X86_PAGE_OFFSET_MASK)
                && pgmHandlePageZeroingMatchOpcodes(pVCpu, pCtx, s_abRepStosD, sizeof(s_abRepStosD)))
            {
                pCtx->ecx  = 0;
                pCtx->edi += X86_PAGE_SIZE;
                Log9(("pgmHandlePageZeroingCode: REP STOSD: eip=%RX32 -> %RX32\n", pCtx->eip, pCtx->eip + sizeof(s_abRepStosD)));
                pCtx->eip += sizeof(s_abRepStosD);
                return true;
            }

            /* 2. Windows 2000 sp4 KiXMMIZeroPageNoSave loop code: */
            static uint8_t const s_abW2kSp4XmmZero[] =
            {
                0x0f, 0x2b, 0x01,
                0x0f, 0x2b, 0x41, 0x10,
                0x0f, 0x2b, 0x41, 0x20,
                0x0f, 0x2b, 0x41, 0x30,
                0x83, 0xc1, 0x40,
                0x48,
                0x75, 0xeb,
            };
            if (   pCtx->eax == 64
                && !(pCtx->ecx & X86_PAGE_OFFSET_MASK)
                && pgmHandlePageZeroingMatchOpcodes(pVCpu, pCtx, s_abW2kSp4XmmZero, sizeof(s_abW2kSp4XmmZero))
                && pgmHandlePageZeroingIsXmm0Zero(pVCpu, pCtx))
            {
                pCtx->eax  = 1;
                pCtx->ecx += X86_PAGE_SIZE;
                Log9(("pgmHandlePageZeroingCode: w2k sp4 xmm: eip=%RX32 -> %RX32\n",
                      pCtx->eip, pCtx->eip + sizeof(s_abW2kSp4XmmZero) - 3));
                pCtx->eip += sizeof(s_abW2kSp4XmmZero) - 3;
                return true;
            }
            Log9(("pgmHandlePageZeroingCode: not page zeroing - 32-bit\n"));
        }
        else if (!pCtx->eflags.Bits.u1VM)
            Log9(("pgmHandlePageZeroingCode: not page zeroing - 16-bit\n"));
        else
            Log9(("pgmHandlePageZeroingCode: not page zeroing - v86\n"));
    }
    return false;
}

#endif /* PGM_WITH_PAGE_ZEROING_DETECTION */
