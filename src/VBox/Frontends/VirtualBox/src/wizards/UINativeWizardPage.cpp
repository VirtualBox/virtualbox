/* $Id: UINativeWizardPage.cpp 107022 2024-11-14 13:56:24Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UINativeWizardPage class implementation.
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

/* GUI includes: */
#include "UICommon.h"
#include "UINativeWizard.h"
#include "UINativeWizardPage.h"
#include "UITranslationEventListener.h"

UINativeWizardPage::UINativeWizardPage(const QString strHelpKeyword /* = QString() */)
{
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UINativeWizardPage::sltRetranslateUI);
    if (!strHelpKeyword.isEmpty())
        uiCommon().setHelpKeyword(this, strHelpKeyword);
}

void UINativeWizardPage::setTitle(const QString &strTitle)
{
    m_strTitle = strTitle;
}

QString UINativeWizardPage::title() const
{
    return m_strTitle;
}

UINativeWizard *UINativeWizardPage::wizard() const
{
    return   parentWidget() && parentWidget()->window()
           ? qobject_cast<UINativeWizard*>(parentWidget()->window())
           : 0;
}
