/* $Id: UITask.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UITask class declaration.
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

#ifndef FEQT_INCLUDED_SRC_globals_UITask_h
#define FEQT_INCLUDED_SRC_globals_UITask_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>

/* GUI includes: */
#include "UILibraryDefs.h"

/** QObject extension used as worker-thread task interface.
  * Describes task to be handled by the UIThreadPool object. */
class SHARED_LIBRARY_STUFF UITask : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies listeners about @a pTask complete. */
    void sigComplete(UITask *pTask);

public:

    /** Task types. */
    enum Type
    {
        Type_MediumEnumeration       = 1,
        Type_DetailsPopulation       = 2,
        Type_CloudListMachines       = 3,
        Type_CloudRefreshMachineInfo = 4,
        Type_CloudGetSettingsForm    = 5,
    };

    /** Constructs the task of passed @a enmType. */
    UITask(UITask::Type enmType) : m_enmType(enmType) {}

    /** Returns the type of the task. */
    UITask::Type type() const { return m_enmType; }

    /** Starts the task. */
    void start();

protected:

    /** Contains the abstract task body. */
    virtual void run() = 0;

private:

    /** Holds the type of the task. */
    const UITask::Type  m_enmType;
};

#endif /* !FEQT_INCLUDED_SRC_globals_UITask_h */
