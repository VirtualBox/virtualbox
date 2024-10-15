/* $Id: SUPDrvTracer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxDrv - The VirtualBox Support Driver - Tracer Interface.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_SUP_DRV
#define SUPDRV_AGNOSTIC
#include "SUPDrvInternal.h"

#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/VBoxTpG.h>

#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>
#include <iprt/param.h>
#include <iprt/uuid.h>

#if defined(RT_OS_LINUX)
# if RTLNX_VER_MIN(4,15,10)
#  include <asm/nospec-branch.h>
# endif /* < 4.15.10 */
# if RTLNX_VER_MIN(5,17,0)
#  include <asm/linkage.h>
# endif /* < 5.17.0 */
#endif /* !RT_OS_LINUX */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to a user tracer module registration record. */
typedef struct SUPDRVTRACERUMOD *PSUPDRVTRACERUMOD;

/**
 * Data for a tracepoint provider.
 */
typedef struct SUPDRVTPPROVIDER
{
    /** The entry in the provider list for this image. */
    RTLISTNODE              ListEntry;
    /** The entry in the per session provider list for this image. */
    RTLISTNODE              SessionListEntry;

    /** The core structure. */
    SUPDRVVDTPROVIDERCORE   Core;

    /** Pointer to the image this provider resides in.  NULL if it's a
     * driver. */
    PSUPDRVLDRIMAGE         pImage;
    /** The session this provider is associated with if registered via
     * SUPR0VtgRegisterDrv.  NULL if pImage is set. */
    PSUPDRVSESSION          pSession;
    /** The user tracepoint module associated with this provider.  NULL if
     *  pImage is set. */
    PSUPDRVTRACERUMOD       pUmod;

    /** Used to indicate that we've called pfnProviderDeregistered already and it
     * failed because the provider was busy.  Next time we must try
     * pfnProviderDeregisterZombie.
     *
     * @remarks This does not necessiarly mean the provider is in the zombie
     *          list.  See supdrvTracerCommonDeregisterImpl. */
    bool                    fZombie;
    /** Set if the provider has been successfully registered with the
     *  tracer. */
    bool                    fRegistered;
    /** The provider name (for logging purposes). */
    RT_FLEXIBLE_ARRAY_EXTENSION
    char                    szName[RT_FLEXIBLE_ARRAY];
} SUPDRVTPPROVIDER;
/** Pointer to the data for a tracepoint provider. */
typedef SUPDRVTPPROVIDER *PSUPDRVTPPROVIDER;


/**
 * User tracer module VTG data copy.
 */
typedef struct SUPDRVVTGCOPY
{
    /** Magic (SUDPRVVTGCOPY_MAGIC).  */
    uint32_t    u32Magic;
    /** Refernece counter (we expect to share a lot of these). */
    uint32_t    cRefs;
    /** The size of the  */
    uint32_t    cbStrTab;
    /** Image type flags. */
    uint32_t    fFlags;
    /** Hash list entry (SUPDRVDEVEXT::aTrackerUmodHash).  */
    RTLISTNODE  ListEntry;
    /** The VTG object header.
     * The rest of the data follows immediately afterwards.  First the object,
     * then the probe locations and finally the probe location string table. All
     * pointers are fixed up to point within this data. */
    VTGOBJHDR   Hdr;
} SUPDRVVTGCOPY;
/** Pointer to a VTG object copy. */
typedef SUPDRVVTGCOPY *PSUPDRVVTGCOPY;
/** Magic value for SUPDRVVTGCOPY. */
#define SUDPRVVTGCOPY_MAGIC UINT32_C(0x00080386)


/**
 * User tracer module registration record.
 */
typedef struct SUPDRVTRACERUMOD
{
    /** Magic (SUPDRVTRACERUMOD_MAGIC).  */
    uint32_t                u32Magic;
    /** List entry.  This is anchored in SUPDRVSESSION::UmodList. */
    RTLISTNODE              ListEntry;
    /** The address of the ring-3 VTG header. */
    RTR3PTR                 R3PtrVtgHdr;
    /** Pointer to the ring-0 copy of the VTG data. */
    PSUPDRVVTGCOPY          pVtgCopy;
    /** The memory object that locks down the user memory. */
    RTR0MEMOBJ              hMemObjLock;
    /** The memory object that maps the locked memory into kernel space. */
    RTR0MEMOBJ              hMemObjMap;
    /** Pointer to the probe enabled-count array within the mapping. */
    uint32_t               *pacProbeEnabled;
    /** Pointer to the probe location array within the mapping. */
    void                   *pvProbeLocs;
    /** The address of the ring-3 probe locations. */
    RTR3PTR                 R3PtrProbeLocs;
    /** The lookup table index. */
    uint8_t                 iLookupTable;
    /** The module bit count. */
    uint8_t                 cBits;
    /** The size of a probe location record. */
    uint8_t                 cbProbeLoc;
    /** The number of probe locations. */
    uint32_t                cProbeLocs;
    /** Ring-0 probe location info. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    SUPDRVPROBELOC          aProbeLocs[RT_FLEXIBLE_ARRAY];
} SUPDRVTRACERUMOD;
/** Magic value for SUPDRVVTGCOPY. */
#define SUPDRVTRACERUMOD_MAGIC UINT32_C(0x00080486)


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Simple SUPR0Printf-style logging.  */
#ifdef DEBUG_bird
# define LOG_TRACER(a_Args)  SUPR0Printf a_Args
#else
# define LOG_TRACER(a_Args)  do { } while (0)
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The address of the current probe fire routine for kernel mode. */
PFNRT       g_pfnSupdrvProbeFireKernel = supdrvTracerProbeFireStub;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void supdrvVtgReleaseObjectCopy(PSUPDRVDEVEXT pDevExt, PSUPDRVVTGCOPY pThis);



/**
 * Validates a VTG string against length and characterset limitations.
 *
 * @returns VINF_SUCCESS, VERR_SUPDRV_VTG_BAD_STRING or
 *          VERR_SUPDRV_VTG_STRING_TOO_LONG.
 * @param   psz                 The string.
 */
static int supdrvVtgValidateString(const char *psz)
{
    size_t off = 0;
    while (off < _4K)
    {
        char const ch = psz[off++];
        if (!ch)
            return VINF_SUCCESS;
        if (   !RTLocCIsAlNum(ch)
            && ch != ' '
            && ch != '_'
            && ch != '-'
            && ch != '('
            && ch != ')'
            && ch != ','
            && ch != '*'
            && ch != '&'
           )
        {
            /*RTAssertMsg2("off=%u '%s'\n",  off, psz);*/
            return VERR_SUPDRV_VTG_BAD_STRING;
        }
    }
    return VERR_SUPDRV_VTG_STRING_TOO_LONG;
}


/** Used by the validation code below. */
#define MY_CHECK_RET(a_Expr, a_rc) \
    MY_CHECK_MSG_RET(a_Expr, ("%s: Validation failed on line " RT_XSTR(__LINE__) ": " #a_Expr "\n", __FUNCTION__), a_rc)

/** Used by the validation code below. */
#define MY_CHECK_MSG_RET(a_Expr, a_PrintfArgs, a_rc) \
    do { if (RT_UNLIKELY(!(a_Expr))) { SUPR0Printf a_PrintfArgs; return (a_rc); } } while (0)

/** Used by the validation code below. */
#define MY_WITHIN_IMAGE(p, rc) \
    do { \
        if (pbImage) \
        { \
            if ((uintptr_t)(p) - (uintptr_t)pbImage > cbImage) \
            { \
                SUPR0Printf("supdrvVtgValidate: " #rc " - p=%p pbImage=%p cbImage=%#zxline=%u %s\n", \
                            p, pbImage, cbImage, #p); \
                return (rc); \
            } \
        } \
        else if (!RT_VALID_PTR(p)) \
            return (rc); \
    } while (0)


/**
 * Validates the VTG object header.
 *
 * @returns VBox status code.
 * @param   pVtgHdr             The header.
 * @param   uVtgHdrAddr         The address where the header is actually
 *                              loaded.
 * @param   pbImage             The image base, if available.
 * @param   cbImage             The image size, if available.
 * @param   fUmod               Whether this is a user module.
 */
static int supdrvVtgValidateHdr(PVTGOBJHDR pVtgHdr, RTUINTPTR uVtgHdrAddr, const uint8_t *pbImage, size_t cbImage, bool fUmod)
{
    struct VTGAREAS
    {
        uint32_t off;
        uint32_t cb;
    } const        *paAreas;
    unsigned        cAreas;
    unsigned        i;
    uint32_t        cbVtgObj;
    uint32_t        off;

#define MY_VALIDATE_SIZE(cb, cMin, cMax, cbUnit, rcBase) \
    do { \
        if ((cb) <  (cMin) * (cbUnit)) \
        { \
            SUPR0Printf("supdrvVtgValidateHdr: " #rcBase "_TOO_FEW - cb=%#zx cMin=%#zx cbUnit=%#zx line=%u %s\n", \
                        (size_t)(cb), (size_t)(cMin), (size_t)cbUnit, __LINE__, #cb); \
            return rcBase ## _TOO_FEW; \
        } \
        if ((cb) >= (cMax) * (cbUnit)) \
        { \
            SUPR0Printf("supdrvVtgValidateHdr: " #rcBase "_TOO_MUCH - cb=%#zx cMax=%#zx cbUnit=%#zx line=%u %s\n", \
                        (size_t)(cb), (size_t)(cMax), (size_t)cbUnit, __LINE__, #cb); \
            return rcBase ## _TOO_MUCH; \
        } \
        if ((cb) / (cbUnit) * (cbUnit) != (cb)) \
        { \
            SUPR0Printf("supdrvVtgValidateHdr: " #rcBase "_NOT_MULTIPLE - cb=%#zx cbUnit=%#zx line=%u %s\n", \
                        (size_t)(cb), (size_t)cbUnit, __LINE__, #cb); \
            return rcBase ## _NOT_MULTIPLE; \
        } \
    } while (0)

#define MY_VALIDATE_OFF(off, cb, cMin, cMax, cbUnit, cbAlign, rcBase) \
    do { \
        if (   (cb) >= cbVtgObj \
            || off > cbVtgObj - (cb) ) \
        { \
            SUPR0Printf("supdrvVtgValidateHdr: " #rcBase "_OFF - off=%#x cb=%#x pVtgHdr=%p cbVtgHdr=%#zx line=%u %s\n", \
                        (off), (cb), pVtgHdr, cbVtgObj, __LINE__, #off); \
            return rcBase ## _OFF; \
        } \
        if (RT_ALIGN(off, cbAlign) != (off)) \
        { \
            SUPR0Printf("supdrvVtgValidateHdr: " #rcBase "_OFF - off=%#x align=%#zx line=%u %s\n", \
                        (off), (size_t)(cbAlign), __LINE__, #off); \
            return rcBase ## _OFF; \
        } \
        MY_VALIDATE_SIZE(cb, cMin, cMax, cbUnit, rcBase); \
    } while (0)

    /*
     * Make sure both pbImage and cbImage are NULL/0 if one if of them is.
     */
    if (!pbImage || !cbImage)
    {
        pbImage = NULL;
        cbImage = 0;
        cbVtgObj = pVtgHdr->cbObj;
    }
    else
    {
        MY_WITHIN_IMAGE(pVtgHdr, VERR_SUPDRV_VTG_BAD_HDR_PTR);
        cbVtgObj = pVtgHdr->cbObj;
        MY_WITHIN_IMAGE((uint8_t *)pVtgHdr + cbVtgObj - 1, VERR_SUPDRV_VTG_BAD_HDR_PTR);
    }

    if (cbVtgObj > _1M)
    {
        SUPR0Printf("supdrvVtgValidateHdr: VERR_SUPDRV_TRACER_TOO_LARGE - cbVtgObj=%#x\n", cbVtgObj);
        return VERR_SUPDRV_TRACER_TOO_LARGE;
    }

    /*
     * Set the probe location array offset and size members.
     */
    if (!pVtgHdr->offProbeLocs)
    {
        uint64_t u64Tmp = pVtgHdr->uProbeLocsEnd.u64 - pVtgHdr->uProbeLocs.u64;
        if (u64Tmp >= UINT32_MAX)
        {
            SUPR0Printf("supdrvVtgValidateHdr: VERR_SUPDRV_VTG_BAD_HDR_TOO_MUCH - u64Tmp=%#llx ProbeLocs=%#llx ProbeLocsEnd=%#llx\n",
                        u64Tmp, pVtgHdr->uProbeLocs.u64, pVtgHdr->uProbeLocsEnd.u64);
            return VERR_SUPDRV_VTG_BAD_HDR_TOO_MUCH;
        }
        /*SUPR0Printf("supdrvVtgValidateHdr: cbProbeLocs %#x -> %#x\n", pVtgHdr->cbProbeLocs, (uint32_t)u64Tmp);*/
        pVtgHdr->cbProbeLocs  = (uint32_t)u64Tmp;

        u64Tmp = pVtgHdr->uProbeLocs.u64 - uVtgHdrAddr;
#ifdef RT_OS_DARWIN
        /* The loader and/or ld64-97.17 seems not to generate fixups for our
           __VTGObj section. Detect this by comparing them with the
           u64VtgObjSectionStart member and assume max image size of 4MB.
           Seems to be worked around by the __VTGPrLc.End and __VTGPrLc.Begin
           padding fudge, meaning that the linker misplaced the relocations. */
        if (   (int64_t)u64Tmp != (int32_t)u64Tmp
            && pVtgHdr->u64VtgObjSectionStart != uVtgHdrAddr
            && pVtgHdr->u64VtgObjSectionStart < _4M
            && pVtgHdr->uProbeLocsEnd.u64     < _4M
            && !fUmod)
        {
            uint64_t offDelta = uVtgHdrAddr - pVtgHdr->u64VtgObjSectionStart;
            /*SUPR0Printf("supdrvVtgValidateHdr: offDelta=%#llx\n", offDelta);*/
            pVtgHdr->uProbeLocs.u64        += offDelta;
            pVtgHdr->uProbeLocsEnd.u64     += offDelta;
            u64Tmp += offDelta;
        }
#endif
        if ((int64_t)u64Tmp != (int32_t)u64Tmp)
        {
            SUPR0Printf("supdrvVtgValidateHdr: VERR_SUPDRV_VTG_BAD_HDR_PTR - u64Tmp=%#llx uProbeLocs=%#llx uVtgHdrAddr=%RTptr\n",
                        u64Tmp, pVtgHdr->uProbeLocs.u64, uVtgHdrAddr);
            return VERR_SUPDRV_VTG_BAD_HDR_PTR;
        }
        /*SUPR0Printf("supdrvVtgValidateHdr: offProbeLocs %#x -> %#x\n", pVtgHdr->offProbeLocs, (int32_t)u64Tmp);*/
        pVtgHdr->offProbeLocs = (int32_t)u64Tmp;
    }

    /*
     * The non-area description fields.
     */
    if (memcmp(pVtgHdr->szMagic, VTGOBJHDR_MAGIC, sizeof(pVtgHdr->szMagic)))
    {
        SUPR0Printf("supdrvVtgValidateHdr: %p: %.16Rhxs\n", pVtgHdr, pVtgHdr->szMagic);
        return VERR_SUPDRV_VTG_MAGIC;
    }
    if (   pVtgHdr->cBits != ARCH_BITS
        && (   !fUmod
            || (   pVtgHdr->cBits != 32
                && pVtgHdr->cBits != 64)) )
        return VERR_SUPDRV_VTG_BITS;
    MY_CHECK_RET(pVtgHdr->au32Reserved1[0] == 0, VERR_SUPDRV_VTG_BAD_HDR_MISC);
    MY_CHECK_RET(pVtgHdr->au32Reserved1[1] == 0, VERR_SUPDRV_VTG_BAD_HDR_MISC);
    MY_CHECK_RET(!RTUuidIsNull(&pVtgHdr->Uuid), VERR_SUPDRV_VTG_BAD_HDR_MISC);

    /*
     * Check the individual area descriptors.
     */
    MY_VALIDATE_OFF(pVtgHdr->offStrTab,          pVtgHdr->cbStrTab,       4,   _1M, sizeof(char),            sizeof(uint8_t),  VERR_SUPDRV_VTG_BAD_HDR);
    MY_VALIDATE_OFF(pVtgHdr->offArgLists,        pVtgHdr->cbArgLists,     1,  _32K, sizeof(uint32_t),        sizeof(uint32_t), VERR_SUPDRV_VTG_BAD_HDR);
    MY_VALIDATE_OFF(pVtgHdr->offProbes,          pVtgHdr->cbProbes,       1,  _32K, sizeof(VTGDESCPROBE),    sizeof(uint32_t), VERR_SUPDRV_VTG_BAD_HDR);
    MY_VALIDATE_OFF(pVtgHdr->offProviders,       pVtgHdr->cbProviders,    1,    16, sizeof(VTGDESCPROVIDER), sizeof(uint32_t), VERR_SUPDRV_VTG_BAD_HDR);
    MY_VALIDATE_OFF(pVtgHdr->offProbeEnabled,    pVtgHdr->cbProbeEnabled, 1,  _32K, sizeof(uint32_t),        sizeof(uint32_t), VERR_SUPDRV_VTG_BAD_HDR);
    if (!fUmod)
    {
        MY_WITHIN_IMAGE(pVtgHdr->uProbeLocs.p,    VERR_SUPDRV_VTG_BAD_HDR_PTR);
        MY_WITHIN_IMAGE(pVtgHdr->uProbeLocsEnd.p, VERR_SUPDRV_VTG_BAD_HDR_PTR);
        MY_VALIDATE_SIZE(                        pVtgHdr->cbProbeLocs,    1, _128K, sizeof(VTGPROBELOC),     VERR_SUPDRV_VTG_BAD_HDR);
    }
    else
    {
        if (pVtgHdr->cBits == 32)
            MY_VALIDATE_SIZE(                   pVtgHdr->cbProbeLocs,    1, _8K,   sizeof(VTGPROBELOC32),    VERR_SUPDRV_VTG_BAD_HDR);
        else
            MY_VALIDATE_SIZE(                   pVtgHdr->cbProbeLocs,    1, _8K,   sizeof(VTGPROBELOC64),    VERR_SUPDRV_VTG_BAD_HDR);
        /* Will check later that offProbeLocs are following closely on the
           enable count array, so no need to validate the offset here. */
    }

    /*
     * Some additional consistency checks.
     */
    if (   pVtgHdr->uProbeLocsEnd.u64 - pVtgHdr->uProbeLocs.u64 != pVtgHdr->cbProbeLocs
        || (int64_t)(pVtgHdr->uProbeLocs.u64 - uVtgHdrAddr)     != pVtgHdr->offProbeLocs)
    {
        SUPR0Printf("supdrvVtgValidateHdr: VERR_SUPDRV_VTG_BAD_HDR_MISC - uProbeLocs=%#llx uProbeLocsEnd=%#llx offProbeLocs=%#llx cbProbeLocs=%#x uVtgHdrAddr=%RTptr\n",
                    pVtgHdr->uProbeLocs.u64, pVtgHdr->uProbeLocsEnd.u64, pVtgHdr->offProbeLocs, pVtgHdr->cbProbeLocs, uVtgHdrAddr);
        return VERR_SUPDRV_VTG_BAD_HDR_MISC;
    }

    if (pVtgHdr->cbProbes / sizeof(VTGDESCPROBE) != pVtgHdr->cbProbeEnabled / sizeof(uint32_t))
    {
        SUPR0Printf("supdrvVtgValidateHdr: VERR_SUPDRV_VTG_BAD_HDR_MISC - cbProbeEnabled=%#zx cbProbes=%#zx\n",
                    pVtgHdr->cbProbeEnabled, pVtgHdr->cbProbes);
        return VERR_SUPDRV_VTG_BAD_HDR_MISC;
    }

    /*
     * Check that there are no overlapping areas.  This is a little bit ugly...
     */
    paAreas = (struct VTGAREAS const *)&pVtgHdr->offStrTab;
    cAreas  = pVtgHdr->offProbeLocs >= 0 ? 6 : 5;
    off     = sizeof(VTGOBJHDR);
    for (i = 0; i < cAreas; i++)
    {
        if (paAreas[i].off < off)
        {
            SUPR0Printf("supdrvVtgValidateHdr: VERR_SUPDRV_VTG_BAD_HDR_MISC - overlapping areas %d and %d\n", i, i-1);
            return VERR_SUPDRV_VTG_BAD_HDR_MISC;
        }
        off = paAreas[i].off + paAreas[i].cb;
    }
    if (   pVtgHdr->offProbeLocs > 0
        && (uint32_t)-pVtgHdr->offProbeLocs < pVtgHdr->cbProbeLocs)
    {
        SUPR0Printf("supdrvVtgValidateHdr: VERR_SUPDRV_VTG_BAD_HDR_MISC - probe locations overlaps the header\n");
        return VERR_SUPDRV_VTG_BAD_HDR_MISC;
    }

    /*
     * Check that the object size is correct.
     */
    if (pVtgHdr->cbObj != pVtgHdr->offProbeEnabled + pVtgHdr->cbProbeEnabled)
    {
        SUPR0Printf("supdrvVtgValidateHdr: VERR_SUPDRV_VTG_BAD_HDR_MISC - bad header size %#x, expected %#x\n",
                    pVtgHdr->cbObj, pVtgHdr->offProbeEnabled + pVtgHdr->cbProbeEnabled);
        return VERR_SUPDRV_VTG_BAD_HDR_MISC;
    }


    return VINF_SUCCESS;
#undef MY_VALIDATE_OFF
#undef MY_VALIDATE_SIZE
}


/**
 * Validates the VTG data.
 *
 * @returns VBox status code.
 * @param   pVtgHdr             The VTG object header of the data to validate.
 * @param   uVtgHdrAddr         The address where the header is actually
 *                              loaded.
 * @param   pbImage             The image base. For validating the probe
 *                              locations.
 * @param   cbImage             The image size to go with @a pbImage.
 * @param   fUmod               Whether this is a user module.
 */
static int supdrvVtgValidate(PVTGOBJHDR pVtgHdr, RTUINTPTR uVtgHdrAddr, const uint8_t *pbImage, size_t cbImage, bool fUmod)
{
    uintptr_t   offTmp;
    uintptr_t   i;
    uintptr_t   cProviders;
    int         rc;

    if (!pbImage || !cbImage)
    {
        pbImage = NULL;
        cbImage = 0;
    }

#define MY_VALIDATE_STR(a_offStrTab) \
    do { \
        if ((a_offStrTab) >= pVtgHdr->cbStrTab) \
            return VERR_SUPDRV_VTG_STRTAB_OFF; \
        rc = supdrvVtgValidateString((char *)pVtgHdr + pVtgHdr->offStrTab + (a_offStrTab)); \
        if (rc != VINF_SUCCESS) \
            return rc; \
    } while (0)
#define MY_VALIDATE_ATTR(Attr) \
    do { \
        if ((Attr).u8Code    <= (uint8_t)kVTGStability_Invalid || (Attr).u8Code    >= (uint8_t)kVTGStability_End) \
            return VERR_SUPDRV_VTG_BAD_ATTR; \
        if ((Attr).u8Data    <= (uint8_t)kVTGStability_Invalid || (Attr).u8Data    >= (uint8_t)kVTGStability_End) \
            return VERR_SUPDRV_VTG_BAD_ATTR; \
        if ((Attr).u8DataDep <= (uint8_t)kVTGClass_Invalid     || (Attr).u8DataDep >= (uint8_t)kVTGClass_End) \
            return VERR_SUPDRV_VTG_BAD_ATTR; \
    } while (0)

    /*
     * The header.
     */
    rc = supdrvVtgValidateHdr(pVtgHdr, uVtgHdrAddr, pbImage, cbImage, fUmod);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Validate the providers.
     */
    cProviders = i = pVtgHdr->cbProviders / sizeof(VTGDESCPROVIDER);
    while (i-- > 0)
    {
        PCVTGDESCPROVIDER pProvider = (PCVTGDESCPROVIDER)((uintptr_t)pVtgHdr + pVtgHdr->offProviders) + i;

        MY_VALIDATE_STR(pProvider->offName);
        MY_CHECK_RET(pProvider->iFirstProbe < pVtgHdr->cbProbeEnabled / sizeof(uint32_t), VERR_SUPDRV_VTG_BAD_PROVIDER);
        MY_CHECK_RET((uint32_t)pProvider->iFirstProbe + pProvider->cProbes <= pVtgHdr->cbProbeEnabled / sizeof(uint32_t),
                     VERR_SUPDRV_VTG_BAD_PROVIDER);
        MY_VALIDATE_ATTR(pProvider->AttrSelf);
        MY_VALIDATE_ATTR(pProvider->AttrModules);
        MY_VALIDATE_ATTR(pProvider->AttrFunctions);
        MY_VALIDATE_ATTR(pProvider->AttrNames);
        MY_VALIDATE_ATTR(pProvider->AttrArguments);
        MY_CHECK_RET(pProvider->bReserved == 0, VERR_SUPDRV_VTG_BAD_PROVIDER);
        MY_CHECK_RET(pProvider->cProbesEnabled == 0, VERR_SUPDRV_VTG_BAD_PROVIDER);
        MY_CHECK_RET(pProvider->uSettingsSerialNo == 0, VERR_SUPDRV_VTG_BAD_PROVIDER);
    }

    /*
     * Validate probes.
     */
    i = pVtgHdr->cbProbes / sizeof(VTGDESCPROBE);
    while (i-- > 0)
    {
        PCVTGDESCPROBE      pProbe    = (PCVTGDESCPROBE)(   (uintptr_t)pVtgHdr + pVtgHdr->offProbes)    + i;
        PCVTGDESCPROVIDER   pProvider = (PCVTGDESCPROVIDER)((uintptr_t)pVtgHdr + pVtgHdr->offProviders) + pProbe->idxProvider;
        PCVTGDESCARGLIST    pArgList  = (PCVTGDESCARGLIST)( (uintptr_t)pVtgHdr + pVtgHdr->offArgLists + pProbe->offArgList );
        unsigned            iArg;
        bool                fHaveLargeArgs;


        MY_VALIDATE_STR(pProbe->offName);
        MY_CHECK_RET(pProbe->offArgList < pVtgHdr->cbArgLists, VERR_SUPDRV_VTG_BAD_PROBE);
        MY_CHECK_RET((pProbe->offArgList & 3) == 0, VERR_SUPDRV_VTG_BAD_PROBE);
        MY_CHECK_RET(pProbe->idxEnabled == i, VERR_SUPDRV_VTG_BAD_PROBE); /* The lists are parallel. */
        MY_CHECK_RET(pProbe->idxProvider < cProviders, VERR_SUPDRV_VTG_BAD_PROBE);
        MY_CHECK_RET(i - pProvider->iFirstProbe < pProvider->cProbes, VERR_SUPDRV_VTG_BAD_PROBE);
        if (pProbe->offObjHdr != (intptr_t)pVtgHdr - (intptr_t)pProbe)
        {
            SUPR0Printf("supdrvVtgValidate: VERR_SUPDRV_VTG_BAD_PROBE - iProbe=%u offObjHdr=%d expected %zd\n",
                        i, pProbe->offObjHdr, (intptr_t)pVtgHdr - (intptr_t)pProbe);
            return VERR_SUPDRV_VTG_BAD_PROBE;
        }

        /* The referenced argument list. */
        if (pArgList->cArgs > 16)
        {
            SUPR0Printf("supdrvVtgValidate: VERR_SUPDRV_VTG_BAD_ARGLIST - iProbe=%u cArgs=%u\n", i, pArgList->cArgs);
            return VERR_SUPDRV_VTG_BAD_ARGLIST;
        }
        if (pArgList->fHaveLargeArgs >= 2)
        {
            SUPR0Printf("supdrvVtgValidate: VERR_SUPDRV_VTG_BAD_ARGLIST - iProbe=%u fHaveLargeArgs=%d\n", i, pArgList->fHaveLargeArgs);
            return VERR_SUPDRV_VTG_BAD_ARGLIST;
        }
        if (   pArgList->abReserved[0]
            || pArgList->abReserved[1])
        {
            SUPR0Printf("supdrvVtgValidate: VERR_SUPDRV_VTG_BAD_ARGLIST - reserved MBZ iProbe=%u\n", i);
            return VERR_SUPDRV_VTG_BAD_ARGLIST;
        }
        fHaveLargeArgs = false;
        iArg = pArgList->cArgs;
        while (iArg-- > 0)
        {
            uint32_t const fType = pArgList->aArgs[iArg].fType;
            if (fType & ~VTG_TYPE_VALID_MASK)
            {
                SUPR0Printf("supdrvVtgValidate: VERR_SUPDRV_TRACER_BAD_ARG_FLAGS - fType=%#x iArg=%u iProbe=%u (#0)\n", fType, iArg, i);
                return VERR_SUPDRV_TRACER_BAD_ARG_FLAGS;
            }

            switch (pArgList->aArgs[iArg].fType & VTG_TYPE_SIZE_MASK)
            {
                case 0:
                    if (pArgList->aArgs[iArg].fType & VTG_TYPE_FIXED_SIZED)
                    {
                        SUPR0Printf("supdrvVtgValidate: VERR_SUPDRV_TRACER_BAD_ARG_FLAGS - fType=%#x iArg=%u iProbe=%u (#1)\n", fType, iArg, i);
                        return VERR_SUPDRV_TRACER_BAD_ARG_FLAGS;
                    }
                    break;
                case 1: case 2: case 4: case 8:
                    break;
                default:
                    SUPR0Printf("supdrvVtgValidate: VERR_SUPDRV_TRACER_BAD_ARG_FLAGS - fType=%#x iArg=%u iProbe=%u (#2)\n", fType, iArg, i);
                    return VERR_SUPDRV_TRACER_BAD_ARG_FLAGS;
            }
            if (VTG_TYPE_IS_LARGE(pArgList->aArgs[iArg].fType))
                fHaveLargeArgs = true;

            MY_VALIDATE_STR(pArgList->aArgs[iArg].offType);
        }
        if ((uint8_t)fHaveLargeArgs != pArgList->fHaveLargeArgs)
        {
            SUPR0Printf("supdrvVtgValidate: VERR_SUPDRV_TRACER_BAD_ARG_FLAGS - iProbe=%u fHaveLargeArgs=%d expected %d\n",
                        i, pArgList->fHaveLargeArgs, fHaveLargeArgs);
            return VERR_SUPDRV_VTG_BAD_PROBE;
        }
    }

    /*
     * Check that pacProbeEnabled is all zeros.
     */
    {
        uint32_t const *pcProbeEnabled = (uint32_t const *)((uintptr_t)pVtgHdr + pVtgHdr->offProbeEnabled);
        i = pVtgHdr->cbProbeEnabled / sizeof(uint32_t);
        while (i-- > 0)
            MY_CHECK_RET(pcProbeEnabled[0] == 0, VERR_SUPDRV_VTG_BAD_PROBE_ENABLED);
    }

    /*
     * Probe locations.
     */
    {
        PVTGPROBELOC paProbeLocs = (PVTGPROBELOC)((intptr_t)pVtgHdr + pVtgHdr->offProbeLocs);
        i = pVtgHdr->cbProbeLocs / sizeof(VTGPROBELOC);
        while (i-- > 0)
        {
            MY_CHECK_RET(paProbeLocs[i].uLine < _1G, VERR_SUPDRV_VTG_BAD_PROBE_LOC);
            MY_CHECK_RET(paProbeLocs[i].fEnabled == false, VERR_SUPDRV_VTG_BAD_PROBE_LOC);
            MY_CHECK_RET(paProbeLocs[i].idProbe == 0, VERR_SUPDRV_VTG_BAD_PROBE_LOC);
            offTmp = (uintptr_t)paProbeLocs[i].pProbe - (uintptr_t)pVtgHdr->offProbes - (uintptr_t)pVtgHdr;
#ifdef RT_OS_DARWIN /* See header validation code. */
            if (   offTmp >= pVtgHdr->cbProbes
                && pVtgHdr->u64VtgObjSectionStart != uVtgHdrAddr
                && pVtgHdr->u64VtgObjSectionStart   < _4M
                && (uintptr_t)paProbeLocs[i].pProbe < _4M
                && !fUmod )
            {
                uint64_t offDelta = uVtgHdrAddr - pVtgHdr->u64VtgObjSectionStart;

                paProbeLocs[i].pProbe = (PVTGDESCPROBE)((uintptr_t)paProbeLocs[i].pProbe + offDelta);
                if ((uintptr_t)paProbeLocs[i].pszFunction < _4M)
                    paProbeLocs[i].pszFunction = (const char *)((uintptr_t)paProbeLocs[i].pszFunction + offDelta);

                offTmp += offDelta;
            }
#endif
            MY_CHECK_RET(offTmp < pVtgHdr->cbProbes, VERR_SUPDRV_VTG_BAD_PROBE_LOC);
            MY_CHECK_RET(offTmp / sizeof(VTGDESCPROBE) * sizeof(VTGDESCPROBE) == offTmp, VERR_SUPDRV_VTG_BAD_PROBE_LOC);
            MY_WITHIN_IMAGE(paProbeLocs[i].pszFunction, VERR_SUPDRV_VTG_BAD_PROBE_LOC);
        }
    }

    return VINF_SUCCESS;
}

#undef MY_VALIDATE_STR
#undef MY_VALIDATE_ATTR
#undef MY_WITHIN_IMAGE


/**
 * Gets a string from the string table.
 *
 * @returns Pointer to the string.
 * @param   pVtgHdr             The VTG object header.
 * @param   offStrTab           The string table offset.
 */
static const char *supdrvVtgGetString(PVTGOBJHDR pVtgHdr,  uint32_t offStrTab)
{
    Assert(offStrTab < pVtgHdr->cbStrTab);
    return (char *)pVtgHdr + pVtgHdr->offStrTab + offStrTab;
}


/**
 * Frees the provider structure and associated resources.
 *
 * @param   pProv               The provider to free.
 */
static void supdrvTracerFreeProvider(PSUPDRVTPPROVIDER pProv)
{
    LOG_TRACER(("Freeing tracepoint provider '%s' / %p\n", pProv->szName, pProv->Core.TracerData.DTrace.idProvider));
    pProv->fRegistered          = false;
    pProv->fZombie              = true;
    pProv->Core.pDesc           = NULL;
    pProv->Core.pHdr            = NULL;
    pProv->Core.paProbeLocsRO   = NULL;
    pProv->Core.pvProbeLocsEn   = NULL;
    pProv->Core.pacProbeEnabled = NULL;
    pProv->Core.paR0ProbeLocs   = NULL;
    pProv->Core.paR0Probes      = NULL;
    RT_ZERO(pProv->Core.TracerData);
    RTMemFree(pProv);
}


/**
 * Unlinks and deregisters a provider.
 *
 * If the provider is still busy, it will be put in the zombie list.
 *
 * @param   pDevExt             The device extension.
 * @param   pProv               The provider.
 *
 * @remarks The caller owns mtxTracer.
 */
static void supdrvTracerDeregisterVtgObj(PSUPDRVDEVEXT pDevExt, PSUPDRVTPPROVIDER pProv)
{
    int rc;

    RTListNodeRemove(&pProv->ListEntry);
    if (pProv->pSession)
    {
        RTListNodeRemove(&pProv->SessionListEntry);
        RTListInit(&pProv->SessionListEntry);
        pProv->pSession->cTpProviders--;
    }

    if (!pProv->fRegistered || !pDevExt->pTracerOps)
        rc = VINF_SUCCESS;
    else
        rc = pDevExt->pTracerOps->pfnProviderDeregister(pDevExt->pTracerOps, &pProv->Core);
    if (RT_SUCCESS(rc))
    {
        supdrvTracerFreeProvider(pProv);
        return;
    }

    pProv->fZombie              = true;
    pProv->pImage               = NULL;
    pProv->pSession             = NULL;
    pProv->pUmod                = NULL;
    pProv->Core.pDesc           = NULL;
    pProv->Core.pHdr            = NULL;
    pProv->Core.paProbeLocsRO   = NULL;
    pProv->Core.pvProbeLocsEn   = NULL;
    pProv->Core.pacProbeEnabled = NULL;
    pProv->Core.paR0ProbeLocs   = NULL;

    RTListAppend(&pDevExt->TracerProviderZombieList, &pProv->ListEntry);
    LOG_TRACER(("Invalidated provider '%s' / %p and put it on the zombie list (rc=%Rrc)\n",
                pProv->szName, pProv->Core.TracerData.DTrace.idProvider, rc));
}


/**
 * Processes the zombie list.
 *
 * @param   pDevExt             The device extension.
 */
static void supdrvTracerProcessZombies(PSUPDRVDEVEXT pDevExt)
{
    PSUPDRVTPPROVIDER pProv, pProvNext;

    RTSemFastMutexRequest(pDevExt->mtxTracer);
    RTListForEachSafe(&pDevExt->TracerProviderZombieList, pProv, pProvNext, SUPDRVTPPROVIDER, ListEntry)
    {
        int rc = pDevExt->pTracerOps->pfnProviderDeregisterZombie(pDevExt->pTracerOps, &pProv->Core);
        if (RT_SUCCESS(rc))
        {
            RTListNodeRemove(&pProv->ListEntry);
            supdrvTracerFreeProvider(pProv);
        }
    }
    RTSemFastMutexRelease(pDevExt->mtxTracer);
}


/**
 * Unregisters all providers, including zombies, waiting for busy providers to
 * go idle and unregister smoothly.
 *
 * This may block.
 *
 * @param   pDevExt             The device extension.
 */
static void supdrvTracerRemoveAllProviders(PSUPDRVDEVEXT pDevExt)
{
    uint32_t            i;
    PSUPDRVTPPROVIDER   pProv;
    PSUPDRVTPPROVIDER   pProvNext;

    /*
     * Unregister all probes (there should only be one).
     */
    RTSemFastMutexRequest(pDevExt->mtxTracer);
    RTListForEachSafe(&pDevExt->TracerProviderList, pProv, pProvNext, SUPDRVTPPROVIDER, ListEntry)
    {
        supdrvTracerDeregisterVtgObj(pDevExt, pProv);
    }
    RTSemFastMutexRelease(pDevExt->mtxTracer);

    /*
     * Try unregister zombies now, sleep on busy ones and tracer opens.
     */
    for (i = 0; ; i++)
    {
        bool fEmpty;

        RTSemFastMutexRequest(pDevExt->mtxTracer);

        /* Zombies */
        RTListForEachSafe(&pDevExt->TracerProviderZombieList, pProv, pProvNext, SUPDRVTPPROVIDER, ListEntry)
        {
            int rc;
            LOG_TRACER(("supdrvTracerRemoveAllProviders: Attemting to unregister '%s' / %p...\n",
                        pProv->szName, pProv->Core.TracerData.DTrace.idProvider));

            if (pDevExt->pTracerOps)
                rc = pDevExt->pTracerOps->pfnProviderDeregisterZombie(pDevExt->pTracerOps, &pProv->Core);
            else
                rc = VINF_SUCCESS;
            if (!rc)
            {
                RTListNodeRemove(&pProv->ListEntry);
                supdrvTracerFreeProvider(pProv);
            }
            else if (!(i & 0xf))
                SUPR0Printf("supdrvTracerRemoveAllProviders: Waiting on busy provider '%s' / %p (rc=%d)\n",
                            pProv->szName, pProv->Core.TracerData.DTrace.idProvider, rc);
            else
                LOG_TRACER(("supdrvTracerRemoveAllProviders: Failed to unregister provider '%s' / %p - rc=%d\n",
                            pProv->szName, pProv->Core.TracerData.DTrace.idProvider, rc));
        }

        fEmpty = RTListIsEmpty(&pDevExt->TracerProviderZombieList);

        /* Tracer opens. */
        if (   pDevExt->cTracerOpens
            && pDevExt->pTracerOps)
        {
            fEmpty = false;
            if (!(i & 0xf))
                SUPR0Printf("supdrvTracerRemoveAllProviders: Waiting on %u opens\n", pDevExt->cTracerOpens);
            else
                LOG_TRACER(("supdrvTracerRemoveAllProviders: Waiting on %u opens\n", pDevExt->cTracerOpens));
        }

        RTSemFastMutexRelease(pDevExt->mtxTracer);

        if (fEmpty)
            break;

        /* Delay...*/
        RTThreadSleep(1000);
    }
}


/**
 * Registers the VTG tracepoint providers of a driver.
 *
 * @returns VBox status code.
 * @param   pDevExt             The device instance data.
 * @param   pVtgHdr             The VTG object header.
 * @param   pImage              The image if applicable.
 * @param   pSession            The session if applicable.
 * @param   pUmod               The associated user tracepoint module if
 *                              applicable.
 * @param   pszModName          The module name.
 */
static int supdrvTracerRegisterVtgObj(PSUPDRVDEVEXT pDevExt, PVTGOBJHDR pVtgHdr, PSUPDRVLDRIMAGE pImage,
                                      PSUPDRVSESSION pSession, PSUPDRVTRACERUMOD pUmod, const char *pszModName)
{
    int                 rc;
    uintptr_t           i;
    PSUPDRVTPPROVIDER   pProv;
    size_t              cchModName;

    /*
     * Validate input.
     */
    AssertPtrReturn(pDevExt, VERR_INVALID_POINTER);
    AssertPtrReturn(pVtgHdr, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pImage, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pszModName, VERR_INVALID_POINTER);
    cchModName = strlen(pszModName);

    if (pImage)
        rc = supdrvVtgValidate(pVtgHdr, (uintptr_t)pVtgHdr,
                               (const uint8_t *)pImage->pvImage, pImage->cbImageBits,
                               false /*fUmod*/);
    else
        rc = supdrvVtgValidate(pVtgHdr, (uintptr_t)pVtgHdr, NULL, 0, pUmod != NULL);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Check that there aren't any obvious duplicates.
     * (Yes, this isn't race free, but it's good enough for now.)
     */
    rc = RTSemFastMutexRequest(pDevExt->mtxTracer);
    if (RT_FAILURE(rc))
        return rc;
    if (pImage || !pSession || pSession->R0Process == NIL_RTPROCESS)
    {
        RTListForEach(&pDevExt->TracerProviderList, pProv, SUPDRVTPPROVIDER, ListEntry)
        {
            if (pProv->Core.pHdr == pVtgHdr)
            {
                rc = VERR_SUPDRV_VTG_ALREADY_REGISTERED;
                break;
            }

            if (   pProv->pSession == pSession
                && pProv->pImage   == pImage)
            {
                rc = VERR_SUPDRV_VTG_ONLY_ONCE_PER_SESSION;
                break;
            }
        }
    }
    else
    {
        RTListForEach(&pSession->TpProviders, pProv, SUPDRVTPPROVIDER, SessionListEntry)
        {
            if (pProv->Core.pHdr == pVtgHdr)
            {
                rc = VERR_SUPDRV_VTG_ALREADY_REGISTERED;
                break;
            }
        }
    }
    RTSemFastMutexRelease(pDevExt->mtxTracer);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Register the providers.
     */
    i = pVtgHdr->cbProviders / sizeof(VTGDESCPROVIDER);
    while (i-- > 0)
    {
        PVTGDESCPROVIDER pDesc   = (PVTGDESCPROVIDER)((uintptr_t)pVtgHdr + pVtgHdr->offProviders) + i;
        const char      *pszName = supdrvVtgGetString(pVtgHdr, pDesc->offName);
        size_t const     cchName = strlen(pszName) + (pUmod ? 16 : 0);

        pProv = (PSUPDRVTPPROVIDER)RTMemAllocZ(RT_UOFFSETOF_DYN(SUPDRVTPPROVIDER, szName[cchName + 1 + cchModName + 1]));
        if (pProv)
        {
            pProv->Core.pszName         = &pProv->szName[0];
            pProv->Core.pszModName      = &pProv->szName[cchName + 1];
            pProv->Core.pDesc           = pDesc;
            pProv->Core.pHdr            = pVtgHdr;
            pProv->Core.paProbeLocsRO   = (PCVTGPROBELOC )((uintptr_t)pVtgHdr + pVtgHdr->offProbeLocs);
            if (!pUmod)
            {
                pProv->Core.pvProbeLocsEn   = (void     *)((uintptr_t)pVtgHdr + pVtgHdr->offProbeLocs);
                pProv->Core.pacProbeEnabled = (uint32_t *)((uintptr_t)pVtgHdr + pVtgHdr->offProbeEnabled);
                pProv->Core.paR0ProbeLocs   = NULL;
                pProv->Core.paR0Probes      = NULL;
                pProv->Core.cbProbeLocsEn   = sizeof(VTGPROBELOC);
                pProv->Core.cBits           = ARCH_BITS;
                pProv->Core.fUmod           = false;
            }
            else
            {
                pProv->Core.pvProbeLocsEn   = pUmod->pvProbeLocs;
                pProv->Core.pacProbeEnabled = pUmod->pacProbeEnabled;
                pProv->Core.paR0ProbeLocs   = &pUmod->aProbeLocs[0];
                pProv->Core.paR0Probes      = (PSUPDRVPROBEINFO)&pUmod->aProbeLocs[pUmod->cProbeLocs];
                pProv->Core.cbProbeLocsEn   = pUmod->cbProbeLoc;
                pProv->Core.cBits           = pUmod->cBits;
                pProv->Core.fUmod           = true;
            }
            pProv->pImage               = pImage;
            pProv->pSession             = pSession;
            pProv->pUmod                = pUmod;
            pProv->fZombie              = false;
            pProv->fRegistered          = true;

            if (!pUmod)
                RT_BCOPY_UNFORTIFIED(pProv->szName, pszName, cchName + 1);
            else
                RTStrPrintf(pProv->szName, cchName + 1, "%s%u", pszName, (uint32_t)pSession->Process);
            RT_BCOPY_UNFORTIFIED((void *)pProv->Core.pszModName, pszModName, cchModName + 1);

            /*
             * Do the actual registration and list manipulations while holding
             * down the lock.
             */
            rc = RTSemFastMutexRequest(pDevExt->mtxTracer);
            if (RT_SUCCESS(rc))
            {
                if (   pDevExt->pTracerOps
                    && !pDevExt->fTracerUnloading)
                    rc = pDevExt->pTracerOps->pfnProviderRegister(pDevExt->pTracerOps, &pProv->Core);
                else
                {
                    pProv->fRegistered = false;
                    rc = VINF_SUCCESS;
                }
                if (RT_SUCCESS(rc))
                {
                    RTListAppend(&pDevExt->TracerProviderList, &pProv->ListEntry);
                    if (pSession)
                    {
                        RTListAppend(&pSession->TpProviders, &pProv->SessionListEntry);
                        pSession->cTpProviders++;
                    }
                    else
                        RTListInit(&pProv->SessionListEntry);
                    RTSemFastMutexRelease(pDevExt->mtxTracer);
                    LOG_TRACER(("Registered tracepoint provider '%s' in '%s' -> %p\n",
                                pProv->szName, pszModName, pProv->Core.TracerData.DTrace.idProvider));
                }
                else
                {
                    RTSemFastMutexRelease(pDevExt->mtxTracer);
                    LOG_TRACER(("Failed to register tracepoint provider '%s' in '%s' -> %Rrc\n",
                                pProv->szName, pszModName, rc));
                }
            }
        }
        else
            rc = VERR_NO_MEMORY;

        /*
         * In case of failure, we have to undo any providers we already
         * managed to register.
         */
        if (RT_FAILURE(rc))
        {
            PSUPDRVTPPROVIDER   pProvNext;

            if (pProv)
                supdrvTracerFreeProvider(pProv);

            RTSemFastMutexRequest(pDevExt->mtxTracer);
            if (pImage)
            {
                RTListForEachReverseSafe(&pDevExt->TracerProviderList, pProv, pProvNext, SUPDRVTPPROVIDER, ListEntry)
                {
                    if (pProv->Core.pHdr == pVtgHdr)
                        supdrvTracerDeregisterVtgObj(pDevExt, pProv);
                }
            }
            else
            {
                RTListForEachSafe(&pSession->TpProviders, pProv, pProvNext, SUPDRVTPPROVIDER, SessionListEntry)
                {
                    if (pProv->Core.pHdr == pVtgHdr)
                        supdrvTracerDeregisterVtgObj(pDevExt, pProv);
                }
            }
            RTSemFastMutexRelease(pDevExt->mtxTracer);
            return rc;
        }
    }

    return VINF_SUCCESS;
}


/**
 * Registers the VTG tracepoint providers of a driver.
 *
 * @returns VBox status code.
 * @param   pSession            The support driver session handle.
 * @param   pVtgHdr             The VTG header.
 * @param   pszName             The driver name.
 */
SUPR0DECL(int) SUPR0TracerRegisterDrv(PSUPDRVSESSION pSession, PVTGOBJHDR pVtgHdr, const char *pszName)
{
    int rc;

    AssertReturn(SUP_IS_SESSION_VALID(pSession), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszName, VERR_INVALID_POINTER);
    AssertPtrReturn(pVtgHdr, VERR_INVALID_POINTER);
    AssertReturn(pSession->R0Process == NIL_RTR0PROCESS, VERR_INVALID_PARAMETER);
    LOG_TRACER(("SUPR0TracerRegisterDrv: pSession=%p pVtgHdr=%p pszName=%s\n", pSession, pVtgHdr, pszName));

    rc = supdrvTracerRegisterVtgObj(pSession->pDevExt, pVtgHdr, NULL /*pImage*/, pSession, NULL /*pUmod*/, pszName);

    /*
     * Try unregister zombies while we have a chance.
     */
    supdrvTracerProcessZombies(pSession->pDevExt);

    return rc;
}
SUPR0_EXPORT_SYMBOL(SUPR0TracerRegisterDrv);


/**
 * Deregister the VTG tracepoint providers of a driver.
 *
 * @param   pSession            The support driver session handle.
 */
SUPR0DECL(void) SUPR0TracerDeregisterDrv(PSUPDRVSESSION pSession)
{
    PSUPDRVTPPROVIDER pProv, pProvNext;
    PSUPDRVDEVEXT     pDevExt;
    AssertReturnVoid(SUP_IS_SESSION_VALID(pSession));
    AssertReturnVoid(pSession->R0Process == NIL_RTR0PROCESS);
    LOG_TRACER(("SUPR0TracerDeregisterDrv: pSession=%p\n", pSession));

    pDevExt = pSession->pDevExt;

    /*
     * Search for providers belonging to this driver session.
     */
    RTSemFastMutexRequest(pDevExt->mtxTracer);
    RTListForEachSafe(&pSession->TpProviders, pProv, pProvNext, SUPDRVTPPROVIDER, SessionListEntry)
    {
        supdrvTracerDeregisterVtgObj(pDevExt, pProv);
    }
    RTSemFastMutexRelease(pDevExt->mtxTracer);

    /*
     * Try unregister zombies while we have a chance.
     */
    supdrvTracerProcessZombies(pDevExt);
}
SUPR0_EXPORT_SYMBOL(SUPR0TracerDeregisterDrv);


/**
 * Registers the VTG tracepoint providers of a module loaded by
 * the support driver.
 *
 * This should be called from the ModuleInit code.
 *
 * @returns VBox status code.
 * @param   hMod                The module handle.
 * @param   pVtgHdr             The VTG header.
 */
SUPR0DECL(int) SUPR0TracerRegisterModule(void *hMod, PVTGOBJHDR pVtgHdr)
{
    PSUPDRVLDRIMAGE pImage = (PSUPDRVLDRIMAGE)hMod;
    PSUPDRVDEVEXT   pDevExt;
    int             rc;

    LOG_TRACER(("SUPR0TracerRegisterModule: %p\n", pVtgHdr));

    /*
     * Validate input and context.
     */
    AssertPtrReturn(pImage,  VERR_INVALID_HANDLE);
    AssertPtrReturn(pVtgHdr, VERR_INVALID_POINTER);

    AssertPtrReturn(pImage, VERR_INVALID_POINTER);
    pDevExt = pImage->pDevExt;
    AssertPtrReturn(pDevExt, VERR_INVALID_POINTER);
    AssertReturn(pDevExt->pLdrInitImage  == pImage, VERR_WRONG_ORDER);
    AssertReturn(pDevExt->hLdrInitThread == RTThreadNativeSelf(), VERR_WRONG_ORDER);
    AssertReturn((uintptr_t)pVtgHdr - (uintptr_t)pImage->pvImage < pImage->cbImageBits, VERR_INVALID_PARAMETER);

    /*
     * Do the job.
     */
    rc = supdrvTracerRegisterVtgObj(pDevExt, pVtgHdr, pImage, NULL /*pSession*/, NULL /*pUmod*/, pImage->szName);
    LOG_TRACER(("SUPR0TracerRegisterModule: rc=%d\n", rc));

    /*
     * Try unregister zombies while we have a chance.
     */
    supdrvTracerProcessZombies(pDevExt);

    return rc;
}
SUPR0_EXPORT_SYMBOL(SUPR0TracerRegisterModule);


/**
 * Registers the tracer implementation.
 *
 * This should be called from the ModuleInit code or from a ring-0 session.
 *
 * @returns VBox status code.
 * @param   hMod                The module handle.
 * @param   pSession            Ring-0 session handle.
 * @param   pReg                Pointer to the tracer registration structure.
 * @param   ppHlp               Where to return the tracer helper method table.
 */
SUPR0DECL(int) SUPR0TracerRegisterImpl(void *hMod, PSUPDRVSESSION pSession, PCSUPDRVTRACERREG pReg, PCSUPDRVTRACERHLP *ppHlp)
{
    PSUPDRVLDRIMAGE     pImage = (PSUPDRVLDRIMAGE)hMod;
    PSUPDRVDEVEXT       pDevExt;
    PSUPDRVTPPROVIDER   pProv;
    int                 rc;
    int                 rc2;

    /*
     * Validate input and context.
     */
    AssertPtrReturn(ppHlp, VERR_INVALID_POINTER);
    *ppHlp = NULL;
    AssertPtrReturn(pReg,  VERR_INVALID_HANDLE);

    if (pImage)
    {
        AssertPtrReturn(pImage, VERR_INVALID_POINTER);
        AssertReturn(pSession == NULL, VERR_INVALID_PARAMETER);
        pDevExt = pImage->pDevExt;
        AssertPtrReturn(pDevExt, VERR_INVALID_POINTER);
        AssertReturn(pDevExt->pLdrInitImage  == pImage, VERR_WRONG_ORDER);
        AssertReturn(pDevExt->hLdrInitThread == RTThreadNativeSelf(), VERR_WRONG_ORDER);
    }
    else
    {
        AssertReturn(SUP_IS_SESSION_VALID(pSession), VERR_INVALID_PARAMETER);
        AssertReturn(pSession->R0Process == NIL_RTR0PROCESS, VERR_INVALID_PARAMETER);
        pDevExt = pSession->pDevExt;
        AssertPtrReturn(pDevExt, VERR_INVALID_POINTER);
    }

    AssertReturn(pReg->u32Magic   == SUPDRVTRACERREG_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(pReg->u32Version == SUPDRVTRACERREG_VERSION, VERR_VERSION_MISMATCH);
    AssertReturn(pReg->uEndMagic  == SUPDRVTRACERREG_MAGIC, VERR_VERSION_MISMATCH);
    AssertPtrReturn(pReg->pfnProbeFireKernel, VERR_INVALID_POINTER);
    AssertPtrReturn(pReg->pfnProbeFireUser, VERR_INVALID_POINTER);
    AssertPtrReturn(pReg->pfnTracerOpen, VERR_INVALID_POINTER);
    AssertPtrReturn(pReg->pfnTracerIoCtl, VERR_INVALID_POINTER);
    AssertPtrReturn(pReg->pfnTracerClose, VERR_INVALID_POINTER);
    AssertPtrReturn(pReg->pfnProviderRegister, VERR_INVALID_POINTER);
    AssertPtrReturn(pReg->pfnProviderDeregister, VERR_INVALID_POINTER);
    AssertPtrReturn(pReg->pfnProviderDeregisterZombie, VERR_INVALID_POINTER);

    /*
     * Do the job.
     */
    rc = RTSemFastMutexRequest(pDevExt->mtxTracer);
    if (RT_SUCCESS(rc))
    {
        if (!pDevExt->pTracerOps)
        {
            LOG_TRACER(("SUPR0TracerRegisterImpl: pReg=%p\n", pReg));
            pDevExt->pTracerOps     = pReg;
            pDevExt->pTracerSession = pSession;
            pDevExt->pTracerImage   = pImage;

            g_pfnSupdrvProbeFireKernel = (PFNRT)pDevExt->pTracerOps->pfnProbeFireKernel;

            *ppHlp = &pDevExt->TracerHlp;
            rc = VINF_SUCCESS;

            /*
             * Iterate the already loaded modules and register their providers.
             */
            RTListForEach(&pDevExt->TracerProviderList, pProv, SUPDRVTPPROVIDER, ListEntry)
            {
                Assert(!pProv->fRegistered);
                pProv->fRegistered = true;
                rc2 = pDevExt->pTracerOps->pfnProviderRegister(pDevExt->pTracerOps, &pProv->Core);
                if (RT_FAILURE(rc2))
                {
                    pProv->fRegistered = false;
                    SUPR0Printf("SUPR0TracerRegisterImpl: Failed to register provider %s::%s - rc=%d\n",
                                pProv->Core.pszModName, pProv->szName, rc2);
                }
            }
        }
        else
            rc = VERR_SUPDRV_TRACER_ALREADY_REGISTERED;
        RTSemFastMutexRelease(pDevExt->mtxTracer);
    }

    return rc;

}
SUPR0_EXPORT_SYMBOL(SUPR0TracerRegisterImpl);


/**
 * Common tracer implementation deregistration code.
 *
 * The caller sets fTracerUnloading prior to calling this function.
 *
 * @param   pDevExt             The device extension structure.
 */
static void supdrvTracerCommonDeregisterImpl(PSUPDRVDEVEXT pDevExt)
{
    uint32_t            i;
    PSUPDRVTPPROVIDER   pProv;
    PSUPDRVTPPROVIDER   pProvNext;

    RTSemFastMutexRequest(pDevExt->mtxTracer);

    /*
     * Reinstall the stub probe-fire function.
     */
    g_pfnSupdrvProbeFireKernel = supdrvTracerProbeFireStub;

    /*
     * Disassociate the tracer implementation from all providers.
     * We will have to wait on busy providers.
     */
    for (i = 0; ; i++)
    {
        uint32_t cZombies = 0;

        /* Live providers. */
        RTListForEachSafe(&pDevExt->TracerProviderList, pProv, pProvNext, SUPDRVTPPROVIDER, ListEntry)
        {
            int rc;
            LOG_TRACER(("supdrvTracerCommonDeregisterImpl: Attemting to unregister '%s' / %p...\n",
                        pProv->szName, pProv->Core.TracerData.DTrace.idProvider));

            if (!pProv->fRegistered)
                continue;
            if (!pProv->fZombie)
            {
                rc = pDevExt->pTracerOps->pfnProviderDeregister(pDevExt->pTracerOps, &pProv->Core);
                if (RT_FAILURE(rc))
                    pProv->fZombie = true;
            }
            else
                rc = pDevExt->pTracerOps->pfnProviderDeregisterZombie(pDevExt->pTracerOps, &pProv->Core);
            if (RT_SUCCESS(rc))
                pProv->fZombie = pProv->fRegistered = false;
            else
            {
                cZombies++;
                if (!(i & 0xf))
                    SUPR0Printf("supdrvTracerCommonDeregisterImpl: Waiting on busy provider '%s' / %p (rc=%d)\n",
                                pProv->szName, pProv->Core.TracerData.DTrace.idProvider, rc);
                else
                    LOG_TRACER(("supdrvTracerCommonDeregisterImpl: Failed to unregister provider '%s' / %p - rc=%d\n",
                                pProv->szName, pProv->Core.TracerData.DTrace.idProvider, rc));
            }
        }

        /* Zombies providers. */
        RTListForEachSafe(&pDevExt->TracerProviderZombieList, pProv, pProvNext, SUPDRVTPPROVIDER, ListEntry)
        {
            int rc;
            LOG_TRACER(("supdrvTracerCommonDeregisterImpl: Attemting to unregister '%s' / %p (zombie)...\n",
                        pProv->szName, pProv->Core.TracerData.DTrace.idProvider));

            rc = pDevExt->pTracerOps->pfnProviderDeregisterZombie(pDevExt->pTracerOps, &pProv->Core);
            if (RT_SUCCESS(rc))
            {
                RTListNodeRemove(&pProv->ListEntry);
                supdrvTracerFreeProvider(pProv);
            }
            else
            {
                cZombies++;
                if (!(i & 0xf))
                    SUPR0Printf("supdrvTracerCommonDeregisterImpl: Waiting on busy provider '%s' / %p (rc=%d)\n",
                                pProv->szName, pProv->Core.TracerData.DTrace.idProvider, rc);
                else
                    LOG_TRACER(("supdrvTracerCommonDeregisterImpl: Failed to unregister provider '%s' / %p - rc=%d\n",
                                pProv->szName, pProv->Core.TracerData.DTrace.idProvider, rc));
            }
        }

        /* Tracer opens. */
        if (pDevExt->cTracerOpens)
        {
            cZombies++;
            if (!(i & 0xf))
                SUPR0Printf("supdrvTracerCommonDeregisterImpl: Waiting on %u opens\n", pDevExt->cTracerOpens);
            else
                LOG_TRACER(("supdrvTracerCommonDeregisterImpl: Waiting on %u opens\n", pDevExt->cTracerOpens));
        }

        /* Tracer calls. */
        if (pDevExt->cTracerCallers)
        {
            cZombies++;
            if (!(i & 0xf))
                SUPR0Printf("supdrvTracerCommonDeregisterImpl: Waiting on %u callers\n", pDevExt->cTracerCallers);
            else
                LOG_TRACER(("supdrvTracerCommonDeregisterImpl: Waiting on %u callers\n", pDevExt->cTracerCallers));
        }

        /* Done? */
        if (cZombies == 0)
            break;

        /* Delay...*/
        RTSemFastMutexRelease(pDevExt->mtxTracer);
        RTThreadSleep(1000);
        RTSemFastMutexRequest(pDevExt->mtxTracer);
    }

    /*
     * Deregister the tracer implementation.
     */
    pDevExt->pTracerImage     = NULL;
    pDevExt->pTracerSession   = NULL;
    pDevExt->pTracerOps       = NULL;
    pDevExt->fTracerUnloading = false;

    RTSemFastMutexRelease(pDevExt->mtxTracer);
}


/**
 * Deregister a tracer implementation.
 *
 * This should be called from the ModuleTerm code or from a ring-0 session.
 *
 * @returns VBox status code.
 * @param   hMod                The module handle.
 * @param   pSession            Ring-0 session handle.
 */
SUPR0DECL(int) SUPR0TracerDeregisterImpl(void *hMod, PSUPDRVSESSION pSession)
{
    PSUPDRVLDRIMAGE pImage = (PSUPDRVLDRIMAGE)hMod;
    PSUPDRVDEVEXT   pDevExt;
    int             rc;

    /*
     * Validate input and context.
     */
    if (pImage)
    {
        AssertPtrReturn(pImage, VERR_INVALID_POINTER);
        AssertReturn(pSession == NULL, VERR_INVALID_PARAMETER);
        pDevExt = pImage->pDevExt;
    }
    else
    {
        AssertReturn(SUP_IS_SESSION_VALID(pSession), VERR_INVALID_PARAMETER);
        AssertReturn(pSession->R0Process == NIL_RTR0PROCESS, VERR_INVALID_PARAMETER);
        pDevExt = pSession->pDevExt;
    }
    AssertPtrReturn(pDevExt, VERR_INVALID_POINTER);

    /*
     * Do the job.
     */
    rc = RTSemFastMutexRequest(pDevExt->mtxTracer);
    if (RT_SUCCESS(rc))
    {
        if (  pImage
            ? pDevExt->pTracerImage   == pImage
            : pDevExt->pTracerSession == pSession)
        {
            LOG_TRACER(("SUPR0TracerDeregisterImpl: Unloading ...\n"));
            pDevExt->fTracerUnloading = true;
            RTSemFastMutexRelease(pDevExt->mtxTracer);
            supdrvTracerCommonDeregisterImpl(pDevExt);
            LOG_TRACER(("SUPR0TracerDeregisterImpl: ... done.\n"));
        }
        else
        {
            rc = VERR_SUPDRV_TRACER_NOT_REGISTERED;
            RTSemFastMutexRelease(pDevExt->mtxTracer);
        }
    }

    return rc;
}
SUPR0_EXPORT_SYMBOL(SUPR0TracerDeregisterImpl);


/*
 * The probe function is a bit more fun since we need tail jump optimizating.
 *
 * Since we cannot ship yasm sources for linux and freebsd, owing to the cursed
 * rebuilding of the kernel module from scratch at install time, we have to
 * deploy some ugly gcc inline assembly here.
 */
#if defined(__GNUC__) && (defined(RT_OS_FREEBSD) || defined(RT_OS_LINUX))
__asm__("\
        .section .text                                                  \n\
                                                                        \n\
        .p2align 4                                                      \n\
        .global SUPR0TracerFireProbe                                    \n\
        .type   SUPR0TracerFireProbe, @function                         \n\
SUPR0TracerFireProbe:                                                   \n\
");
# if   defined(RT_ARCH_AMD64)
__asm__("\
            movq    g_pfnSupdrvProbeFireKernel(%rip), %rax              \n\
            "
#  if defined(RT_OS_LINUX)
#   if RTLNX_VER_MIN(4,15,10)
            ANNOTATE_RETPOLINE_SAFE
#   endif
#  endif
            " \n\
            jmp     *%rax \n\
");
# elif defined(RT_ARCH_X86)
__asm__("\
            movl    g_pfnSupdrvProbeFireKernel, %eax                    \n\
            "
#  if defined(RT_OS_LINUX)
#   if RTLNX_VER_MIN(4,15,10)
            ANNOTATE_RETPOLINE_SAFE
#   endif
#  endif
            " \n\
            jmp     *%eax \n\
");
# else
#  error "Which arch is this?"
# endif
__asm__("\
        .size SUPR0TracerFireProbe, . - SUPR0TracerFireProbe            \n\
                                                                        \n\
        .type supdrvTracerProbeFireStub,@function                       \n\
        .global supdrvTracerProbeFireStub                               \n\
supdrvTracerProbeFireStub:                                              \n\
        "
# if defined(RT_OS_LINUX)
#  if RTLNX_VER_MIN(5,17,0)
        ASM_RET "\n\
        "
#  else /* < 5.17.0 */
        "ret \n\
        "
#  endif /* < 5.17.0 */
# else /* !RT_OS_LINUX */
        "ret \n\
        "
# endif /* !RT_OS_LINUX */
        ".size supdrvTracerProbeFireStub, . - supdrvTracerProbeFireStub  \n\
                                                                        \n\
        .previous                                                       \n\
");
# if 0 /* Slickedit on windows highlighting fix */
 )
# endif
#endif
SUPR0_EXPORT_SYMBOL(SUPR0TracerFireProbe);


/**
 * Module unloading hook, called after execution in the module have ceased.
 *
 * @param   pDevExt             The device extension structure.
 * @param   pImage              The image being unloaded.
 */
void VBOXCALL supdrvTracerModuleUnloading(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage)
{
    PSUPDRVTPPROVIDER pProv, pProvNext;
    AssertPtrReturnVoid(pImage);        /* paranoia */

    RTSemFastMutexRequest(pDevExt->mtxTracer);

    /*
     * If it is the tracer image, we have to unload all the providers.
     */
    if (pDevExt->pTracerImage == pImage)
    {
        LOG_TRACER(("supdrvTracerModuleUnloading: Unloading tracer ...\n"));
        pDevExt->fTracerUnloading = true;
        RTSemFastMutexRelease(pDevExt->mtxTracer);
        supdrvTracerCommonDeregisterImpl(pDevExt);
        LOG_TRACER(("supdrvTracerModuleUnloading: ... done.\n"));
    }
    else
    {
        /*
         * Unregister all providers belonging to this image.
         */
        RTListForEachSafe(&pDevExt->TracerProviderList, pProv, pProvNext, SUPDRVTPPROVIDER, ListEntry)
        {
            if (pProv->pImage == pImage)
                supdrvTracerDeregisterVtgObj(pDevExt, pProv);
        }

        RTSemFastMutexRelease(pDevExt->mtxTracer);

        /*
         * Try unregister zombies while we have a chance.
         */
        supdrvTracerProcessZombies(pDevExt);
    }
}


/**
 * Called when a session is being cleaned up.
 *
 * @param   pDevExt             The device extension structure.
 * @param   pSession            The session that is being torn down.
 */
void VBOXCALL supdrvTracerCleanupSession(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession)
{
    /*
     * Deregister all providers.
     */
    SUPDRVTPPROVIDER   *pProvNext;
    SUPDRVTPPROVIDER   *pProv;
    RTSemFastMutexRequest(pDevExt->mtxTracer);
    RTListForEachSafe(&pSession->TpProviders, pProv, pProvNext, SUPDRVTPPROVIDER, SessionListEntry)
    {
        supdrvTracerDeregisterVtgObj(pDevExt, pProv);
    }
    RTSemFastMutexRelease(pDevExt->mtxTracer);

    /*
     * Clean up instance data the trace may have associated with the session.
     */
    if (pSession->uTracerData)
        supdrvIOCtl_TracerClose(pDevExt, pSession);

    /*
     * Deregister any tracer implementation.
     */
    if (pSession->R0Process == NIL_RTR0PROCESS)
        (void)SUPR0TracerDeregisterImpl(NULL, pSession);

    if (pSession->R0Process != NIL_RTR0PROCESS)
    {
        /*
         * Free any lingering user modules.  We don't bother holding the lock
         * here as there shouldn't be anyone messing with the session at this
         * point.
         */
        PSUPDRVTRACERUMOD pUmodNext;
        PSUPDRVTRACERUMOD pUmod;
        RTListForEachSafe(&pSession->TpUmods, pUmod, pUmodNext, SUPDRVTRACERUMOD, ListEntry)
        {
            RTR0MemObjFree(pUmod->hMemObjMap, false /*fFreeMappings*/);
            RTR0MemObjFree(pUmod->hMemObjLock, false /*fFreeMappings*/);
            supdrvVtgReleaseObjectCopy(pDevExt, pUmod->pVtgCopy);
            RTMemFree(pUmod);
        }
    }
}


static void supdrvVtgReleaseObjectCopy(PSUPDRVDEVEXT pDevExt, PSUPDRVVTGCOPY pThis)
{
    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    if (!cRefs)
    {
        RTSemFastMutexRequest(pDevExt->mtxTracer);
        pThis->u32Magic = ~SUDPRVVTGCOPY_MAGIC;
        RTListNodeRemove(&pThis->ListEntry);
        RTSemFastMutexRelease(pDevExt->mtxTracer);

        RTMemFree(pThis);
    }
}


/**
 * Finds a matching VTG object copy, caller owns the lock already.
 *
 * @returns Copy with reference. NULL if not found.
 * @param   pHashList           The hash list to search.
 * @param   pHdr                The VTG header (valid).
 * @param   cbStrTab            The string table size.
 * @param   fFlags              The user module flags.
 */
static PSUPDRVVTGCOPY supdrvVtgFindObjectCopyLocked(PRTLISTANCHOR pHashList, PCVTGOBJHDR pHdr, uint32_t cbStrTab, uint32_t fFlags)
{
    PSUPDRVVTGCOPY  pCur;

    fFlags &= SUP_TRACER_UMOD_FLAGS_TYPE_MASK;
    RTListForEach(pHashList, pCur, SUPDRVVTGCOPY, ListEntry)
    {
#define HDR_EQUALS(member) pCur->Hdr.member == pHdr->member
        if (   HDR_EQUALS(Uuid.au32[0])
            && HDR_EQUALS(Uuid.au32[1])
            && HDR_EQUALS(Uuid.au32[2])
            && HDR_EQUALS(Uuid.au32[3])
            && HDR_EQUALS(cbObj)
            && HDR_EQUALS(cBits)
            && pCur->cbStrTab == cbStrTab
            && pCur->fFlags == fFlags
           )
        {
            if (RT_LIKELY(   HDR_EQUALS(offStrTab)
                          && HDR_EQUALS(cbStrTab)
                          && HDR_EQUALS(offArgLists)
                          && HDR_EQUALS(cbArgLists)
                          && HDR_EQUALS(offProbes)
                          && HDR_EQUALS(cbProbes)
                          && HDR_EQUALS(offProviders)
                          && HDR_EQUALS(cbProviders)
                          && HDR_EQUALS(offProbeEnabled)
                          && HDR_EQUALS(cbProbeEnabled)
                          && HDR_EQUALS(offProbeLocs)
                          && HDR_EQUALS(cbProbeLocs)
                         )
                )
            {
                Assert(pCur->cRefs > 0);
                Assert(pCur->cRefs < _1M);
                pCur->cRefs++;
                return pCur;
            }
        }
#undef HDR_EQUALS
    }

    return NULL;
}


/**
 * Finds a matching VTG object copy.
 *
 * @returns Copy with reference. NULL if not found.
 * @param   pDevExt             The device extension.
 * @param   pHdr                The VTG header (valid).
 * @param   cbStrTab            The string table size.
 * @param   fFlags              The user module flags.
 */
static PSUPDRVVTGCOPY supdrvVtgFindObjectCopy(PSUPDRVDEVEXT pDevExt, PCVTGOBJHDR pHdr, uint32_t cbStrTab, uint32_t fFlags)
{
    PRTLISTANCHOR   pHashList = &pDevExt->aTrackerUmodHash[pHdr->Uuid.au8[3] % RT_ELEMENTS(pDevExt->aTrackerUmodHash)];
    PSUPDRVVTGCOPY pRet;

    int rc = RTSemFastMutexRequest(pDevExt->mtxTracer);
    AssertRCReturn(rc, NULL);

    pRet = supdrvVtgFindObjectCopyLocked(pHashList, pHdr, cbStrTab, fFlags);

    RTSemFastMutexRelease(pDevExt->mtxTracer);
    return pRet;
}


/**
 * Makes a shared copy of the VTG object.
 *
 * @returns VBox status code.
 * @param   pDevExt             The device extension.
 * @param   pVtgHdr             The VTG header (valid).
 * @param   R3PtrVtgHdr         The ring-3 VTG header address.
 * @param   uVtgHdrAddr         The address of the VTG header in the context
 *                              where it is actually used.
 * @param   R3PtrStrTab         The ring-3 address of the probe location string
 *                              table.  The probe location array have offsets
 *                              into this instead of funciton name pointers.
 * @param   cbStrTab            The size of the probe location string table.
 * @param   fFlags              The user module flags.
 * @param   pUmod               The structure we've allocated to track the
 *                              module.  This have a valid kernel mapping of the
 *                              probe location array.  Upon successful return,
 *                              the pVtgCopy member will hold the address of our
 *                              copy (with a referenced of course).
 */
static int supdrvVtgCreateObjectCopy(PSUPDRVDEVEXT pDevExt, PCVTGOBJHDR pVtgHdr, RTR3PTR R3PtrVtgHdr, RTUINTPTR uVtgHdrAddr,
                                     RTR3PTR R3PtrStrTab, uint32_t cbStrTab, uint32_t fFlags, PSUPDRVTRACERUMOD pUmod)
{
    /*
     * Calculate the space required, allocate and copy in the data.
     */
    int             rc;
    uint32_t const  cProbeLocs   = pVtgHdr->cbProbeLocs / (pVtgHdr->cBits == 32 ? sizeof(VTGPROBELOC32) : sizeof(VTGPROBELOC64));
    uint32_t const  cbProbeLocs  = cProbeLocs * sizeof(VTGPROBELOC);
    uint32_t const  offProbeLocs = RT_ALIGN(pVtgHdr->cbObj, 8);
    size_t const    cb           = offProbeLocs + cbProbeLocs + cbStrTab + 1;
    PSUPDRVVTGCOPY  pThis = (PSUPDRVVTGCOPY)RTMemAlloc(RT_UOFFSETOF(SUPDRVVTGCOPY, Hdr) + cb);
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->u32Magic = SUDPRVVTGCOPY_MAGIC;
    pThis->cRefs    = 1;
    pThis->cbStrTab = cbStrTab;
    pThis->fFlags   = fFlags & SUP_TRACER_UMOD_FLAGS_TYPE_MASK;
    RTListInit(&pThis->ListEntry);

    rc = RTR0MemUserCopyFrom(&pThis->Hdr, R3PtrVtgHdr, pVtgHdr->cbObj);
    if (RT_SUCCESS(rc))
    {
        char  *pchStrTab = (char *)&pThis->Hdr + offProbeLocs + cbProbeLocs;
        rc = RTR0MemUserCopyFrom(pchStrTab, R3PtrStrTab, cbStrTab);
        if (RT_SUCCESS(rc))
        {
            PVTGPROBELOC    paDst = (PVTGPROBELOC)((char *)&pThis->Hdr + offProbeLocs);
            uint32_t        i;

            /*
             * Some paranoia: Overwrite the header with the copy we've already
             * validated and zero terminate the string table.
             */
            pThis->Hdr = *pVtgHdr;
            pchStrTab[cbStrTab] = '\0';

            /*
             * Set the probe location array related header members since we're
             * making our own copy in a different location.
             */
            pThis->Hdr.uProbeLocs.u64     = (uintptr_t)paDst;
            pThis->Hdr.uProbeLocsEnd.u64  = (uintptr_t)paDst + cbProbeLocs;
            pThis->Hdr.offProbeLocs       = offProbeLocs;
            pThis->Hdr.cbProbeLocs        = cbProbeLocs;
            pThis->Hdr.cBits              = ARCH_BITS;

            /*
             * Copy, convert and fix up the probe location table.
             */
            if (pVtgHdr->cBits == 32)
            {
                uintptr_t const offDelta = (uintptr_t)&pThis->Hdr - uVtgHdrAddr;
                PCVTGPROBELOC32 paSrc    = (PCVTGPROBELOC32)pUmod->pvProbeLocs;

                for (i = 0; i < cProbeLocs; i++)
                {
                    paDst[i].uLine    = paSrc[i].uLine;
                    paDst[i].fEnabled = paSrc[i].fEnabled;
                    paDst[i].idProbe  = paSrc[i].idProbe;
                    if (paSrc[i].pszFunction > cbStrTab)
                    {
                        rc = VERR_SUPDRV_TRACER_UMOD_STRTAB_OFF_BAD;
                        break;
                    }
                    paDst[i].pszFunction = pchStrTab + paSrc[i].pszFunction;
                    paDst[i].pProbe      = (PVTGDESCPROBE)(uintptr_t)(paSrc[i].pProbe + offDelta);
                }
            }
            else
            {
                uint64_t const  offDelta = (uintptr_t)&pThis->Hdr - uVtgHdrAddr;
                PCVTGPROBELOC64 paSrc    = (PCVTGPROBELOC64)pUmod->pvProbeLocs;

                for (i = 0; i < cProbeLocs; i++)
                {
                    paDst[i].uLine    = paSrc[i].uLine;
                    paDst[i].fEnabled = paSrc[i].fEnabled;
                    paDst[i].idProbe  = paSrc[i].idProbe;
                    if (paSrc[i].pszFunction > cbStrTab)
                    {
                        rc = VERR_SUPDRV_TRACER_UMOD_STRTAB_OFF_BAD;
                        break;
                    }
                    paDst[i].pszFunction = pchStrTab + (uintptr_t)paSrc[i].pszFunction;
                    paDst[i].pProbe      = (PVTGDESCPROBE)(uintptr_t)(paSrc[i].pProbe + offDelta);
                }
            }

            /*
             * Validate it
             *
             * Note! fUmod is false as this is a kernel copy with all native
             *       structures.
             */
            if (RT_SUCCESS(rc))
                rc = supdrvVtgValidate(&pThis->Hdr, (uintptr_t)&pThis->Hdr, (uint8_t *)&pThis->Hdr, cb, false /*fUmod*/);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Add it to the hash list, making sure nobody raced us.
                 */
                PRTLISTANCHOR pHashList = &pDevExt->aTrackerUmodHash[  pVtgHdr->Uuid.au8[3]
                                                                     % RT_ELEMENTS(pDevExt->aTrackerUmodHash)];

                rc = RTSemFastMutexRequest(pDevExt->mtxTracer);
                if (RT_SUCCESS(rc))
                {
                    pUmod->pVtgCopy = supdrvVtgFindObjectCopyLocked(pHashList, pVtgHdr, cbStrTab, fFlags);
                    if (!pUmod->pVtgCopy)
                    {
                        pUmod->pVtgCopy = pThis;
                        RTListAppend(pHashList, &pThis->ListEntry);
                        RTSemFastMutexRelease(pDevExt->mtxTracer);
                        return rc;
                    }

                    /*
                     * Someone raced us, free our copy and return the existing
                     * one instead.
                     */
                    RTSemFastMutexRelease(pDevExt->mtxTracer);
                }
            }
        }
    }
    RTMemFree(pThis);
    return rc;
}


/**
 * Undoes what supdrvTracerUmodSetProbeIds did.
 *
 * @param   pDevExt             The device extension.
 * @param   pSession            The current session.
 * @param   pUmod               The user tracepoint module.
 */
static void supdrvTracerUmodClearProbeIds(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, PSUPDRVTRACERUMOD pUmod)
{
    uint32_t i;

    AssertReturnVoid(pUmod->iLookupTable < RT_ELEMENTS(pSession->apTpLookupTable));
    AssertReturnVoid(pSession->apTpLookupTable[pUmod->iLookupTable] == pUmod);

    /*
     * Clear the probe IDs and disable the probes.
     */
    i = pUmod->cProbeLocs;
    if (pUmod->cBits == 32)
    {
        PVTGPROBELOC32 paProbeLocs = (PVTGPROBELOC32)pUmod->pvProbeLocs;
        while (i-- > 0)
            paProbeLocs[i].idProbe = 0;
    }
    else
    {
        PVTGPROBELOC64 paProbeLocs = (PVTGPROBELOC64)pUmod->pvProbeLocs;
        while (i-- > 0)
            paProbeLocs[i].idProbe = 0;
    }

    /*
     * Free the lookup table entry.  We'll have to wait for the table to go
     * idle to make sure there are no current users of pUmod.
     */
    RTSemFastMutexRequest(pDevExt->mtxTracer);
    if (pSession->apTpLookupTable[pUmod->iLookupTable] == pUmod)
    {
        if (pSession->cTpProbesFiring > 0)
        {
            i = 0;
            while (pSession->cTpProbesFiring > 0)
            {
                RTSemFastMutexRelease(pDevExt->mtxTracer);
                i++;
                if (!(i & 0xff))
                    SUPR0Printf("supdrvTracerUmodClearProbeIds: waiting for lookup table to go idle (i=%u)\n", i);
                RTThreadSleep(10);
                RTSemFastMutexRequest(pDevExt->mtxTracer);
            }
        }
        ASMAtomicWriteNullPtr(&pSession->apTpLookupTable[pUmod->iLookupTable]);
    }
    RTSemFastMutexRelease(pDevExt->mtxTracer);
}


/**
 * Allocates a lookup table entry for the Umod and sets the
 * VTGPROBELOC::idProbe fields in user mode.
 *
 * @returns VINF_SUCCESS or VERR_SUPDRV_TRACER_TOO_MANY_PROVIDERS.
 * @param   pDevExt             The device extension.
 * @param   pSession            The current session.
 * @param   pUmod               The user tracepoint module.
 */
static int supdrvTracerUmodSetProbeIds(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, PSUPDRVTRACERUMOD pUmod)
{
    uint32_t iBase;
    uint32_t i;

    /*
     * Allocate a lookup table entry.
     */
    RTSemFastMutexRequest(pDevExt->mtxTracer);
    for (i = 0; i < RT_ELEMENTS(pSession->apTpLookupTable); i++)
    {
        if (!pSession->apTpLookupTable[i])
        {
            pSession->apTpLookupTable[i] = pUmod;
            pUmod->iLookupTable = i;
            break;
        }
    }
    RTSemFastMutexRelease(pDevExt->mtxTracer);
    if (i >= RT_ELEMENTS(pSession->apTpLookupTable))
        return VERR_SUPDRV_TRACER_TOO_MANY_PROVIDERS;

    /*
     * Set probe IDs of the usermode probe location to indicate our lookup
     * table entry as well as the probe location array entry.
     */
    iBase = (uint32_t)pUmod->iLookupTable << 24;
    i = pUmod->cProbeLocs;
    if (pUmod->cBits == 32)
    {
        PVTGPROBELOC32 paProbeLocs = (PVTGPROBELOC32)pUmod->pvProbeLocs;
        while (i-- > 0)
            paProbeLocs[i].idProbe = iBase | i;
    }
    else
    {
        PVTGPROBELOC64 paProbeLocs = (PVTGPROBELOC64)pUmod->pvProbeLocs;
        while (i-- > 0)
            paProbeLocs[i].idProbe = iBase | i;
    }

    return VINF_SUCCESS;
}


int  VBOXCALL   supdrvIOCtl_TracerUmodRegister(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession,
                                               RTR3PTR R3PtrVtgHdr, RTUINTPTR uVtgHdrAddr,
                                               RTR3PTR R3PtrStrTab, uint32_t cbStrTab,
                                               const char *pszModName, uint32_t fFlags)
{
    VTGOBJHDR           Hdr;
    PSUPDRVTRACERUMOD   pUmod;
    RTR3PTR             R3PtrLock;
    size_t              cbLock;
    uint32_t            cProbeLocs;
    int                 rc;

    /*
     * Validate input.
     */
    if (pSession->R0Process == NIL_RTR0PROCESS)
        return VERR_INVALID_CONTEXT;
    if (   fFlags != SUP_TRACER_UMOD_FLAGS_EXE
        && fFlags != SUP_TRACER_UMOD_FLAGS_SHARED)
        return VERR_INVALID_PARAMETER;

    if (pSession->cTpProviders >= RT_ELEMENTS(pSession->apTpLookupTable))
        return VERR_SUPDRV_TRACER_TOO_MANY_PROVIDERS;

    if (   cbStrTab < 2
        || cbStrTab > _1M)
        return VERR_SUPDRV_TRACER_UMOD_STRTAB_TOO_BIG;

    /*
     * Read the VTG header into a temporary buffer and perform some simple
     * validations to make sure we aren't wasting our time here.
     */
    rc = RTR0MemUserCopyFrom(&Hdr, R3PtrVtgHdr, sizeof(Hdr));
    if (RT_FAILURE(rc))
        return rc;
    rc = supdrvVtgValidateHdr(&Hdr, uVtgHdrAddr, NULL, 0, true /*fUmod*/);
    if (RT_FAILURE(rc))
        return rc;
    if (Hdr.cbProviders / sizeof(VTGDESCPROVIDER) > 2)
        return VERR_SUPDRV_TRACER_TOO_MANY_PROVIDERS;

    /*
     * Check how much needs to be locked down and how many probe locations
     * there are.
     */
    if (   Hdr.offProbeLocs <= 0
        || Hdr.offProbeEnabled > (uint32_t)Hdr.offProbeLocs
        || (uint32_t)Hdr.offProbeLocs - Hdr.offProbeEnabled - Hdr.cbProbeEnabled > 128)
        return VERR_SUPDRV_TRACER_UMOD_NOT_ADJACENT;
    R3PtrLock  = R3PtrVtgHdr + Hdr.offProbeEnabled;
    cbLock     = Hdr.offProbeLocs + Hdr.cbProbeLocs - Hdr.offProbeEnabled + (R3PtrLock & PAGE_OFFSET_MASK);
    R3PtrLock &= ~(RTR3PTR)PAGE_OFFSET_MASK;
    if (cbLock > _64K)
        return VERR_SUPDRV_TRACER_UMOD_TOO_MANY_PROBES;

    cProbeLocs = Hdr.cbProbeLocs / (Hdr.cBits == 32 ? sizeof(VTGPROBELOC32) : sizeof(VTGPROBELOC64));

    /*
     * Allocate the tracker data we keep in the session.
     */
    pUmod = (PSUPDRVTRACERUMOD)RTMemAllocZ(  RT_UOFFSETOF_DYN(SUPDRVTRACERUMOD, aProbeLocs[cProbeLocs])
                                           + (Hdr.cbProbeEnabled / sizeof(uint32_t) * sizeof(SUPDRVPROBEINFO)) );
    if (!pUmod)
        return VERR_NO_MEMORY;
    pUmod->u32Magic         = SUPDRVTRACERUMOD_MAGIC;
    RTListInit(&pUmod->ListEntry);
    pUmod->R3PtrVtgHdr      = R3PtrVtgHdr;
    pUmod->pVtgCopy         = NULL;
    pUmod->hMemObjLock      = NIL_RTR0MEMOBJ;
    pUmod->hMemObjMap       = NIL_RTR0MEMOBJ;
    pUmod->R3PtrProbeLocs   = (RTR3INTPTR)R3PtrVtgHdr + Hdr.offProbeLocs;
    pUmod->iLookupTable     = UINT8_MAX;
    pUmod->cBits            = Hdr.cBits;
    pUmod->cbProbeLoc       = Hdr.cBits == 32 ? sizeof(VTGPROBELOC32) : sizeof(VTGPROBELOC64);
    pUmod->cProbeLocs       = cProbeLocs;

    /*
     * Lock down and map the user-mode structures.
     */
    rc = RTR0MemObjLockUser(&pUmod->hMemObjLock, R3PtrLock, cbLock, RTMEM_PROT_READ | RTMEM_PROT_WRITE, NIL_RTR0PROCESS);
    if (RT_SUCCESS(rc))
    {
        rc = RTR0MemObjMapKernel(&pUmod->hMemObjMap, pUmod->hMemObjLock, (void *)-1, 0, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
        if (RT_SUCCESS(rc))
        {
            pUmod->pacProbeEnabled  = (uint32_t *)(  (uintptr_t)RTR0MemObjAddress(pUmod->hMemObjMap)
                                                   + ((uintptr_t)(R3PtrVtgHdr + Hdr.offProbeEnabled) & PAGE_OFFSET_MASK));
            pUmod->pvProbeLocs      = (uint8_t *)pUmod->pacProbeEnabled + Hdr.offProbeLocs - Hdr.offProbeEnabled;

            /*
             * Does some other process use the same module already?  If so,
             * share the VTG data with it.  Otherwise, make a ring-0 copy it.
             */
            pUmod->pVtgCopy = supdrvVtgFindObjectCopy(pDevExt, &Hdr, cbStrTab, fFlags);
            if (!pUmod->pVtgCopy)
                rc = supdrvVtgCreateObjectCopy(pDevExt, &Hdr, R3PtrVtgHdr, uVtgHdrAddr, R3PtrStrTab, cbStrTab, fFlags, pUmod);
            if (RT_SUCCESS(rc))
            {
                AssertPtr(pUmod->pVtgCopy);

                /*
                 * Grabe a place in apTpLookupTable and set the probe IDs
                 * accordingly.
                 */
                rc = supdrvTracerUmodSetProbeIds(pDevExt, pSession, pUmod);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Register the providers.
                     */
                    rc = supdrvTracerRegisterVtgObj(pDevExt, &pUmod->pVtgCopy->Hdr,
                                                    NULL /*pImage*/, pSession, pUmod, pszModName);
                    if (RT_SUCCESS(rc))
                    {
                        RTSemFastMutexRequest(pDevExt->mtxTracer);
                        RTListAppend(&pSession->TpUmods, &pUmod->ListEntry);
                        RTSemFastMutexRelease(pDevExt->mtxTracer);

                        return VINF_SUCCESS;
                    }

                    /* bail out. */
                    supdrvTracerUmodClearProbeIds(pDevExt, pSession, pUmod);
                }
                supdrvVtgReleaseObjectCopy(pDevExt, pUmod->pVtgCopy);
            }
            RTR0MemObjFree(pUmod->hMemObjMap, false /*fFreeMappings*/);
        }
        RTR0MemObjFree(pUmod->hMemObjLock, false /*fFreeMappings*/);
    }
    pUmod->u32Magic = ~SUPDRVTRACERUMOD_MAGIC;
    RTMemFree(pUmod);
    return rc;
}


int  VBOXCALL   supdrvIOCtl_TracerUmodDeregister(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, RTR3PTR R3PtrVtgHdr)
{
    PSUPDRVTRACERUMOD   pUmod = NULL;
    uint32_t            i;
    int                 rc;

    /*
     * Validate the request.
     */
    RTSemFastMutexRequest(pDevExt->mtxTracer);
    for (i = 0; i < RT_ELEMENTS(pSession->apTpLookupTable); i++)
    {
        pUmod = pSession->apTpLookupTable[i];
        if (   pUmod
            && pUmod->u32Magic    == SUPDRVTRACERUMOD_MAGIC
            && pUmod->R3PtrVtgHdr == R3PtrVtgHdr)
            break;
    }
    RTSemFastMutexRelease(pDevExt->mtxTracer);
    if (pUmod)
    {
        SUPDRVTPPROVIDER   *pProvNext;
        SUPDRVTPPROVIDER   *pProv;

        /*
         * Remove ourselves from the lookup table and clean up the ring-3 bits
         * we've dirtied.  We do this first to make sure no probes are firing
         * when we're destroying the providers in the next step.
         */
        supdrvTracerUmodClearProbeIds(pDevExt, pSession, pUmod);

        /*
         * Deregister providers related to the VTG object.
         */
        RTSemFastMutexRequest(pDevExt->mtxTracer);
        RTListForEachSafe(&pSession->TpProviders, pProv, pProvNext, SUPDRVTPPROVIDER, SessionListEntry)
        {
            if (pProv->pUmod == pUmod)
                supdrvTracerDeregisterVtgObj(pDevExt, pProv);
        }
        RTSemFastMutexRelease(pDevExt->mtxTracer);

        /*
         * Destroy the Umod object.
         */
        pUmod->u32Magic = ~SUPDRVTRACERUMOD_MAGIC;
        supdrvVtgReleaseObjectCopy(pDevExt, pUmod->pVtgCopy);
        RTR0MemObjFree(pUmod->hMemObjMap, false /*fFreeMappings*/);
        RTR0MemObjFree(pUmod->hMemObjLock, false /*fFreeMappings*/);
        RTMemFree(pUmod);
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_NOT_FOUND;
    return rc;
}


/**
 * Implementation of supdrvIOCtl_TracerUmodProbeFire and
 * SUPR0TracerUmodProbeFire.
 *
 * @param   pDevExt             The device extension.
 * @param   pSession            The calling session.
 * @param   pCtx                The context record.
 */
static void supdrvTracerUmodProbeFire(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, PSUPDRVTRACERUSRCTX pCtx)
{
    /*
     * We cannot trust user mode to hand us the right bits nor not calling us
     * when disabled.  So, we have to check for our selves.
     */
    PSUPDRVTRACERUMOD   pUmod;
    uint32_t const      iLookupTable = pCtx->idProbe >> 24;
    uint32_t const      iProbeLoc    = pCtx->idProbe & UINT32_C(0x00ffffff);

    if (RT_UNLIKELY(   !pDevExt->pTracerOps
                    || pDevExt->fTracerUnloading))
        return;
    if (RT_UNLIKELY(iLookupTable >= RT_ELEMENTS(pSession->apTpLookupTable)))
        return;
    if (RT_UNLIKELY(   pCtx->cBits != 32
                    && pCtx->cBits != 64))
        return;

    ASMAtomicIncU32(&pSession->cTpProviders);

    pUmod = pSession->apTpLookupTable[iLookupTable];
    if (RT_LIKELY(pUmod))
    {
        if (RT_LIKELY(   pUmod->u32Magic == SUPDRVTRACERUMOD_MAGIC
                      && iProbeLoc < pUmod->cProbeLocs
                      && pCtx->cBits == pUmod->cBits))
        {
#if 0 /* This won't work for RC modules. */
            RTR3PTR R3PtrProbeLoc = pUmod->R3PtrProbeLocs + iProbeLoc * pUmod->cbProbeLoc;
            if (RT_LIKELY(   (pCtx->cBits == 32 ? (RTR3PTR)pCtx->u.X86.uVtgProbeLoc : pCtx->u.Amd64.uVtgProbeLoc)
                          == R3PtrProbeLoc))
#endif
            {
                if (RT_LIKELY(pUmod->aProbeLocs[iProbeLoc].fEnabled))
                {
                    PSUPDRVVTGCOPY pVtgCopy;
                    ASMAtomicIncU32(&pDevExt->cTracerCallers);
                    pVtgCopy = pUmod->pVtgCopy;
                    if (RT_LIKELY(   pDevExt->pTracerOps
                                  && !pDevExt->fTracerUnloading
                                  && pVtgCopy))
                    {
                        PCVTGPROBELOC pProbeLocRO;
                        pProbeLocRO = (PCVTGPROBELOC)((uintptr_t)&pVtgCopy->Hdr + pVtgCopy->Hdr.offProbeLocs) + iProbeLoc;

                        pCtx->idProbe = pUmod->aProbeLocs[iProbeLoc].idProbe;
                        pDevExt->pTracerOps->pfnProbeFireUser(pDevExt->pTracerOps, pSession, pCtx, &pVtgCopy->Hdr, pProbeLocRO);
                    }
                    ASMAtomicDecU32(&pDevExt->cTracerCallers);
                }
            }
        }
    }

    ASMAtomicDecU32(&pSession->cTpProviders);
}


SUPR0DECL(void) SUPR0TracerUmodProbeFire(PSUPDRVSESSION pSession, PSUPDRVTRACERUSRCTX pCtx)
{
    AssertReturnVoid(SUP_IS_SESSION_VALID(pSession));
    AssertPtrReturnVoid(pCtx);

    supdrvTracerUmodProbeFire(pSession->pDevExt, pSession, pCtx);
}
SUPR0_EXPORT_SYMBOL(SUPR0TracerUmodProbeFire);


void  VBOXCALL  supdrvIOCtl_TracerUmodProbeFire(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, PSUPDRVTRACERUSRCTX pCtx)
{
    supdrvTracerUmodProbeFire(pDevExt, pSession, pCtx);
}


/**
 * Open the tracer.
 *
 * @returns VBox status code
 * @param   pDevExt             The device extension structure.
 * @param   pSession            The current session.
 * @param   uCookie             The tracer cookie.
 * @param   uArg                The tracer open argument.
 */
int  VBOXCALL   supdrvIOCtl_TracerOpen(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, uint32_t uCookie, uintptr_t uArg)
{
    RTNATIVETHREAD  hNativeSelf = RTThreadNativeSelf();
    int             rc;

    RTSemFastMutexRequest(pDevExt->mtxTracer);

    if (!pSession->uTracerData)
    {
        if (pDevExt->pTracerOps)
        {
            if (pDevExt->pTracerSession != pSession)
            {
                if (!pDevExt->fTracerUnloading)
                {
                    if (pSession->hTracerCaller == NIL_RTNATIVETHREAD)
                    {
                        pDevExt->cTracerOpens++;
                        pSession->uTracerData   = ~(uintptr_t)0;
                        pSession->hTracerCaller = hNativeSelf;
                        RTSemFastMutexRelease(pDevExt->mtxTracer);

                        rc = pDevExt->pTracerOps->pfnTracerOpen(pDevExt->pTracerOps, pSession, uCookie, uArg, &pSession->uTracerData);

                        RTSemFastMutexRequest(pDevExt->mtxTracer);
                        if (RT_FAILURE(rc))
                        {
                            pDevExt->cTracerOpens--;
                            pSession->uTracerData = 0;
                        }
                        pSession->hTracerCaller = NIL_RTNATIVETHREAD;
                    }
                    else
                        rc = VERR_SUPDRV_TRACER_SESSION_BUSY;
                }
                else
                    rc = VERR_SUPDRV_TRACER_UNLOADING;
            }
            else
                rc = VERR_SUPDRV_TRACER_CANNOT_OPEN_SELF;
        }
        else
            rc = VERR_SUPDRV_TRACER_NOT_PRESENT;
    }
    else
        rc = VERR_SUPDRV_TRACER_ALREADY_OPENED;

    RTSemFastMutexRelease(pDevExt->mtxTracer);
    return rc;
}


/**
 * Closes the tracer.
 *
 * @returns VBox status code.
 * @param   pDevExt             The device extension structure.
 * @param   pSession            The current session.
 */
int  VBOXCALL   supdrvIOCtl_TracerClose(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession)
{
    RTNATIVETHREAD  hNativeSelf = RTThreadNativeSelf();
    int             rc;

    RTSemFastMutexRequest(pDevExt->mtxTracer);

    if (pSession->uTracerData)
    {
        Assert(pDevExt->cTracerOpens > 0);

        if (pDevExt->pTracerOps)
        {
            if (pSession->hTracerCaller == NIL_RTNATIVETHREAD)
            {
                uintptr_t uTracerData   = pSession->uTracerData;
                pSession->uTracerData   = 0;
                pSession->hTracerCaller = hNativeSelf;
                RTSemFastMutexRelease(pDevExt->mtxTracer);

                pDevExt->pTracerOps->pfnTracerClose(pDevExt->pTracerOps, pSession, uTracerData);
                rc = VINF_SUCCESS;

                RTSemFastMutexRequest(pDevExt->mtxTracer);
                pSession->hTracerCaller = NIL_RTNATIVETHREAD;
                Assert(pDevExt->cTracerOpens > 0);
                pDevExt->cTracerOpens--;
            }
            else
                rc = VERR_SUPDRV_TRACER_SESSION_BUSY;
        }
        else
        {
            rc = VERR_SUPDRV_TRACER_NOT_PRESENT;
            pSession->uTracerData = 0;
            Assert(pDevExt->cTracerOpens > 0);
            pDevExt->cTracerOpens--;
        }
    }
    else
        rc = VERR_SUPDRV_TRACER_NOT_OPENED;

    RTSemFastMutexRelease(pDevExt->mtxTracer);
    return rc;
}


/**
 * Performs a tracer I/O control request.
 *
 * @returns VBox status code.
 * @param   pDevExt             The device extension structure.
 * @param   pSession            The current session.
 * @param   uCmd                The tracer command.
 * @param   uArg                The tracer argument.
 * @param   piRetVal            Where to store the tracer specific return value.
 */
int  VBOXCALL   supdrvIOCtl_TracerIOCtl(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, uintptr_t uCmd, uintptr_t uArg, int32_t *piRetVal)
{
    RTNATIVETHREAD  hNativeSelf = RTThreadNativeSelf();
    int             rc;

    *piRetVal = 0;
    RTSemFastMutexRequest(pDevExt->mtxTracer);

    if (pSession->uTracerData)
    {
        Assert(pDevExt->cTracerOpens > 0);
        if (pDevExt->pTracerOps)
        {
            if (!pDevExt->fTracerUnloading)
            {
                if (pSession->hTracerCaller == NIL_RTNATIVETHREAD)
                {
                    uintptr_t uTracerData = pSession->uTracerData;
                    pDevExt->cTracerOpens++;
                    pSession->hTracerCaller = hNativeSelf;
                    RTSemFastMutexRelease(pDevExt->mtxTracer);

                    rc = pDevExt->pTracerOps->pfnTracerIoCtl(pDevExt->pTracerOps, pSession, uTracerData, uCmd, uArg, piRetVal);

                    RTSemFastMutexRequest(pDevExt->mtxTracer);
                    pSession->hTracerCaller = NIL_RTNATIVETHREAD;
                    Assert(pDevExt->cTracerOpens > 0);
                    pDevExt->cTracerOpens--;
                }
                else
                    rc = VERR_SUPDRV_TRACER_SESSION_BUSY;
            }
            else
                rc = VERR_SUPDRV_TRACER_UNLOADING;
        }
        else
            rc = VERR_SUPDRV_TRACER_NOT_PRESENT;
    }
    else
        rc = VERR_SUPDRV_TRACER_NOT_OPENED;

    RTSemFastMutexRelease(pDevExt->mtxTracer);
    return rc;
}


/**
 * Early module initialization hook.
 *
 * @returns VBox status code.
 * @param   pDevExt             The device extension structure.
 */
int VBOXCALL supdrvTracerInit(PSUPDRVDEVEXT pDevExt)
{
    /*
     * Initialize the tracer.
     */
    int rc = RTSemFastMutexCreate(&pDevExt->mtxTracer);
    if (RT_SUCCESS(rc))
    {
        uint32_t i;

        pDevExt->TracerHlp.uVersion    = SUPDRVTRACERHLP_VERSION;
        /** @todo  */
        pDevExt->TracerHlp.uEndVersion = SUPDRVTRACERHLP_VERSION;
        RTListInit(&pDevExt->TracerProviderList);
        RTListInit(&pDevExt->TracerProviderZombieList);
        for (i = 0; i < RT_ELEMENTS(pDevExt->aTrackerUmodHash); i++)
            RTListInit(&pDevExt->aTrackerUmodHash[i]);

#ifdef VBOX_WITH_NATIVE_DTRACE
        pDevExt->pTracerOps = supdrvDTraceInit();
        if (pDevExt->pTracerOps)
            g_pfnSupdrvProbeFireKernel = (PFNRT)pDevExt->pTracerOps->pfnProbeFireKernel;
#endif

        /*
         * Register the provider for this module, if compiled in.
         */
#ifdef VBOX_WITH_DTRACE_R0DRV
        rc = supdrvTracerRegisterVtgObj(pDevExt, &g_VTGObjHeader, NULL /*pImage*/, NULL /*pSession*/, NULL /*pUmod*/, "vboxdrv");
        if (RT_SUCCESS(rc))
            return rc;
        SUPR0Printf("supdrvTracerInit: supdrvTracerRegisterVtgObj failed with rc=%d\n", rc);
        RTSemFastMutexDestroy(pDevExt->mtxTracer);
#else

        return VINF_SUCCESS;
#endif
    }
    pDevExt->mtxTracer = NIL_RTSEMFASTMUTEX;
    return rc;
}


/**
 * Late module termination hook.
 *
 * @param   pDevExt             The device extension structure.
 */
void VBOXCALL supdrvTracerTerm(PSUPDRVDEVEXT pDevExt)
{
    LOG_TRACER(("supdrvTracerTerm\n"));

    supdrvTracerRemoveAllProviders(pDevExt);
#ifdef VBOX_WITH_NATIVE_DTRACE
    supdrvDTraceFini();
#endif
    RTSemFastMutexDestroy(pDevExt->mtxTracer);
    pDevExt->mtxTracer = NIL_RTSEMFASTMUTEX;
    LOG_TRACER(("supdrvTracerTerm: Done\n"));
}

