/* $Id: hardenedmain.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Hardened main().
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#include <VBox/sup.h>


/**
 * No CRT on windows, so cook our own strcmp.
 *
 * @returns See man strcmp.
 * @param   psz1                The first string.
 * @param   psz2                The second string.
 */
static int MyStrCmp(const char *psz1, const char *psz2)
{
    for (;;)
    {
        char ch1 = *psz1++;
        char ch2 = *psz2++;
        if (ch1 != ch2)
            return ch1 < ch2 ? -1 : 1;
        if (!ch1)
            return 0;
    }
}


int main(int argc, char **argv, char **envp)
{
    /*
     * Do partial option parsing to see if we're starting a VM and how we're
     * going about that.
     *
     * Note! This must must match the corresponding parsing in main.cpp and
     *       UICommon.cpp exactly, otherwise there will be weird error messages.
     *
     * Note! ASSUMES that argv is in an ASCII compatible codeset.
     */
    unsigned cOptionsLeft     = 4;
    bool     fStartVM         = false;
    bool     fSeparateProcess = false;
    bool     fExecuteAllInIem = false;
    bool     fDriverless      = false;
    for (int i = 1; i < argc && cOptionsLeft > 0; ++i)
    {
        if (   !MyStrCmp(argv[i], "--startvm")
            || !MyStrCmp(argv[i], "-startvm"))
        {
            cOptionsLeft -= fStartVM == false;
            fStartVM = true;
            i++;
        }
        else if (   !MyStrCmp(argv[i], "--separate")
                 || !MyStrCmp(argv[i], "-separate"))
        {
            cOptionsLeft -= fSeparateProcess == false;
            fSeparateProcess = true;
        }
        else if (!MyStrCmp(argv[i], "--execute-all-in-iem"))
        {
            cOptionsLeft -= fExecuteAllInIem == false;
            fExecuteAllInIem = true;
        }
        else if (!MyStrCmp(argv[i], "--driverless"))
        {
            cOptionsLeft -= fDriverless == false;
            fDriverless = true;
        }
    }

    /*
     * Convert the command line options to SUPSECMAIN_FLAGS_XXX flags
     * and call the hardened main code.
     */
    uint32_t fFlags = SUPSECMAIN_FLAGS_TRUSTED_ERROR;
#ifdef RT_OS_DARWIN
    fFlags |= SUPSECMAIN_FLAGS_LOC_OSX_HLP_APP;
#endif
    if (!fStartVM || fSeparateProcess)
        fFlags |= SUPSECMAIN_FLAGS_DONT_OPEN_DEV;
    else
    {
        if (fExecuteAllInIem)
            fFlags |= SUPSECMAIN_FLAGS_DRIVERLESS_IEM_ALLOWED;
#ifdef VBOX_WITH_DRIVERLESS_NEM_FALLBACK
        else
            fFlags |= SUPSECMAIN_FLAGS_DRIVERLESS_NEM_FALLBACK;
#endif
        if (fDriverless)
            fFlags |= SUPSECMAIN_FLAGS_DRIVERLESS;
    }

    return SUPR3HardenedMain("VirtualBoxVM", fFlags, argc, argv, envp);
}

