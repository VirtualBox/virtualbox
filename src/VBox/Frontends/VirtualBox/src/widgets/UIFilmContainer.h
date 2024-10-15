/* $Id: UIFilmContainer.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFilmContainer class declaration.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UIFilmContainer_h
#define FEQT_INCLUDED_SRC_widgets_UIFilmContainer_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QScrollArea;
class QVBoxLayout;
class UIFilm;

/** QWidget subclass providing GUI with QScrollArea-based container for UIFilm widgets.
  * @todo Rename to something more suitable like UIScreenThumbnailContainer. */
class SHARED_LIBRARY_STUFF UIFilmContainer : public QWidget
{
    Q_OBJECT;

public:

    /** Constructs film-container passing @a pParent to the base-class. */
    UIFilmContainer(QWidget *pParent = 0);

    /** Returns the film-container check-box values. */
    QVector<bool> value() const;
    /** Defines the film-container check-box @a values. */
    void setValue(const QVector<bool> &values);

private:

    /** Prepares all. */
    void prepare();
    /** Prepares layout. */
    void prepareLayout();
    /** Prepares scroller. */
    void prepareScroller();

    /** Holds the main layout intance. */
    QVBoxLayout    *m_pMainLayout;
    /** Holds the scroller intance. */
    QScrollArea    *m_pScroller;
    /** Holds the list of film widgets. */
    QList<UIFilm*>  m_widgets;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UIFilmContainer_h */
