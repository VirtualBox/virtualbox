/* $Id: VBoxLwipCore.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Lwip Core Initiatetor/Finilizer.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @todo: this should be somehow shared with with DevINIP, because
 * we want that every NAT and DevINIP instance uses a initialized LWIP
 * initialization of LWIP should happen on iLWIPInitiatorCounter 0 -> 1.
 * see pfnConstruct/Destruct.
 *
 * @note: see comment to DevINIP.cpp:DevINIPConfigured
 * @note: perhaps initilization stuff would be better move out of NAT driver,
 *  because we have to deal with attaching detaching NAT driver at runtime.
 */
#include <iprt/types.h>
#include "VBoxLwipCore.h"
/** @todo lwip or nat ? */
#define LOG_GROUP LOG_GROUP_DRV_NAT
#include <iprt/cpp/lock.h>
#include <iprt/timer.h>
#include <iprt/errcore.h>
#include <VBox/log.h>

extern "C" {
#include "lwip/opt.h"
#include "lwip/sys.h"
#include "netif/etharp.h"
#include "lwip/stats.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/tcp_impl.h"
#include "lwip/tcpip.h"
}

typedef struct LWIPCOREUSERCALLBACK
{
    PFNRT1 pfn;
    void *pvUser;
} LWIPCOREUSERCALLBACK, *PLWIPCOREUSERCALLBACK;


RTCLockMtx g_mtxLwip;

typedef struct LWIPCORE
{
    int iLWIPInitiatorCounter;
    sys_sem_t LwipTcpIpSem;
} LWIPCORE;

static LWIPCORE g_LwipCore;


/**
 * @note this function executes on TCPIP thread.
 */
static void lwipCoreUserCallback(void *pvArg) RT_NOTHROW_DEF
{
    LogFlowFunc(("ENTER: pvArg:%p\n", pvArg));

    PLWIPCOREUSERCALLBACK pUserClbk = (PLWIPCOREUSERCALLBACK)pvArg;
    if (pUserClbk != NULL && pUserClbk->pfn != NULL)
        pUserClbk->pfn(pUserClbk->pvUser);

    /* wake up caller on EMT/main */
    sys_sem_signal(&g_LwipCore.LwipTcpIpSem);
    LogFlowFuncLeave();
}


/**
 * @note this function executes on TCPIP thread.
 */
static void lwipCoreInitDone(void *pvArg) RT_NOTHROW_DEF
{
    LogFlowFunc(("ENTER: pvArg:%p\n", pvArg));

    /* ... init code goes here if need be ... */

    lwipCoreUserCallback(pvArg);
    LogFlowFuncLeave();
}


/**
 * @note this function executes on TCPIP thread.
 */
static void lwipCoreFiniDone(void *pvArg) RT_NOTHROW_DEF
{
    LogFlowFunc(("ENTER: pvArg:%p\n", pvArg));

    /* ... fini code goes here if need be ... */

    lwipCoreUserCallback(pvArg);
    LogFlowFuncLeave();
}


/**
 * This function initializes lwip core once.  Further NAT instancies
 * should just add netifs configured according their needs.
 *
 * We're on EMT-n or on the main thread of a network service, and we
 * want to execute something on the lwip tcpip thread.
 */
int vboxLwipCoreInitialize(PFNRT1 pfnCallback, void *pvCallbackArg)
{
    int rc = VINF_SUCCESS;
    int lwipRc = ERR_OK;
    LogFlowFuncEnter();

    LWIPCOREUSERCALLBACK callback;
    callback.pfn = pfnCallback;
    callback.pvUser = pvCallbackArg;

    {
        RTCLock lock(g_mtxLwip);

        if (g_LwipCore.iLWIPInitiatorCounter == 0)
        {
            lwipRc = sys_sem_new(&g_LwipCore.LwipTcpIpSem, 0);
            if (lwipRc != ERR_OK)
            {
                LogFlowFunc(("sys_sem_new error %d\n", lwipRc));
                goto done;
            }

            tcpip_init(lwipCoreInitDone, &callback);
        }
        else
        {
            lwipRc = tcpip_callback(lwipCoreUserCallback, &callback);
            if (lwipRc != ERR_OK)
            {
                LogFlowFunc(("tcpip_callback error %d\n", lwipRc));
                goto done;
            }
        }

        sys_sem_wait(&g_LwipCore.LwipTcpIpSem);
        ++g_LwipCore.iLWIPInitiatorCounter;
    }
  done:
    if (lwipRc != ERR_OK)
    {
        /** @todo map lwip error code? */
        rc = VERR_INTERNAL_ERROR;
    }
    LogFlowFuncLeaveRC(rc);
    return rc;
}


/**
 * This function decrement lwip reference counter
 * and calls tcpip thread termination function.
 */
void vboxLwipCoreFinalize(PFNRT1 pfnCallback, void *pvCallbackArg)
{
    int lwipRc = ERR_OK;
    LogFlowFuncEnter();

    LWIPCOREUSERCALLBACK callback;
    callback.pfn = pfnCallback;
    callback.pvUser = pvCallbackArg;

    {
        RTCLock lock(g_mtxLwip);

        if (g_LwipCore.iLWIPInitiatorCounter == 1)
        {
            /*
             * TCPIP_MSG_CALLBACK_TERMINATE is like a static callback,
             * but causes tcpip_thread() to return afterward.
             *
             * This should probably be hidden in a function inside
             * lwip, but for it to be static callback the semaphore
             * dance should also be done inside that function.  There
             * is tcpip_msg::sem, but it seems to be unused and may be
             * gone in future versions of lwip.
             */
            struct tcpip_msg *msg = (struct tcpip_msg *)memp_malloc(MEMP_TCPIP_MSG_API);
            if (msg)
            {
                msg->type = TCPIP_MSG_CALLBACK_TERMINATE;
                msg->msg.cb.function = lwipCoreFiniDone;
                msg->msg.cb.ctx = &callback;

                lwipRc = tcpip_callbackmsg((struct tcpip_callback_msg *)msg);
                if (lwipRc != ERR_OK)
                    LogFlowFunc(("tcpip_callback_msg error %d\n", lwipRc));
            }
            else
                LogFlowFunc(("memp_malloc no memory\n"));
        }
        else
        {
            lwipRc = tcpip_callback(lwipCoreUserCallback, &callback);
            if (lwipRc != ERR_OK)
                LogFlowFunc(("tcpip_callback error %d\n", lwipRc));
        }

        if (lwipRc == ERR_OK)
            sys_sem_wait(&g_LwipCore.LwipTcpIpSem);
    }

    LogFlowFuncLeave();
}
