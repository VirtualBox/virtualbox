/* $Id: tstMediumLock.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Medium lock test cases.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#define LOG_ENABLED
#define LOG_GROUP LOG_GROUP_MAIN
#include <VBox/log.h>

#include <VBox/com/com.h>
#include <VBox/com/ptr.h>
#include <VBox/com/defs.h>
#include <VBox/com/array.h>
#include <VBox/com/string.h>
#include <VBox/com/VirtualBox.h>

#include <iprt/assert.h>
#include <iprt/uuid.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/initterm.h>
#include <iprt/test.h>

using namespace com;


#define TEST_RT_SUCCESS(x,y,z) \
    do \
    { \
        int rc = (y); \
        if (RT_FAILURE(rc)) \
            RTTestFailed((x), "%s %Rrc", (z), rc); \
    } while (0)

#define TEST_COM_SUCCESS(x,y,z) \
    do \
    { \
        HRESULT hrc = (y); \
        if (FAILED(hrc)) \
            RTTestFailed((x), "%s %Rhrc", (z), hrc); \
    } while (0)

#define TEST_COM_FAILURE(x,y,z) \
    do \
    { \
        HRESULT hrc = (y); \
        if (SUCCEEDED(hrc)) \
            RTTestFailed((x), "%s", (z)); \
    } while (0)

int main(int argc, char *argv[])
{
    /* Init the runtime without loading the support driver. */
    RTR3InitExe(argc, &argv, 0);

    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstMediumLock", &hTest);
    if (rcExit)
        return rcExit;
    RTTestBanner(hTest);

    bool fComInit = false;
    ComPtr<IVirtualBoxClient> pVirtualBoxClient;
    ComPtr<IVirtualBox> pVirtualBox;
    char szPathTemp[RTPATH_MAX] = "";
    ComPtr<IMedium> pMedium;

    if (!RTTestSubErrorCount(hTest))
    {
        RTTestSub(hTest, "Constructing temp image name");
        TEST_RT_SUCCESS(hTest, RTPathTemp(szPathTemp, sizeof(szPathTemp)), "temp directory");
        RTUUID uuid;
        RTUuidCreate(&uuid);
        char szFile[50];
        RTStrPrintf(szFile, sizeof(szFile), "%RTuuid.vdi", &uuid);
        TEST_RT_SUCCESS(hTest, RTPathAppend(szPathTemp, sizeof(szPathTemp), szFile), "concatenate image name");
    }

    if (!RTTestSubErrorCount(hTest))
    {
        RTTestSub(hTest, "Initializing COM");
        TEST_COM_SUCCESS(hTest, Initialize(), "init");
    }

    if (!RTTestSubErrorCount(hTest))
    {
        fComInit = true;

        RTTestSub(hTest, "Getting VirtualBox reference");
        TEST_COM_SUCCESS(hTest, pVirtualBoxClient.createInprocObject(CLSID_VirtualBoxClient), "vboxclient reference");
        TEST_COM_SUCCESS(hTest, pVirtualBoxClient->COMGETTER(VirtualBox)(pVirtualBox.asOutParam()), "vbox reference");
    }

    if (!RTTestSubErrorCount(hTest))
    {
        RTTestSub(hTest, "Creating temp hard disk medium");
        TEST_COM_SUCCESS(hTest, pVirtualBox->CreateMedium(Bstr("VDI").raw(), Bstr(szPathTemp).raw(), AccessMode_ReadWrite, DeviceType_HardDisk, pMedium.asOutParam()), "create medium");
        if (!pMedium.isNull())
        {
            ComPtr<IProgress> pProgress;
            SafeArray<MediumVariant_T> variant;
            variant.push_back(MediumVariant_Standard);
            TEST_COM_SUCCESS(hTest, pMedium->CreateBaseStorage(_1M, ComSafeArrayAsInParam(variant), pProgress.asOutParam()), "create base storage");
            if (!pProgress.isNull())
                TEST_COM_SUCCESS(hTest, pProgress->WaitForCompletion(30000), "waiting for completion of create");
        }
    }

    if (!RTTestSubErrorCount(hTest))
    {
        RTTestSub(hTest, "Write locks");
        ComPtr<IToken> pToken1, pToken2;

        MediumState_T mediumState = MediumState_NotCreated;
        TEST_COM_SUCCESS(hTest, pMedium->COMGETTER(State)(&mediumState), "getting state");
        if (mediumState != MediumState_Created)
            RTTestFailed(hTest, "wrong medium state %d", mediumState);

        TEST_COM_SUCCESS(hTest, pMedium->LockWrite(pToken1.asOutParam()), "write lock");

        TEST_COM_SUCCESS(hTest, pMedium->COMGETTER(State)(&mediumState), "getting lock write state");
        if (mediumState != MediumState_LockedWrite)
            RTTestFailed(hTest, "wrong lock write medium state %d", mediumState);

        TEST_COM_FAILURE(hTest, pMedium->LockWrite(pToken2.asOutParam()), "nested write lock succeeded");
        if (!pToken2.isNull())
            RTTestFailed(hTest, "pToken2 is not null");

        TEST_COM_SUCCESS(hTest, pMedium->COMGETTER(State)(&mediumState), "getting after nested lock write state");
        if (mediumState != MediumState_LockedWrite)
            RTTestFailed(hTest, "wrong after nested lock write medium state %d", mediumState);

        if (!pToken1.isNull())
            TEST_COM_SUCCESS(hTest, pToken1->Abandon(), "write unlock");
        else
            RTTestFailed(hTest, "pToken1 is null");

        TEST_COM_SUCCESS(hTest, pMedium->COMGETTER(State)(&mediumState), "getting unlock write state");
        if (mediumState != MediumState_Created)
            RTTestFailed(hTest, "wrong unlock write medium state %d", mediumState);
    }

    if (!RTTestSubErrorCount(hTest))
    {
        RTTestSub(hTest, "Read locks");
        ComPtr<IToken> pToken1, pToken2;

        MediumState_T mediumState = MediumState_NotCreated;
        TEST_COM_SUCCESS(hTest, pMedium->COMGETTER(State)(&mediumState), "getting state");
        if (mediumState != MediumState_Created)
            RTTestFailed(hTest, "wrong medium state %d", mediumState);

        TEST_COM_SUCCESS(hTest, pMedium->LockRead(pToken1.asOutParam()), "read lock");

        TEST_COM_SUCCESS(hTest, pMedium->COMGETTER(State)(&mediumState), "getting lock read state");
        if (mediumState != MediumState_LockedRead)
            RTTestFailed(hTest, "wrong lock read medium state %d", mediumState);

        TEST_COM_SUCCESS(hTest, pMedium->LockRead(pToken2.asOutParam()), "nested read lock failed");

        TEST_COM_SUCCESS(hTest, pMedium->COMGETTER(State)(&mediumState), "getting after nested lock read state");
        if (mediumState != MediumState_LockedRead)
            RTTestFailed(hTest, "wrong after nested lock read medium state %d", mediumState);

        if (!pToken2.isNull())
            TEST_COM_SUCCESS(hTest, pToken2->Abandon(), "read nested unlock");
        else
            RTTestFailed(hTest, "pToken2 is null");

        TEST_COM_SUCCESS(hTest, pMedium->COMGETTER(State)(&mediumState), "getting after nested lock read state");
        if (mediumState != MediumState_LockedRead)
            RTTestFailed(hTest, "wrong after nested lock read medium state %d", mediumState);

        if (!pToken1.isNull())
            TEST_COM_SUCCESS(hTest, pToken1->Abandon(), "read nested unlock");
        else
            RTTestFailed(hTest, "pToken1 is null");

        TEST_COM_SUCCESS(hTest, pMedium->COMGETTER(State)(&mediumState), "getting unlock read state");
        if (mediumState != MediumState_Created)
            RTTestFailed(hTest, "wrong unlock read medium state %d", mediumState);
    }

    if (!RTTestSubErrorCount(hTest))
    {
        RTTestSub(hTest, "Mixing write and read locks");
        ComPtr<IToken> pToken1, pToken2;

        MediumState_T mediumState = MediumState_NotCreated;
        TEST_COM_SUCCESS(hTest, pMedium->COMGETTER(State)(&mediumState), "getting state");
        if (mediumState != MediumState_Created)
            RTTestFailed(hTest, "wrong medium state %d", mediumState);

        TEST_COM_SUCCESS(hTest, pMedium->LockWrite(pToken1.asOutParam()), "write lock");

        TEST_COM_SUCCESS(hTest, pMedium->COMGETTER(State)(&mediumState), "getting lock write state");
        if (mediumState != MediumState_LockedWrite)
            RTTestFailed(hTest, "wrong lock write medium state %d", mediumState);

        TEST_COM_FAILURE(hTest, pMedium->LockRead(pToken2.asOutParam()), "write+read lock succeeded");
        if (!pToken2.isNull())
            RTTestFailed(hTest, "pToken2 is not null");

        TEST_COM_SUCCESS(hTest, pMedium->COMGETTER(State)(&mediumState), "getting after nested lock write state");
        if (mediumState != MediumState_LockedWrite)
            RTTestFailed(hTest, "wrong after nested lock write medium state %d", mediumState);

        if (!pToken1.isNull())
            TEST_COM_SUCCESS(hTest, pToken1->Abandon(), "write unlock");
        else
            RTTestFailed(hTest, "pToken1 is null");

        TEST_COM_SUCCESS(hTest, pMedium->COMGETTER(State)(&mediumState), "getting unlock write state");
        if (mediumState != MediumState_Created)
            RTTestFailed(hTest, "wrong unlock write medium state %d", mediumState);
    }

    if (!RTTestSubErrorCount(hTest))
    {
        RTTestSub(hTest, "Mixing read and write locks");
        ComPtr<IToken> pToken1, pToken2;

        MediumState_T mediumState = MediumState_NotCreated;
        TEST_COM_SUCCESS(hTest, pMedium->COMGETTER(State)(&mediumState), "getting state");
        if (mediumState != MediumState_Created)
            RTTestFailed(hTest, "wrong medium state %d", mediumState);

        TEST_COM_SUCCESS(hTest, pMedium->LockRead(pToken1.asOutParam()), "read lock");

        TEST_COM_SUCCESS(hTest, pMedium->COMGETTER(State)(&mediumState), "getting lock read state");
        if (mediumState != MediumState_LockedRead)
            RTTestFailed(hTest, "wrong lock read medium state %d", mediumState);

        TEST_COM_FAILURE(hTest, pMedium->LockWrite(pToken2.asOutParam()), "read+write lock succeeded");
        if (!pToken2.isNull())
            RTTestFailed(hTest, "pToken2 is not null");

        TEST_COM_SUCCESS(hTest, pMedium->COMGETTER(State)(&mediumState), "getting after nested lock read state");
        if (mediumState != MediumState_LockedRead)
            RTTestFailed(hTest, "wrong after nested lock read medium state %d", mediumState);

        if (!pToken1.isNull())
            TEST_COM_SUCCESS(hTest, pToken1->Abandon(), "read unlock");
        else
            RTTestFailed(hTest, "pToken1 is null");

        TEST_COM_SUCCESS(hTest, pMedium->COMGETTER(State)(&mediumState), "getting unlock read state");
        if (mediumState != MediumState_Created)
            RTTestFailed(hTest, "wrong unlock read medium state %d", mediumState);
    }

    /* Cleanup, also part of the testcase */

    if (!pMedium.isNull())
    {
        RTTestSub(hTest, "Closing medium");
        MediumState_T mediumState = MediumState_NotCreated;
        TEST_COM_SUCCESS(hTest, pMedium->COMGETTER(State)(&mediumState), "getting state");
        if (mediumState == MediumState_Created)
        {
            ComPtr<IProgress> pProgress;
            TEST_COM_SUCCESS(hTest, pMedium->DeleteStorage(pProgress.asOutParam()), "deleting storage");
            if (!pProgress.isNull())
                TEST_COM_SUCCESS(hTest, pProgress->WaitForCompletion(30000), "waiting for completion of delete");
        }
        TEST_COM_SUCCESS(hTest, pMedium->Close(), "closing");
        pMedium.setNull();
    }

    pVirtualBox.setNull();
    pVirtualBoxClient.setNull();

    /* Make sure that there are no object references alive here, XPCOM does
     * a very bad job at cleaning up such leftovers, spitting out warning
     * messages in a debug build. */

    if (fComInit)
    {
        RTTestIPrintf(RTTESTLVL_DEBUG, "Shutting down COM...\n");
        Shutdown();
    }

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}
