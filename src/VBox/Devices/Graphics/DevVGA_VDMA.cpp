/* $Id: DevVGA_VDMA.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Video DMA (VDMA) support.
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
#define LOG_GROUP LOG_GROUP_DEV_VGA
#include <VBox/VMMDev.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/pgm.h>
#include <VBoxVideo.h>
#include <VBox/AssertGuest.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>
#include <iprt/mem.h>
#include <iprt/asm.h>
#include <iprt/list.h>
#include <iprt/param.h>

#include "DevVGA.h"
#include "HGSMI/SHGSMIHost.h"

#ifdef DEBUG_misha
# define VBOXVDBG_MEMCACHE_DISABLE
#endif

#ifndef VBOXVDBG_MEMCACHE_DISABLE
# include <iprt/memcache.h>
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef DEBUG_misha
# define WARN_BP() do { AssertFailed(); } while (0)
#else
# define WARN_BP() do { } while (0)
#endif
#define WARN(_msg) do { \
        LogRel(_msg); \
        WARN_BP(); \
    } while (0)

#define VBOXVDMATHREAD_STATE_TERMINATED             0
#define VBOXVDMATHREAD_STATE_CREATING               1
#define VBOXVDMATHREAD_STATE_CREATED                3
#define VBOXVDMATHREAD_STATE_TERMINATING            4


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
struct VBOXVDMATHREAD;

typedef DECLCALLBACKPTR(void, PFNVBOXVDMATHREAD_CHANGED,(struct VBOXVDMATHREAD *pThread, int rc,
                                                         void *pvThreadContext, void *pvChangeContext));

typedef struct VBOXVDMATHREAD
{
    RTTHREAD hWorkerThread;
    RTSEMEVENT hEvent;
    volatile uint32_t u32State;
    PFNVBOXVDMATHREAD_CHANGED pfnChanged;
    void *pvChanged;
} VBOXVDMATHREAD, *PVBOXVDMATHREAD;


/* state transformations:
 *
 *   submitter   |    processor
 *
 *  LISTENING   --->  PROCESSING
 *
 *  */
#define VBVAEXHOSTCONTEXT_STATE_LISTENING      0
#define VBVAEXHOSTCONTEXT_STATE_PROCESSING     1

#define VBVAEXHOSTCONTEXT_ESTATE_DISABLED     -1
#define VBVAEXHOSTCONTEXT_ESTATE_PAUSED        0
#define VBVAEXHOSTCONTEXT_ESTATE_ENABLED       1

typedef struct VBVAEXHOSTCONTEXT
{
    VBVABUFFER RT_UNTRUSTED_VOLATILE_GUEST *pVBVA;
    /** Maximum number of data bytes addressible relative to pVBVA. */
    uint32_t                                cbMaxData;
    volatile int32_t i32State;
    volatile int32_t i32EnableState;
    volatile uint32_t u32cCtls;
    /* critical section for accessing ctl lists */
    RTCRITSECT CltCritSect;
    RTLISTANCHOR GuestCtlList;
    RTLISTANCHOR HostCtlList;
#ifndef VBOXVDBG_MEMCACHE_DISABLE
    RTMEMCACHE CtlCache;
#endif
} VBVAEXHOSTCONTEXT;

typedef enum
{
    VBVAEXHOSTCTL_TYPE_UNDEFINED = 0,
    VBVAEXHOSTCTL_TYPE_HH_INTERNAL_PAUSE,
    VBVAEXHOSTCTL_TYPE_HH_INTERNAL_RESUME,
    VBVAEXHOSTCTL_TYPE_HH_SAVESTATE,
    VBVAEXHOSTCTL_TYPE_HH_LOADSTATE,
    VBVAEXHOSTCTL_TYPE_HH_LOADSTATE_DONE,
    VBVAEXHOSTCTL_TYPE_HH_BE_OPAQUE,
    VBVAEXHOSTCTL_TYPE_HH_ON_HGCM_UNLOAD,
    VBVAEXHOSTCTL_TYPE_GHH_BE_OPAQUE,
    VBVAEXHOSTCTL_TYPE_GHH_ENABLE,
    VBVAEXHOSTCTL_TYPE_GHH_ENABLE_PAUSED,
    VBVAEXHOSTCTL_TYPE_GHH_DISABLE,
    VBVAEXHOSTCTL_TYPE_GHH_RESIZE
} VBVAEXHOSTCTL_TYPE;

struct VBVAEXHOSTCTL;

typedef DECLCALLBACKTYPE(void, FNVBVAEXHOSTCTL_COMPLETE,(VBVAEXHOSTCONTEXT *pVbva, struct VBVAEXHOSTCTL *pCtl, int rc, void *pvComplete));
typedef FNVBVAEXHOSTCTL_COMPLETE *PFNVBVAEXHOSTCTL_COMPLETE;

typedef struct VBVAEXHOSTCTL
{
    RTLISTNODE Node;
    VBVAEXHOSTCTL_TYPE enmType;
    union
    {
        struct
        {
            void RT_UNTRUSTED_VOLATILE_GUEST *pvCmd;
            uint32_t cbCmd;
        } cmd;

        struct
        {
            PSSMHANDLE pSSM;
            uint32_t u32Version;
        } state;
    } u;
    PFNVBVAEXHOSTCTL_COMPLETE pfnComplete;
    void *pvComplete;
} VBVAEXHOSTCTL;

/* VBoxVBVAExHP**, i.e. processor functions, can NOT be called concurrently with each other,
 * but can be called with other VBoxVBVAExS** (submitter) functions except Init/Start/Term aparently.
 * Can only be called be the processor, i.e. the entity that acquired the processor state by direct or indirect call to the VBoxVBVAExHSCheckCommands
 * see mor edetailed comments in headers for function definitions */
typedef enum
{
    VBVAEXHOST_DATA_TYPE_NO_DATA = 0,
    VBVAEXHOST_DATA_TYPE_CMD,
    VBVAEXHOST_DATA_TYPE_HOSTCTL,
    VBVAEXHOST_DATA_TYPE_GUESTCTL
} VBVAEXHOST_DATA_TYPE;


typedef struct VBOXVDMAHOST
{
    PHGSMIINSTANCE pHgsmi; /**< Same as VGASTATE::pHgsmi. */
    PVGASTATE pThis;
} VBOXVDMAHOST, *PVBOXVDMAHOST;


/**
 * List selector for VBoxVBVAExHCtlSubmit(), vdmaVBVACtlSubmit().
 */
typedef enum
{
    VBVAEXHOSTCTL_SOURCE_GUEST = 0,
    VBVAEXHOSTCTL_SOURCE_HOST
} VBVAEXHOSTCTL_SOURCE;




/**
 * Called by vgaR3Construct() to initialize the state.
 *
 * @returns VBox status code.
 */
int vboxVDMAConstruct(PVGASTATE pThis, PVGASTATECC pThisCC, uint32_t cPipeElements)
{
    RT_NOREF(cPipeElements);
    PVBOXVDMAHOST pVdma = (PVBOXVDMAHOST)RTMemAllocZ(sizeof(*pVdma));
    Assert(pVdma);
    if (pVdma)
    {
        pVdma->pHgsmi = pThisCC->pHGSMI;
        pVdma->pThis  = pThis;

        pThisCC->pVdma = pVdma;
        return VINF_SUCCESS;
    }
    return VERR_NO_MEMORY;
}

/**
 * Called by vgaR3Reset() to do reset.
 */
void  vboxVDMAReset(struct VBOXVDMAHOST *pVdma)
{
    RT_NOREF(pVdma);
}

/**
 * Called by vgaR3Destruct() to do cleanup.
 */
void vboxVDMADestruct(struct VBOXVDMAHOST *pVdma)
{
    if (!pVdma)
        return;
    RTMemFree(pVdma);
}

/**
 * Handle VBVA_VDMA_CTL, see vbvaChannelHandler
 *
 * @param   pVdma   The VDMA channel.
 * @param   pCmd    The control command to handle.  Considered volatile.
 * @param   cbCmd   The size of the command.  At least sizeof(VBOXVDMA_CTL).
 */
void vboxVDMAControl(struct VBOXVDMAHOST *pVdma, VBOXVDMA_CTL RT_UNTRUSTED_VOLATILE_GUEST *pCmd, uint32_t cbCmd)
{
    RT_NOREF(cbCmd);
    PHGSMIINSTANCE pIns = pVdma->pHgsmi;

    VBOXVDMA_CTL_TYPE enmCtl = pCmd->enmCtl;
    RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();

    int rc;
    if (enmCtl < VBOXVDMA_CTL_TYPE_END)
    {
        RT_UNTRUSTED_VALIDATED_FENCE();

        switch (enmCtl)
        {
            case VBOXVDMA_CTL_TYPE_ENABLE:
                rc = VINF_SUCCESS;
                break;
            case VBOXVDMA_CTL_TYPE_DISABLE:
                rc = VINF_SUCCESS;
                break;
            case VBOXVDMA_CTL_TYPE_FLUSH:
                rc = VINF_SUCCESS;
                break;
            case VBOXVDMA_CTL_TYPE_WATCHDOG:
                rc = VERR_NOT_SUPPORTED;
                break;
            default:
                AssertFailedBreakStmt(rc = VERR_IPE_NOT_REACHED_DEFAULT_CASE);
        }
    }
    else
    {
        RT_UNTRUSTED_VALIDATED_FENCE();
        ASSERT_GUEST_FAILED();
        rc = VERR_NOT_SUPPORTED;
    }

    pCmd->i32Result = rc;
    rc = VBoxSHGSMICommandComplete(pIns, pCmd);
    AssertRC(rc);
}

/**
 * Handle VBVA_VDMA_CMD, see vbvaChannelHandler().
 *
 * @param   pVdma   The VDMA channel.
 * @param   pCmd    The command to handle.  Considered volatile.
 * @param   cbCmd   The size of the command.  At least sizeof(VBOXVDMACBUF_DR).
 * @thread  EMT
 */
void vboxVDMACommand(struct VBOXVDMAHOST *pVdma, VBOXVDMACBUF_DR RT_UNTRUSTED_VOLATILE_GUEST *pCmd, uint32_t cbCmd)
{
    /*
     * Process the command.
     */
    bool fAsyncCmd = false;
    RT_NOREF(cbCmd);
    int rc = VERR_NOT_IMPLEMENTED;

    /*
     * Complete the command unless it's asynchronous (e.g. chromium).
     */
    if (!fAsyncCmd)
    {
        pCmd->rc = rc;
        int rc2 = VBoxSHGSMICommandComplete(pVdma->pHgsmi, pCmd);
        AssertRC(rc2);
    }
}



/*
 *
 *
 * Saved state.
 * Saved state.
 * Saved state.
 *
 *
 */

int vboxVDMASaveStateExecPrep(struct VBOXVDMAHOST *pVdma)
{
    RT_NOREF(pVdma);
    return VINF_SUCCESS;
}

int vboxVDMASaveStateExecDone(struct VBOXVDMAHOST *pVdma)
{
    RT_NOREF(pVdma);
    return VINF_SUCCESS;
}

int vboxVDMASaveStateExecPerform(PCPDMDEVHLPR3 pHlp, struct VBOXVDMAHOST *pVdma, PSSMHANDLE pSSM)
{
    int rc;
    RT_NOREF(pVdma);

    rc = pHlp->pfnSSMPutU32(pSSM, UINT32_MAX);
    AssertRCReturn(rc, rc);
    return VINF_SUCCESS;
}

int vboxVDMASaveLoadExecPerform(PCPDMDEVHLPR3 pHlp, struct VBOXVDMAHOST *pVdma, PSSMHANDLE pSSM, uint32_t u32Version)
{
    uint32_t u32;
    int rc = pHlp->pfnSSMGetU32(pSSM, &u32);
    AssertLogRelRCReturn(rc, rc);

    if (u32 != UINT32_MAX)
    {
        RT_NOREF(pVdma, u32Version);
        WARN(("Unsupported VBVACtl info!\n"));
        return VERR_VERSION_MISMATCH;
    }

    return VINF_SUCCESS;
}

int vboxVDMASaveLoadDone(struct VBOXVDMAHOST *pVdma)
{
    RT_NOREF(pVdma);
    return VINF_SUCCESS;
}

