/* $Id: UIGuestOSTypeSelectionButton.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGuestOSTypeSelectionButton class implementation.
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

/* Qt includes */
#include <QApplication>
#include <QMenu>
#include <QSignalMapper>
#include <QStyle>

/* GUI includes */
#include "UIGlobalSession.h"
#include "UIGuestOSType.h"
#include "UIGuestOSTypeSelectionButton.h"
#include "UIIconPool.h"
#include "UITranslationEventListener.h"

UIGuestOSTypeSelectionButton::UIGuestOSTypeSelectionButton(QWidget *pParent)
    : QPushButton(pParent)
{
    /* Determine icon metric: */
    const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
    setIconSize(QSize(iIconMetric, iIconMetric));

    /* We have to make sure that the button has strong focus, otherwise
     * the editing is ended when the menu is shown: */
    setFocusPolicy(Qt::StrongFocus);

    /* Create a signal mapper so that we not have to react to
     * every single menu activation ourself: */
    m_pSignalMapper = new QSignalMapper(this);
    if (m_pSignalMapper)
        connect(m_pSignalMapper, &QSignalMapper::mappedString,
                this, &UIGuestOSTypeSelectionButton::setOSTypeId);

    /* Create main menu: */
    m_pMainMenu = new QMenu(pParent);
    if (m_pMainMenu)
        setMenu(m_pMainMenu);

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIGuestOSTypeSelectionButton::sltRetranslateUI);
}

bool UIGuestOSTypeSelectionButton::isMenuShown() const
{
    return m_pMainMenu->isVisible();
}

void UIGuestOSTypeSelectionButton::setOSTypeId(const QString &strOSTypeId)
{
    if (m_strOSTypeId == strOSTypeId)
        return;
    m_strOSTypeId = strOSTypeId;

#ifndef VBOX_WS_MAC
    /* Looks ugly on the Mac: */
    setIcon(generalIconPool().guestOSTypePixmapDefault(m_strOSTypeId));
#endif

    setText(gpGlobalSession->guestOSTypeManager().getDescription(m_strOSTypeId));
}

void UIGuestOSTypeSelectionButton::sltRetranslateUI()
{
    populateMenu();
}

void UIGuestOSTypeSelectionButton::createOSTypeMenu(const UIGuestOSTypeManager::UIGuestOSTypeInfo &typeList, QMenu *pMenu)
{
    for (int j = 0; j < typeList.size(); ++j)
    {
        const QPair<QString, QString> &typeInfo = typeList[j];
        QAction *pAction = pMenu->addAction(generalIconPool().guestOSTypePixmapDefault(typeInfo.first), typeInfo.second);
        connect(pAction, &QAction::triggered,
                m_pSignalMapper, static_cast<void(QSignalMapper::*)(void)>(&QSignalMapper::map)); // swallow bool argument ..
        m_pSignalMapper->setMapping(pAction, typeInfo.first);
    }
}

void UIGuestOSTypeSelectionButton::populateMenu()
{
    /* Clear initially: */
    m_pMainMenu->clear();

    const UIGuestOSTypeManager::UIGuestOSFamilyInfo families
        = gpGlobalSession->guestOSTypeManager().getFamilies();

    for (int i = 0; i < families.size(); ++i)
    {
        const UIFamilyInfo &fi = families.at(i);
        QMenu *pSubMenu = m_pMainMenu->addMenu(fi.m_strDescription);
        const UIGuestOSTypeManager::UIGuestOSSubtypeInfo distributions
            = gpGlobalSession->guestOSTypeManager().getSubtypesForFamilyId(fi.m_strId);

        if (distributions.isEmpty())
            createOSTypeMenu(gpGlobalSession->guestOSTypeManager().getTypesForFamilyId(fi.m_strId), pSubMenu);
        else
        {
            foreach (const UISubtypeInfo &distribution, distributions)
                createOSTypeMenu(gpGlobalSession->guestOSTypeManager().getTypesForSubtype(distribution.m_strName),
                                 pSubMenu->addMenu(distribution.m_strName));
        }
    }
}
