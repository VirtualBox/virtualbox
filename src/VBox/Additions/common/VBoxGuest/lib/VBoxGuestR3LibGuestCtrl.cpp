/* $Id: VBoxGuestR3LibGuestCtrl.cpp 109156 2025-04-09 16:04:13Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestR3Lib - Ring-3 Support Library for VirtualBox guest additions, guest control.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <iprt/string.h>
#include <iprt/mem.h>
#include <iprt/assert.h>
#include <iprt/cpp/autores.h>
#include <iprt/stdarg.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/GuestHost/GuestControl.h>
#include <VBox/HostServices/GuestControlSvc.h>

#ifndef RT_OS_WINDOWS
# include <signal.h>
# ifdef RT_OS_DARWIN
#  include <pthread.h>
#  define sigprocmask pthread_sigmask /* On xnu sigprocmask works on the process, not the calling thread as elsewhere. */
# endif
#endif

#include "VBoxGuestR3LibInternal.h"

using namespace guestControl;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Set if GUEST_MSG_PEEK_WAIT and friends are supported. */
static int      g_fVbglR3GuestCtrlHavePeekGetCancel = -1;
/** Represents the currently cached host features 0.
 *  Set to 0 if not available / not cached yet. */
static uint64_t g_fVbglR3GuestCtrlHostFeatures0 = 0;


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/
static int vbglR3GuestCtrlQueryFeatures(uint32_t idClient, uint64_t *pfHostFeatures0, uint64_t *pfHostFeatures1);


/**
 * Invalidates the internal state of the Guest Control API.
 *
 * @returns VBox status code.
 * @param   idClient        The client ID returned by VbglR3GuestCtrlConnect().
 */
static int vbglR3GuestCtrlInvalidate(uint32_t idClient)
{
    uint64_t fVbglR3GuestCtrlHostFeatures0 = 0;
    int const rc2 = vbglR3GuestCtrlQueryFeatures(idClient,
                                                 &fVbglR3GuestCtrlHostFeatures0, NULL /* pfHostFeatures1, unused */);
    if (RT_SUCCESS(rc2))
    {
        /* Check if the host's feature set has changed. This might happen on a VM restore. */
        if (   g_fVbglR3GuestCtrlHostFeatures0
            && g_fVbglR3GuestCtrlHostFeatures0 != fVbglR3GuestCtrlHostFeatures0)
            LogRelFunc(("Host feature set has changed (%#x -> %#x)\n",
                        g_fVbglR3GuestCtrlHostFeatures0, fVbglR3GuestCtrlHostFeatures0));

        g_fVbglR3GuestCtrlHostFeatures0 = fVbglR3GuestCtrlHostFeatures0;
    }
    else
        LogRelFunc(("Querying host features not supported, rc=%Rrc\n", rc2));
    /* Note: Very old hosts don't know about querying host features, so this isn't fatal for the caller. */

    return VINF_SUCCESS;
}


/**
 * Connects to the guest control service.
 *
 * @returns VBox status code
 * @param   pidClient     Where to put The client ID on success. The client ID
 *                        must be passed to all the other calls to the service.
 */
VBGLR3DECL(int) VbglR3GuestCtrlConnect(uint32_t *pidClient)
{
    AssertPtrReturn(pidClient, VERR_INVALID_POINTER);

    int rc = VbglR3HGCMConnect("VBoxGuestControlSvc", pidClient);
    if (RT_SUCCESS(rc))
        rc = vbglR3GuestCtrlInvalidate(*pidClient);

    return rc;
}


/**
 * Disconnect from the guest control service.
 *
 * @returns VBox status code.
 * @param   idClient        The client ID returned by VbglR3GuestCtrlConnect().
 */
VBGLR3DECL(int) VbglR3GuestCtrlDisconnect(uint32_t idClient)
{
    return VbglR3HGCMDisconnect(idClient);
}


/**
 * Waits until a new host message arrives.
 * This will block until a message becomes available.
 *
 * @returns VBox status code.
 * @param   idClient        The client ID returned by VbglR3GuestCtrlConnect().
 * @param   pidMsg          Where to store the message id.
 * @param   pcParameters    Where to store the number  of parameters which will
 *                          be received in a second call to the host.
 */
static int vbglR3GuestCtrlMsgWaitFor(uint32_t idClient, uint32_t *pidMsg, uint32_t *pcParameters)
{
    AssertPtrReturn(pidMsg, VERR_INVALID_POINTER);
    AssertPtrReturn(pcParameters, VERR_INVALID_POINTER);

    HGCMMsgWaitFor Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, idClient,
                       GUEST_MSG_WAIT,      /* Tell the host we want our next message. */
                       2);                  /* Just peek for the next message! */
    VbglHGCMParmUInt32Set(&Msg.msg, 0);
    VbglHGCMParmUInt32Set(&Msg.num_parms, 0);

    /*
     * We should always get a VERR_TOO_MUCH_DATA response here, see
     * guestControl::HostMessage::Peek() and its caller ClientState::SendReply().
     * We accept success too here, in case someone decide to make the protocol
     * slightly more sane.
     *
     * Note! A really sane protocol design would have a separate call for getting
     *       info about a pending message (returning VINF_SUCCESS), and a separate
     *       one for retriving the actual message parameters.  Not this weird
     *       stuff, to put it rather bluntly.
     *
     * Note! As a result of this weird design, we are not able to correctly
     *       retrieve message if we're interrupted by a signal, like SIGCHLD.
     *       Because IPRT wants to use waitpid(), we're forced to have a handler
     *       installed for SIGCHLD, so when working with child processes there
     *       will be signals in the air and we will get VERR_INTERRUPTED returns.
     *       The way HGCM handles interrupted calls is to silently (?) drop them
     *       as they complete (see VMMDev), so the server knows little about it
     *       and just goes on to the next message inline.
     *
     *       So, as a "temporary" mesasure, we block SIGCHLD here while waiting,
     *       because it will otherwise be impossible do simple stuff like 'mkdir'
     *       on a mac os x guest, and probably most other unix guests.
     */
#ifdef RT_OS_WINDOWS
    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
#else
    sigset_t SigSet;
    sigemptyset(&SigSet);
    sigaddset(&SigSet, SIGCHLD);
    sigprocmask(SIG_BLOCK, &SigSet, NULL);
    int rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
    sigprocmask(SIG_UNBLOCK, &SigSet, NULL);
#endif
    if (   rc == VERR_TOO_MUCH_DATA
        || RT_SUCCESS(rc))
    {
        int rc2 = VbglHGCMParmUInt32Get(&Msg.msg, pidMsg);
        if (RT_SUCCESS(rc2))
        {
            rc2 = VbglHGCMParmUInt32Get(&Msg.num_parms, pcParameters);
            if (RT_SUCCESS(rc2))
            {
                /* Ok, so now we know what message type and how much parameters there are. */
                return rc;
            }
        }
        rc = rc2;
    }
    *pidMsg       = UINT32_MAX - 1;
    *pcParameters = UINT32_MAX - 2;
    return rc;
}


/**
 * Determins the value of g_fVbglR3GuestCtrlHavePeekGetCancel.
 *
 * @returns true if supported, false if not.
 * @param   idClient         The client ID to use for the testing.
 */
DECL_NO_INLINE(static, bool) vbglR3GuestCtrlDetectPeekGetCancelSupport(uint32_t idClient)
{
    /*
     * Seems we get VINF_SUCCESS back from the host if we try unsupported
     * guest control messages, so we need to supply some random message
     * parameters and check that they change.
     */
    uint32_t const idDummyMsg      = UINT32_C(0x8350bdca);
    uint32_t const cDummyParmeters = UINT32_C(0x7439604f);
    uint32_t const cbDummyMask     = UINT32_C(0xc0ffe000);
    Assert(cDummyParmeters > VMMDEV_MAX_HGCM_PARMS);

    int rc;
    struct
    {
        VBGLIOCHGCMCALL         Hdr;
        HGCMFunctionParameter   idMsg;
        HGCMFunctionParameter   cParams;
        HGCMFunctionParameter   acbParams[14];
    } PeekCall;
    Assert(RT_ELEMENTS(PeekCall.acbParams) + 2 < VMMDEV_MAX_HGCM_PARMS);

    do
    {
        memset(&PeekCall, 0xf6, sizeof(PeekCall));
        VBGL_HGCM_HDR_INIT(&PeekCall.Hdr, idClient, GUEST_MSG_PEEK_NOWAIT, 16);
        VbglHGCMParmUInt32Set(&PeekCall.idMsg, idDummyMsg);
        VbglHGCMParmUInt32Set(&PeekCall.cParams, cDummyParmeters);
        for (uint32_t i = 0; i < RT_ELEMENTS(PeekCall.acbParams); i++)
            VbglHGCMParmUInt32Set(&PeekCall.acbParams[i], i | cbDummyMask);

        rc = VbglR3HGCMCall(&PeekCall.Hdr, sizeof(PeekCall));
    } while (rc == VERR_INTERRUPTED);

    LogRel2(("vbglR3GuestCtrlDetectPeekGetCancelSupport: rc=%Rrc %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x %#x\n",
             rc, PeekCall.idMsg.u.value32,     PeekCall.cParams.u.value32,
             PeekCall.acbParams[ 0].u.value32, PeekCall.acbParams[ 1].u.value32,
             PeekCall.acbParams[ 2].u.value32, PeekCall.acbParams[ 3].u.value32,
             PeekCall.acbParams[ 4].u.value32, PeekCall.acbParams[ 5].u.value32,
             PeekCall.acbParams[ 6].u.value32, PeekCall.acbParams[ 7].u.value32,
             PeekCall.acbParams[ 8].u.value32, PeekCall.acbParams[ 9].u.value32,
             PeekCall.acbParams[10].u.value32, PeekCall.acbParams[11].u.value32,
             PeekCall.acbParams[12].u.value32, PeekCall.acbParams[13].u.value32));

    /*
     * VERR_TRY_AGAIN is likely and easy.
     */
    if (   rc == VERR_TRY_AGAIN
        && PeekCall.idMsg.u.value32 == 0
        && PeekCall.cParams.u.value32 == 0
        && PeekCall.acbParams[0].u.value32 == 0
        && PeekCall.acbParams[1].u.value32 == 0
        && PeekCall.acbParams[2].u.value32 == 0
        && PeekCall.acbParams[3].u.value32 == 0)
    {
        g_fVbglR3GuestCtrlHavePeekGetCancel = 1;
        LogRel(("vbglR3GuestCtrlDetectPeekGetCancelSupport: Supported (#1)\n"));
        return true;
    }

    /*
     * VINF_SUCCESS is annoying but with 16 parameters we've got plenty to check.
     */
    if (   rc == VINF_SUCCESS
        && PeekCall.idMsg.u.value32 != idDummyMsg
        && PeekCall.idMsg.u.value32 != 0
        && PeekCall.cParams.u.value32 <= VMMDEV_MAX_HGCM_PARMS)
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(PeekCall.acbParams); i++)
            if (PeekCall.acbParams[i].u.value32 != (i | cbDummyMask))
            {
                g_fVbglR3GuestCtrlHavePeekGetCancel = 0;
                LogRel(("vbglR3GuestCtrlDetectPeekGetCancelSupport: Not supported (#1)\n"));
                return false;
            }
        g_fVbglR3GuestCtrlHavePeekGetCancel = 1;
        LogRel(("vbglR3GuestCtrlDetectPeekGetCancelSupport: Supported (#2)\n"));
        return true;
    }

    /*
     * Okay, pretty sure it's not supported then.
     */
    LogRel(("vbglR3GuestCtrlDetectPeekGetCancelSupport: Not supported (#3)\n"));
    g_fVbglR3GuestCtrlHavePeekGetCancel = 0;
    return false;
}


/**
 * Reads g_fVbglR3GuestCtrlHavePeekGetCancel and resolved -1.
 *
 * @returns true if supported, false if not.
 * @param   idClient         The client ID to use for the testing.
 */
DECLINLINE(bool) vbglR3GuestCtrlSupportsPeekGetCancel(uint32_t idClient)
{
    int fState = g_fVbglR3GuestCtrlHavePeekGetCancel;
    if (RT_LIKELY(fState != -1))
        return fState != 0;
    return vbglR3GuestCtrlDetectPeekGetCancelSupport(idClient);
}


/**
 * Figures which getter function to use to retrieve the message.
 */
DECLINLINE(uint32_t) vbglR3GuestCtrlGetMsgFunctionNo(uint32_t idClient)
{
    return vbglR3GuestCtrlSupportsPeekGetCancel(idClient) ? GUEST_MSG_GET : GUEST_MSG_WAIT;
}


/**
 * Checks if the host supports the optimizes message and session functions.
 *
 * @returns true / false.
 * @param   idClient    The client ID returned by VbglR3GuestCtrlConnect().
 *                      We may need to use this for checking.
 * @since   6.0
 */
VBGLR3DECL(bool) VbglR3GuestCtrlSupportsOptimizations(uint32_t idClient)
{
    return vbglR3GuestCtrlSupportsPeekGetCancel(idClient);
}


/**
 * Make us the guest control master client.
 *
 * @returns VBox status code.
 * @param   idClient    The client ID returned by VbglR3GuestCtrlConnect().
 */
VBGLR3DECL(int) VbglR3GuestCtrlMakeMeMaster(uint32_t idClient)
{
    int rc;
    do
    {
        VBGLIOCHGCMCALL Hdr;
        VBGL_HGCM_HDR_INIT(&Hdr, idClient, GUEST_MSG_MAKE_ME_MASTER, 0);
        rc = VbglR3HGCMCall(&Hdr, sizeof(Hdr));
    } while (rc == VERR_INTERRUPTED);
    return rc;
}


/**
 * Reports features to the host and retrieve host features set.
 *
 * @returns VBox status code.
 * @param   idClient        The client ID returned by VbglR3GuestCtrlConnect().
 * @param   fGuestFeatures  Features to report, VBOX_GUESTCTRL_GF_XXX.
 * @param   pfHostFeatures  Where to store the features VBOX_GUESTCTRL_HF_XXX.
 */
VBGLR3DECL(int) VbglR3GuestCtrlReportFeatures(uint32_t idClient, uint64_t fGuestFeatures, uint64_t *pfHostFeatures)
{
    int rc;
    do
    {
        struct
        {
            VBGLIOCHGCMCALL         Hdr;
            HGCMFunctionParameter   f64Features0;
            HGCMFunctionParameter   f64Features1;
        } Msg;
        VBGL_HGCM_HDR_INIT(&Msg.Hdr, idClient, GUEST_MSG_REPORT_FEATURES, 2);
        VbglHGCMParmUInt64Set(&Msg.f64Features0, fGuestFeatures);
        VbglHGCMParmUInt64Set(&Msg.f64Features1, VBOX_GUESTCTRL_GF_1_MUST_BE_ONE);

        rc = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Assert(Msg.f64Features0.type == VMMDevHGCMParmType_64bit);
            Assert(Msg.f64Features1.type == VMMDevHGCMParmType_64bit);
            if (Msg.f64Features1.u.value64 & VBOX_GUESTCTRL_GF_1_MUST_BE_ONE)
                rc = VERR_NOT_SUPPORTED;
            else if (pfHostFeatures)
                *pfHostFeatures = Msg.f64Features0.u.value64;
            break;
        }
    } while (rc == VERR_INTERRUPTED);
    return rc;

}


/**
 * Queries the host features, internal version.
 *
 * @returns VBox status code.
 * @param   idClient         The client ID returned by VbglR3GuestCtrlConnect().
 * @param   pfHostFeatures0  Where to store the host features, VBOX_GUESTCTRL_HF_0_XXX.
 *                           Optional and can be NULL.
 * @param   pfHostFeatures1  Where to store the host features, VBOX_GUESTCTRL_HF_1_XXX.
 *                           Currently unused. Optional and can be NULL.
 */
static int vbglR3GuestCtrlQueryFeatures(uint32_t idClient, uint64_t *pfHostFeatures0, uint64_t *pfHostFeatures1)
{
    int rc;
    do
    {
        struct
        {
            VBGLIOCHGCMCALL         Hdr;
            HGCMFunctionParameter   f64Features0;
            HGCMFunctionParameter   f64Features1;
        } Msg;
        VBGL_HGCM_HDR_INIT(&Msg.Hdr, idClient, GUEST_MSG_QUERY_FEATURES, 2);
        VbglHGCMParmUInt64Set(&Msg.f64Features0, 0);
        VbglHGCMParmUInt64Set(&Msg.f64Features1, RT_BIT_64(63));

        rc = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Assert(Msg.f64Features0.type == VMMDevHGCMParmType_64bit);
            Assert(Msg.f64Features1.type == VMMDevHGCMParmType_64bit);
            if (Msg.f64Features1.u.value64 & RT_BIT_64(63))
                rc = VERR_NOT_SUPPORTED;
            else
            {
                if (pfHostFeatures0)
                    *pfHostFeatures0 = Msg.f64Features0.u.value64;
                if (pfHostFeatures1)
                    *pfHostFeatures1 = Msg.f64Features1.u.value64;
            }
            break;
        }
    } while (rc == VERR_INTERRUPTED);
    return rc;
}


/**
 * Queries the host features.
 *
 * @returns VBox status code.
 * @param   idClient        The client ID returned by VbglR3GuestCtrlConnect().
 * @param   pfHostFeatures  Where to store the host features, VBOX_GUESTCTRL_HF_0_XXX. Optional and can be NULL.
 */
VBGLR3DECL(int) VbglR3GuestCtrlQueryFeatures(uint32_t idClient, uint64_t *pfHostFeatures)
{
    return vbglR3GuestCtrlQueryFeatures(idClient, pfHostFeatures, NULL /* pfHostFeatures1, unused */);
}


/**
 * Peeks at the next host message, waiting for one to turn up.
 *
 * @returns VBox status code.
 * @retval  VERR_INTERRUPTED if interrupted.  Does the necessary cleanup, so
 *          caller just have to repeat this call.
 * @retval  VERR_VM_RESTORED if the VM has been restored (idRestoreCheck).
 *
 * @param   idClient        The client ID returned by VbglR3GuestCtrlConnect().
 * @param   pidMsg          Where to store the message id.
 * @param   pcParameters    Where to store the number  of parameters which will
 *                          be received in a second call to the host.
 * @param   pidRestoreCheck Pointer to the VbglR3GetSessionId() variable to use
 *                          for the VM restore check.  Optional.
 *
 * @note    Restore check is only performed optimally with a 6.0 host.
 */
VBGLR3DECL(int) VbglR3GuestCtrlMsgPeekWait(uint32_t idClient, uint32_t *pidMsg, uint32_t *pcParameters, uint64_t *pidRestoreCheck)
{
    AssertPtrReturn(pidMsg, VERR_INVALID_POINTER);
    AssertPtrReturn(pcParameters, VERR_INVALID_POINTER);

    int rc;
    if (vbglR3GuestCtrlSupportsPeekGetCancel(idClient))
    {
        struct
        {
            VBGLIOCHGCMCALL Hdr;
            HGCMFunctionParameter idMsg;       /* Doubles as restore check on input. */
            HGCMFunctionParameter cParameters;
        } Msg;
        VBGL_HGCM_HDR_INIT(&Msg.Hdr, idClient, GUEST_MSG_PEEK_WAIT, 2);
        VbglHGCMParmUInt64Set(&Msg.idMsg, pidRestoreCheck ? *pidRestoreCheck : 0);
        VbglHGCMParmUInt32Set(&Msg.cParameters, 0);
        rc = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));
        LogRel2(("VbglR3GuestCtrlMsgPeekWait -> %Rrc\n", rc));
        if (RT_SUCCESS(rc))
        {
            AssertMsgReturn(   Msg.idMsg.type       == VMMDevHGCMParmType_64bit
                            && Msg.cParameters.type == VMMDevHGCMParmType_32bit,
                            ("msg.type=%d num_parms.type=%d\n", Msg.idMsg.type, Msg.cParameters.type),
                            VERR_INTERNAL_ERROR_3);

            *pidMsg       = (uint32_t)Msg.idMsg.u.value64;
            *pcParameters = Msg.cParameters.u.value32;
            return rc;
        }

        /*
         * If interrupted we must cancel the call so it doesn't prevent us from making another one.
         */
        if (rc == VERR_INTERRUPTED)
        {
            VBGL_HGCM_HDR_INIT(&Msg.Hdr, idClient, GUEST_MSG_CANCEL, 0);
            int rc2 = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg.Hdr));
            AssertRC(rc2);
        }

        /*
         * If restored, update pidRestoreCheck.
         */
        if (rc == VERR_VM_RESTORED && pidRestoreCheck)
            *pidRestoreCheck = Msg.idMsg.u.value64;

        *pidMsg       = UINT32_MAX - 1;
        *pcParameters = UINT32_MAX - 2;
        return rc;
    }

    /*
     * Fallback if host < v6.0.
     *
     * Note! The restore check isn't perfect. Would require checking afterwards
     *       and stash the result if we were restored during the call.  Too much
     *       hazzle for a downgrade scenario.
     */
    if (pidRestoreCheck)
    {
        uint64_t idRestoreCur = *pidRestoreCheck;
        rc = VbglR3GetSessionId(&idRestoreCur);
        if (RT_SUCCESS(rc) && idRestoreCur != *pidRestoreCheck)
        {
            *pidRestoreCheck = idRestoreCur;
            return VERR_VM_RESTORED;
        }
    }

    rc = vbglR3GuestCtrlMsgWaitFor(idClient, pidMsg, pcParameters);
    if (rc == VERR_TOO_MUCH_DATA)
        rc = VINF_SUCCESS;
    return rc;
}


/**
 * Asks the host guest control service to set a message filter to this
 * client so that it only will receive certain messages in the future.
 * The filter(s) are a bitmask for the context IDs, served from the host.
 *
 * @return  IPRT status code.
 * @param   idClient        The client ID returned by VbglR3GuestCtrlConnect().
 * @param   uValue          The value to filter messages for.
 * @param   uMaskAdd        Filter mask to add.
 * @param   uMaskRemove     Filter mask to remove.
 */
VBGLR3DECL(int) VbglR3GuestCtrlMsgFilterSet(uint32_t idClient, uint32_t uValue, uint32_t uMaskAdd, uint32_t uMaskRemove)
{
    HGCMMsgFilterSet Msg;

    /* Tell the host we want to set a filter. */
    VBGL_HGCM_HDR_INIT(&Msg.hdr, idClient, GUEST_MSG_FILTER_SET, 4);
    VbglHGCMParmUInt32Set(&Msg.value, uValue);
    VbglHGCMParmUInt32Set(&Msg.mask_add, uMaskAdd);
    VbglHGCMParmUInt32Set(&Msg.mask_remove, uMaskRemove);
    VbglHGCMParmUInt32Set(&Msg.flags, 0 /* Flags, unused */);

    return VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
}


/**
 * Replies to a message from the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   rc                  Guest rc to reply.
 */
VBGLR3DECL(int) VbglR3GuestCtrlMsgReply(PVBGLR3GUESTCTRLCMDCTX pCtx,
                                        int rc)
{
    return VbglR3GuestCtrlMsgReplyEx(pCtx, rc, 0 /* uType */,
                                     NULL /* pvPayload */, 0 /* cbPayload */);
}


/**
 * Replies to a message from the host, extended version.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   rc                  Guest rc to reply.
 * @param   uType               Reply type; not used yet and must be 0.
 * @param   pvPayload           Pointer to data payload to reply. Optional.
 * @param   cbPayload           Size of data payload (in bytes) to reply.
 */
VBGLR3DECL(int) VbglR3GuestCtrlMsgReplyEx(PVBGLR3GUESTCTRLCMDCTX pCtx,
                                          int rc, uint32_t uType,
                                          void *pvPayload, uint32_t cbPayload)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    /* Everything else is optional. */

    HGCMMsgReply Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_MSG_REPLY, 4);
    VbglHGCMParmUInt32Set(&Msg.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.type, uType);
    VbglHGCMParmUInt32Set(&Msg.rc, (uint32_t)rc); /* int vs. uint32_t */
    VbglHGCMParmPtrSet(&Msg.payload, pvPayload, cbPayload);

    return VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
}

/**
 * Tell the host to skip the current message replying VERR_NOT_SUPPORTED
 *
 * @return  IPRT status code.
 * @param   idClient        The client ID returned by VbglR3GuestCtrlConnect().
 * @param   rcSkip          The status code to pass back to Main when skipping.
 * @param   idMsg           The message ID to skip, pass UINT32_MAX to pass any.
 */
VBGLR3DECL(int) VbglR3GuestCtrlMsgSkip(uint32_t idClient, int rcSkip, uint32_t idMsg)
{
    if (vbglR3GuestCtrlSupportsPeekGetCancel(idClient))
    {
        struct
        {
            VBGLIOCHGCMCALL         Hdr;
            HGCMFunctionParameter   rcSkip;
            HGCMFunctionParameter   idMsg;
        } Msg;
        VBGL_HGCM_HDR_INIT(&Msg.Hdr, idClient, GUEST_MSG_SKIP, 2);
        VbglHGCMParmUInt32Set(&Msg.rcSkip, (uint32_t)rcSkip);
        VbglHGCMParmUInt32Set(&Msg.idMsg, idMsg);
        return VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));
    }

    /* This is generally better than nothing... */
    return VbglR3GuestCtrlMsgSkipOld(idClient);
}


/**
 * Tells the host service to skip the current message returned by
 * VbglR3GuestCtrlMsgWaitFor().
 *
 * @return  IPRT status code.
 * @param   idClient        The client ID returned by VbglR3GuestCtrlConnect().
 */
VBGLR3DECL(int) VbglR3GuestCtrlMsgSkipOld(uint32_t idClient)
{
    HGCMMsgSkip Msg;

    /* Tell the host we want to skip the current assigned message. */
    VBGL_HGCM_HDR_INIT(&Msg.hdr, idClient, GUEST_MSG_SKIP_OLD, 1);
    VbglHGCMParmUInt32Set(&Msg.flags, 0 /* Flags, unused */);
    return VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
}


/**
 * Asks the host to cancel (release) all pending waits which were deferred.
 *
 * @returns VBox status code.
 * @param   idClient        The client ID returned by VbglR3GuestCtrlConnect().
 */
VBGLR3DECL(int) VbglR3GuestCtrlCancelPendingWaits(uint32_t idClient)
{
    HGCMMsgCancelPendingWaits Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, idClient, GUEST_MSG_CANCEL, 0);
    return VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
}


/**
 * Prepares a session.
 * @since   6.0
 * @sa      GUEST_SESSION_PREPARE
 */
VBGLR3DECL(int) VbglR3GuestCtrlSessionPrepare(uint32_t idClient, uint32_t idSession, void const *pvKey, uint32_t cbKey)
{
    int rc;
    do
    {
        struct
        {
            VBGLIOCHGCMCALL         Hdr;
            HGCMFunctionParameter   idSession;
            HGCMFunctionParameter   pKey;
        } Msg;
        VBGL_HGCM_HDR_INIT(&Msg.Hdr, idClient, GUEST_MSG_SESSION_PREPARE, 2);
        VbglHGCMParmUInt32Set(&Msg.idSession, idSession);
        VbglHGCMParmPtrSet(&Msg.pKey, (void *)pvKey, cbKey);
        rc = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));
    } while (rc == VERR_INTERRUPTED);
    return rc;
}


/**
 * Accepts a session.
 * @since   6.0
 * @sa      GUEST_SESSION_ACCEPT
 */
VBGLR3DECL(int) VbglR3GuestCtrlSessionAccept(uint32_t idClient, uint32_t idSession, void const *pvKey, uint32_t cbKey)
{
    int rc;
    do
    {
        struct
        {
            VBGLIOCHGCMCALL         Hdr;
            HGCMFunctionParameter   idSession;
            HGCMFunctionParameter   pKey;
        } Msg;
        VBGL_HGCM_HDR_INIT(&Msg.Hdr, idClient, GUEST_MSG_SESSION_ACCEPT, 2);
        VbglHGCMParmUInt32Set(&Msg.idSession, idSession);
        VbglHGCMParmPtrSet(&Msg.pKey, (void *)pvKey, cbKey);
        rc = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));
    } while (rc == VERR_INTERRUPTED);
    return rc;
}


/**
 * Cancels a prepared session.
 * @since   6.0
 * @sa      GUEST_SESSION_CANCEL_PREPARED
 */
VBGLR3DECL(int) VbglR3GuestCtrlSessionCancelPrepared(uint32_t idClient, uint32_t idSession)
{
    int rc;
    do
    {
        struct
        {
            VBGLIOCHGCMCALL         Hdr;
            HGCMFunctionParameter   idSession;
        } Msg;
        VBGL_HGCM_HDR_INIT(&Msg.Hdr, idClient, GUEST_MSG_SESSION_CANCEL_PREPARED, 1);
        VbglHGCMParmUInt32Set(&Msg.idSession, idSession);
        rc = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));
    } while (rc == VERR_INTERRUPTED);
    return rc;
}


/**
 * Invalidates the internal state because the (VM) session has been changed (i.e. restored).
 *
 * @returns VBox status code.
 * @param   idClient                Client ID to use for invalidating state.
 * @param   idNewControlSession     New control session ID. Currently unused.
 */
VBGLR3DECL(int) VbglR3GuestCtrlSessionHasChanged(uint32_t idClient, uint64_t idNewControlSession)
{
    RT_NOREF(idNewControlSession);

    vbglR3GuestCtrlDetectPeekGetCancelSupport(idClient);

    return VINF_SUCCESS;
}


/**
 * Asks a specific guest session to close.
 *
 * @return  IPRT status code.
 * @param   pCtx                    Guest control command context to use.
 * @param   fFlags                  Some kind of flag. Figure it out yourself.
 */
VBGLR3DECL(int) VbglR3GuestCtrlSessionClose(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t fFlags)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 2, VERR_INVALID_PARAMETER);

    HGCMMsgSessionClose Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_MSG_SESSION_CLOSE, pCtx->uNumParms);
    VbglHGCMParmUInt32Set(&Msg.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.flags, fFlags);

    return VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
}


/**
 * Notifies a guest session.
 *
 * @returns VBox status code.
 * @param   pCtx                    Guest control command context to use.
 * @param   uType                   Notification type of type GUEST_SESSION_NOTIFYTYPE_XXX.
 * @param   iResult                 Result code (rc) to notify.
 */
VBGLR3DECL(int) VbglR3GuestCtrlSessionNotify(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uType, int32_t iResult)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMMsgSessionNotify Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_MSG_SESSION_NOTIFY, 3);
    VbglHGCMParmUInt32Set(&Msg.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.type, uType);
    VbglHGCMParmUInt32Set(&Msg.result, (uint32_t)iResult);

    return VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
}

/**
 * Initializes a session startup info, extended version.
 *
 * @returns VBox status code.
 * @param   pStartupInfo        Session startup info to initializes.
 * @param   cbUser              Size (in bytes) to use for the user name buffer.
 * @param   cbPassword          Size (in bytes) to use for the password buffer.
 * @param   cbDomain            Size (in bytes) to use for the domain name buffer.
 */
VBGLR3DECL(int)  VbglR3GuestCtrlSessionStartupInfoInitEx(PVBGLR3GUESTCTRLSESSIONSTARTUPINFO pStartupInfo,
                                                         size_t cbUser, size_t cbPassword, size_t cbDomain)
{
    AssertPtrReturn(pStartupInfo, VERR_INVALID_POINTER);

    RT_BZERO(pStartupInfo, sizeof(VBGLR3GUESTCTRLSESSIONSTARTUPINFO));

#define ALLOC_STR(a_Str, a_cb) \
    if ((a_cb) > 0) \
    { \
        pStartupInfo->psz##a_Str = RTStrAlloc(a_cb); \
        AssertPtrBreak(pStartupInfo->psz##a_Str); \
        pStartupInfo->cb##a_Str  = (uint32_t)a_cb; \
    }

    do
    {
        ALLOC_STR(User,     cbUser);
        ALLOC_STR(Password, cbPassword);
        ALLOC_STR(Domain,   cbDomain);

        return VINF_SUCCESS;

    } while (0);

#undef ALLOC_STR

    VbglR3GuestCtrlSessionStartupInfoDestroy(pStartupInfo);
    return VERR_NO_MEMORY;
}

/**
 * Initializes a session startup info.
 *
 * @returns VBox status code.
 * @param   pStartupInfo        Session startup info to initializes.
 */
VBGLR3DECL(int) VbglR3GuestCtrlSessionStartupInfoInit(PVBGLR3GUESTCTRLSESSIONSTARTUPINFO pStartupInfo)
{
    return VbglR3GuestCtrlSessionStartupInfoInitEx(pStartupInfo,
                                                   GUEST_PROC_DEF_USER_LEN, GUEST_PROC_DEF_PASSWORD_LEN,
                                                   GUEST_PROC_DEF_DOMAIN_LEN);
}

/**
 * Destroys a session startup info.
 *
 * @param   pStartupInfo        Session startup info to destroy.
 */
VBGLR3DECL(void) VbglR3GuestCtrlSessionStartupInfoDestroy(PVBGLR3GUESTCTRLSESSIONSTARTUPINFO pStartupInfo)
{
    if (!pStartupInfo)
        return;

    RTStrFree(pStartupInfo->pszUser);
    RTStrFree(pStartupInfo->pszPassword);
    RTStrFree(pStartupInfo->pszDomain);

    RT_BZERO(pStartupInfo, sizeof(VBGLR3GUESTCTRLSESSIONSTARTUPINFO));
}

/**
 * Free's a session startup info.
 *
 * @param   pStartupInfo        Session startup info to free.
 *                              The pointer will not be valid anymore after return.
 */
VBGLR3DECL(void) VbglR3GuestCtrlSessionStartupInfoFree(PVBGLR3GUESTCTRLSESSIONSTARTUPINFO pStartupInfo)
{
    if (!pStartupInfo)
        return;

    VbglR3GuestCtrlSessionStartupInfoDestroy(pStartupInfo);

    RTMemFree(pStartupInfo);
}

/**
 * Duplicates a session startup info.
 *
 * @returns Duplicated session startup info on success, or NULL on error.
 * @param   pStartupInfo        Session startup info to duplicate.
 */
VBGLR3DECL(PVBGLR3GUESTCTRLSESSIONSTARTUPINFO) VbglR3GuestCtrlSessionStartupInfoDup(PVBGLR3GUESTCTRLSESSIONSTARTUPINFO pStartupInfo)
{
    AssertPtrReturn(pStartupInfo, NULL);

    PVBGLR3GUESTCTRLSESSIONSTARTUPINFO pStartupInfoDup = (PVBGLR3GUESTCTRLSESSIONSTARTUPINFO)
                                                                RTMemDup(pStartupInfo, sizeof(VBGLR3GUESTCTRLSESSIONSTARTUPINFO));
    if (pStartupInfoDup)
    {
        do
        {
            pStartupInfoDup->pszUser     = NULL;
            pStartupInfoDup->pszPassword = NULL;
            pStartupInfoDup->pszDomain   = NULL;

#define DUP_STR(a_Str) \
    if (pStartupInfo->cb##a_Str) \
    { \
        pStartupInfoDup->psz##a_Str = (char *)RTStrDup(pStartupInfo->psz##a_Str); \
        AssertPtrBreak(pStartupInfoDup->psz##a_Str); \
        pStartupInfoDup->cb##a_Str  = (uint32_t)strlen(pStartupInfoDup->psz##a_Str) + 1 /* Include terminator */; \
    }
            DUP_STR(User);
            DUP_STR(Password);
            DUP_STR(Domain);

#undef DUP_STR

            return pStartupInfoDup;

        } while (0); /* To use break macros above. */

        VbglR3GuestCtrlSessionStartupInfoFree(pStartupInfoDup);
    }

    return NULL;
}

/**
 * Retrieves a HOST_SESSION_CREATE message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   ppStartupInfo       Where to store the allocated session startup info.
 *                              Needs to be free'd by VbglR3GuestCtrlSessionStartupInfoFree().
 */
VBGLR3DECL(int) VbglR3GuestCtrlSessionGetOpen(PVBGLR3GUESTCTRLCMDCTX pCtx, PVBGLR3GUESTCTRLSESSIONSTARTUPINFO *ppStartupInfo)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 6, VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppStartupInfo, VERR_INVALID_POINTER);

    PVBGLR3GUESTCTRLSESSIONSTARTUPINFO pStartupInfo
        = (PVBGLR3GUESTCTRLSESSIONSTARTUPINFO)RTMemAlloc(sizeof(VBGLR3GUESTCTRLSESSIONSTARTUPINFO));
    if (!pStartupInfo)
        return VERR_NO_MEMORY;

    int rc = VbglR3GuestCtrlSessionStartupInfoInit(pStartupInfo);
    if (RT_FAILURE(rc))
    {
        VbglR3GuestCtrlSessionStartupInfoFree(pStartupInfo);
        return rc;
    }

    do
    {
        HGCMMsgSessionOpen Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_SESSION_CREATE);
        VbglHGCMParmUInt32Set(&Msg.protocol, 0);
        VbglHGCMParmPtrSet(&Msg.username, pStartupInfo->pszUser, pStartupInfo->cbUser);
        VbglHGCMParmPtrSet(&Msg.password, pStartupInfo->pszPassword, pStartupInfo->cbPassword);
        VbglHGCMParmPtrSet(&Msg.domain, pStartupInfo->pszDomain, pStartupInfo->cbDomain);
        VbglHGCMParmUInt32Set(&Msg.flags, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.protocol.GetUInt32(&pStartupInfo->uProtocol);
            Msg.flags.GetUInt32(&pStartupInfo->fFlags);

            pStartupInfo->uSessionID = VBOX_GUESTCTRL_CONTEXTID_GET_SESSION(pCtx->uContextID);
        }

    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);

    if (RT_SUCCESS(rc))
    {
        *ppStartupInfo = pStartupInfo;
    }
    else
        VbglR3GuestCtrlSessionStartupInfoFree(pStartupInfo);

    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * Retrieves a HOST_SESSION_CLOSE message.
 */
VBGLR3DECL(int) VbglR3GuestCtrlSessionGetClose(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *pfFlags, uint32_t *pidSession)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 2, VERR_INVALID_PARAMETER);

    AssertPtrReturn(pfFlags, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgSessionClose Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_SESSION_CLOSE);
        VbglHGCMParmUInt32Set(&Msg.flags, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.flags.GetUInt32(pfFlags);

            if (pidSession)
                *pidSession = VBOX_GUESTCTRL_CONTEXTID_GET_SESSION(pCtx->uContextID);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
/**
 * Retrieves a HOST_MSG_DIR_OPEN message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   pszPath             Where to return the directory path to open.
 * @param   cbPath              Size (in bytes) of \a pszPath.
 * @param   pfFlags             Where to return the directory listing flags.
 * @param   enmFilter           Where to return the directory filter type.
 * @param   penmAttrAdd         Where to return the additional attributes enumeration to use for reading directory entries later.
 * @param   pfReadFlags         Where to return the flags for reading directory entries later.
 */
VBGLR3DECL(int) VbglR3GuestCtrlDirGetOpen(PVBGLR3GUESTCTRLCMDCTX pCtx, char *pszPath, uint32_t cbPath, uint32_t *pfFlags,
                                          GSTCTLDIRFILTER *penmFilter, GSTCTLFSOBJATTRADD *penmReadAttrAdd, uint32_t *pfReadFlags)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 6, VERR_INVALID_PARAMETER);

    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(cbPath, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pfFlags, VERR_INVALID_POINTER);
    AssertPtrReturn(penmFilter, VERR_INVALID_POINTER);
    AssertPtrReturn(penmReadAttrAdd, VERR_INVALID_POINTER);
    AssertPtrReturn(pfReadFlags, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgDirOpen Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_DIR_OPEN);
        VbglHGCMParmPtrSet(&Msg.path, pszPath, cbPath);
        VbglHGCMParmUInt32Set(&Msg.filter, 0);
        VbglHGCMParmUInt32Set(&Msg.flags, 0);
        VbglHGCMParmUInt32Set(&Msg.read_attr_add, 0);
        VbglHGCMParmUInt32Set(&Msg.read_flags, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.filter.GetUInt32((uint32_t *)penmFilter);
            Msg.flags.GetUInt32(pfFlags);
            Msg.read_attr_add.GetUInt32((uint32_t *)penmReadAttrAdd);
            Msg.read_flags.GetUInt32(pfReadFlags);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


/**
 * Retrieves a HOST_MSG_DIR_CLOSE message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   puHandle            Where to return the handle of the guest directory to close.
 */
VBGLR3DECL(int) VbglR3GuestCtrlDirGetClose(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puHandle)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 2, VERR_INVALID_PARAMETER);

    AssertPtrReturn(puHandle, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgDirClose Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_DIR_CLOSE);
        VbglHGCMParmUInt32Set(&Msg.handle, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.handle.GetUInt32(puHandle);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


/**
 * Retrieves a HOST_MSG_DIR_READ message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   puHandle            Where to return the directory handle to rewind.
 */
VBGLR3DECL(int) VbglR3GuestCtrlDirGetRead(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puHandle)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 2, VERR_INVALID_PARAMETER);

    AssertPtrReturn(puHandle, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgDirRead Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_DIR_READ);
        VbglHGCMParmUInt32Set(&Msg.handle, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.handle.GetUInt32(puHandle);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


/**
 * Retrieves a HOST_MSG_DIR_REWIND message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   puHandle            Where to return the directory handle to rewind.
 */
VBGLR3DECL(int) VbglR3GuestCtrlDirGetRewind(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puHandle)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 2, VERR_INVALID_PARAMETER);

    AssertPtrReturn(puHandle, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgDirRewind Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_DIR_REWIND);
        VbglHGCMParmUInt32Set(&Msg.handle, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.handle.GetUInt32(puHandle);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


/**
 * Retrieves a HOST_MSG_DIR_LIST message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   puHandle            Where to return the directory handle to rewind.
 * @param   pcEntries           Where to return the number of directory entries to read.
 * @param   pfFlags             Where to return the directory listing flags.
 */
VBGLR3DECL(int) VbglR3GuestCtrlDirGetList(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puHandle, uint32_t *pcEntries, uint32_t *pfFlags)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 4, VERR_INVALID_PARAMETER);

    AssertPtrReturn(puHandle, VERR_INVALID_POINTER);
    AssertPtrReturn(pcEntries, VERR_INVALID_POINTER);
    AssertPtrReturn(pfFlags, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgDirList Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_DIR_LIST);
        VbglHGCMParmUInt32Set(&Msg.handle, 0);
        VbglHGCMParmUInt32Set(&Msg.num_entries, 0);
        VbglHGCMParmUInt32Set(&Msg.flags, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.handle.GetUInt32(puHandle);
            Msg.num_entries.GetUInt32(pcEntries);
            Msg.flags.GetUInt32(pfFlags);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */


/**
 * Retrieves a HOST_PATH_RENAME message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   pszSource           Where to return the source path.
 * @param   cbSource            Size (in bytes) of \a pszSource.
 * @param   pszDest             Where to return the destination path.
 * @param   cbDest              Size (in bytes) of \a pszDest.
 * @param   pfFlags             Where to return the rename flags.
 */
VBGLR3DECL(int) VbglR3GuestCtrlPathGetRename(PVBGLR3GUESTCTRLCMDCTX     pCtx,
                                             char     *pszSource,       uint32_t cbSource,
                                             char     *pszDest,         uint32_t cbDest,
                                             uint32_t *pfFlags)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 4, VERR_INVALID_PARAMETER);

    AssertPtrReturn(pszSource, VERR_INVALID_POINTER);
    AssertReturn(cbSource, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszDest, VERR_INVALID_POINTER);
    AssertReturn(cbDest, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pfFlags, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgPathRename Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_PATH_RENAME);
        VbglHGCMParmPtrSet(&Msg.source, pszSource, cbSource);
        VbglHGCMParmPtrSet(&Msg.dest, pszDest, cbDest);
        VbglHGCMParmUInt32Set(&Msg.flags, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.flags.GetUInt32(pfFlags);
        }

    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


/**
 * Retrieves a HOST_PATH_USER_DOCUMENTS message.
 *
 * @param   pCtx                Guest control command context to use.
 */
VBGLR3DECL(int) VbglR3GuestCtrlPathGetUserDocuments(PVBGLR3GUESTCTRLCMDCTX pCtx)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 1, VERR_INVALID_PARAMETER);

    int rc;
    do
    {
        HGCMMsgPathUserDocuments Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_PATH_USER_DOCUMENTS);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
            Msg.context.GetUInt32(&pCtx->uContextID);
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


/**
 * Retrieves a HOST_PATH_USER_HOME message.
 *
 * @param   pCtx                Guest control command context to use.
 */
VBGLR3DECL(int) VbglR3GuestCtrlPathGetUserHome(PVBGLR3GUESTCTRLCMDCTX pCtx)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 1, VERR_INVALID_PARAMETER);

    int rc;
    do
    {
        HGCMMsgPathUserHome Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_PATH_USER_HOME);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
            Msg.context.GetUInt32(&pCtx->uContextID);
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


/**
 * Retrieves a HOST_MSG_MOUNT_POINTS message.
 *
 * @param   pCtx                Guest control command context to use.
 * @param   pfFlags             Where to return the get mount point flags on success.
 */
VBGLR3DECL(int) VbglR3GuestCtrlGetMountPoints(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *pfFlags)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 2, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pfFlags, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgMountPoints Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_MOUNT_POINTS);
        VbglHGCMParmUInt32Set(&Msg.flags, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.flags.GetUInt32(pfFlags);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
/**
 * Retrieves a HOST_MSG_FS_QUERY_INFO message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   pszPath             Where to return the path of the file system object to query.
 * @param   cbPath              Size (in bytes) of \a pszPath.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFsGetQueryInfo(PVBGLR3GUESTCTRLCMDCTX pCtx, char *pszPath, uint32_t cbPath)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(cbPath, VERR_INVALID_PARAMETER);

    int rc;
    do
    {
        HGCMMsgFsQueryInfo Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_FS_QUERY_INFO);
        VbglHGCMParmPtrSet(&Msg.path, pszPath, cbPath);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
            Msg.context.GetUInt32(&pCtx->uContextID);

    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}

/**
 * Retrieves a HOST_MSG_FS_OBJ_QUERY_INFO message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   pszPath             Where to return the path of the file system object to query.
 * @param   cbPath              Size (in bytes) of \a pszPath.
 * @param   penmAddAttrib       Where to return the additional attributes enumeration.
 * @param   pfFlags             Where to return the flags for .
 */
VBGLR3DECL(int) VbglR3GuestCtrlFsObjGetQueryInfo(PVBGLR3GUESTCTRLCMDCTX pCtx,
                                                 char *pszPath, uint32_t cbPath, GSTCTLFSOBJATTRADD *penmAddAttrib,
                                                 uint32_t *pfFlags)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 4, VERR_INVALID_PARAMETER);

    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(cbPath, VERR_INVALID_PARAMETER);
    AssertPtrReturn(penmAddAttrib, VERR_INVALID_POINTER);
    AssertPtrReturn(pfFlags, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgFsObjQueryInfo Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_FS_OBJ_QUERY_INFO);
        VbglHGCMParmPtrSet(&Msg.path, pszPath, cbPath);
        VbglHGCMParmUInt32Set(&Msg.add_attributes, 0);
        VbglHGCMParmUInt32Set(&Msg.flags, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.add_attributes.GetUInt32((uint32_t *)penmAddAttrib);
            Msg.flags.GetUInt32(pfFlags);
        }

    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


/**
 * Retrieves a HOST_MSG_FS_CREATE_TEMP message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   pszTemplate         Where to return the template name.
 * @param   cbTemplate          Size (in bytes) of \a pszTemplate.
 * @param   pszPath             Where to return the temporary directory path.
 * @param   cbTemplate          Size (in bytes) of \a pszPath.
 * @param   pfFlags             Where to return the creation flags.
 * @param   pfMode              Where to return the creation mode.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFsGetCreateTemp(PVBGLR3GUESTCTRLCMDCTX pCtx,
                                               char *pszTemplate, uint32_t cbTemplate, char *pszPath, uint32_t cbPath,
                                               uint32_t *pfFlags, uint32_t *pfMode)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 5, VERR_INVALID_PARAMETER);

    AssertPtrReturn(pszTemplate, VERR_INVALID_POINTER);
    AssertReturn(cbTemplate, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(cbPath, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pfFlags, VERR_INVALID_POINTER);
    AssertPtrReturn(pfMode, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgFsCreateTemp Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_FS_CREATE_TEMP);
        VbglHGCMParmPtrSet(&Msg.template_name, pszTemplate, cbTemplate);
        VbglHGCMParmPtrSet(&Msg.tmpdir, pszPath, cbPath);
        VbglHGCMParmUInt32Set(&Msg.flags, 0);
        VbglHGCMParmUInt32Set(&Msg.mode, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.flags.GetUInt32(pfFlags);
            Msg.mode.GetUInt32(pfMode);
        }

    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */


/**
 * Retrieves a HOST_MSG_SHUTDOWN message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   pfAction            Where to store the action flags on success.
 */
VBGLR3DECL(int) VbglR3GuestCtrlGetShutdown(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *pfAction)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 2, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pfAction, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgShutdown Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_SHUTDOWN);
        VbglHGCMParmUInt32Set(&Msg.action,  0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.action.GetUInt32(pfAction);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}

/**
 * Initializes a process startup info, extended version.
 *
 * @returns VBox status code.
 * @param   pStartupInfo        Process startup info to initializes.
 * @param   cbCmd               Size (in bytes) to use for the command buffer.
 * @param   cbArgs              Size (in bytes) to use for the arguments buffer.
 * @param   cbEnv               Size (in bytes) to use for the environment buffer.
 * @param   cbCwd               Size (in bytes) to use for the current working directory.
 * @param   cbUser              Size (in bytes) to use for the user name buffer.
 * @param   cbPassword          Size (in bytes) to use for the password buffer.
 * @param   cbDomain            Size (in bytes) to use for the domain buffer.
 */
VBGLR3DECL(int) VbglR3GuestCtrlProcStartupInfoInitEx(PVBGLR3GUESTCTRLPROCSTARTUPINFO pStartupInfo,
                                                     size_t cbCmd, size_t cbArgs, size_t cbEnv,
                                                     size_t cbCwd,
                                                     size_t cbUser, size_t cbPassword, size_t cbDomain)
{
    AssertPtrReturn(pStartupInfo, VERR_INVALID_POINTER);
    AssertReturn(cbCmd,           VERR_INVALID_PARAMETER);
    AssertReturn(cbArgs,          VERR_INVALID_PARAMETER);
    AssertReturn(cbEnv,           VERR_INVALID_PARAMETER);
    AssertReturn(cbCwd,           VERR_INVALID_PARAMETER);
    AssertReturn(cbUser,          VERR_INVALID_PARAMETER);
    AssertReturn(cbPassword,      VERR_INVALID_PARAMETER);
    AssertReturn(cbDomain,        VERR_INVALID_PARAMETER);

    RT_BZERO(pStartupInfo, sizeof(VBGLR3GUESTCTRLPROCSTARTUPINFO));

#define ALLOC_STR(a_Str, a_cb) \
    if ((a_cb) > 0) \
    { \
        pStartupInfo->psz##a_Str = RTStrAlloc(a_cb); \
        AssertPtrBreak(pStartupInfo->psz##a_Str); \
        pStartupInfo->cb##a_Str  = (uint32_t)a_cb; \
    }

    do
    {
        ALLOC_STR(Cmd,      cbCmd);
        ALLOC_STR(Args,     cbArgs);
        ALLOC_STR(Env,      cbEnv);
        ALLOC_STR(Cwd,      cbCwd);
        ALLOC_STR(User,     cbUser);
        ALLOC_STR(Password, cbPassword);
        ALLOC_STR(Domain,   cbDomain);

        return VINF_SUCCESS;

    } while (0);

#undef ALLOC_STR

    VbglR3GuestCtrlProcStartupInfoDestroy(pStartupInfo);
    return VERR_NO_MEMORY;
}

/**
 * Initializes a process startup info with default values.
 *
 * @param   pStartupInfo        Process startup info to initialize.
 */
VBGLR3DECL(int) VbglR3GuestCtrlProcStartupInfoInit(PVBGLR3GUESTCTRLPROCSTARTUPINFO pStartupInfo)
{
    return VbglR3GuestCtrlProcStartupInfoInitEx(pStartupInfo,
                                                GUEST_PROC_DEF_CMD_LEN, GUEST_PROC_DEF_ARGS_LEN, GUEST_PROC_DEF_ENV_LEN,
                                                GUEST_PROC_DEF_CWD_LEN,
                                                GUEST_PROC_DEF_USER_LEN     /* Deprecated, now handled via session creation. */,
                                                GUEST_PROC_DEF_PASSWORD_LEN /* Ditto. */,
                                                GUEST_PROC_DEF_DOMAIN_LEN   /* Ditto. */);
}

/**
 * Destroys a process startup info.
 *
 * @param   pStartupInfo        Process startup info to destroy.
 */
VBGLR3DECL(void) VbglR3GuestCtrlProcStartupInfoDestroy(PVBGLR3GUESTCTRLPROCSTARTUPINFO pStartupInfo)
{
    if (!pStartupInfo)
        return;

    RTStrFree(pStartupInfo->pszCmd);
    RTStrFree(pStartupInfo->pszArgs);
    RTStrFree(pStartupInfo->pszEnv);
    RTStrFree(pStartupInfo->pszCwd);
    RTStrFree(pStartupInfo->pszUser);
    RTStrFree(pStartupInfo->pszPassword);
    RTStrFree(pStartupInfo->pszDomain);

    RT_BZERO(pStartupInfo, sizeof(VBGLR3GUESTCTRLPROCSTARTUPINFO));
}

/**
 * Free's a process startup info.
 *
 * @param   pStartupInfo        Process startup info to free.
 *                              The pointer will not be valid anymore after return.
 */
VBGLR3DECL(void) VbglR3GuestCtrlProcStartupInfoFree(PVBGLR3GUESTCTRLPROCSTARTUPINFO pStartupInfo)
{
    if (!pStartupInfo)
        return;

    VbglR3GuestCtrlProcStartupInfoDestroy(pStartupInfo);

    RTMemFree(pStartupInfo);
    pStartupInfo = NULL;
}

/**
 * Duplicates a process startup info.
 *
 * @returns Duplicated process startup info on success, or NULL on error.
 * @param   pStartupInfo        Process startup info to duplicate.
 */
VBGLR3DECL(PVBGLR3GUESTCTRLPROCSTARTUPINFO) VbglR3GuestCtrlProcStartupInfoDup(PVBGLR3GUESTCTRLPROCSTARTUPINFO pStartupInfo)
{
    AssertPtrReturn(pStartupInfo, NULL);

    PVBGLR3GUESTCTRLPROCSTARTUPINFO pStartupInfoDup = (PVBGLR3GUESTCTRLPROCSTARTUPINFO)
                                                            RTMemDup(pStartupInfo, sizeof(VBGLR3GUESTCTRLPROCSTARTUPINFO));
    if (pStartupInfoDup)
    {
        do
        {
            pStartupInfoDup->pszCmd      = NULL;
            pStartupInfoDup->pszArgs     = NULL;
            pStartupInfoDup->pszEnv      = NULL;
            pStartupInfoDup->pszCwd      = NULL;
            pStartupInfoDup->pszUser     = NULL;
            pStartupInfoDup->pszPassword = NULL;
            pStartupInfoDup->pszDomain   = NULL;

#define DUP_STR(a_Str) \
    if (pStartupInfo->cb##a_Str) \
    { \
        pStartupInfoDup->psz##a_Str = (char *)RTStrDup(pStartupInfo->psz##a_Str); \
        AssertPtrBreak(pStartupInfoDup->psz##a_Str); \
        pStartupInfoDup->cb##a_Str  = (uint32_t)strlen(pStartupInfoDup->psz##a_Str) + 1 /* Include terminator */; \
    }

#define DUP_MEM(a_Str) \
    if (pStartupInfo->cb##a_Str) \
    { \
        pStartupInfoDup->psz##a_Str = (char *)RTMemDup(pStartupInfo->psz##a_Str, pStartupInfo->cb##a_Str); \
        AssertPtrBreak(pStartupInfoDup->psz##a_Str); \
        pStartupInfoDup->cb##a_Str  = (uint32_t)pStartupInfo->cb##a_Str; \
    }

            DUP_STR(Cmd);
            DUP_MEM(Args);
            DUP_MEM(Env);
            DUP_MEM(Cwd);
            DUP_STR(User);
            DUP_STR(Password);
            DUP_STR(Domain);

#undef DUP_STR
#undef DUP_MEM

            return pStartupInfoDup;

        } while (0); /* To use break macros above. */

        VbglR3GuestCtrlProcStartupInfoFree(pStartupInfoDup);
    }

    return NULL;
}

/**
 * Retrieves a HOST_EXEC_CMD message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   ppStartupInfo       Where to store the allocated session startup info.
 *                              Needs to be free'd by VbglR3GuestCtrlProcStartupInfoFree().
 */
VBGLR3DECL(int) VbglR3GuestCtrlProcGetStart(PVBGLR3GUESTCTRLCMDCTX pCtx, PVBGLR3GUESTCTRLPROCSTARTUPINFO *ppStartupInfo)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(ppStartupInfo, VERR_INVALID_POINTER);

    PVBGLR3GUESTCTRLPROCSTARTUPINFO pStartupInfo
        = (PVBGLR3GUESTCTRLPROCSTARTUPINFO)RTMemAlloc(sizeof(VBGLR3GUESTCTRLPROCSTARTUPINFO));
    if (!pStartupInfo)
        return VERR_NO_MEMORY;

    int rc = VbglR3GuestCtrlProcStartupInfoInit(pStartupInfo);
    if (RT_FAILURE(rc))
    {
        VbglR3GuestCtrlProcStartupInfoFree(pStartupInfo);
        return rc;
    }

    unsigned       cRetries      = 0;
    const unsigned cMaxRetries   = 32; /* Should be enough for now. */
    const unsigned cGrowthFactor = 2;  /* By how much the buffers will grow if they're too small yet. */

    do
    {
        LogRel(("VbglR3GuestCtrlProcGetStart: Retrieving\n"));

        HGCMMsgProcExec Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_EXEC_CMD);
        VbglHGCMParmPtrSet(&Msg.cmd, pStartupInfo->pszCmd, pStartupInfo->cbCmd);
        VbglHGCMParmUInt32Set(&Msg.flags, 0);
        VbglHGCMParmUInt32Set(&Msg.num_args, 0);
        VbglHGCMParmPtrSet(&Msg.args, pStartupInfo->pszArgs, pStartupInfo->cbArgs);
        VbglHGCMParmUInt32Set(&Msg.num_env, 0);
        VbglHGCMParmUInt32Set(&Msg.cb_env, 0);
        VbglHGCMParmPtrSet(&Msg.env, pStartupInfo->pszEnv, pStartupInfo->cbEnv);
        if (pCtx->uProtocol < 2) /* Protocol v1, deprecated. */
        {
            VbglHGCMParmPtrSet(&Msg.u.v1.username, pStartupInfo->pszUser, pStartupInfo->cbUser);
            VbglHGCMParmPtrSet(&Msg.u.v1.password, pStartupInfo->pszPassword, pStartupInfo->cbPassword);
            VbglHGCMParmUInt32Set(&Msg.u.v1.timeout, 0);
        }
        else /* Protocol v2. */
        {
            VbglHGCMParmUInt32Set(&Msg.u.v2.timeout, 0);
            VbglHGCMParmUInt32Set(&Msg.u.v2.priority, 0);
            VbglHGCMParmUInt32Set(&Msg.u.v2.num_affinity, 0);
            VbglHGCMParmPtrSet(&Msg.u.v2.affinity, pStartupInfo->uAffinity, sizeof(pStartupInfo->uAffinity));
        }

        /* CWD support was added in VBox 7.1. Otherwise just skip setting it. */
        if (g_fVbglR3GuestCtrlHostFeatures0 & VBOX_GUESTCTRL_HF_0_PROCESS_CWD)
            VbglHGCMParmPtrSet(&Msg.cwd, pStartupInfo->pszCwd, pStartupInfo->cbCwd);

        /*
         * We need to calculate the data size ourselves here and not rely on sizeof(),
         * as sizeof(Msg) might be different to what the host expects.
         *
         * This first was needed when excluding the CWD support for hosts running VBox < 7.1.
         */
        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(VBGLIOCHGCMCALL) + pCtx->uNumParms * sizeof(HGCMFunctionParameter));
        if (RT_FAILURE(rc))
        {
            LogRel(("VbglR3GuestCtrlProcGetStart: 1 - %Rrc (retry %u, cbCmd=%RU32, cbCwd=%RU32, cbArgs=%RU32, cbEnv=%RU32)\n",
                    rc, cRetries, pStartupInfo->cbCmd, pStartupInfo->cbCwd, pStartupInfo->cbArgs, pStartupInfo->cbEnv));

            if (   rc == VERR_BUFFER_OVERFLOW
                && cRetries++ < cMaxRetries)
            {
#define GROW_STR(a_Str, a_cbMax) \
        pStartupInfo->psz##a_Str = (char *)RTMemRealloc(pStartupInfo->psz##a_Str, \
           RT_MIN(pStartupInfo->cb##a_Str * cGrowthFactor, a_cbMax)); \
        AssertPtrBreakStmt(pStartupInfo->psz##a_Str, rc = VERR_NO_MEMORY); \
        pStartupInfo->cb##a_Str  = RT_MIN(pStartupInfo->cb##a_Str * cGrowthFactor, a_cbMax);

                /* We can't tell which parameter doesn't fit, so we have to resize all. */
                GROW_STR(Cmd , GUEST_PROC_MAX_CMD_LEN);
                GROW_STR(Args, GUEST_PROC_MAX_ARGS_LEN);
                GROW_STR(Env,  GUEST_PROC_MAX_ENV_LEN);
                GROW_STR(Cwd,  GUEST_PROC_MAX_CWD_LEN);
#undef GROW_STR
                LogRel(("VbglR3GuestCtrlProcGetStart: 2 - %Rrc (retry %u, cbCmd=%RU32, cbCwd=%RU32, cbArgs=%RU32, cbEnv=%RU32)\n",
                        rc, cRetries, pStartupInfo->cbCmd, pStartupInfo->cbCwd, pStartupInfo->cbArgs, pStartupInfo->cbEnv));
                LogRel(("g_fVbglR3GuestCtrlHavePeekGetCancel=%RTbool\n", RT_BOOL(g_fVbglR3GuestCtrlHavePeekGetCancel)));
            }
            else
                break;
        }
        else
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.flags.GetUInt32(&pStartupInfo->fFlags);
            Msg.num_args.GetUInt32(&pStartupInfo->cArgs);
            Msg.num_env.GetUInt32(&pStartupInfo->cEnvVars);
            Msg.cb_env.GetUInt32(&pStartupInfo->cbEnv);
            if (pCtx->uProtocol < 2)
                Msg.u.v1.timeout.GetUInt32(&pStartupInfo->uTimeLimitMS);
            else
            {
                Msg.u.v2.timeout.GetUInt32(&pStartupInfo->uTimeLimitMS);
                Msg.u.v2.priority.GetUInt32(&pStartupInfo->uPriority);
                Msg.u.v2.num_affinity.GetUInt32(&pStartupInfo->cAffinity);
            }
        }
    } while ((   rc == VERR_INTERRUPTED
              || rc == VERR_BUFFER_OVERFLOW) && g_fVbglR3GuestCtrlHavePeekGetCancel);

    LogRel(("VbglR3GuestCtrlProcGetStart: Returning %Rrc (retry %u, cbCmd=%RU32, cbArgs=%RU32, cbEnv=%RU32)\n",
            rc, cRetries, pStartupInfo->cbCmd, pStartupInfo->cbArgs, pStartupInfo->cbEnv));

    if (RT_SUCCESS(rc))
    {
        *ppStartupInfo = pStartupInfo;
    }
    else
        VbglR3GuestCtrlProcStartupInfoFree(pStartupInfo);

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Allocates and gets host data, based on the message ID.
 *
 * This will block until data becomes available.
 *
 * @returns VBox status code.
 * @param   pCtx                    Guest control command context to use.
 * @param   puPID                   Where to return the guest PID to retrieve output from on success.
 * @param   puHandle                Where to return the guest process handle to retrieve output from on success.
 * @param   pfFlags                 Where to return the output flags on success.
 */
VBGLR3DECL(int) VbglR3GuestCtrlProcGetOutput(PVBGLR3GUESTCTRLCMDCTX pCtx,
                                             uint32_t *puPID, uint32_t *puHandle, uint32_t *pfFlags)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 4, VERR_INVALID_PARAMETER);

    AssertPtrReturn(puPID, VERR_INVALID_POINTER);
    AssertPtrReturn(puHandle, VERR_INVALID_POINTER);
    AssertPtrReturn(pfFlags, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgProcOutput Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_EXEC_GET_OUTPUT);
        VbglHGCMParmUInt32Set(&Msg.pid, 0);
        VbglHGCMParmUInt32Set(&Msg.handle, 0);
        VbglHGCMParmUInt32Set(&Msg.flags, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, RT_UOFFSETOF(HGCMMsgProcOutput, data));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.pid.GetUInt32(puPID);
            Msg.handle.GetUInt32(puHandle);
            Msg.flags.GetUInt32(pfFlags);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


/**
 * Retrieves the input data from host which then gets sent to the started
 * process (HOST_EXEC_SET_INPUT).
 *
 * This will block until data becomes available.
 */
VBGLR3DECL(int) VbglR3GuestCtrlProcGetInput(PVBGLR3GUESTCTRLCMDCTX  pCtx,
                                            uint32_t  *puPID,       uint32_t *pfFlags,
                                            void      *pvData,      uint32_t  cbData,
                                            uint32_t  *pcbSize)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 5, VERR_INVALID_PARAMETER);

    AssertPtrReturn(puPID, VERR_INVALID_POINTER);
    AssertPtrReturn(pfFlags, VERR_INVALID_POINTER);
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbSize, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgProcInput Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_EXEC_SET_INPUT);
        VbglHGCMParmUInt32Set(&Msg.pid, 0);
        VbglHGCMParmUInt32Set(&Msg.flags, 0);
        VbglHGCMParmPtrSet(&Msg.data, pvData, cbData);
        VbglHGCMParmUInt32Set(&Msg.size, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.pid.GetUInt32(puPID);
            Msg.flags.GetUInt32(pfFlags);
            Msg.size.GetUInt32(pcbSize);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);

    if (   rc != VERR_TOO_MUCH_DATA
        || g_fVbglR3GuestCtrlHavePeekGetCancel)
        return rc;
    return VERR_BUFFER_OVERFLOW;
}


#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
/**
 * Retrieves a HOST_MSG_DIR_CREATE message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   pszPath             Where to return the path.
 * @param   cbPath              Size (in bytes) of \a pszPath.
 * @param   pfMode              Where to return the creation mode.
 * @param   pfFlags             Where to return the creation flags (GSTCTL_CREATEDIRECTORY_F_XXX).
 */
VBGLR3DECL(int) VbglR3GuestCtrlDirGetCreate(PVBGLR3GUESTCTRLCMDCTX pCtx, char *pszPath, uint32_t cbPath, uint32_t *pfMode, uint32_t *pfFlags)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 4, VERR_INVALID_PARAMETER);

    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(cbPath, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pfFlags, VERR_INVALID_POINTER);
    AssertPtrReturn(pfMode, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgDirCreate Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_DIR_CREATE);
        VbglHGCMParmPtrSet(&Msg.path, pszPath, cbPath);
        VbglHGCMParmUInt32Set(&Msg.mode, 0);
        VbglHGCMParmUInt32Set(&Msg.flags, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.flags.GetUInt32(pfFlags);
            Msg.mode.GetUInt32(pfMode);
        }

    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */


/**
 * Retrieves a HOST_DIR_REMOVE message.
 */
VBGLR3DECL(int) VbglR3GuestCtrlDirGetRemove(PVBGLR3GUESTCTRLCMDCTX     pCtx,
                                            char     *pszPath,         uint32_t cbPath,
                                            uint32_t *pfFlags)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 3, VERR_INVALID_PARAMETER);

    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(cbPath, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pfFlags, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgDirRemove Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_DIR_REMOVE);
        VbglHGCMParmPtrSet(&Msg.path, pszPath, cbPath);
        VbglHGCMParmUInt32Set(&Msg.flags, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.flags.GetUInt32(pfFlags);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


/**
 * Retrieves a HOST_FILE_OPEN message.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFileGetOpen(PVBGLR3GUESTCTRLCMDCTX      pCtx,
                                           char     *pszFileName,      uint32_t cbFileName,
                                           char     *pszAccess,        uint32_t cbAccess,
                                           char     *pszDisposition,   uint32_t cbDisposition,
                                           char     *pszSharing,       uint32_t cbSharing,
                                           uint32_t *puCreationMode,
                                           uint64_t *poffAt)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->uNumParms == 7, VERR_INVALID_PARAMETER);

    AssertPtrReturn(pszFileName, VERR_INVALID_POINTER);
    AssertReturn(cbFileName, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszAccess, VERR_INVALID_POINTER);
    AssertReturn(cbAccess, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszDisposition, VERR_INVALID_POINTER);
    AssertReturn(cbDisposition, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszSharing, VERR_INVALID_POINTER);
    AssertReturn(cbSharing, VERR_INVALID_PARAMETER);
    AssertPtrReturn(puCreationMode, VERR_INVALID_POINTER);
    AssertPtrReturn(poffAt, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgFileOpen Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_FILE_OPEN);
        VbglHGCMParmPtrSet(&Msg.filename, pszFileName, cbFileName);
        VbglHGCMParmPtrSet(&Msg.openmode, pszAccess, cbAccess);
        VbglHGCMParmPtrSet(&Msg.disposition, pszDisposition, cbDisposition);
        VbglHGCMParmPtrSet(&Msg.sharing, pszSharing, cbSharing);
        VbglHGCMParmUInt32Set(&Msg.creationmode, 0);
        VbglHGCMParmUInt64Set(&Msg.offset, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.creationmode.GetUInt32(puCreationMode);
            Msg.offset.GetUInt64(poffAt);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


/**
 * Retrieves a HOST_FILE_CLOSE message.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFileGetClose(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puHandle)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    AssertReturn(pCtx->uNumParms == 2, VERR_INVALID_PARAMETER);
    AssertPtrReturn(puHandle, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgFileClose Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_FILE_CLOSE);
        VbglHGCMParmUInt32Set(&Msg.handle, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.handle.GetUInt32(puHandle);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


/**
 * Retrieves a HOST_FILE_READ message.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFileGetRead(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puHandle, uint32_t *puToRead)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    AssertReturn(pCtx->uNumParms == 3, VERR_INVALID_PARAMETER);
    AssertPtrReturn(puHandle, VERR_INVALID_POINTER);
    AssertPtrReturn(puToRead, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgFileRead Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_FILE_READ);
        VbglHGCMParmUInt32Set(&Msg.handle, 0);
        VbglHGCMParmUInt32Set(&Msg.size, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.handle.GetUInt32(puHandle);
            Msg.size.GetUInt32(puToRead);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


/**
 * Retrieves a HOST_FILE_READ_AT message.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFileGetReadAt(PVBGLR3GUESTCTRLCMDCTX pCtx,
                                             uint32_t *puHandle, uint32_t *puToRead, uint64_t *poffAt)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    AssertReturn(pCtx->uNumParms == 4, VERR_INVALID_PARAMETER);
    AssertPtrReturn(puHandle, VERR_INVALID_POINTER);
    AssertPtrReturn(puToRead, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgFileReadAt Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_FILE_READ_AT);
        VbglHGCMParmUInt32Set(&Msg.handle, 0);
        VbglHGCMParmUInt64Set(&Msg.offset, 0);
        VbglHGCMParmUInt32Set(&Msg.size, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.handle.GetUInt32(puHandle);
            Msg.offset.GetUInt64(poffAt);
            Msg.size.GetUInt32(puToRead);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


/**
 * Retrieves a HOST_FILE_WRITE message.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFileGetWrite(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puHandle,
                                            void *pvData, uint32_t cbData, uint32_t *pcbSize)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    AssertReturn(pCtx->uNumParms == 4, VERR_INVALID_PARAMETER);
    AssertPtrReturn(puHandle, VERR_INVALID_POINTER);
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbSize, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgFileWrite Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_FILE_WRITE);
        VbglHGCMParmUInt32Set(&Msg.handle, 0);
        VbglHGCMParmPtrSet(&Msg.data, pvData, cbData);
        VbglHGCMParmUInt32Set(&Msg.size, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.handle.GetUInt32(puHandle);
            Msg.size.GetUInt32(pcbSize);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);

    if (   rc != VERR_TOO_MUCH_DATA
        || g_fVbglR3GuestCtrlHavePeekGetCancel)
        return rc;
    return VERR_BUFFER_OVERFLOW;
}


/**
 * Retrieves a HOST_FILE_WRITE_AT message.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFileGetWriteAt(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puHandle,
                                              void *pvData, uint32_t cbData, uint32_t *pcbSize, uint64_t *poffAt)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    AssertReturn(pCtx->uNumParms == 5, VERR_INVALID_PARAMETER);
    AssertPtrReturn(puHandle, VERR_INVALID_POINTER);
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbSize, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgFileWriteAt Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_FILE_WRITE_AT);
        VbglHGCMParmUInt32Set(&Msg.handle, 0);
        VbglHGCMParmPtrSet(&Msg.data, pvData, cbData);
        VbglHGCMParmUInt32Set(&Msg.size, 0);
        VbglHGCMParmUInt64Set(&Msg.offset, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.handle.GetUInt32(puHandle);
            Msg.size.GetUInt32(pcbSize);
            Msg.offset.GetUInt64(poffAt);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);

    if (   rc != VERR_TOO_MUCH_DATA
        || g_fVbglR3GuestCtrlHavePeekGetCancel)
        return rc;
    return VERR_BUFFER_OVERFLOW;
}


/**
 * Retrieves a HOST_FILE_SEEK message.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFileGetSeek(PVBGLR3GUESTCTRLCMDCTX pCtx,
                                           uint32_t *puHandle, uint32_t *puSeekMethod, uint64_t *poffAt)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    AssertReturn(pCtx->uNumParms == 4, VERR_INVALID_PARAMETER);
    AssertPtrReturn(puHandle, VERR_INVALID_POINTER);
    AssertPtrReturn(puSeekMethod, VERR_INVALID_POINTER);
    AssertPtrReturn(poffAt, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgFileSeek Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_FILE_SEEK);
        VbglHGCMParmUInt32Set(&Msg.handle, 0);
        VbglHGCMParmUInt32Set(&Msg.method, 0);
        VbglHGCMParmUInt64Set(&Msg.offset, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.handle.GetUInt32(puHandle);
            Msg.method.GetUInt32(puSeekMethod);
            Msg.offset.GetUInt64(poffAt);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


/**
 * Retrieves a HOST_FILE_TELL message.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFileGetTell(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puHandle)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    AssertReturn(pCtx->uNumParms == 2, VERR_INVALID_PARAMETER);
    AssertPtrReturn(puHandle, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgFileTell Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_FILE_TELL);
        VbglHGCMParmUInt32Set(&Msg.handle, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.handle.GetUInt32(puHandle);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


/**
 * Retrieves a HOST_FILE_SET_SIZE message.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFileGetSetSize(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puHandle, uint64_t *pcbNew)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    AssertReturn(pCtx->uNumParms == 3, VERR_INVALID_PARAMETER);
    AssertPtrReturn(puHandle, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbNew, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgFileSetSize Msg;
        VBGL_HGCM_HDR_INIT(&Msg.Hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.id32Context, HOST_MSG_FILE_SET_SIZE);
        VbglHGCMParmUInt32Set(&Msg.id32Handle, 0);
        VbglHGCMParmUInt64Set(&Msg.cb64NewSize, 0);

        rc = VbglR3HGCMCall(&Msg.Hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.id32Context.GetUInt32(&pCtx->uContextID);
            Msg.id32Handle.GetUInt32(puHandle);
            Msg.cb64NewSize.GetUInt64(pcbNew);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
VBGLR3DECL(int) VbglR3GuestCtrlFileGetRemove(PVBGLR3GUESTCTRLCMDCTX pCtx, char *pszFileName, uint32_t cbFileName)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    AssertReturn(pCtx->uNumParms == 2, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszFileName, VERR_INVALID_POINTER);
    AssertReturn(cbFileName, VERR_INVALID_PARAMETER);

    int rc;
    do
    {
        HGCMMsgFileRemove Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_FILE_REMOVE);
        VbglHGCMParmPtrSet(&Msg.filename, pszFileName, cbFileName);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */


/**
 * Retrieves a HOST_EXEC_TERMINATE message.
 */
VBGLR3DECL(int) VbglR3GuestCtrlProcGetTerminate(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puPID)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    AssertReturn(pCtx->uNumParms == 2, VERR_INVALID_PARAMETER);
    AssertPtrReturn(puPID, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgProcTerminate Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_EXEC_TERMINATE);
        VbglHGCMParmUInt32Set(&Msg.pid, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.pid.GetUInt32(puPID);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


/**
 * Retrieves a HOST_EXEC_WAIT_FOR message.
 */
VBGLR3DECL(int) VbglR3GuestCtrlProcGetWaitFor(PVBGLR3GUESTCTRLCMDCTX pCtx,
                                              uint32_t *puPID, uint32_t *puWaitFlags, uint32_t *puTimeoutMS)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    AssertReturn(pCtx->uNumParms == 5, VERR_INVALID_PARAMETER);
    AssertPtrReturn(puPID, VERR_INVALID_POINTER);

    int rc;
    do
    {
        HGCMMsgProcWaitFor Msg;
        VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, vbglR3GuestCtrlGetMsgFunctionNo(pCtx->uClientID), pCtx->uNumParms);
        VbglHGCMParmUInt32Set(&Msg.context, HOST_MSG_EXEC_WAIT_FOR);
        VbglHGCMParmUInt32Set(&Msg.pid, 0);
        VbglHGCMParmUInt32Set(&Msg.flags, 0);
        VbglHGCMParmUInt32Set(&Msg.timeout, 0);

        rc = VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
        if (RT_SUCCESS(rc))
        {
            Msg.context.GetUInt32(&pCtx->uContextID);
            Msg.pid.GetUInt32(puPID);
            Msg.flags.GetUInt32(puWaitFlags);
            Msg.timeout.GetUInt32(puTimeoutMS);
        }
    } while (rc == VERR_INTERRUPTED && g_fVbglR3GuestCtrlHavePeekGetCancel);
    return rc;
}


/*********************************************************************************************************************************
 * Directory callbacks                                                                                                           *
 ********************************************************************************************************************************/

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
/**
 * Replies to a HOST_MSG_DIR_OPEN message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 * @param   uDirHandle          Directory handle of opened directory.
 */
VBGLR3DECL(int) VbglR3GuestCtrlDirCbOpen(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc, uint32_t uDirHandle)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMReplyDirNotify Msg;
    VBGL_HGCM_HDR_INIT(&Msg.reply_hdr.hdr, pCtx->uClientID, GUEST_MSG_DIR_NOTIFY, GSTCTL_HGCM_REPLY_HDR_PARMS + 1);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.type, GUEST_DIR_NOTIFYTYPE_OPEN);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.rc, uRc);

    VbglHGCMParmUInt32Set(&Msg.u.open.handle, uDirHandle);

    return VbglR3HGCMCall(&Msg.reply_hdr.hdr, RT_UOFFSET_AFTER(HGCMReplyDirNotify, u.open));
}


/**
 * Replies to a HOST_MSG_DIR_CLOSE message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 */
VBGLR3DECL(int) VbglR3GuestCtrlDirCbClose(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMReplyDirNotify Msg;
    VBGL_HGCM_HDR_INIT(&Msg.reply_hdr.hdr, pCtx->uClientID, GUEST_MSG_DIR_NOTIFY, GSTCTL_HGCM_REPLY_HDR_PARMS);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.type, GUEST_DIR_NOTIFYTYPE_CLOSE);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.rc, uRc);

    return VbglR3HGCMCall(&Msg.reply_hdr.hdr, RT_UOFFSET_AFTER(HGCMReplyDirNotify, reply_hdr));
}


/**
 * Replies to a HOST_MSG_DIR_READ message, extended version.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 * @param   pEntry              Directory entry to send.
 * @param   cbSize              Size (in bytes) of the OFFSET(GSTCTLDIRENTRYEX, szName[pEntry->cbName + 1]).
 *                              See RTDirReadEx() for more information.
 * @param   pszUser             Associated user ID (owner, uid) as a string.
 * @param   pszGroups           Associated user groups as a string.
 *                              Multiple groups are delimited by GSTCTL_DIRENTRY_GROUPS_DELIMITER_STR,
 *                              whereas the first group always is the primary group.
 */
VBGLR3DECL(int) VbglR3GuestCtrlDirCbReadEx(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc, PGSTCTLDIRENTRYEX pEntry, uint32_t cbSize,
                                           const char *pszUser, const char *pszGroups)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMReplyDirNotify Msg;
    VBGL_HGCM_HDR_INIT(&Msg.reply_hdr.hdr, pCtx->uClientID, GUEST_MSG_DIR_NOTIFY, GSTCTL_HGCM_REPLY_HDR_PARMS + 3);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.type, GUEST_DIR_NOTIFYTYPE_READ);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.rc, uRc);

    VbglHGCMParmPtrSet      (&Msg.u.read.entry,   pEntry, cbSize);
    VbglHGCMParmPtrSetString(&Msg.u.read.user,    pszUser);
    VbglHGCMParmPtrSetString(&Msg.u.read.groups,  pszGroups);

    return VbglR3HGCMCall(&Msg.reply_hdr.hdr, RT_UOFFSET_AFTER(HGCMReplyDirNotify, u.read));
}


/**
 * Replies to a HOST_MSG_DIR_READ message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 * @param   pEntry              Directory entry to send.
 * @param   cbSize              Size (in bytes) of the OFFSET(GSTCTLDIRENTRYEX, szName[pEntry->cbName + 1]).
 *                              See RTDirReadEx() for more information.
 */
VBGLR3DECL(int) VbglR3GuestCtrlDirCbRead(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc, PGSTCTLDIRENTRYEX pEntry, uint32_t cbSize)
{
    char szIgnored[1] = { 0 };
    return VbglR3GuestCtrlDirCbReadEx(pCtx, uRc, pEntry, cbSize, szIgnored /* pszUser */, szIgnored /* pszGroups */);
}


/**
 * Replies to a HOST_MSG_DIR_REWIND message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 */
VBGLR3DECL(int) VbglR3GuestCtrlDirCbRewind(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMReplyDirNotify Msg;
    VBGL_HGCM_HDR_INIT(&Msg.reply_hdr.hdr, pCtx->uClientID, GUEST_MSG_DIR_NOTIFY, GSTCTL_HGCM_REPLY_HDR_PARMS);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.type, GUEST_DIR_NOTIFYTYPE_REWIND);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.rc, uRc);

    return VbglR3HGCMCall(&Msg.reply_hdr.hdr, RT_UOFFSET_AFTER(HGCMReplyDirNotify, u));
}


/**
 * Replies to a HOST_MSG_DIR_LIST message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 * @þaram   cEntries            Number of directory entries to send.
 * @param   pvBuf               Buffer of directory entries to send.
 * @param   cbBuf               Size (in bytes) of \a pvBuf.
 */
VBGLR3DECL(int) VbglR3GuestCtrlDirCbList(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc,
                                         uint32_t cEntries, void *pvBuf, uint32_t cbBuf)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMReplyDirNotify Msg;
    VBGL_HGCM_HDR_INIT(&Msg.reply_hdr.hdr, pCtx->uClientID, GUEST_MSG_DIR_NOTIFY, GSTCTL_HGCM_REPLY_HDR_PARMS + 2);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.type, GUEST_DIR_NOTIFYTYPE_LIST);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.rc, uRc);

    VbglHGCMParmUInt32Set(&Msg.u.list.num_entries, cEntries);
    VbglHGCMParmPtrSet(&Msg.u.list.buffer, pvBuf, cbBuf);

    return VbglR3HGCMCall(&Msg.reply_hdr.hdr, RT_UOFFSET_AFTER(HGCMReplyDirNotify, u.list));
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */


/*********************************************************************************************************************************
 * File callbacks                                                                                                                *
 ********************************************************************************************************************************/

/**
 * Replies to a HOST_MSG_FILE_OPEN message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 * @param   uFileHandle         File handle of opened file on success.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFileCbOpen(PVBGLR3GUESTCTRLCMDCTX pCtx,
                                          uint32_t uRc, uint32_t uFileHandle)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMReplyFileNotify Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_MSG_FILE_NOTIFY, 4);
    VbglHGCMParmUInt32Set(&Msg.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.type, GUEST_FILE_NOTIFYTYPE_OPEN);
    VbglHGCMParmUInt32Set(&Msg.rc, uRc);
    VbglHGCMParmUInt32Set(&Msg.u.open.handle, uFileHandle);

    return VbglR3HGCMCall(&Msg.hdr, RT_UOFFSET_AFTER(HGCMReplyFileNotify, u.open));
}


/**
 * Replies to a HOST_MSG_FILE_CLOSE message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 */
VBGLR3DECL(int) VbglR3GuestCtrlFileCbClose(PVBGLR3GUESTCTRLCMDCTX pCtx,
                                           uint32_t uRc)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMReplyFileNotify Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_MSG_FILE_NOTIFY, 3);
    VbglHGCMParmUInt32Set(&Msg.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.type, GUEST_FILE_NOTIFYTYPE_CLOSE);
    VbglHGCMParmUInt32Set(&Msg.rc, uRc);

    return VbglR3HGCMCall(&Msg.hdr, RT_UOFFSETOF(HGCMReplyFileNotify, u));
}


/**
 * Sends an unexpected file handling error to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 */
VBGLR3DECL(int) VbglR3GuestCtrlFileCbError(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMReplyFileNotify Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_MSG_FILE_NOTIFY, 3);
    VbglHGCMParmUInt32Set(&Msg.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.type, GUEST_FILE_NOTIFYTYPE_ERROR);
    VbglHGCMParmUInt32Set(&Msg.rc, uRc);

    return VbglR3HGCMCall(&Msg.hdr, RT_UOFFSETOF(HGCMReplyFileNotify, u));
}


/**
 * Replies to a HOST_MSG_FILE_READ message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 * @param   pvData              Pointer to read file data from guest on success.
 * @param   cbData              Size (in bytes) of read file data from guest on success.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFileCbRead(PVBGLR3GUESTCTRLCMDCTX pCtx,
                                          uint32_t uRc,
                                          void *pvData, uint32_t cbData)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMReplyFileNotify Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_MSG_FILE_NOTIFY, 4);
    VbglHGCMParmUInt32Set(&Msg.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.type, GUEST_FILE_NOTIFYTYPE_READ);
    VbglHGCMParmUInt32Set(&Msg.rc, uRc);
    VbglHGCMParmPtrSet(&Msg.u.read.data, pvData, cbData);

    return VbglR3HGCMCall(&Msg.hdr, RT_UOFFSET_AFTER(HGCMReplyFileNotify, u.read));
}


/**
 * Replies to a HOST_MSG_FILE_READ_AT message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 * @param   pvData              Pointer to read file data from guest on success.
 * @param   cbData              Size (in bytes) of read file data from guest on success.
 * @param   offNew              New offset (in bytes) the guest file pointer points at on success.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFileCbReadOffset(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc,
                                                void *pvData, uint32_t cbData, int64_t offNew)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMReplyFileNotify Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_MSG_FILE_NOTIFY, 5);
    VbglHGCMParmUInt32Set(&Msg.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.type, GUEST_FILE_NOTIFYTYPE_READ_OFFSET);
    VbglHGCMParmUInt32Set(&Msg.rc, uRc);
    VbglHGCMParmPtrSet(&Msg.u.ReadOffset.pvData, pvData, cbData);
    VbglHGCMParmUInt64Set(&Msg.u.ReadOffset.off64New, (uint64_t)offNew);

    return VbglR3HGCMCall(&Msg.hdr, RT_UOFFSET_AFTER(HGCMReplyFileNotify, u.ReadOffset));
}


/**
 * Replies to a HOST_MSG_FILE_WRITE message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 * @param   cbWritten           Size (in bytes) of file data successfully written to guest file. Can be partial.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFileCbWrite(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc, uint32_t cbWritten)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMReplyFileNotify Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_MSG_FILE_NOTIFY, 4);
    VbglHGCMParmUInt32Set(&Msg.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.type, GUEST_FILE_NOTIFYTYPE_WRITE);
    VbglHGCMParmUInt32Set(&Msg.rc, uRc);
    VbglHGCMParmUInt32Set(&Msg.u.write.written, cbWritten);

    return VbglR3HGCMCall(&Msg.hdr, RT_UOFFSET_AFTER(HGCMReplyFileNotify, u.write));
}


/**
 * Replies to a HOST_MSG_FILE_WRITE_AT message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 * @param   cbWritten           Size (in bytes) of file data successfully written to guest file. Can be partial.
 * @param   offNew              New offset (in bytes) the guest file pointer points at on success.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFileCbWriteOffset(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc, uint32_t cbWritten, int64_t offNew)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMReplyFileNotify Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_MSG_FILE_NOTIFY, 5);
    VbglHGCMParmUInt32Set(&Msg.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.type, GUEST_FILE_NOTIFYTYPE_WRITE_OFFSET);
    VbglHGCMParmUInt32Set(&Msg.rc, uRc);
    VbglHGCMParmUInt32Set(&Msg.u.WriteOffset.cb32Written, cbWritten);
    VbglHGCMParmUInt64Set(&Msg.u.WriteOffset.off64New, (uint64_t)offNew);

    return VbglR3HGCMCall(&Msg.hdr, RT_UOFFSET_AFTER(HGCMReplyFileNotify, u.WriteOffset));
}


/**
 * Replies to a HOST_MSG_FILE_SEEK message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 * @param   offCurrent          New offset (in bytes) the guest file pointer points at on success.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFileCbSeek(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc, uint64_t offCurrent)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMReplyFileNotify Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_MSG_FILE_NOTIFY, 4);
    VbglHGCMParmUInt32Set(&Msg.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.type, GUEST_FILE_NOTIFYTYPE_SEEK);
    VbglHGCMParmUInt32Set(&Msg.rc, uRc);
    VbglHGCMParmUInt64Set(&Msg.u.seek.offset, offCurrent);

    return VbglR3HGCMCall(&Msg.hdr, RT_UOFFSET_AFTER(HGCMReplyFileNotify, u.seek));
}


/**
 * Replies to a HOST_MSG_FILE_TELL message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 * @param   offCurrent          Current offset (in bytes) the guest file pointer points at on success.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFileCbTell(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc, uint64_t offCurrent)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMReplyFileNotify Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_MSG_FILE_NOTIFY, 4);
    VbglHGCMParmUInt32Set(&Msg.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.type, GUEST_FILE_NOTIFYTYPE_TELL);
    VbglHGCMParmUInt32Set(&Msg.rc, uRc);
    VbglHGCMParmUInt64Set(&Msg.u.tell.offset, offCurrent);

    return VbglR3HGCMCall(&Msg.hdr, RT_UOFFSET_AFTER(HGCMReplyFileNotify, u.tell));
}


/**
 * Replies to a HOST_MSG_FILE_SET_SIZE message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 * @param   cbNew               New file size (in bytes) of the guest file on success.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFileCbSetSize(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc, uint64_t cbNew)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMReplyFileNotify Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_MSG_FILE_NOTIFY, 4);
    VbglHGCMParmUInt32Set(&Msg.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.type, GUEST_FILE_NOTIFYTYPE_SET_SIZE);
    VbglHGCMParmUInt32Set(&Msg.rc, uRc);
    VbglHGCMParmUInt64Set(&Msg.u.SetSize.cb64Size, cbNew);

    return VbglR3HGCMCall(&Msg.hdr, RT_UOFFSET_AFTER(HGCMReplyFileNotify, u.SetSize));
}


/*********************************************************************************************************************************
 * File system callbacks                                                                                                         *
 ********************************************************************************************************************************/

#ifdef VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS
/**
 * Replies to a HOST_MSG_FS_OBJ_QUERY_INFO message, extended version.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 * @param   pFsObjInfo          Guest file system object information to send.
 * @param   pszUser             Associated user ID (owner, uid) as a string.
 * @param   pszGroups           Associated user groups as a string.
 *                              Multiple groups are delimited by GSTCTL_DIRENTRY_GROUPS_DELIMITER_STR,
 *                              whereas the first group always is the primary group.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFsObjCbQueryInfoEx(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc, PGSTCTLFSOBJINFO pFsObjInfo,
                                                  const char *pszUser, const char *pszGroups)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pFsObjInfo, VERR_INVALID_POINTER);
    AssertPtrReturn(pszUser, VERR_INVALID_POINTER);
    AssertPtrReturn(pszGroups, VERR_INVALID_POINTER);

    HGCMReplyFsNotify Msg;
    VBGL_HGCM_HDR_INIT(&Msg.reply_hdr.hdr, pCtx->uClientID, GUEST_MSG_FS_NOTIFY, GSTCTL_HGCM_REPLY_HDR_PARMS + 3);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.type, GUEST_FS_NOTIFYTYPE_QUERY_OBJ_INFO);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.rc, uRc);

    VbglHGCMParmPtrSet      (&Msg.u.queryobjinfo.obj_info, pFsObjInfo, sizeof(GSTCTLFSOBJINFO));
    VbglHGCMParmPtrSetString(&Msg.u.queryobjinfo.user,   pszUser);
    VbglHGCMParmPtrSetString(&Msg.u.queryobjinfo.groups, pszGroups);

    return VbglR3HGCMCall(&Msg.reply_hdr.hdr, RT_UOFFSET_AFTER(HGCMReplyFsNotify, u.queryobjinfo));
}


/**
 * Replies to a HOST_MSG_FS_OBJ_QUERY_INFO message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 * @param   pFsObjInfo          Guest file system object information to send.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFsObjCbQueryInfo(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc, PGSTCTLFSOBJINFO pFsObjInfo)
{
    char szIgnored[1] = { 0 };
    return VbglR3GuestCtrlFsObjCbQueryInfoEx(pCtx, uRc, pFsObjInfo, szIgnored /* pszUser */, szIgnored /* pszGroups */);
}


/**
 * Replies to a HOST_MSG_FS_CREATE_TEMP message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 * @param   pszPath             Path of created temporary file / directory, if \a uRc marks a success.
 *                              Specify an empty path on failure -- NULL is not allowed!
 */
VBGLR3DECL(int) VbglR3GuestCtrlFsCbCreateTemp(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc, const char *pszPath)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);

    HGCMReplyFsNotify Msg;
    VBGL_HGCM_HDR_INIT(&Msg.reply_hdr.hdr, pCtx->uClientID, GUEST_MSG_FS_NOTIFY, GSTCTL_HGCM_REPLY_HDR_PARMS + 1);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.type, GUEST_FS_NOTIFYTYPE_CREATE_TEMP);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.rc, uRc);

    VbglHGCMParmPtrSetString(&Msg.u.createtemp.path, pszPath);

    return VbglR3HGCMCall(&Msg.reply_hdr.hdr, RT_UOFFSET_AFTER(HGCMReplyFsNotify, u.createtemp));
}

/**
 * Replies to a HOST_MSG_FS_QUERY_INFO message.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uRc                 Guest rc of operation (note: IPRT-style signed int).
 * @param   pFsInfo             File system information to return.
 * @param   cbFsInfo            Size (in bytes) of \a pFsInfo.
 */
VBGLR3DECL(int) VbglR3GuestCtrlFsCbQueryInfo(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc, PGSTCTLFSINFO pFsInfo, uint32_t cbFsInfo)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pFsInfo, VERR_INVALID_POINTER);
    AssertReturn(cbFsInfo, VERR_INVALID_PARAMETER);

    HGCMReplyFsNotify Msg;
    VBGL_HGCM_HDR_INIT(&Msg.reply_hdr.hdr, pCtx->uClientID, GUEST_MSG_FS_NOTIFY, GSTCTL_HGCM_REPLY_HDR_PARMS + 1);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.type, GUEST_FS_NOTIFYTYPE_QUERY_INFO);
    VbglHGCMParmUInt32Set(&Msg.reply_hdr.rc, uRc);

    VbglHGCMParmPtrSet(&Msg.u.queryinfo.fs_info, pFsInfo, cbFsInfo);

    return VbglR3HGCMCall(&Msg.reply_hdr.hdr, RT_UOFFSET_AFTER(HGCMReplyFsNotify, u.queryinfo));
}
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_AS_CMDS */


/*********************************************************************************************************************************
 * Process callbacks                                                                                                             *
 ********************************************************************************************************************************/

/**
 * Callback for reporting a guest process status (along with some other stuff) to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uPID                Guest process PID to report status for.
 * @param   uStatus             Status to report. Of type PROC_STS_XXX.
 * @param   fFlags              Additional status flags, depending on the reported status. See RTPROCSTATUS.
 * @param   pvData              Pointer to additional status data. Optional.
 * @param   cbData              Size (in bytes) of additional status data.
 */
VBGLR3DECL(int) VbglR3GuestCtrlProcCbStatus(PVBGLR3GUESTCTRLCMDCTX pCtx,
                                            uint32_t uPID, uint32_t uStatus, uint32_t fFlags,
                                            void  *pvData, uint32_t cbData)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMMsgProcStatus Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_MSG_EXEC_STATUS, 5);
    VbglHGCMParmUInt32Set(&Msg.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.pid, uPID);
    VbglHGCMParmUInt32Set(&Msg.status, uStatus);
    VbglHGCMParmUInt32Set(&Msg.flags, fFlags);
    VbglHGCMParmPtrSet(&Msg.data, pvData, cbData);

    return VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
}


/**
 * Sends output (from stdout/stderr) from a running process.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uPID                Guest process PID to report status for.
 * @param   uHandle             Guest process handle the output belong to.
 * @param   fFlags              Additional output flags.
 * @param   pvData              Pointer to actual output data.
 * @param   cbData              Size (in bytes) of output data.
 */
VBGLR3DECL(int) VbglR3GuestCtrlProcCbOutput(PVBGLR3GUESTCTRLCMDCTX pCtx,
                                            uint32_t uPID,uint32_t uHandle, uint32_t fFlags,
                                            void *pvData, uint32_t cbData)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMMsgProcOutput Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_MSG_EXEC_OUTPUT, 5);
    VbglHGCMParmUInt32Set(&Msg.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.pid, uPID);
    VbglHGCMParmUInt32Set(&Msg.handle, uHandle);
    VbglHGCMParmUInt32Set(&Msg.flags, fFlags);
    VbglHGCMParmPtrSet(&Msg.data, pvData, cbData);

    return VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
}


/**
 * Callback for reporting back the input status of a guest process to the host.
 *
 * @returns VBox status code.
 * @param   pCtx                Guest control command context to use.
 * @param   uPID                Guest process PID to report status for.
 * @param   uStatus             Status to report. Of type INPUT_STS_XXX.
 * @param   fFlags              Additional input flags.
 * @param   cbWritten           Size (in bytes) of input data handled.
 */
VBGLR3DECL(int) VbglR3GuestCtrlProcCbStatusInput(PVBGLR3GUESTCTRLCMDCTX pCtx,
                                                 uint32_t uPID, uint32_t uStatus,
                                                 uint32_t fFlags, uint32_t cbWritten)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    HGCMMsgProcStatusInput Msg;
    VBGL_HGCM_HDR_INIT(&Msg.hdr, pCtx->uClientID, GUEST_MSG_EXEC_INPUT_STATUS, 5);
    VbglHGCMParmUInt32Set(&Msg.context, pCtx->uContextID);
    VbglHGCMParmUInt32Set(&Msg.pid, uPID);
    VbglHGCMParmUInt32Set(&Msg.status, uStatus);
    VbglHGCMParmUInt32Set(&Msg.flags, fFlags);
    VbglHGCMParmUInt32Set(&Msg.written, cbWritten);

    return VbglR3HGCMCall(&Msg.hdr, sizeof(Msg));
}

