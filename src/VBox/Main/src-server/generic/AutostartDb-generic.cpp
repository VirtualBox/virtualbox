/* $Id: AutostartDb-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Autostart implementation.
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

#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/process.h>
#include <iprt/path.h>
#include <iprt/mem.h>
#include <iprt/file.h>
#include <iprt/string.h>

#include "AutostartDb.h"

#if defined(RT_OS_LINUX)
/**
 * Modifies the autostart database.
 *
 * @returns VBox status code.
 * @param   fAutostart    Flag whether the autostart or autostop database is modified.
 * @param   fAddVM        Flag whether a VM is added or removed from the database.
 */
int AutostartDb::autostartModifyDb(bool fAutostart, bool fAddVM)
{
    int vrc = VINF_SUCCESS;
    char *pszUser = NULL;

    /* Check if the path is set. */
    if (!m_pszAutostartDbPath)
        return VERR_PATH_NOT_FOUND;

    vrc = RTProcQueryUsernameA(RTProcSelf(), &pszUser);
    if (RT_SUCCESS(vrc))
    {
        char *pszFile;
        uint64_t fOpen = RTFILE_O_DENY_ALL | RTFILE_O_READWRITE;
        RTFILE hAutostartFile;

        AssertPtr(pszUser);

        if (fAddVM)
            fOpen |= RTFILE_O_OPEN_CREATE;
        else
            fOpen |= RTFILE_O_OPEN;

        vrc = RTStrAPrintf(&pszFile, "%s/%s.%s", m_pszAutostartDbPath, pszUser, fAutostart ? "start" : "stop");
        if (RT_SUCCESS(vrc))
        {
            vrc = RTFileOpen(&hAutostartFile, pszFile, fOpen);
            if (RT_SUCCESS(vrc))
            {
                uint64_t cbFile;

                /*
                 * Files with more than 16 bytes are rejected because they just contain
                 * a number of the amount of VMs with autostart configured, so they
                 * should be really really small. Anything else is bogus.
                 */
                vrc = RTFileQuerySize(hAutostartFile, &cbFile);
                if (   RT_SUCCESS(vrc)
                    && cbFile <= 16)
                {
                    char abBuf[16 + 1]; /* trailing \0 */
                    uint32_t cAutostartVms = 0;

                    RT_ZERO(abBuf);

                    /* Check if the file was just created. */
                    if (cbFile)
                    {
                        vrc = RTFileRead(hAutostartFile, abBuf, (size_t)cbFile, NULL);
                        if (RT_SUCCESS(vrc))
                        {
                            vrc = RTStrToUInt32Ex(abBuf, NULL, 10 /* uBase */, &cAutostartVms);
                            if (   vrc == VWRN_TRAILING_CHARS
                                || vrc == VWRN_TRAILING_SPACES)
                                vrc = VINF_SUCCESS;
                        }
                    }

                    if (RT_SUCCESS(vrc))
                    {
                        size_t cbBuf;

                        /* Modify VM counter and write back. */
                        if (fAddVM)
                            cAutostartVms++;
                        else
                            cAutostartVms--;

                        if (cAutostartVms > 0)
                        {
                            cbBuf = RTStrPrintf(abBuf, sizeof(abBuf), "%u", cAutostartVms);
                            vrc = RTFileSetSize(hAutostartFile, cbBuf);
                            if (RT_SUCCESS(vrc))
                                vrc = RTFileWriteAt(hAutostartFile, 0, abBuf, cbBuf, NULL);
                        }
                        else
                        {
                            /* Just delete the file if there are no VMs left. */
                            RTFileClose(hAutostartFile);
                            RTFileDelete(pszFile);
                            hAutostartFile = NIL_RTFILE;
                        }
                    }
                }
                else if (RT_SUCCESS(vrc))
                    vrc = VERR_FILE_TOO_BIG;

                if (hAutostartFile != NIL_RTFILE)
                    RTFileClose(hAutostartFile);
            }
            RTStrFree(pszFile);
        }

        RTStrFree(pszUser);
    }

    return vrc;
}

#endif

AutostartDb::AutostartDb()
{
#ifdef RT_OS_LINUX
    int vrc = RTCritSectInit(&this->CritSect);
    NOREF(vrc);
    m_pszAutostartDbPath = NULL;
#endif
}

AutostartDb::~AutostartDb()
{
#ifdef RT_OS_LINUX
    RTCritSectDelete(&this->CritSect);
    if (m_pszAutostartDbPath)
        RTStrFree(m_pszAutostartDbPath);
#endif
}

int AutostartDb::setAutostartDbPath(const char *pszAutostartDbPathNew)
{
#if defined(RT_OS_LINUX)
    char *pszAutostartDbPathTmp = NULL;

    if (pszAutostartDbPathNew)
    {
        pszAutostartDbPathTmp = RTStrDup(pszAutostartDbPathNew);
        if (!pszAutostartDbPathTmp)
            return VERR_NO_MEMORY;
    }

    RTCritSectEnter(&this->CritSect);
    if (m_pszAutostartDbPath)
        RTStrFree(m_pszAutostartDbPath);

    m_pszAutostartDbPath = pszAutostartDbPathTmp;
    RTCritSectLeave(&this->CritSect);
    return VINF_SUCCESS;
#else
    NOREF(pszAutostartDbPathNew);
    return VERR_NOT_SUPPORTED;
#endif
}

int AutostartDb::addAutostartVM(const char *pszVMId)
{
    int vrc = VERR_NOT_SUPPORTED;

#if defined(RT_OS_LINUX)
    NOREF(pszVMId); /* Not needed */

    RTCritSectEnter(&this->CritSect);
    vrc = autostartModifyDb(true /* fAutostart */, true /* fAddVM */);
    RTCritSectLeave(&this->CritSect);
#elif defined(RT_OS_DARWIN) || defined(RT_OS_SOLARIS) || defined(RT_OS_WINDOWS)
    NOREF(pszVMId); /* Not needed */
    vrc = VINF_SUCCESS;
#else
    NOREF(pszVMId);
    vrc = VERR_NOT_SUPPORTED;
#endif

    return vrc;
}

int AutostartDb::removeAutostartVM(const char *pszVMId)
{
    int vrc = VINF_SUCCESS;

#if defined(RT_OS_LINUX)
    NOREF(pszVMId); /* Not needed */
    RTCritSectEnter(&this->CritSect);
    vrc = autostartModifyDb(true /* fAutostart */, false /* fAddVM */);
    RTCritSectLeave(&this->CritSect);
#elif defined(RT_OS_DARWIN) || defined(RT_OS_SOLARIS) || defined(RT_OS_WINDOWS)
    NOREF(pszVMId); /* Not needed */
    vrc = VINF_SUCCESS;
#else
    NOREF(pszVMId);
    vrc = VERR_NOT_SUPPORTED;
#endif

    return vrc;
}

int AutostartDb::addAutostopVM(const char *pszVMId)
{
    int vrc = VINF_SUCCESS;

#if defined(RT_OS_LINUX)
    NOREF(pszVMId); /* Not needed */
    RTCritSectEnter(&this->CritSect);
    vrc = autostartModifyDb(false /* fAutostart */, true /* fAddVM */);
    RTCritSectLeave(&this->CritSect);
#elif defined(RT_OS_DARWIN) || defined(RT_OS_WINDOWS)
    NOREF(pszVMId); /* Not needed */
    vrc = VINF_SUCCESS;
#else
    NOREF(pszVMId);
    vrc = VERR_NOT_SUPPORTED;
#endif

    return vrc;
}

int AutostartDb::removeAutostopVM(const char *pszVMId)
{
    int vrc = VINF_SUCCESS;

#if defined(RT_OS_LINUX)
    NOREF(pszVMId); /* Not needed */
    RTCritSectEnter(&this->CritSect);
    vrc = autostartModifyDb(false /* fAutostart */, false /* fAddVM */);
    RTCritSectLeave(&this->CritSect);
#elif defined(RT_OS_DARWIN) || defined (RT_OS_WINDOWS)
    NOREF(pszVMId); /* Not needed */
    vrc = VINF_SUCCESS;
#else
    NOREF(pszVMId);
    vrc = VERR_NOT_SUPPORTED;
#endif

    return vrc;
}

