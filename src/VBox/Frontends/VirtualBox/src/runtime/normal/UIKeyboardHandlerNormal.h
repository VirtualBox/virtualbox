/* $Id: UIKeyboardHandlerNormal.h 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * VBox Qt GUI - UIKeyboardHandlerNormal class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_normal_UIKeyboardHandlerNormal_h
#define FEQT_INCLUDED_SRC_runtime_normal_UIKeyboardHandlerNormal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIKeyboardHandler.h"

/** UIKeyboardHandler reimplementation
  * providing machine-logic with PopupMenu keyboard handler. */
class UIKeyboardHandlerNormal : public UIKeyboardHandler
{
    Q_OBJECT;

public:

    /** Normal keyboard-handler constructor. */
    UIKeyboardHandlerNormal(UIMachineLogic *pMachineLogic);
    /** Normal keyboard-handler destructor. */
    virtual ~UIKeyboardHandlerNormal();

private:

#ifndef VBOX_WS_MAC
    /** General event-filter. */
    bool eventFilter(QObject *pWatched, QEvent *pEvent) RT_OVERRIDE RT_FINAL;
#endif /* !VBOX_WS_MAC */
};

#endif /* !FEQT_INCLUDED_SRC_runtime_normal_UIKeyboardHandlerNormal_h */
