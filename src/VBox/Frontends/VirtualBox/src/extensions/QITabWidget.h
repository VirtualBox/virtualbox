/* $Id: QITabWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QITabWidget class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QITabWidget_h
#define FEQT_INCLUDED_SRC_extensions_QITabWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QTabWidget>

/* GUI includes: */
#include "UILibraryDefs.h"

/** QTabWidget extension with advanced functionality. */
class SHARED_LIBRARY_STUFF QITabWidget : public QTabWidget
{
    Q_OBJECT;

public:

    /** Constructs tab-widget passing @a pParent to the base-class. */
    QITabWidget(QWidget *pParent = 0);
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QITabWidget_h */
