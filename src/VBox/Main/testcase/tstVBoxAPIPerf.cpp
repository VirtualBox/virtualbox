/* $Id: tstVBoxAPIPerf.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * tstVBoxAPIPerf - Checks the performance of the COM / XPOM API.
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
#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/array.h>
#include <VBox/com/Guid.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/VirtualBox.h>
#include <VBox/sup.h>

#include <iprt/test.h>
#include <iprt/time.h>



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest;


/** Worker fro TST_COM_EXPR(). */
static HRESULT tstComExpr(HRESULT hrc, const char *pszOperation, int iLine)
{
    if (FAILED(hrc))
        RTTestFailed(g_hTest, "%s failed on line %u with hrc=%Rhrc", pszOperation, iLine, hrc);
    return hrc;
}

/** Macro that executes the given expression and report any failure.
 *  The expression must return a HRESULT. */
#define TST_COM_EXPR(expr) tstComExpr(expr, #expr, __LINE__)



static void tstApiPrf1(IVirtualBox *pVBox)
{
    RTTestSub(g_hTest, "IVirtualBox::Revision performance");

    uint32_t const cCalls   = 65536;
    uint32_t       cLeft    = cCalls;
    uint64_t       uStartTS = RTTimeNanoTS();
    while (cLeft-- > 0)
    {
        ULONG uRev;
        HRESULT hrc = pVBox->COMGETTER(Revision)(&uRev);
        if (FAILED(hrc))
        {
            tstComExpr(hrc, "IVirtualBox::Revision", __LINE__);
            return;
        }
    }
    uint64_t uElapsed = RTTimeNanoTS() - uStartTS;
    RTTestValue(g_hTest, "IVirtualBox::Revision average", uElapsed / cCalls, RTTESTUNIT_NS_PER_CALL);
    RTTestSubDone(g_hTest);
}


static void tstApiPrf2(IVirtualBox *pVBox)
{
    RTTestSub(g_hTest, "IVirtualBox::Version performance");

    uint32_t const cCalls   = 65536;
    uint32_t       cLeft    = cCalls;
    uint64_t       uStartTS = RTTimeNanoTS();
    while (cLeft-- > 0)
    {
        com::Bstr bstrVersion;
        HRESULT hrc = pVBox->COMGETTER(Version)(bstrVersion.asOutParam());
        if (FAILED(hrc))
        {
            tstComExpr(hrc, "IVirtualBox::Version", __LINE__);
            return;
        }
    }
    uint64_t uElapsed = RTTimeNanoTS() - uStartTS;
    RTTestValue(g_hTest, "IVirtualBox::Version average", uElapsed / cCalls, RTTESTUNIT_NS_PER_CALL);
    RTTestSubDone(g_hTest);
}


static void tstApiPrf3(IVirtualBox *pVBox)
{
    RTTestSub(g_hTest, "IVirtualBox::Host performance");

    /* The first call. */
    uint64_t    uStartTS = RTTimeNanoTS();
    IHost      *pHost = NULL;
    HRESULT     hrc = pVBox->COMGETTER(Host)(&pHost);
    if (FAILED(hrc))
    {
        tstComExpr(hrc, "IVirtualBox::Host", __LINE__);
        return;
    }
    pHost->Release();
    uint64_t uElapsed = RTTimeNanoTS() - uStartTS;
    RTTestValue(g_hTest, "IVirtualBox::Host first", uElapsed, RTTESTUNIT_NS);

    /* Subsequent calls. */
    uint32_t const cCalls1  = 4096;
    uint32_t       cLeft    = cCalls1;
    uStartTS = RTTimeNanoTS();
    while (cLeft-- > 0)
    {
        IHost *pHost2 = NULL;
        hrc = pVBox->COMGETTER(Host)(&pHost2);
        if (FAILED(hrc))
        {
            tstComExpr(hrc, "IVirtualBox::Host", __LINE__);
            return;
        }
        pHost2->Release();
    }
    uElapsed = RTTimeNanoTS() - uStartTS;
    RTTestValue(g_hTest, "IVirtualBox::Host average", uElapsed / cCalls1, RTTESTUNIT_NS_PER_CALL);

    /* Keep a reference around and see how that changes things.
       Note! VBoxSVC is not creating and destroying Host().  */
    pHost = NULL;
    hrc = pVBox->COMGETTER(Host)(&pHost);

    uint32_t const cCalls2  = 16384;
    cLeft    = cCalls2;
    uStartTS = RTTimeNanoTS();
    while (cLeft-- > 0)
    {
        IHost *pHost2 = NULL;
        hrc = pVBox->COMGETTER(Host)(&pHost2);
        if (FAILED(hrc))
        {
            tstComExpr(hrc, "IVirtualBox::Host", __LINE__);
            pHost->Release();
            return;
        }
        pHost2->Release();
    }
    uElapsed = RTTimeNanoTS() - uStartTS;
    RTTestValue(g_hTest, "IVirtualBox::Host 2nd ref", uElapsed / cCalls2, RTTESTUNIT_NS_PER_CALL);
    pHost->Release();

    RTTestSubDone(g_hTest);
}


static void tstApiPrf4(IVirtualBox *pVBox)
{
    RTTestSub(g_hTest, "IHost::GetProcessorFeature performance");

    IHost      *pHost = NULL;
    HRESULT     hrc = pVBox->COMGETTER(Host)(&pHost);
    if (FAILED(hrc))
    {
        tstComExpr(hrc, "IVirtualBox::Host", __LINE__);
        return;
    }

    uint32_t const  cCalls   = 65536;
    uint32_t        cLeft    = cCalls;
    uint64_t        uStartTS = RTTimeNanoTS();
    while (cLeft-- > 0)
    {
        BOOL fSupported;
        hrc = pHost->GetProcessorFeature(ProcessorFeature_PAE, &fSupported);
        if (FAILED(hrc))
        {
            tstComExpr(hrc, "IHost::GetProcessorFeature", __LINE__);
            pHost->Release();
            return;
        }
    }
    uint64_t uElapsed = RTTimeNanoTS() - uStartTS;
    RTTestValue(g_hTest, "IHost::GetProcessorFeature average", uElapsed / cCalls, RTTESTUNIT_NS_PER_CALL);
    pHost->Release();
    RTTestSubDone(g_hTest);
}



int main()
{
    /*
     * Initialization.
     */
    RTEXITCODE rcExit = RTTestInitAndCreate("tstVBoxAPIPerf", &g_hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    SUPR3Init(NULL); /* Better time support. */
    RTTestBanner(g_hTest);

    RTTestSub(g_hTest, "Initializing COM and singletons");
    HRESULT hrc = com::Initialize();
    if (SUCCEEDED(hrc))
    {
        ComPtr<IVirtualBoxClient> ptrVBoxClient;
        ComPtr<IVirtualBox> ptrVBox;
        hrc = TST_COM_EXPR(ptrVBoxClient.createInprocObject(CLSID_VirtualBoxClient));
        if (SUCCEEDED(hrc))
            hrc = TST_COM_EXPR(ptrVBoxClient->COMGETTER(VirtualBox)(ptrVBox.asOutParam()));
        if (SUCCEEDED(hrc))
        {
            ComPtr<ISession> ptrSession;
            hrc = TST_COM_EXPR(ptrSession.createInprocObject(CLSID_Session));
            if (SUCCEEDED(hrc))
            {
                RTTestSubDone(g_hTest);

                /*
                 * Call test functions.
                 */
                tstApiPrf1(ptrVBox);
                tstApiPrf2(ptrVBox);
                tstApiPrf3(ptrVBox);

                /** @todo Find something that returns a 2nd instance of an interface and see
                 *        how if wrapper stuff is reused in any way. */
                tstApiPrf4(ptrVBox);
            }
        }

        ptrVBox.setNull();
        ptrVBoxClient.setNull();
        com::Shutdown();
    }
    else
        RTTestIFailed("com::Initialize failed with hrc=%Rhrc", hrc);
    return RTTestSummaryAndDestroy(g_hTest);
}

