/* $Id: QIGraphicsWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIGraphicsWidget class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extensions_graphics_QIGraphicsWidget_h
#define FEQT_INCLUDED_SRC_extensions_graphics_QIGraphicsWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QGraphicsWidget>

/* GUI includes: */
#include "UILibraryDefs.h"

/** QGraphicsWidget extension with advanced functionality. */
class SHARED_LIBRARY_STUFF QIGraphicsWidget : public QGraphicsWidget
{
    Q_OBJECT;

public:

    /** Constructs graphics-widget passing @a pParent to the base-class. */
    QIGraphicsWidget(QGraphicsWidget *pParent = 0);

    /** Returns minimum size-hint. */
    virtual QSizeF minimumSizeHint() const;
};

#endif /* !FEQT_INCLUDED_SRC_extensions_graphics_QIGraphicsWidget_h */
