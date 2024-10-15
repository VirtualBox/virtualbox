/* $Id: UIMachineViewScale.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineViewScale class declaration.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef FEQT_INCLUDED_SRC_runtime_scale_UIMachineViewScale_h
#define FEQT_INCLUDED_SRC_runtime_scale_UIMachineViewScale_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIMachineView.h"

/** UIMachineView subclass used as scaled machine view implementation. */
class UIMachineViewScale : public UIMachineView
{
    Q_OBJECT;

public:

    /* Scale machine-view constructor: */
    UIMachineViewScale(UIMachineWindow *pMachineWindow, ulong uScreenId);
    /* Scale machine-view destructor: */
    virtual ~UIMachineViewScale() {}

private slots:

    /* Slot to perform guest resize: */
    void sltPerformGuestScale();

private:

    /* Event handlers: */
    bool eventFilter(QObject *pWatched, QEvent *pEvent) RT_OVERRIDE RT_FINAL;

    /** Applies machine-view scale-factor. */
    void applyMachineViewScaleFactor() RT_OVERRIDE RT_FINAL;

    /** Resends guest size-hint. */
    void resendSizeHint() RT_OVERRIDE RT_FINAL;

    /* Private helpers: */
    QSize sizeHint() const RT_OVERRIDE RT_FINAL;
    QRect workingArea() const RT_OVERRIDE RT_FINAL;
    QSize calculateMaxGuestSize() const RT_OVERRIDE RT_FINAL;
    void updateSliders() RT_OVERRIDE RT_FINAL;
};

#endif /* !FEQT_INCLUDED_SRC_runtime_scale_UIMachineViewScale_h */
