/* $Id: UIMenuBar.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMenuBar class declaration.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UIMenuBar_h
#define FEQT_INCLUDED_SRC_widgets_UIMenuBar_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMenuBar>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QPaintEvent;
class QWidget;

/** QMenuBar extension
  * which reflects BETA label when necessary. */
class SHARED_LIBRARY_STUFF UIMenuBar: public QMenuBar
{
    Q_OBJECT;

public:

    /** Constructor, passes @a pParent to the QMenuBar constructor. */
    UIMenuBar(QWidget *pParent = 0);

protected:

    /** Paint event handler. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE;

private:

    /** Reflects whether we should show BETA label or not. */
    bool m_fShowBetaLabel;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UIMenuBar_h */
