/* $Id: DBGCIo.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGC - Debugger Console, I/O provider handling.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
#include <VBox/dbg.h>
#include <VBox/vmm/cfgm.h>
#include <VBox/err.h>

#include <iprt/mem.h>
#include <iprt/thread.h>
#include <VBox/log.h>
#include <iprt/assert.h>

#include <iprt/string.h>

#include "DBGCIoProvInternal.h"
#include "DBGCInternal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Stub descriptor.
 */
typedef struct DBGCSTUB
{
    /** Name of the stub. */
    const char                  *pszName;
    /** Flag whether this is an ASCII based protocol which requires some newline handling. */
    bool                        fAscii;
    /**
     * The runloop callback.
     *
     * @returns VBox status code.
     * @param   pUVM            The user mode VM handle.
     * @param   pIo             Pointer to the I/O callback table.
     * @param   fFlags          Flags for the runloop, MBZ for now.
     */
    DECLCALLBACKMEMBER(int, pfnRunloop, (PUVM pUVM, PCDBGCIO pIo, unsigned fFlags));
} DBGCSTUB;
/** Pointer to a stub descriptor. */
typedef DBGCSTUB *PDBGCSTUB;
/** Pointer to a const stub descriptor. */
typedef const DBGCSTUB *PCDBGCSTUB;


/** Pointer to the instance data of the debug console I/O. */
typedef struct DBGCIOINT *PDBGCIOINT;


/**
 * A single debug console I/O service.
 */
typedef struct DBGCIOSVC
{
    /** Pointer to the owning structure. */
    PDBGCIOINT                  pDbgcIo;
    /** The user mode VM handle this service belongs to. */
    PUVM                        pUVM;
    /** The I/O provider registration record for this service. */
    PCDBGCIOPROVREG             pIoProvReg;
    /** The I/O provider instance. */
    DBGCIOPROV                  hDbgcIoProv;
    /** The stub type. */
    PCDBGCSTUB                  pStub;
    /** The thread managing the service. */
    RTTHREAD                    hThreadSvc;
    /** Pointer to the I/O callback table currently being served. */
    PCDBGCIO                    pIo;
    /** The wrapping DBGC I/O callback table for ASCII based protocols. */
    DBGCIO                      IoAscii;
} DBGCIOSVC;
/** Pointer to a single debug console I/O service. */
typedef DBGCIOSVC *PDBGCIOSVC;
/** Poitner to a const single debug console I/O service. */
typedef const DBGCIOSVC *PCDBGCIOSVC;


/**
 * Debug console I/O instance data.
 */
typedef struct DBGCIOINT
{
    /** Number of configured I/O service instances. */
    volatile uint32_t           cSvcsCfg;
    /** Number of running I/O service instances. */
    volatile uint32_t           cSvcsRunning;
    /** Flag whether the services were asked to shut down. */
    volatile bool               fShutdown;
    /** Array of active I/O service instances. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    DBGCIOSVC                   aSvc[RT_FLEXIBLE_ARRAY];
} DBGCIOINT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/**
 * Array of supported I/O providers.
 */
static PCDBGCIOPROVREG g_aIoProv[] =
{
    &g_DbgcIoProvTcp,
    &g_DbgcIoProvUdp,
    &g_DbgcIoProvIpc
};


static DECLCALLBACK(int) dbgcIoNativeStubRunloop(PUVM pUVM, PCDBGCIO pIo, unsigned fFlags);

/**
 * Array of supported stubs.
 */
static const DBGCSTUB g_aStubs[] =
{
    /** pszName         fAscii              pfnRunloop */
    { "Native",         true,               dbgcIoNativeStubRunloop },
    { "Gdb",            false,              dbgcGdbStubRunloop      },
    { "Kd",             false,              dbgcKdStubRunloop       }
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/


/**
 * Destroys all allocated data for the given dbeugger console I/O instance.
 *
 * @param   pDbgcIo             Pointer to the dbeugger console I/O instance data.
 */
static void dbgcIoDestroy(PDBGCIOINT pDbgcIo)
{
    for (uint32_t i = 0; i < pDbgcIo->cSvcsCfg; i++)
    {
        PDBGCIOSVC pIoSvc = &pDbgcIo->aSvc[i];

        if (pIoSvc->hThreadSvc != NIL_RTTHREAD)
        {
            int rc = RTThreadWait(pIoSvc->hThreadSvc, RT_MS_10SEC, NULL /*prc*/);
            AssertRC(rc);

            pIoSvc->hThreadSvc = NIL_RTTHREAD;
            pIoSvc->pIoProvReg->pfnDestroy(pIoSvc->hDbgcIoProv);
        }
    }

    RTMemFree(pDbgcIo);
}


/**
 * Returns the number of I/O services configured.
 *
 * @returns I/O service count.
 * @param   pCfgRoot            The root of the config.
 */
static uint32_t dbgcIoGetSvcCount(PCFGMNODE pCfgRoot)
{
    uint32_t cSvcs = 0;
    PCFGMNODE pNd = CFGMR3GetFirstChild(pCfgRoot);
    while (pNd)
    {
        cSvcs++;
        pNd = CFGMR3GetNextChild(pNd);
    }

    return cSvcs;
}


/**
 * Returns a pointer to the I/O provider registration record matching the given name.
 *
 * @returns Pointer to the registration record or NULL if not found.
 * @param   pszName             The name to look for (case insensitive matching).
 */
static PCDBGCIOPROVREG dbgcIoProvFindRegByName(const char *pszName)
{
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aIoProv); i++)
    {
        if (!RTStrICmp(g_aIoProv[i]->pszName, pszName))
            return g_aIoProv[i];
    }
    return NULL;
}


/**
 * Returns a pointer to the stub record matching the given name.
 *
 * @returns Pointer to the stub record or NULL if not found.
 * @param   pszName             The name to look for (case insensitive matching).
 */
static PCDBGCSTUB dbgcIoFindStubByName(const char *pszName)
{
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aStubs); i++)
    {
        if (!RTStrICmp(g_aStubs[i].pszName, pszName))
            return &g_aStubs[i];
    }
    return NULL;
}


/**
 * Wrapper around DBGCCreate() to get it working as a callback.
 */
static DECLCALLBACK(int) dbgcIoNativeStubRunloop(PUVM pUVM, PCDBGCIO pIo, unsigned fFlags)
{
    return DBGCCreate(pUVM, pIo, fFlags);
}


/**
 * @interface_method_impl{DBGCIO,pfnDestroy}
 */
static DECLCALLBACK(void) dbgcIoAsciiDestroy(PCDBGCIO pIo)
{
    PDBGCIOSVC pIoSvc = RT_FROM_MEMBER(pIo, DBGCIOSVC, IoAscii);
    pIoSvc->pIo->pfnDestroy(pIoSvc->pIo);
}


/**
 * @interface_method_impl{DBGCIO,pfnInput}
 */
static DECLCALLBACK(bool) dbgcIoAsciiInput(PCDBGCIO pIo, uint32_t cMillies)
{
    PDBGCIOSVC pIoSvc = RT_FROM_MEMBER(pIo, DBGCIOSVC, IoAscii);
    return pIoSvc->pIo->pfnInput(pIoSvc->pIo, cMillies);
}


/**
 * @interface_method_impl{DBGCIO,pfnRead}
 */
static DECLCALLBACK(int) dbgcIoAsciiRead(PCDBGCIO pIo, void *pvBuf, size_t cbBuf, size_t *pcbRead)
{
    PDBGCIOSVC pIoSvc = RT_FROM_MEMBER(pIo, DBGCIOSVC, IoAscii);
    return pIoSvc->pIo->pfnRead(pIoSvc->pIo, pvBuf, cbBuf, pcbRead);
}


/**
 * @interface_method_impl{DBGCIO,pfnWrite}
 */
static DECLCALLBACK(int) dbgcIoAsciiWrite(PCDBGCIO pIo, const void *pvBuf, size_t cbBuf, size_t *pcbWritten)
{
    PDBGCIOSVC pIoSvc = RT_FROM_MEMBER(pIo, DBGCIOSVC, IoAscii);

    /*
     * convert '\n' to '\r\n' while writing.
     */
    int     rc = 0;
    size_t  cbLeft = cbBuf;
    while (cbLeft)
    {
        size_t  cb = cbLeft;
        /* write newlines */
        if (*(const char *)pvBuf == '\n')
        {
            rc = pIoSvc->pIo->pfnWrite(pIoSvc->pIo, "\r\n", 2, NULL);
            cb = 1;
        }
        /* write till next newline */
        else
        {
            const char *pszNL = (const char *)memchr(pvBuf, '\n', cbLeft);
            if (pszNL)
                cb = (uintptr_t)pszNL - (uintptr_t)pvBuf;
            rc = pIoSvc->pIo->pfnWrite(pIoSvc->pIo, pvBuf, cb, NULL);
        }
        if (RT_FAILURE(rc))
            break;

        /* advance */
        cbLeft -= cb;
        pvBuf = (const char *)pvBuf + cb;
    }

    /*
     * Set returned value and return.
     */
    if (pcbWritten)
        *pcbWritten = cbBuf - cbLeft;
    return rc;
}


/**
 * @interface_method_impl{DBGCIO,pfnSetReady}
 */
static DECLCALLBACK(void) dbgcIoAsciiSetReady(PCDBGCIO pIo, bool fReady)
{
    PDBGCIOSVC pIoSvc = RT_FROM_MEMBER(pIo, DBGCIOSVC, IoAscii);
    return pIoSvc->pIo->pfnSetReady(pIoSvc->pIo, fReady);
}


/**
 * The I/O thread handling the service.
 */
static DECLCALLBACK(int) dbgcIoSvcThread(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(hThreadSelf);

    int rc = VINF_SUCCESS;
    PDBGCIOSVC pIoSvc = (PDBGCIOSVC)pvUser;
    PDBGCIOINT pDbgcIo = pIoSvc->pDbgcIo;
    PCDBGCIOPROVREG pIoProvReg = pIoSvc->pIoProvReg;

    while (!ASMAtomicReadBool(&pDbgcIo->fShutdown))
    {
        /* Wait until someone connects. */
        rc = pIoProvReg->pfnWaitForConnect(pIoSvc->hDbgcIoProv, RT_INDEFINITE_WAIT, &pIoSvc->pIo);
        if (RT_SUCCESS(rc))
        {
            PCDBGCIO pIo = pIoSvc->pIo;

            if (pIoSvc->pStub->fAscii)
            {
                pIoSvc->IoAscii.pfnDestroy  = dbgcIoAsciiDestroy;
                pIoSvc->IoAscii.pfnInput    = dbgcIoAsciiInput;
                pIoSvc->IoAscii.pfnRead     = dbgcIoAsciiRead;
                pIoSvc->IoAscii.pfnWrite    = dbgcIoAsciiWrite;
                pIoSvc->IoAscii.pfnSetReady = dbgcIoAsciiSetReady;
                pIo = &pIoSvc->IoAscii;
            }

            /* call the runloop for the connection. */
            pIoSvc->pStub->pfnRunloop(pIoSvc->pUVM, pIo, 0 /*fFlags*/);

            pIo->pfnDestroy(pIo);
        }
        else if (   rc != VERR_TIMEOUT
                 && rc != VERR_INTERRUPTED)
            break;
    }

    if (!ASMAtomicDecU32(&pDbgcIo->cSvcsRunning))
        dbgcIoDestroy(pDbgcIo);

    return rc;
}


static int dbgcIoSvcInitWorker(PUVM pUVM, PDBGCIOSVC pIoSvc, PCDBGCIOPROVREG pIoProvReg,
                               PCDBGCSTUB pStub, PCFGMNODE pCfg, const char *pszName,
                               bool fIgnoreNetAddrInUse)
{
    pIoSvc->pUVM       = pUVM;
    pIoSvc->pIoProvReg = pIoProvReg;
    pIoSvc->pStub      = pStub;

    /* Create the provider instance and spawn the dedicated thread handling that service. */
    int rc = pIoProvReg->pfnCreate(&pIoSvc->hDbgcIoProv, pCfg);
    if (RT_SUCCESS(rc))
    {
        rc = RTThreadCreateF(&pIoSvc->hThreadSvc, dbgcIoSvcThread, pIoSvc, 0 /*cbStack*/,
                             RTTHREADTYPE_DEBUGGER, RTTHREADFLAGS_WAITABLE, "DbgcThrd-%s", pszName);
        if (RT_SUCCESS(rc))
        {
            ASMAtomicIncU32(&pIoSvc->pDbgcIo->cSvcsRunning);
            return VINF_SUCCESS;
        }
        else
            rc = VMR3SetError(pUVM, rc, RT_SRC_POS,
                              "Configuration error: Creating an instance of the service \"%s\" failed",
                              pszName);

        pIoProvReg->pfnDestroy(pIoSvc->hDbgcIoProv);
    }
    else if (   rc != VERR_NET_ADDRESS_IN_USE
             || !fIgnoreNetAddrInUse)
        rc = VMR3SetError(pUVM, rc, RT_SRC_POS,
                          "Configuration error: Creating an instance of the I/O provider \"%s\" failed",
                          pIoProvReg->pszName);

    return rc;
}


/**
 * Tries to initialize the given I/O service from the given config.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pIoSvc              The I/O service instance to initialize.
 * @param   pCfg                The config for the instance.
 */
static int dbgcIoSvcInit(PUVM pUVM, PDBGCIOSVC pIoSvc, PCFGMNODE pCfg)
{
    char szName[32 + 1]; RT_ZERO(szName);
    int rc = CFGMR3GetName(pCfg, &szName[0], sizeof(szName));
    if (RT_SUCCESS(rc))
    {
        char szIoProvName[32 + 1]; RT_ZERO(szIoProvName);
        rc = CFGMR3QueryString(pCfg, "Provider", &szIoProvName[0], sizeof(szIoProvName));
        if (RT_SUCCESS(rc))
        {
            char szStub[32 + 1]; RT_ZERO(szStub);
            rc = CFGMR3QueryString(pCfg, "StubType", &szStub[0], sizeof(szStub));
            if (RT_SUCCESS(rc))
            {
                PCDBGCIOPROVREG pIoProvReg = dbgcIoProvFindRegByName(szIoProvName);
                if (pIoProvReg)
                {
                    PCDBGCSTUB pStub = dbgcIoFindStubByName(szStub);
                    if (pStub)
                        rc = dbgcIoSvcInitWorker(pUVM, pIoSvc, pIoProvReg, pStub, pCfg, szName,
                                                 false /*fIgnoreNetAddrInUse*/);
                    else
                        rc = VMR3SetError(pUVM, VERR_NOT_FOUND, RT_SRC_POS, "Configuration error: The stub type \"%s\" could not be found",
                                          szStub);
                }
                else
                    rc = VMR3SetError(pUVM, VERR_NOT_FOUND, RT_SRC_POS, "Configuration error: The provider \"%s\" could not be found",
                                      szIoProvName);
            }
            else
                rc = VM_SET_ERROR_U(pUVM, rc, "Configuration error: Querying \"StubType\" failed");
        }
        else
            rc = VM_SET_ERROR_U(pUVM, rc, "Configuration error: Querying \"Provider\" failed");
    }
    else
        rc = VM_SET_ERROR_U(pUVM, rc, "Configuration error: Querying service identifier failed (maybe too long)");

    return rc;
}


/**
 * Creates the DBGC I/O services from the legacy TCP config.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pKey                The config key.
 * @param   ppvData             Where to store the I/o instance data on success.
 */
static int dbgcIoCreateLegacyTcp(PUVM pUVM, PCFGMNODE pKey, void **ppvData)
{
    bool fEnabled;
    int rc = CFGMR3QueryBoolDef(pKey, "Enabled", &fEnabled,
#if defined(VBOX_WITH_DEBUGGER) && defined(VBOX_WITH_DEBUGGER_TCP_BY_DEFAULT)
        true
#else
        false
#endif
        );
    if (RT_FAILURE(rc))
        return VM_SET_ERROR_U(pUVM, rc, "Configuration error: Failed querying \"DBGC/Enabled\"");

    if (!fEnabled)
    {
        LogFlow(("DBGCTcpCreate: returns VINF_SUCCESS (Disabled)\n"));
        return VINF_SUCCESS;
    }

    PDBGCIOINT pDbgcIo = (PDBGCIOINT)RTMemAllocZ(RT_UOFFSETOF_DYN(DBGCIOINT, aSvc[1]));
    if (RT_LIKELY(pDbgcIo))
    {
        pDbgcIo->aSvc[0].pDbgcIo = pDbgcIo;
        pDbgcIo->cSvcsCfg        = 1;
        pDbgcIo->cSvcsRunning    = 1;
        rc = dbgcIoSvcInitWorker(pUVM, &pDbgcIo->aSvc[0], &g_DbgcIoProvTcp, &g_aStubs[0], pKey, "TCP",
                                 true /*fIgnoreNetAddrInUse*/);
        if (RT_SUCCESS(rc))
        {
            *ppvData = pDbgcIo;
            return VINF_SUCCESS;
        }

        RTMemFree(pDbgcIo);
        if (rc == VERR_NET_ADDRESS_IN_USE)
            rc = VINF_SUCCESS;
    }
    else
        rc = VERR_NO_MEMORY;

    if (RT_FAILURE(rc))
        rc = VM_SET_ERROR_U(pUVM, rc, "Cannot start TCP-based debugging console service");
    return rc;
}


/**
 * Sets up debugger I/O based on the VM config.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   ppvData     Where to store a pointer to the instance data.
 */
DBGDECL(int) DBGCIoCreate(PUVM pUVM, void **ppvData)
{
    /*
     * Check what the configuration says.
     */
    PCFGMNODE pKey = CFGMR3GetChild(CFGMR3GetRootU(pUVM), "DBGC");
    uint32_t cSvcs = dbgcIoGetSvcCount(pKey);
    int rc = VINF_SUCCESS;

    /* If no services are configured try the legacy config supporting TCP only. */
    if (cSvcs)
    {
        PDBGCIOINT pDbgcIo = (PDBGCIOINT)RTMemAllocZ(RT_UOFFSETOF_DYN(DBGCIOINT, aSvc[cSvcs]));
        if (RT_LIKELY(pDbgcIo))
        {
            pDbgcIo->cSvcsCfg     = 0;
            pDbgcIo->cSvcsRunning = 1;
            pDbgcIo->fShutdown    = false;

            for (uint32_t i = 0; i < cSvcs; i++)
                pDbgcIo->aSvc[i].hThreadSvc = NIL_RTTHREAD;

            PCFGMNODE pSvcCfg = CFGMR3GetFirstChild(pKey);
            for (uint32_t i = 0; i < cSvcs && RT_SUCCESS(rc); i++)
            {
                pDbgcIo->aSvc[i].pDbgcIo = pDbgcIo;

                rc = dbgcIoSvcInit(pUVM, &pDbgcIo->aSvc[i], pSvcCfg);
                if (RT_SUCCESS(rc))
                    pDbgcIo->cSvcsCfg++;
                else
                    rc = VM_SET_ERROR_U(pUVM, rc, "Failed to initialize the debugger I/O service");

                pSvcCfg = CFGMR3GetNextChild(pSvcCfg);
            }

            if (RT_SUCCESS(rc))
                *ppvData = pDbgcIo;
            else
            {
                if (!ASMAtomicDecU32(&pDbgcIo->cSvcsRunning))
                    dbgcIoDestroy(pDbgcIo);
            }
        }
        else
            rc = VM_SET_ERROR_U(pUVM, VERR_NO_MEMORY, "Failed to allocate memory for the debugger I/O service");
    }
    else
        rc = dbgcIoCreateLegacyTcp(pUVM, pKey, ppvData);

    return rc;
}


/**
 * Terminates any running debugger services.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   pvData          The data returned by DBGCIoCreate.
 */
DBGDECL(int) DBGCIoTerminate(PUVM pUVM, void *pvData)
{
    RT_NOREF(pUVM);
    PDBGCIOINT pDbgcIo = (PDBGCIOINT)pvData;

    if (pDbgcIo)
    {
        ASMAtomicXchgBool(&pDbgcIo->fShutdown, true);

        for (uint32_t i = 0; i < pDbgcIo->cSvcsCfg; i++)
        {
            PDBGCIOSVC pIoSvc = &pDbgcIo->aSvc[i];

            if (pIoSvc->hThreadSvc != NIL_RTTHREAD)
                pIoSvc->pIoProvReg->pfnWaitInterrupt(pIoSvc->hDbgcIoProv);
        }

        if (!ASMAtomicDecU32(&pDbgcIo->cSvcsRunning))
            dbgcIoDestroy(pDbgcIo);
    }

    return VINF_SUCCESS;
}

