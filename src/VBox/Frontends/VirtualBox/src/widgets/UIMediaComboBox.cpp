/* $Id: UIMediaComboBox.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMediaComboBox class implementation.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

/* Qt includes: */
#include <QAbstractItemView>
#include <QDir>
#include <QFileInfo>

/* GUI includes: */
#include "UIGlobalSession.h"
#include "UIMediaComboBox.h"
#include "UIMediumEnumerator.h"
#include "UIMedium.h"


UIMediaComboBox::UIMediaComboBox(QWidget *pParent /* = 0 */)
    : QComboBox(pParent)
    , m_enmMediaType(UIMediumDeviceType_Invalid)
    , m_uMachineId(QUuid())
    , m_uLastItemId(QUuid())
{
    /* Prepare: */
    prepare();
}

void UIMediaComboBox::refresh()
{
    /* Clearing lists: */
    clear(), m_media.clear();

    /* Use the medium creation handler to add all the items:  */
    foreach (const QUuid &uMediumId, gpMediumEnumerator->mediumIDs())
        sltHandleMediumCreated(uMediumId);

    /* If at least one real medium present,
     * remove null medium: */
    if (count() > 1)
    {
        removeItem(0);
        m_media.erase(m_media.begin());
    }

    /* Notify listeners about active item changed. */
    emit activated(currentIndex());
}

void UIMediaComboBox::repopulate()
{
    /* Start medium-enumeration for optical drives/images (if necessary): */
    if (   m_enmMediaType == UIMediumDeviceType_DVD
        && !gpMediumEnumerator->isFullMediumEnumerationRequested())
    {
        CMediumVector comMedia;
        comMedia << gpGlobalSession->host().GetDVDDrives();
        comMedia << gpGlobalSession->virtualBox().GetDVDImages();
        gpMediumEnumerator->enumerateMedia(comMedia);
    }
    refresh();
}

void UIMediaComboBox::setCurrentItem(const QUuid &uItemId)
{
    m_uLastItemId = uItemId;

    int iIndex;
    // WORKAROUND:
    // Note that the media combo-box may be not populated here yet,
    // so we don't assert..
    if (findMediaIndex(uItemId, iIndex))
    {
        QComboBox::setCurrentIndex(iIndex);
        emit activated(iIndex);
    }
}

QUuid UIMediaComboBox::id(int iIndex /* = -1 */) const
{
    AssertReturn(iIndex == -1 ||
                 (iIndex >= 0 && iIndex < m_media.size()),
                  QUuid());

    if (iIndex == -1)
        iIndex = currentIndex();
    return iIndex == -1 ? QUuid() : m_media.at(iIndex).id;
}

QString UIMediaComboBox::location(int iIndex /* = -1 */) const
{
    AssertReturn(iIndex == -1 ||
                 (iIndex >= 0 && iIndex < m_media.size()),
                  QString());

    if (iIndex == -1)
        iIndex = currentIndex();
    return iIndex == -1 ? QString() : m_media.at(iIndex).location;
}

void UIMediaComboBox::sltHandleMediumCreated(const QUuid &uMediumId)
{
    /* Search for corresponding medium: */
    UIMedium guiMedium = gpMediumEnumerator->medium(uMediumId);

    /* Ignore media (and their children) which are
     * marked as hidden or attached to hidden machines only: */
    if (UIMedium::isMediumAttachedToHiddenMachinesOnly(guiMedium))
        return;

    /* Add only 1. NULL medium and 2. media of required type: */
    if (!guiMedium.isNull() && guiMedium.type() != m_enmMediaType)
        return;

    /* Ignore all diffs: */
    if (guiMedium.type() == UIMediumDeviceType_HardDisk && guiMedium.parentID() != UIMedium::nullID())
        return;

    /* Append medium into combo-box: */
    appendItem(guiMedium);

    /* Activate the required item if any: */
    if (guiMedium.id() == m_uLastItemId)
        setCurrentItem(guiMedium.id());
    /* Select last added item if there is no item selected: */
    else if (currentText().isEmpty())
        QComboBox::setCurrentIndex(count() - 1);
}

void UIMediaComboBox::sltHandleMediumEnumerated(const QUuid &uMediumId)
{
    /* Search for corresponding medium: */
    UIMedium guiMedium = gpMediumEnumerator->medium(uMediumId);

    /* Add only 1. NULL medium and 2. media of required type: */
    if (!guiMedium.isNull() && guiMedium.type() != m_enmMediaType)
        return;

    /* Search for corresponding item index: */
    int iIndex;
    if (!findMediaIndex(guiMedium.id(), iIndex))
        return;

    /* Replace medium in combo-box: */
    replaceItem(iIndex, guiMedium);

    /* Ensure the parent dialog handles the change of the selected item's data: */
    emit activated(currentIndex());
}

void UIMediaComboBox::sltHandleMediumDeleted(const QUuid &uMediumId)
{
    /* Search for corresponding item index: */
    int iIndex;
    if (!findMediaIndex(uMediumId, iIndex))
        return;

    /* Replace medium from combo-box: */
    removeItem(iIndex);
    m_media.erase(m_media.begin() + iIndex);

    /* If no real medium left, add the NULL medium: */
    if (count() == 0)
        sltHandleMediumCreated(UIMedium::nullID());

    /* Ensure the parent dialog handles the change of the selected item: */
    emit activated(currentIndex());
}

void UIMediaComboBox::sltHandleMediumEnumerationStart()
{
    refresh();
}

void UIMediaComboBox::sltHandleComboActivated(int iIndex)
{
    AssertReturnVoid(iIndex >= 0 && iIndex < m_media.size());

    m_uLastItemId = m_media.at(iIndex).id;

    updateToolTip(iIndex);
}

void UIMediaComboBox::sltHandleComboHovered(const QModelIndex &index)
{
    /* Set the combo-box item's tooltip: */
    const int iIndex = index.row();
    view()->viewport()->setToolTip(QString());
    view()->viewport()->setToolTip(m_media.at(iIndex).toolTip);
}

void UIMediaComboBox::prepare()
{
    /* Setup the elide mode: */
    view()->setTextElideMode(Qt::ElideRight);
    QSizePolicy sp1(QSizePolicy::Ignored, QSizePolicy::Fixed, QSizePolicy::ComboBox);
    sp1.setHorizontalStretch(2);
    setSizePolicy(sp1);

    /* Setup medium-processing handlers: */
    connect(gpMediumEnumerator, &UIMediumEnumerator::sigMediumCreated,
            this, &UIMediaComboBox::sltHandleMediumCreated);
    connect(gpMediumEnumerator, &UIMediumEnumerator::sigMediumDeleted,
            this, &UIMediaComboBox::sltHandleMediumDeleted);

    /* Setup medium-enumeration handlers: */
    connect(gpMediumEnumerator, &UIMediumEnumerator::sigMediumEnumerationStarted,
            this, &UIMediaComboBox::sltHandleMediumEnumerationStart);
    connect(gpMediumEnumerator, &UIMediumEnumerator::sigMediumEnumerated,
            this, &UIMediaComboBox::sltHandleMediumEnumerated);

    /* Setup other connections: */
    connect(this, &UIMediaComboBox::activated,
            this, &UIMediaComboBox::sltHandleComboActivated);
    connect(view(), &QAbstractItemView::entered,
            this, &UIMediaComboBox::sltHandleComboHovered);
}

void UIMediaComboBox::updateToolTip(int iIndex)
{
    /* Set the combo-box tooltip: */
    setToolTip(QString());
    if (iIndex >= 0 && iIndex < m_media.size())
        setToolTip(m_media.at(iIndex).toolTip);
}

void UIMediaComboBox::appendItem(const UIMedium &guiMedium)
{
    m_media.append(Medium(guiMedium.id(), guiMedium.location(),
                          guiMedium.toolTipCheckRO(true, false)));

    insertItem(count(), guiMedium.iconCheckRO(true), guiMedium.details(true));
}

void UIMediaComboBox::replaceItem(int iIndex, const UIMedium &guiMedium)
{
    AssertReturnVoid(iIndex >= 0 && iIndex < m_media.size());

    m_media[iIndex].id = guiMedium.id();
    m_media[iIndex].location = guiMedium.location();
    m_media[iIndex].toolTip = guiMedium.toolTipCheckRO(true, false);

    setItemText(iIndex, guiMedium.details(true));
    setItemIcon(iIndex, guiMedium.iconCheckRO(true));

    if (iIndex == currentIndex())
        updateToolTip(iIndex);
}

bool UIMediaComboBox::findMediaIndex(const QUuid &uId, int &iIndex)
{
    iIndex = 0;

    for (; iIndex < m_media.size(); ++ iIndex)
        if (m_media.at(iIndex).id == uId)
            break;

    return iIndex < m_media.size();
}
