/* $Id: symlink-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Symbolic Links, Windows.
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
#define LOG_GROUP RTLOGGROUP_SYMLINK
#include <iprt/win/windows.h>

#include <iprt/symlink.h>
#include "internal-r3-win.h"

#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/path.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include "internal/path.h"



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct MY_REPARSE_DATA_BUFFER
{
    ULONG           ReparseTag;
#define MY_IO_REPARSE_TAG_SYMLINK       0xa000000c
#define MY_IO_REPARSE_TAG_MOUNT_POINT   0xa0000003

    USHORT          ReparseDataLength;
    USHORT          Reserved;
    union
    {
        struct
        {
            USHORT  SubstituteNameOffset;
            USHORT  SubstituteNameLength;
            USHORT  PrintNameOffset;
            USHORT  PrintNameLength;
            ULONG   Flags;
#define MY_SYMLINK_FLAG_RELATIVE 1
            WCHAR   PathBuffer[1];
        } SymbolicLinkReparseBuffer;
        struct
        {
            USHORT  SubstituteNameOffset;
            USHORT  SubstituteNameLength;
            USHORT  PrintNameOffset;
            USHORT  PrintNameLength;
            WCHAR   PathBuffer[1];
        } MountPointReparseBuffer;
        struct
        {
            UCHAR   DataBuffer[1];
        } GenericReparseBuffer;
    };
} MY_REPARSE_DATA_BUFFER;
#define MY_FSCTL_GET_REPARSE_POINT CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 42, METHOD_BUFFERED, FILE_ANY_ACCESS)


RTDECL(bool) RTSymlinkExists(const char *pszSymlink)
{
    bool        fRc = false;
    RTFSOBJINFO ObjInfo;
    int rc = RTPathQueryInfoEx(pszSymlink, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
    if (RT_SUCCESS(rc))
        fRc = RTFS_IS_SYMLINK(ObjInfo.Attr.fMode);

    LogFlow(("RTSymlinkExists(%p={%s}): returns %RTbool\n", pszSymlink, pszSymlink, fRc));
    return fRc;
}


RTDECL(bool) RTSymlinkIsDangling(const char *pszSymlink)
{
    bool        fRc = false;
    RTFSOBJINFO ObjInfo;
    int rc = RTPathQueryInfoEx(pszSymlink, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
    if (RT_SUCCESS(rc))
    {
        fRc = RTFS_IS_SYMLINK(ObjInfo.Attr.fMode);
        if (fRc)
        {
            rc = RTPathQueryInfoEx(pszSymlink, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
            fRc = !RT_SUCCESS_NP(rc);
        }
    }

    LogFlow(("RTSymlinkIsDangling(%p={%s}): returns %RTbool\n", pszSymlink, pszSymlink, fRc));
    return fRc;
}


RTDECL(int) RTSymlinkCreate(const char *pszSymlink, const char *pszTarget, RTSYMLINKTYPE enmType, uint32_t fCreate)
{
    /*
     * Validate the input.
     */
    AssertReturn(enmType > RTSYMLINKTYPE_INVALID && enmType < RTSYMLINKTYPE_END, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszSymlink, VERR_INVALID_POINTER);
    AssertPtrReturn(pszTarget, VERR_INVALID_POINTER);
    RT_NOREF_PV(fCreate);

    /*
     * Resolve the API.
     */
    typedef BOOLEAN (WINAPI *PFNCREATESYMBOLICLINKW)(LPCWSTR, LPCWSTR, DWORD);
    static PFNCREATESYMBOLICLINKW   s_pfnCreateSymbolicLinkW = NULL;
    static bool                     s_fTried = FALSE;
    if (!s_fTried)
    {
        PFNCREATESYMBOLICLINKW pfn = (PFNCREATESYMBOLICLINKW)GetProcAddress(g_hModKernel32, "CreateSymbolicLinkW");
        if (pfn)
            s_pfnCreateSymbolicLinkW = pfn;
        s_fTried = true;
    }
    if (!s_pfnCreateSymbolicLinkW)
    {
        LogFlow(("RTSymlinkCreate(%p={%s}, %p={%s}, %d, %#x): returns VERR_NOT_SUPPORTED - Windows API not found\n",
                 pszSymlink, pszSymlink, pszTarget, pszTarget, enmType, fCreate));
        return VERR_NOT_SUPPORTED;
    }

    /*
     * Convert the paths.
     */
    PRTUTF16 pwszNativeSymlink;
    int rc = RTPathWinFromUtf8(&pwszNativeSymlink, pszSymlink, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        PRTUTF16 pwszNativeTarget;
        rc = RTPathWinFromUtf8(&pwszNativeTarget, pszTarget, 0 /*fFlags*/);
        if (RT_SUCCESS(rc))
        {
            /* The link target path must use backslashes to work reliably. */
            RTUTF16  wc;
            PRTUTF16 pwsz = pwszNativeTarget;
            while ((wc = *pwsz) != '\0')
            {
                if (wc == '/')
                    *pwsz = '\\';
                pwsz++;
            }

            /*
             * Massage the target path, determin the link type.
             */
            size_t cchTarget        = strlen(pszTarget);
            size_t cchVolSpecTarget = rtPathVolumeSpecLen(pszTarget);
#if 0 /* looks like this isn't needed after all. That makes everything much simper :-) */
            if (   cchTarget > RT_MIN(cchVolSpecTarget, 1)
                && RTPATH_IS_SLASH(pszTarget[cchTarget - 1]))
            {
                size_t cwcNativeTarget = RTUtf16Len(pwszNativeTarget);
                size_t offFromEnd = 1;
                while (   offFromEnd < cchTarget
                       && cchTarget - offFromEnd >= cchVolSpecTarget
                       && RTPATH_IS_SLASH(pszTarget[cchTarget - offFromEnd]))
                {
                    Assert(offFromEnd < cwcNativeTarget);
                    pwszNativeTarget[cwcNativeTarget - offFromEnd] = 0;
                    offFromEnd++;
                }
            }
#endif

            if (enmType == RTSYMLINKTYPE_UNKNOWN)
            {
                if (   cchTarget > cchVolSpecTarget
                    && RTPATH_IS_SLASH(pszTarget[cchTarget - 1]))
                    enmType = RTSYMLINKTYPE_DIR;
                else if (cchVolSpecTarget)
                {
                    /** @todo this is subject to sharing violations. */
                    DWORD dwAttr = GetFileAttributesW(pwszNativeTarget);
                    if (   dwAttr != INVALID_FILE_ATTRIBUTES
                        && (dwAttr & FILE_ATTRIBUTE_DIRECTORY))
                        enmType = RTSYMLINKTYPE_DIR;
                }
                else
                {
                    /** @todo Join the symlink directory with the target and
                     *        look up the attributes on that. -lazy bird. */
                }
            }

            /*
             * Create the link.
             */
            if (s_pfnCreateSymbolicLinkW(pwszNativeSymlink, pwszNativeTarget, enmType == RTSYMLINKTYPE_DIR))
                rc = VINF_SUCCESS;
            else
                rc = RTErrConvertFromWin32(GetLastError());

            RTPathWinFree(pwszNativeTarget);
        }
        RTPathWinFree(pwszNativeSymlink);
    }

    LogFlow(("RTSymlinkCreate(%p={%s}, %p={%s}, %d, %#x): returns %Rrc\n", pszSymlink, pszSymlink, pszTarget, pszTarget, enmType, fCreate, rc));
    return rc;
}


RTDECL(int) RTSymlinkDelete(const char *pszSymlink, uint32_t fDelete)
{
    RT_NOREF_PV(fDelete);

    /*
     * Convert the path.
     */
    PRTUTF16 pwszNativeSymlink;
    int rc = RTPathWinFromUtf8(&pwszNativeSymlink, pszSymlink, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        /*
         * We have to use different APIs depending on whether this is a
         * directory or file link.  This means we're subject to one more race
         * than on posix at the moment.  We could probably avoid this though,
         * if we wanted to go talk with the native API layer below Win32...
         */
        DWORD dwAttr = GetFileAttributesW(pwszNativeSymlink);
        if (dwAttr != INVALID_FILE_ATTRIBUTES)
        {
            if (dwAttr & FILE_ATTRIBUTE_REPARSE_POINT)
            {
                BOOL fRc;
                if (dwAttr & FILE_ATTRIBUTE_DIRECTORY)
                    fRc = RemoveDirectoryW(pwszNativeSymlink);
                else
                    fRc = DeleteFileW(pwszNativeSymlink);
                if (fRc)
                    rc = VINF_SUCCESS;
                else
                    rc = RTErrConvertFromWin32(GetLastError());
            }
            else
                rc = VERR_NOT_SYMLINK;
        }
        else
            rc = RTErrConvertFromWin32(GetLastError());
        RTPathWinFree(pwszNativeSymlink);
    }

    LogFlow(("RTSymlinkDelete(%p={%s}, %#x): returns %Rrc\n", pszSymlink, pszSymlink, fDelete, rc));
    return rc;
}


RTDECL(int) RTSymlinkRead(const char *pszSymlink, char *pszTarget, size_t cbTarget, uint32_t fRead)
{
    RT_NOREF_PV(fRead);

    char *pszMyTarget;
    int rc = RTSymlinkReadA(pszSymlink, &pszMyTarget);
    if (RT_SUCCESS(rc))
    {
        rc = RTStrCopy(pszTarget, cbTarget, pszMyTarget);
        RTStrFree(pszMyTarget);
    }
    LogFlow(("RTSymlinkRead(%p={%s}): returns %Rrc\n", pszSymlink, pszSymlink, rc));
    return rc;
}


RTDECL(int) RTSymlinkReadA(const char *pszSymlink, char **ppszTarget)
{
    AssertPtr(ppszTarget);
    PRTUTF16 pwszNativeSymlink;
    int rc = RTPathWinFromUtf8(&pwszNativeSymlink, pszSymlink, 0 /*fFlags*/);
    if (RT_SUCCESS(rc))
    {
        HANDLE hSymlink = CreateFileW(pwszNativeSymlink,
                                      GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      NULL,
                                      OPEN_EXISTING,
                                      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                                      NULL);
        if (hSymlink != INVALID_HANDLE_VALUE)
        {
            DWORD cbReturned = 0;
            union
            {
                MY_REPARSE_DATA_BUFFER  Buf;
                uint8_t                 abBuf[16*_1K + sizeof(WCHAR)];
            } u;
            if (DeviceIoControl(hSymlink,
                                MY_FSCTL_GET_REPARSE_POINT,
                                NULL /*pInBuffer */,
                                0 /*cbInBuffer */,
                                &u.Buf,
                                sizeof(u) - sizeof(WCHAR),
                                &cbReturned,
                                NULL /*pOverlapped*/))
            {
                if (u.Buf.ReparseTag == MY_IO_REPARSE_TAG_SYMLINK)
                {
                    PWCHAR pwszTarget = &u.Buf.SymbolicLinkReparseBuffer.PathBuffer[0];
                    pwszTarget += u.Buf.SymbolicLinkReparseBuffer.SubstituteNameOffset / 2;
                    pwszTarget[u.Buf.SymbolicLinkReparseBuffer.SubstituteNameLength / 2] = 0;
                    if (   !(u.Buf.SymbolicLinkReparseBuffer.Flags & MY_SYMLINK_FLAG_RELATIVE)
                        && pwszTarget[0] == '\\'
                        && pwszTarget[1] == '?'
                        && pwszTarget[2] == '?'
                        && pwszTarget[3] == '\\'
                        && pwszTarget[4] != 0
                       )
                        pwszTarget += 4;
                    rc = RTUtf16ToUtf8(pwszTarget, ppszTarget);
                }
                else
                    rc = VERR_NOT_SYMLINK;
            }
            else
                rc = RTErrConvertFromWin32(GetLastError());
            CloseHandle(hSymlink);
        }
        else
            rc = RTErrConvertFromWin32(GetLastError());
        RTPathWinFree(pwszNativeSymlink);
    }

    if (RT_SUCCESS(rc))
        LogFlow(("RTSymlinkReadA(%p={%s},%p): returns %Rrc *ppszTarget=%p:{%s}\n", pszSymlink, pszSymlink, ppszTarget, rc, *ppszTarget, *ppszTarget));
    else
        LogFlow(("RTSymlinkReadA(%p={%s},%p): returns %Rrc\n", pszSymlink, pszSymlink, ppszTarget, rc));
    return rc;
}

