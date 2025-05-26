/* $Id: vboximgMedia.cpp 169001 2025-05-26 19:52:08Z klaus.espenlaub@oracle.com $ */
/** @file
 * vboximgMedia.cpp - Disk Image Flattening FUSE Program.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#include <VirtualBox_XPCOM.h>
#include <VBox/com/VirtualBox.h>
#include <VBox/vd.h>
#include <VBox/vd-ifs.h>
#include <VBox/log.h>
#include <iprt/errcore.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/NativeEventQueue.h>
#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/array.h>
#include <VBox/com/errorprint.h>
#include <VBox/vd-plugin.h>
#include <iprt/initterm.h>
#include <iprt/assert.h>
#include <iprt/message.h>
#include <iprt/critsect.h>
#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/initterm.h>
#include <iprt/stream.h>
#include <iprt/types.h>
#include <iprt/path.h>
#include <iprt/utf16.h>
#include <math.h>
#include "vboximgOpts.h"

using namespace com;

extern VBOXIMGOPTS g_vboximgOpts;

#define SAFENULL(strPtr)   (strPtr ? strPtr : "")  /** Makes null harmless to print */
#define CSTR(arg)          Utf8Str(arg).c_str()    /** Converts XPCOM string type to C string type */
#define MAX_UUID_LEN       256                     /** Max length of a UUID */
#define VM_MAX_NAME        32                      /** Max length of VM name we handle */

typedef struct MEDIUMINFO
{
    char *pszName;
    char *pszUuid;
    char *pszBaseUuid;
    char *pszPath;
    char *pszDescription;
    char *pszState;
    char *pszType;
    char *pszFormat;
    bool fSnapshot;
    PRInt64 cbSize;
    MediumType_T type;
    MediumState_T state;
    ~MEDIUMINFO()
    {
        RTMemFree(pszName);
        RTMemFree(pszUuid);
        RTMemFree(pszPath);
        RTMemFree(pszDescription);
        RTMemFree(pszFormat);
    }
} MEDIUMINFO;


char *vboximgScaledSize(size_t size)
{
    uint64_t exp = 0;
    if (size > 0)
        exp = log2((double)size);
    static const char s_aMagnitude[] = { ' ', 'K', 'M', 'G', 'T', 'P' };
    char scaledMagnitude = s_aMagnitude[exp / 10];
     /* This workaround is because IPRT RT*Printf* funcs don't handle floating point format specifiers */
    double cbScaled = (double)size / pow(2, (double)(((uint64_t)(exp / 10)) * 10));
    uint64_t intPart = cbScaled;
    uint64_t fracPart = (cbScaled - (double)intPart) * 10;
    char tmp[256];
    RTStrPrintf(tmp, sizeof (tmp), "%d.%d%c", intPart, fracPart, scaledMagnitude);
    return RTStrDup(tmp);
}

static int getMediumInfo(IMachine *pMachine, IMedium *pMedium, MEDIUMINFO **ppMediumInfo)
{
    RT_NOREF(pMachine);

    MEDIUMINFO *info = new MEDIUMINFO();
    *ppMediumInfo = info;

    Bstr name;
    Bstr uuid;
    Bstr baseUuid;
    Bstr path;
    Bstr description;
    Bstr format;
    PRInt64 *pSize = &info->cbSize;
    ComPtr<IMedium> pBase;
    MediumType_T *pType = &info->type;
    MediumState_T *pState = &info->state;

    *pState = MediumState_NotCreated;

    HRESULT hrc;

    CHECK_ERROR(pMedium, RefreshState(pState));
    CHECK_ERROR(pMedium, COMGETTER(Id)(uuid.asOutParam()));
    CHECK_ERROR(pMedium, COMGETTER(Base)(pBase.asOutParam()));
    CHECK_ERROR(pBase,   COMGETTER(Id)(baseUuid.asOutParam()));

    CHECK_ERROR(pMedium, COMGETTER(State)(pState));

    CHECK_ERROR(pMedium, COMGETTER(Location)(path.asOutParam()));
    CHECK_ERROR(pMedium, COMGETTER(Format)(format.asOutParam()));
    CHECK_ERROR(pMedium, COMGETTER(Type)(pType));
    CHECK_ERROR(pMedium, COMGETTER(Size)(pSize));

    info->pszUuid        =  RTStrDup((char *)CSTR(uuid));
    info->pszBaseUuid    =  RTStrDup((char *)CSTR(baseUuid));
    info->pszPath        =  RTStrDup((char *)CSTR(path));
    info->pszFormat      =  RTStrDup((char *)CSTR(format));
    info->fSnapshot      =  RTStrCmp(CSTR(uuid), CSTR(baseUuid)) != 0;

    if (info->fSnapshot)
    {
        /** @todo Determine the VM snapshot this and set name and description
         *         to the snapshot name/description
         */
        CHECK_ERROR(pMedium, COMGETTER(Name)(name.asOutParam()));
        CHECK_ERROR(pMedium, COMGETTER(Description)(description.asOutParam()));
    }
    else
    {
        CHECK_ERROR(pMedium, COMGETTER(Name)(name.asOutParam()));
        CHECK_ERROR(pMedium, COMGETTER(Description)(description.asOutParam()));
    }

    info->pszName        =  RTStrDup((char *)CSTR(name));
    info->pszDescription =  RTStrDup((char *)CSTR(description));

    switch(*pType)
    {
        case MediumType_Normal:
            info->pszType = (char *)"normal";
            break;
        case MediumType_Immutable:
            info->pszType = (char *)"immutable";
            break;
        case MediumType_Writethrough:
            info->pszType = (char *)"writethrough";
            break;
        case MediumType_Shareable:
            info->pszType = (char *)"shareable";
            break;
        case MediumType_Readonly:
            info->pszType = (char *)"readonly";
            break;
        case MediumType_MultiAttach:
            info->pszType = (char *)"multiattach";
            break;
        default:
            info->pszType = (char *)"?";
    }

    switch(*pState)
    {
        case MediumState_NotCreated:
            info->pszState = (char *)"uncreated";
            break;
        case MediumState_Created:
            info->pszState = (char *)"created";
            break;
        case MediumState_LockedRead:
            info->pszState = (char *)"rlock";
            break;
        case MediumState_LockedWrite:
            info->pszState = (char *)"wlock";
            break;
        case MediumState_Inaccessible:
            info->pszState = (char *)"no access";
            break;
        case MediumState_Creating:
            info->pszState = (char *)"creating";
            break;
        case MediumState_Deleting:
            info->pszState = (char *)"deleting";
            break;
        default:
            info->pszState = (char *)"?";
    }
    return VINF_SUCCESS;
}

static void displayMediumInfo(MEDIUMINFO *pInfo, int nestLevel, bool fLast)
{
    char *pszSzScaled = vboximgScaledSize(pInfo->cbSize);
    int cPad = nestLevel * 2;
    if (g_vboximgOpts.fWide && !g_vboximgOpts.fVerbose)
    {
        RTPrintf("%3s %-*s %7s  %-9s %9s %-*s %s\n",
            !fLast ? (pInfo->fSnapshot ? " | " : " +-") : (pInfo->fSnapshot ? "   " : " +-"),
            VM_MAX_NAME, pInfo->fSnapshot ? "+- <snapshot>" : pInfo->pszName,
            pszSzScaled,
            pInfo->pszFormat,
            pInfo->pszState,
            cPad, "", pInfo->pszUuid);
    }
    else
    {
        if (!pInfo->fSnapshot)
        {
            RTPrintf("    Image:   %s\n", pInfo->pszName);
            if (pInfo->pszDescription && RTStrNLen(pInfo->pszDescription, 256) > 0)
                RTPrintf("Desc:    %s\n", pInfo->pszDescription);
            RTPrintf("    UUID:    %s\n", pInfo->pszUuid);
            if (g_vboximgOpts.fVerbose)
            {
                RTPrintf("    Path:    %s\n", pInfo->pszPath);
                RTPrintf("    Format:  %s\n", pInfo->pszFormat);
                RTPrintf("    Size:    %s\n", pszSzScaled);
                RTPrintf("    State:   %s\n", pInfo->pszState);
                RTPrintf("    Type:    %s\n", pInfo->pszType);
            }
            RTPrintf("\n");
        }
        else
        {
            RTPrintf("         Snapshot: %s\n", pInfo->pszUuid);
            if (g_vboximgOpts.fVerbose)
            {
                RTPrintf("         Name:     %s\n", pInfo->pszName);
                RTPrintf("         Desc:     %s\n", pInfo->pszDescription);
            }
            RTPrintf("         Size:     %s\n", pszSzScaled);
            if (g_vboximgOpts.fVerbose)
                RTPrintf("         Path:     %s\n", pInfo->pszPath);
            RTPrintf("\n");
        }
    }
    RTMemFree(pszSzScaled);
}

static int vboximgListBranch(IMachine *pMachine, IMedium *pMedium, uint8_t nestLevel, bool fLast)
{
    MEDIUMINFO *pMediumInfo;
    int vrc = getMediumInfo(pMachine, pMedium, &pMediumInfo);
    if (RT_FAILURE(vrc))
        return vrc;

    displayMediumInfo(pMediumInfo, nestLevel, fLast);

    HRESULT hrc;
    com::SafeIfaceArray<IMedium> pChildren;
    CHECK_ERROR_RET(pMedium, COMGETTER(Children)(ComSafeArrayAsOutParam(pChildren)), VERR_NOT_FOUND); /** @todo r=andy Find a better rc. */

    for (size_t i = 0; i < pChildren.size(); i++)
        vboximgListBranch(pMachine, pChildren[i], nestLevel + 1, fLast);

    delete pMediumInfo;

    return VINF_SUCCESS;
}

static int listMedia(IVirtualBox *pVirtualBox, IMachine *pMachine, char *vmName, char *vmUuid)
{
    RT_NOREF(pVirtualBox);
    RT_NOREF(vmName);
    RT_NOREF(vmUuid);

    int vrc = VINF_SUCCESS;

    com::SafeIfaceArray<IMediumAttachment> pMediumAttachments;

    HRESULT hrc;
    CHECK_ERROR(pMachine, COMGETTER(MediumAttachments)(ComSafeArrayAsOutParam(pMediumAttachments)));

    for (size_t i = 0; i < pMediumAttachments.size(); i++)
    {
        bool fLast = (i == pMediumAttachments.size() - 1);
        DeviceType_T deviceType;

        CHECK_ERROR(pMediumAttachments[i], COMGETTER(Type)(&deviceType));
        if (deviceType != DeviceType_HardDisk)
            continue;

        ComPtr<IMedium> pMedium;
        CHECK_ERROR(pMediumAttachments[i], COMGETTER(Medium)(pMedium.asOutParam()));

        ComPtr<IMedium> pBase;
        CHECK_ERROR(pMedium, COMGETTER(Base)(pBase.asOutParam()));
        if (g_vboximgOpts.fWide && !g_vboximgOpts.fVerbose)
            RTPrintf(" |\n");
        else
            RTPrintf("\n");

        vrc = vboximgListBranch(pMachine, pBase, 0, fLast);
        if (RT_FAILURE(vrc))
        {
            RTPrintf("vboximgListBranch failed with %Rrc\n", vrc);
            break;
        }
    }

    return vrc;
}
/**
 * Display all registered VMs on the screen with some information about each
 *
 * @param virtualBox VirtualBox instance object.
 */
int vboximgListVMs(IVirtualBox *pVirtualBox)
{
    HRESULT hrc;
    com::SafeIfaceArray<IMachine> pMachines;
    CHECK_ERROR(pVirtualBox, COMGETTER(Machines)(ComSafeArrayAsOutParam(pMachines)));

    if (g_vboximgOpts.fWide)
    {
        RTPrintf("\n");
        RTPrintf("VM  Image                             Size   Type          State  UUID (hierarchy)\n");
    }

    int vrc = VINF_SUCCESS;

    for (size_t i = 0; i < pMachines.size(); ++i)
    {
        ComPtr<IMachine> pMachine = pMachines[i];
        if (pMachine)
        {
            BOOL fAccessible;
            CHECK_ERROR(pMachines[i], COMGETTER(Accessible)(&fAccessible));
            if (fAccessible)
            {
                Bstr machineName;
                Bstr machineUuid;
                Bstr description;
                Bstr machineLocation;

                CHECK_ERROR(pMachine, COMGETTER(Name)(machineName.asOutParam()));
                CHECK_ERROR(pMachine, COMGETTER(Id)(machineUuid.asOutParam()));
                CHECK_ERROR(pMachine, COMGETTER(Description)(description.asOutParam()));
                CHECK_ERROR(pMachine, COMGETTER(SettingsFilePath)(machineLocation.asOutParam()));


                if (   g_vboximgOpts.pszVm == NULL
                    || RTStrNCmp(CSTR(machineUuid), g_vboximgOpts.pszVm, MAX_UUID_LEN) == 0
                    || RTStrNCmp((const char *)machineName.raw(), g_vboximgOpts.pszVm, MAX_UUID_LEN) == 0)
                {
                    if (g_vboximgOpts.fVerbose)
                    {
                        RTPrintf("-----------------------------------------------------------------\n");
                        RTPrintf("VM Name:   \"%s\"\n", CSTR(machineName));
                        RTPrintf("UUID:      %s\n",     CSTR(machineUuid));
                        if (*description.raw() != '\0')
                            RTPrintf("Desc:     %s\n",  CSTR(description));
                        RTPrintf("Path:      %s\n",     CSTR(machineLocation));
                    }
                    else
                    {
                        if (g_vboximgOpts.fWide)
                        {
                            RTPrintf("-----------------------------------------------------------------  "
                                 "------------------------------------\n");
                            RTPrintf("%-*s %*s %s\n", VM_MAX_NAME, CSTR(machineName), 33, "",  CSTR(machineUuid));
                        }
                        else
                        {
                            RTPrintf("-----------------------------------------------------------------\n");
                            RTPrintf("VM:   %s\n", CSTR(machineName));
                            RTPrintf("UUID: %s\n", CSTR(machineUuid));
                        }
                    }

                    int vrc2 = listMedia(pVirtualBox, pMachine,
                                         RTStrDup(CSTR(machineName)), RTStrDup(CSTR(machineUuid)));
                    if (RT_SUCCESS(vrc))
                        vrc = vrc2;

                    RTPrintf("\n");
                }
            }
        }
    }

    return vrc;
}

