/* $Id: QISplitter.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QISplitter class declaration.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QISplitter_h
#define FEQT_INCLUDED_SRC_extensions_QISplitter_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QSplitter>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QSplitterHandle;

/** QSplitter subclass with extended functionality. */
class SHARED_LIBRARY_STUFF QISplitter : public QSplitter
{
    Q_OBJECT;

public:

    /** Constructs splitter passing @a enmOrientation and @a pParent to the base-class. */
    QISplitter(Qt::Orientation enmOrientation = Qt::Horizontal, QWidget *pParent = 0);

protected:

    /** Preprocesses Qt @a pEvent for passed @a pObject. */
    virtual bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE;

    /** Handles show @a pEvent. */
    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;

    /** Creates a handle. */
    virtual QSplitterHandle *createHandle() RT_OVERRIDE;

private:

    /** Holds the serialized base-state. */
    QByteArray  m_baseState;

    /** Holds whether the splitter is polished. */
    bool  m_fPolished;

    /** Holds the handle color. */
    QColor  m_color;
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QISplitter_h */
