/* $Id: AutostartDb.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Main - Autostart database Interfaces.
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

#ifndef MAIN_INCLUDED_AutostartDb_h
#define MAIN_INCLUDED_AutostartDb_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/critsect.h>

class AutostartDb
{
    public:

        AutostartDb();
        ~AutostartDb();

        /**
         * Sets the path to the autostart database.
         *
         * @returns VBox status code.
         * @param   pszAutostartDbPathNew    Path to the autostart database.
         */
        int setAutostartDbPath(const char *pszAutostartDbPathNew);

        /**
         * Add a autostart VM to the global database.
         *
         * @returns VBox status code.
         * @retval VERR_PATH_NOT_FOUND if the autostart database directory is not set.
         * @param   pszVMId    ID of the VM to add.
         */
        int addAutostartVM(const char *pszVMId);

        /**
         * Remove a autostart VM from the global database.
         *
         * @returns VBox status code.
         * @retval VERR_PATH_NOT_FOUND if the autostart database directory is not set.
         * @param   pszVMId    ID of the VM to remove.
         */
        int removeAutostartVM(const char *pszVMId);

        /**
         * Add a autostop VM to the global database.
         *
         * @returns VBox status code.
         * @retval VERR_PATH_NOT_FOUND if the autostart database directory is not set.
         * @param   pszVMId    ID of the VM to add.
         */
        int addAutostopVM(const char *pszVMId);

        /**
         * Remove a autostop VM from the global database.
         *
         * @returns VBox status code.
         * @retval VERR_PATH_NOT_FOUND if the autostart database directory is not set.
         * @param   pszVMId    ID of the VM to remove.
         */
        int removeAutostopVM(const char *pszVMId);

    private:

#ifdef RT_OS_LINUX
        /** Critical section protecting the database against concurrent access. */
        RTCRITSECT      CritSect;
        /** Path to the autostart database. */
        char           *m_pszAutostartDbPath;

        /**
         * Autostart database modification worker.
         *
         * @returns VBox status code.
         * @param   fAutostart    Flag whether the autostart or autostop database is modified.
         * @param   fAddVM        Flag whether a VM is added or removed from the database.
         */
        int autostartModifyDb(bool fAutostart, bool fAddVM);
#endif
};

#endif /* !MAIN_INCLUDED_AutostartDb_h */

