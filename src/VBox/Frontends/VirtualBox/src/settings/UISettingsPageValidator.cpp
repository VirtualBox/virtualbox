/* $Id: UISettingsPageValidator.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: UISettingsPageValidator class implementation.
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

/* GUI includes: */
#include "UILoggingDefs.h"
#include "UISettingsPage.h"
#include "UISettingsPageValidator.h"


UISettingsPageValidator::UISettingsPageValidator(QObject *pParent, UISettingsPage *pPage)
    : QObject(pParent)
    , m_pPage(pPage)
    , m_fIsValid(true)
{
}

QPixmap UISettingsPageValidator::warningPixmap() const
{
    return m_pPage->warningPixmap();
}

QString UISettingsPageValidator::internalName() const
{
    return m_pPage->internalName();
}

void UISettingsPageValidator::setTitlePrefix(const QString &strPrefix)
{
    /* Assign new prefix: */
    m_strPrefix = strPrefix;

    /* Revalidate if we had errors previously: */
    if (!lastMessage().isEmpty())
        revalidate();
}

void UISettingsPageValidator::setLastMessage(const QString &strLastMessage)
{
    /* Remember new message: */
    m_strLastMessage = strLastMessage;

    /* Should we show corresponding warning icon? */
    if (m_strLastMessage.isEmpty())
        emit sigHideWarningIcon();
    else
        emit sigShowWarningIcon();
}

void UISettingsPageValidator::invalidate()
{
    /* Notify listener(s) about validity change: */
    emit sigValidityChanged(this);
}

void UISettingsPageValidator::revalidate()
{
    /* Perform page revalidation: */
    QList<UIValidationMessage> messages;
    setValid(m_pPage->validate(messages));

    /* Remember warning/error message: */
    if (messages.isEmpty())
        setLastMessage(QString());
    else
    {
        /* Prepare text: */
        QStringList text;
        foreach (const UIValidationMessage &message, messages)
        {
            /* Prepare title: */
            const QString strTitle(message.first.isNull() ? tr("<b>%1</b> page:").arg(m_strPrefix) :
                                                            tr("<b>%1: %2</b> page:").arg(m_strPrefix, message.first));

            /* Prepare paragraph: */
            QStringList paragraph(message.second);
            paragraph.prepend(strTitle);

            /* Format text for iterated message: */
            text << paragraph.join("<br>");
        }

        /* Remember text: */
        setLastMessage(text.join("<br><br>"));
        LogRelFlow(("Settings Dialog:  Page validation FAILED: {%s}\n",
                    lastMessage().toUtf8().constData()));
    }
}
