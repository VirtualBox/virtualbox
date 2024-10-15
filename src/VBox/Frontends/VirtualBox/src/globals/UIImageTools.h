/* $Id: UIImageTools.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Declarations of utility classes and functions for image manipulation.
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

#ifndef FEQT_INCLUDED_SRC_globals_UIImageTools_h
#define FEQT_INCLUDED_SRC_globals_UIImageTools_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QImage>
#include <QPixmap>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QPalette;

/** Image operation namespace. */
namespace UIImageTools
{
    /** Converts @a image to gray-scale. */
    SHARED_LIBRARY_STUFF QImage toGray(const QImage &image);

    /** Makes @a image more dark and dim. */
    SHARED_LIBRARY_STUFF void dimImage(QImage &image);

    /** Blurs passed @a source image to @a destination cropping by certain @a iRadius. */
    SHARED_LIBRARY_STUFF void blurImage(const QImage &source, QImage &destination, int iRadius);
    /** Blurs passed @a source image horizontally to @a destination cropping by certain @a iRadius. */
    SHARED_LIBRARY_STUFF void blurImageHorizontal(const QImage &source, QImage &destination, int iRadius);
    /** Blurs passed @a source image vertically to @a destination cropping by certain @a iRadius. */
    SHARED_LIBRARY_STUFF void blurImageVertical(const QImage &source, QImage &destination, int iRadius);

    /** Applies BET-label of passed @a size. */
    SHARED_LIBRARY_STUFF QPixmap betaLabel(const QSize &size = QSize(80, 16), QWidget *pHint = 0);

    /** Gathers suitable foreground color for passed @a background color. */
    SHARED_LIBRARY_STUFF QColor suitableForegroundColor(const QPalette &pal, const QColor &background);
}
using namespace UIImageTools /* if header included */;

#endif /* !FEQT_INCLUDED_SRC_globals_UIImageTools_h */
