/* $Id: QIProcess.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIProcess class implementation.
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

/* GUI includes: */
#include "QIProcess.h"

/* External includes: */
#ifdef VBOX_WS_NIX
# include <sys/wait.h>
#endif


QIProcess::QIProcess(QObject *pParent /* = 0 */)
    : QProcess(pParent)
{
}

/* static */
QByteArray QIProcess::singleShot(const QString &strProcessName, int iTimeout /* = 5000 */)
{
    // WORKAROUND:
    // Why is it really needed is because of Qt4.3 bug with QProcess.
    // This bug is about QProcess sometimes (~70%) do not receive
    // notification about process was finished, so this makes
    // 'bool QProcess::waitForFinished (int)' block the GUI thread and
    // never dismissed with 'true' result even if process was really
    // started&finished. So we just waiting for some information
    // on process output and destroy the process with force. Due to
    // QProcess::~QProcess() has the same 'waitForFinished (int)' blocker
    // we have to change process state to QProcess::NotRunning.

    /// @todo Do we still need this?
    QByteArray result;
    QIProcess process;
    process.start(strProcessName);
    bool firstShotReady = process.waitForReadyRead(iTimeout);
    if (firstShotReady)
        result = process.readAllStandardOutput();
    process.setProcessState(QProcess::NotRunning);
#ifdef VBOX_WS_NIX
    int iStatus;
    if (process.processId() > 0)
        waitpid(process.processId(), &iStatus, 0);
#endif /* VBOX_WS_NIX */
    return result;
}
