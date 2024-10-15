/* $Id: UITaskCloudGetSettingsForm.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UITaskCloudGetSettingsForm class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_UITaskCloudGetSettingsForm_h
#define FEQT_INCLUDED_SRC_manager_UITaskCloudGetSettingsForm_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMutex>

/* GUI includes: */
#include "UITask.h"

/* COM includes: */
#include "CCloudMachine.h"
#include "CForm.h"

/** UITask extension used to get cloud machine settings form. */
class UITaskCloudGetSettingsForm : public UITask
{
    Q_OBJECT;

public:

    /** Constructs update task taking @a comCloudMachine as data.
      * @param  comCloudMachine  Brings the cloud machine object. */
    UITaskCloudGetSettingsForm(const CCloudMachine &comCloudMachine);

    /** Returns cloud machine object. */
    CCloudMachine cloudMachine() const { return m_comCloudMachine; }

    /** Returns error info. */
    QString errorInfo() const;

    /** Returns the task result. */
    CForm result() const;

protected:

    /** Contains the task body. */
    virtual void run() RT_OVERRIDE;

private:

    /** Holds the mutex to access result. */
    mutable QMutex  m_mutex;

    /** Holds the cloud machine object. */
    CCloudMachine  m_comCloudMachine;

    /** Holds the error info. */
    QString  m_strErrorInfo;

    /** Holds the task result. */
    CForm  m_comResult;
};

/** QObject extension used to receive and redirect result
  * of get cloud machine settings form task described above. */
class UIReceiverCloudGetSettingsForm : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies about task is complete with certain comResult. */
    void sigTaskComplete(const CForm &comResult);
    /** Notifies about task is failed with certain strErrorMessage. */
    void sigTaskFailed(const QString &strErrorMessage);

public:

    /** Constructs receiver passing @a pParent to the base-class. */
    UIReceiverCloudGetSettingsForm(QWidget *pParent);

public slots:

    /** Handles thread-pool signal about @a pTask is complete. */
    void sltHandleTaskComplete(UITask *pTask);

private:

    /** Holds the parent widget reference. */
    QWidget *m_pParent;
};

#endif /* !FEQT_INCLUDED_SRC_manager_UITaskCloudGetSettingsForm_h */
