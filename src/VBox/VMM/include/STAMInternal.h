/* $Id: STAMInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * STAM Internal Header.
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

#ifndef VMM_INCLUDED_SRC_include_STAMInternal_h
#define VMM_INCLUDED_SRC_include_STAMInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/gvmm.h>
#include <VBox/vmm/gmm.h>
#include <iprt/list.h>
#include <iprt/semaphore.h>



RT_C_DECLS_BEGIN

/** @defgroup grp_stam_int   Internals
 * @ingroup grp_stam
 * @internal
 * @{
 */

/** Pointer to sample descriptor. */
typedef struct STAMDESC    *PSTAMDESC;
/** Pointer to a sample lookup node. */
typedef struct STAMLOOKUP  *PSTAMLOOKUP;

/**
 * Sample lookup node.
 */
typedef struct STAMLOOKUP
{
    /** The parent lookup record. This is NULL for the root node. */
    PSTAMLOOKUP         pParent;
    /** Array of children (using array for binary searching). */
    PSTAMLOOKUP        *papChildren;
    /** Pointer to the description node, if any. */
    PSTAMDESC           pDesc;
    /** Number of decentants with descriptors. (Use for freeing up sub-trees.) */
    uint32_t            cDescsInTree;
    /** The number of children. */
    uint16_t            cChildren;
    /** The index in the parent paChildren array. UINT16_MAX for the root node. */
    uint16_t            iParent;
    /** The path offset. */
    uint16_t            off;
    /** The size of the path component. */
    uint16_t            cch;
    /** The name (variable size). */
    char                szName[1];
} STAMLOOKUP;


/**
 * The "sample data" of a STAMTYPE_INTERNAL_SUM sample.
 */
typedef struct STAMSUMSAMPLE
{
    /** The sum value. */
    union
    {
        /** Any counter or unsigned number type. */
        STAMCOUNTER     Counter;
        /** Profile or advance profile.   */
        STAMPROFILE     Profile;
    } u;
    /** The actual type of the SUM. */
    STAMTYPE            enmType;
    /** The type of the first sample. */
    uint8_t             enmTypeFirst;
    /** Used to decide the unit when gathering summands during registration. */
    uint8_t             enmUnit : 7;
    /** Used by pct-of-sum to decide whether to include the value in the sum. */
    uint8_t             fAddValueToSum : 1;
    /** Max number of items paSummands can hold. */
    uint8_t             cSummandsAlloc;
    /** The number of summands in paSummands. */
    uint8_t             cSummands;
    /** Pointer to the description of each of the samples to be summed up. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    PSTAMDESC           apSummands[RT_FLEXIBLE_ARRAY];
} STAMSUMSAMPLE;
/** Pointer to the data for a sum sample. */
typedef STAMSUMSAMPLE *PSTAMSUMSAMPLE;


/**
 * Sample descriptor.
 */
typedef struct STAMDESC
{
    /** Our entry in the big linear list. */
    RTLISTNODE          ListEntry;
    /** Pointer to our lookup node. */
    PSTAMLOOKUP         pLookup;
    /** Sample name. */
    const char         *pszName;
    /** Sample type. */
    STAMTYPE            enmType;
    /** Visibility type. */
    STAMVISIBILITY      enmVisibility;
    /** Pointer to the sample data. */
    union STAMDESCSAMPLEDATA
    {
        /** Counter. */
        PSTAMCOUNTER    pCounter;
        /** Profile. */
        PSTAMPROFILE    pProfile;
        /** Advanced profile. */
        PSTAMPROFILEADV pProfileAdv;
        /** Ratio, unsigned 32-bit. */
        PSTAMRATIOU32   pRatioU32;
        /** unsigned 8-bit. */
        uint8_t        *pu8;
        /** unsigned 16-bit. */
        uint16_t       *pu16;
        /** unsigned 32-bit. */
        uint32_t       *pu32;
        /** unsigned 64-bit. */
        uint64_t       *pu64;
        /** Simple void pointer. */
        void           *pv;
        /** Boolean. */
        bool           *pf;
        /** */
        struct STAMDESCSAMPLEDATACALLBACKS
        {
            /** The same pointer. */
            void                   *pvSample;
            /** Pointer to the reset callback. */
            PFNSTAMR3CALLBACKRESET  pfnReset;
            /** Pointer to the print callback. */
            PFNSTAMR3CALLBACKPRINT  pfnPrint;
        }               Callback;
        /** Sum.  This is allocated separately. */
        PSTAMSUMSAMPLE  pSum;
    }                   u;
    /** Unit. */
    STAMUNIT            enmUnit;
    /** The refresh group number (STAM_REFRESH_GRP_XXX). */
    uint8_t             iRefreshGroup;
    /** Description. */
    const char         *pszDesc;
} STAMDESC;


/**
 * STAM data kept in the UVM.
 */
typedef struct STAMUSERPERVM
{
    /** List of samples. */
    RTLISTANCHOR            List;
    /** Root of the lookup tree. */
    PSTAMLOOKUP             pRoot;

    /** RW Lock for the list and tree. */
    RTSEMRW                 RWSem;

    /** The copy of the GVMM statistics. */
    GVMMSTATS               GVMMStats;
    /** The number of registered host CPU leaves. */
    uint32_t                cRegisteredHostCpus;

    /** Explicit alignment padding. */
    uint32_t                uAlignment;
    /** The copy of the GMM statistics. */
    GMMSTATS                GMMStats;
} STAMUSERPERVM;
#ifdef IN_RING3
AssertCompileMemberAlignment(STAMUSERPERVM, GMMStats, 8);
#endif

/** Pointer to the STAM data kept in the UVM. */
typedef STAMUSERPERVM *PSTAMUSERPERVM;


/** Locks the sample descriptors for reading. */
#define STAM_LOCK_RD(pUVM)      do { int rcSem = RTSemRWRequestRead(pUVM->stam.s.RWSem, RT_INDEFINITE_WAIT);  AssertRC(rcSem); } while (0)
/** Locks the sample descriptors for writing. */
#define STAM_LOCK_WR(pUVM)      do { int rcSem = RTSemRWRequestWrite(pUVM->stam.s.RWSem, RT_INDEFINITE_WAIT); AssertRC(rcSem); } while (0)
/** UnLocks the sample descriptors after reading. */
#define STAM_UNLOCK_RD(pUVM)    do { int rcSem = RTSemRWReleaseRead(pUVM->stam.s.RWSem);  AssertRC(rcSem); } while (0)
/** UnLocks the sample descriptors after writing. */
#define STAM_UNLOCK_WR(pUVM)    do { int rcSem = RTSemRWReleaseWrite(pUVM->stam.s.RWSem); AssertRC(rcSem); } while (0)
/** Lazy initialization */
#define STAM_LAZY_INIT(pUVM)    do { } while (0)

/** @} */

RT_C_DECLS_END

#endif /* !VMM_INCLUDED_SRC_include_STAMInternal_h */
