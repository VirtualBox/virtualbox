/* $Id: UIMousePointerShapeData.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMousePointerShapeData class implementation.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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
#include "UIMousePointerShapeData.h"

UIMousePointerShapeData::UIMousePointerShapeData(bool fVisible /* = false */,
                                                 bool fAlpha /* = false */,
                                                 const QPoint &hotSpot /* = QPoint() */,
                                                 const QSize &shapeSize /* = QSize() */,
                                                 const QVector<BYTE> &shape /* = QVector<BYTE>() */)
    : m_fVisible(fVisible)
    , m_fAlpha(fAlpha)
    , m_hotSpot(hotSpot)
    , m_shapeSize(shapeSize)
    , m_shape(shape)
{
}

UIMousePointerShapeData::UIMousePointerShapeData(const UIMousePointerShapeData &another)
    : m_fVisible(another.isVisible())
    , m_fAlpha(another.hasAlpha())
    , m_hotSpot(another.hotSpot())
    , m_shapeSize(another.shapeSize())
    , m_shape(another.shape())
{
}

UIMousePointerShapeData &UIMousePointerShapeData::operator=(const UIMousePointerShapeData &another)
{
    m_fVisible = another.isVisible();
    m_fAlpha = another.hasAlpha();
    m_hotSpot = another.hotSpot();
    m_shapeSize = another.shapeSize();
    m_shape = another.shape();
    return *this;
}
