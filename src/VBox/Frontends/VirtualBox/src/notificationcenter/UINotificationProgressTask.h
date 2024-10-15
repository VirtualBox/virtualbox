/* $Id: UINotificationProgressTask.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINotificationProgressTask class declaration.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_notificationcenter_UINotificationProgressTask_h
#define FEQT_INCLUDED_SRC_notificationcenter_UINotificationProgressTask_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIProgressTask.h"

/* Forward declarations: */
class UINotificationProgress;

/** UIProgressTask extension for notification-center needs executed the silent way.
  * That means no modal messages will arise, you'll be able to retreive error information via API
  * provided. createProgress() and handleProgressFinished() being reloaded to handle everythig
  * silently, you just need to implement UINotificationProgress::createProgress() instead. */
class SHARED_LIBRARY_STUFF UINotificationProgressTask : public UIProgressTask
{
    Q_OBJECT;

public:

    /** Creates notification progress task passing @a pParent to the base-class.
      * @param  pParent  Brings the notification progress this task belongs to. */
    UINotificationProgressTask(UINotificationProgress *pParent);

    /** Returns error message. */
    QString errorMessage() const;

protected:

    /** Creates and returns started progress-wrapper required to init UIProgressObject.
      * @note  You don't need to reload it, it uses pParent's createProgress()
      *        which should be reloaded in your pParent sub-class. */
    virtual CProgress createProgress() RT_OVERRIDE RT_FINAL;
    /** Handles finished @a comProgress wrapper.
      * @note  You don't need to reload it. */
    virtual void handleProgressFinished(CProgress &comProgress) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the notification progress this task belongs to. */
    UINotificationProgress *m_pParent;

    /** Holds the error message. */
    QString  m_strErrorMessage;
};

#endif /* !FEQT_INCLUDED_SRC_notificationcenter_UINotificationProgressTask_h */
