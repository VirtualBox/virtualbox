/* $Id: tstPageFusion.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxService - Guest page sharing testcase
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
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/messages.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/initterm.h>
#include <VBox/VBoxGuestLib.h>
#include <iprt/x86.h>
#include <stdio.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

#ifdef RT_OS_WINDOWS
#include <iprt/win/windows.h>
#include <process.h> /* Needed for file version information. */
#include <tlhelp32.h>
#include <psapi.h>
#include <winternl.h>

#define SystemModuleInformation     11

typedef struct _RTL_PROCESS_MODULE_INFORMATION
{
    ULONG Section;
    PVOID MappedBase;
    PVOID ImageBase;
    ULONG ImageSize;
    ULONG Flags;
    USHORT LoadOrderIndex;
    USHORT InitOrderIndex;
    USHORT LoadCount;
    USHORT OffsetToFileName;
    CHAR FullPathName[256];
} RTL_PROCESS_MODULE_INFORMATION, *PRTL_PROCESS_MODULE_INFORMATION;

typedef struct _RTL_PROCESS_MODULES
{
    ULONG NumberOfModules;
    RTL_PROCESS_MODULE_INFORMATION Modules[1];
} RTL_PROCESS_MODULES, *PRTL_PROCESS_MODULES;

typedef NTSTATUS (WINAPI *PFNZWQUERYSYSTEMINFORMATION)(ULONG, PVOID, ULONG, PULONG);
static PFNZWQUERYSYSTEMINFORMATION ZwQuerySystemInformation = NULL;
static HMODULE hNtdll = 0;

#define PAGE_STATE_INVALID           0
#define PAGE_STATE_SHARED            1
#define PAGE_STATE_READ_WRITE        2
#define PAGE_STATE_READ_ONLY         3
#define PAGE_STATE_NOT_PRESENT       4

/* Page counters. */
static unsigned cNotPresentPages = 0;
static unsigned cWritablePages   = 0;
static unsigned cSharedPages     = 0;
static unsigned cPrivatePages    = 0;

/**
 * Registers a new module with the VMM
 * @param   pModule         Module ptr
 */
void VBoxServicePageSharingCheckModule(MODULEENTRY32 *pModule)
{
    DWORD       dwModuleSize = pModule->modBaseSize;
    BYTE       *pBaseAddress = pModule->modBaseAddr;
    bool        fFirstLine = true;
    unsigned    uPageState, uLastPageState;
    bool        fLastWritable = false;
    BYTE       *pLastBaseAddress = pBaseAddress;

    uPageState = uLastPageState = PAGE_STATE_INVALID;

    printf("Check module %s base %p size %x\n", pModule->szModule, pBaseAddress, dwModuleSize);
    do
    {
        bool     fShared;
        uint64_t uPageFlags;

#ifdef RT_ARCH_X86
        int rc = VbglR3PageIsShared((uint32_t)pLastBaseAddress, &fShared, &uPageFlags);
#else
        int rc = VbglR3PageIsShared((RTGCPTR)pLastBaseAddress, &fShared, &uPageFlags);
#endif
        if (RT_FAILURE(rc))
            printf("VbglR3PageIsShared %p failed with %d\n", pLastBaseAddress, rc);

        if (RT_SUCCESS(rc))
        {
            if (uPageFlags & X86_PTE_P)
            {
                if (uPageFlags & X86_PTE_RW)
                {
                    cWritablePages++;
                    uPageState = PAGE_STATE_READ_WRITE;
                }
                else
                if (fShared)
                {
                    cSharedPages++;
                    uPageState = PAGE_STATE_SHARED;
                }
                else
                {
                    cPrivatePages++;
                    uPageState = PAGE_STATE_READ_ONLY;
                }
            }
            else
            {
                cNotPresentPages++;
                uPageState = PAGE_STATE_NOT_PRESENT;
            }

            if (    !fFirstLine
                &&  uPageState != uLastPageState)
            {
                printf("0x%p\n", pLastBaseAddress + 0xfff);
            }

            if (uPageState != uLastPageState)
            {
                switch (uPageState)
                {
                case PAGE_STATE_READ_WRITE:
                    printf("%s RW     0x%p - ", pModule->szModule, pBaseAddress);
                    break;
                case PAGE_STATE_SHARED:
                    printf("%s SHARED 0x%p - ", pModule->szModule, pBaseAddress);
                    break;
                case PAGE_STATE_READ_ONLY:
                    printf("%s PRIV   0x%p - ", pModule->szModule, pBaseAddress);
                    break;
                case PAGE_STATE_NOT_PRESENT:
                    printf("%s NP     0x%p - ", pModule->szModule, pBaseAddress);
                    break;
                }

                fFirstLine       = false;
            }
            uLastPageState = uPageState;
        }
        else
        if (!fFirstLine)
        {
            printf("0x%p\n", pLastBaseAddress + 0xfff);
            fFirstLine = true;
        }

        if (dwModuleSize > PAGE_SIZE)
            dwModuleSize -= PAGE_SIZE;
        else
            dwModuleSize = 0;

        pLastBaseAddress  = pBaseAddress;
        pBaseAddress     += PAGE_SIZE;
    }
    while (dwModuleSize);

    printf("0x%p\n", pLastBaseAddress + 0xfff);
    return;
}

/**
 * Inspect all loaded modules for the specified process
 * @param   dwProcessId     Process id
 */
void VBoxServicePageSharingInspectModules(DWORD dwProcessId)
{
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, dwProcessId);
    if (hSnapshot == INVALID_HANDLE_VALUE)
    {
        printf("VBoxServicePageSharingInspectModules: CreateToolhelp32Snapshot failed with %d\n", GetLastError());
        return;
    }

    printf("VBoxServicePageSharingInspectModules\n");

    MODULEENTRY32 ModuleInfo;
    BOOL          bRet;

    ModuleInfo.dwSize = sizeof(ModuleInfo);
    bRet = Module32First(hSnapshot, &ModuleInfo);
    do
    {
        /** @todo when changing this make sure VBoxService.exe is excluded! */
        char *pszDot = strrchr(ModuleInfo.szModule, '.');
        if (    pszDot
            &&  (pszDot[1] == 'e' || pszDot[1] == 'E'))
            continue;   /* ignore executables for now. */

        VBoxServicePageSharingCheckModule(&ModuleInfo);
    }
    while (Module32Next(hSnapshot, &ModuleInfo));

    CloseHandle(hSnapshot);
}

/**
 * Inspect all running processes for executables and dlls that might be worth sharing
 * with other VMs.
 *
 */
void VBoxServicePageSharingInspectGuest()
{
    VBoxServicePageSharingInspectModules(GetCurrentProcessId());

    printf("\n\nUSER RESULTS\n");
    printf("cNotPresentPages = %d\n", cNotPresentPages);
    printf("cWritablePages   = %d\n", cWritablePages);
    printf("cPrivatePages    = %d\n", cPrivatePages);
    printf("cSharedPages     = %d\n", cSharedPages);

    cNotPresentPages = 0;
    cWritablePages   = 0;
    cPrivatePages    = 0;
    cSharedPages     = 0;

    /* Check all loaded kernel modules. */
    if (ZwQuerySystemInformation)
    {
        ULONG                cbBuffer = 0;
        PVOID                pBuffer = NULL;
        PRTL_PROCESS_MODULES pSystemModules;

        NTSTATUS ret = ZwQuerySystemInformation(SystemModuleInformation, (PVOID)&cbBuffer, 0, &cbBuffer);
        if (!cbBuffer)
        {
            printf("ZwQuerySystemInformation returned length 0\n");
            goto skipkernelmodules;
        }

        pBuffer = RTMemAllocZ(cbBuffer);
        if (!pBuffer)
            goto skipkernelmodules;

        ret = ZwQuerySystemInformation(SystemModuleInformation, pBuffer, cbBuffer, &cbBuffer);
        if (ret != 0)
        {
            printf("ZwQuerySystemInformation returned %x (1)\n", ret);
            goto skipkernelmodules;
        }

        pSystemModules = (PRTL_PROCESS_MODULES)pBuffer;
        for (unsigned i = 0; i < pSystemModules->NumberOfModules; i++)
        {
            /* User-mode modules seem to have no flags set; skip them as we detected them above. */
            if (pSystemModules->Modules[i].Flags == 0)
                continue;

            /* New module; register it. */
            char          szFullFilePath[512];
            MODULEENTRY32 ModuleInfo;

            strcpy(ModuleInfo.szModule, &pSystemModules->Modules[i].FullPathName[pSystemModules->Modules[i].OffsetToFileName]);
            GetSystemDirectoryA(szFullFilePath, sizeof(szFullFilePath));

            /* skip \Systemroot\system32 */
            char *lpPath = strchr(&pSystemModules->Modules[i].FullPathName[1], '\\');
            if (!lpPath)
            {
                printf("Unexpected kernel module name %s\n", pSystemModules->Modules[i].FullPathName);
                break;
            }

            lpPath = strchr(lpPath+1, '\\');
            if (!lpPath)
            {
                printf("Unexpected kernel module name %s\n", pSystemModules->Modules[i].FullPathName);
                break;
            }

            strcat(szFullFilePath, lpPath);
            strcpy(ModuleInfo.szExePath, szFullFilePath);
            ModuleInfo.modBaseAddr = (BYTE *)pSystemModules->Modules[i].ImageBase;
            ModuleInfo.modBaseSize = pSystemModules->Modules[i].ImageSize;

            VBoxServicePageSharingCheckModule(&ModuleInfo);
        }
skipkernelmodules:
        if (pBuffer)
            RTMemFree(pBuffer);
    }
    printf("\n\nKERNEL RESULTS\n");
    printf("cNotPresentPages = %d\n", cNotPresentPages);
    printf("cWritablePages   = %d\n", cWritablePages);
    printf("cPrivatePages    = %d\n", cPrivatePages);
    printf("cSharedPages     = %d\n", cSharedPages);
}
#else
void VBoxServicePageSharingInspectGuest()
{
    /** @todo other platforms */
}
#endif


/** @copydoc VBOXSERVICE::pfnInit */
static DECLCALLBACK(int) VBoxServicePageSharingInit(void)
{
    printf("VBoxServicePageSharingInit\n");

#ifdef RT_OS_WINDOWS
    hNtdll = LoadLibrary("ntdll.dll");

    if (hNtdll)
        ZwQuerySystemInformation = (PFNZWQUERYSYSTEMINFORMATION)GetProcAddress(hNtdll, "ZwQuerySystemInformation");
#endif

    /** @todo report system name and version */
    /* Never fail here. */
    return VINF_SUCCESS;
}

static DECLCALLBACK(void) VBoxServicePageSharingTerm(void)
{
    printf("VBoxServicePageSharingTerm\n");

#ifdef RT_OS_WINDOWS
    if (hNtdll)
        FreeLibrary(hNtdll);
#endif
    return;
}

int main(int argc, char **argv)
{
    /*
     * Init globals and such.
     */
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Connect to the kernel part before daemonizing so we can fail
     * and complain if there is some kind of problem. We need to initialize
     * the guest lib *before* we do the pre-init just in case one of services
     * needs do to some initial stuff with it.
     */
    printf("Calling VbgR3Init()\n");
    rc = VbglR3Init();
    if (RT_FAILURE(rc))
    {
        printf("VbglR3Init failed with rc=%Rrc.\n", rc);
        return -1;
    }
    VBoxServicePageSharingInit();

    VBoxServicePageSharingInspectGuest();

    VBoxServicePageSharingTerm();
    return 0;
}

