/* $Id: UISettingsPage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISettingsPage class implementation.
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
#include <QApplication>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QStyle>
#include <QVariant>
#include <QVBoxLayout>

/* GUI includes: */
#include "UICommon.h"
#include "UIConverter.h"
#include "UISettingsPage.h"
#include "UISettingsPageValidator.h"


/*********************************************************************************************************************************
*   Class UISettingsPage implementation.                                                                                         *
*********************************************************************************************************************************/

UISettingsPage::UISettingsPage()
    : m_enmConfigurationAccessLevel(ConfigurationAccessLevel_Null)
    , m_cId(-1)
    , m_pFirstWidget(0)
    , m_pValidator(0)
    , m_fIsValidatorBlocked(true)
    , m_fProcessed(false)
    , m_fFailed(false)
{
}

void UISettingsPage::notifyOperationProgressError(const QString &strErrorInfo)
{
    QMetaObject::invokeMethod(this,
                              "sigOperationProgressError",
                              Qt::BlockingQueuedConnection,
                              Q_ARG(QString, strErrorInfo));
}

void UISettingsPage::setValidator(UISettingsPageValidator *pValidator)
{
    /* Make sure validator is not yet assigned: */
    AssertMsg(!m_pValidator, ("Validator already assigned!\n"));
    if (m_pValidator)
        return;

    /* Assign validator: */
    m_pValidator = pValidator;
}

void UISettingsPage::setConfigurationAccessLevel(ConfigurationAccessLevel enmConfigurationAccessLevel)
{
    m_enmConfigurationAccessLevel = enmConfigurationAccessLevel;
    polishPage();
}

void UISettingsPage::revalidate()
{
    /* Invalidate validator if allowed: */
    if (!m_fIsValidatorBlocked && m_pValidator)
        m_pValidator->invalidate();
}


/*********************************************************************************************************************************
*   Class UISettingsPageGlobal implementation.                                                                                   *
*********************************************************************************************************************************/

UISettingsPageGlobal::UISettingsPageGlobal()
{
}

GlobalSettingsPageType UISettingsPageGlobal::internalID() const
{
    return static_cast<GlobalSettingsPageType>(id());
}

QString UISettingsPageGlobal::internalName() const
{
    return gpConverter->toInternalString(internalID());
}

QPixmap UISettingsPageGlobal::warningPixmap() const
{
    return gpConverter->toWarningPixmap(internalID());
}

void UISettingsPageGlobal::fetchData(const QVariant &data)
{
    /* Fetch data to m_host & m_properties: */
    m_host = data.value<UISettingsDataGlobal>().m_host;
    m_properties = data.value<UISettingsDataGlobal>().m_properties;
}

void UISettingsPageGlobal::uploadData(QVariant &data) const
{
    /* Upload m_host & m_properties to data: */
    data = QVariant::fromValue(UISettingsDataGlobal(m_host, m_properties));
}


/*********************************************************************************************************************************
*   Class UISettingsPageMachine implementation.                                                                                  *
*********************************************************************************************************************************/

UISettingsPageMachine::UISettingsPageMachine()
{
}

MachineSettingsPageType UISettingsPageMachine::internalID() const
{
    return static_cast<MachineSettingsPageType>(id());
}

QString UISettingsPageMachine::internalName() const
{
    return gpConverter->toInternalString(internalID());
}

QPixmap UISettingsPageMachine::warningPixmap() const
{
    return gpConverter->toWarningPixmap(internalID());
}

void UISettingsPageMachine::fetchData(const QVariant &data)
{
    /* Fetch data to m_machine & m_console: */
    m_machine = data.value<UISettingsDataMachine>().m_machine;
    m_console = data.value<UISettingsDataMachine>().m_console;
}

void UISettingsPageMachine::uploadData(QVariant &data) const
{
    /* Upload m_machine & m_console to data: */
    data = QVariant::fromValue(UISettingsDataMachine(m_machine, m_console));
}


/*********************************************************************************************************************************
*   Class UISettingsPageFrame implementation.                                                                                    *
*********************************************************************************************************************************/

UISettingsPageFrame::UISettingsPageFrame(UISettingsPage *pPage, QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_pPage(pPage)
    , m_pLabelName(0)
    , m_pWidget(0)
    , m_pLayout(0)
{
    prepare();
}

void UISettingsPageFrame::setName(const QString &strName)
{
    if (m_strName == strName)
        return;

    m_strName = strName;
    if (m_pLabelName)
        m_pLabelName->setText(m_strName);
}

void UISettingsPageFrame::sltRetranslateUI()
{
    // No NLS tags for now; We are receiving our name through the getter.
}

void UISettingsPageFrame::paintEvent(QPaintEvent *pPaintEvent)
{
    /* Prepare painter: */
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    /* Avoid painting more than necessary: */
    painter.setClipRect(pPaintEvent->rect());

    /* Prepare colors: */
    const bool fActive = window() && window()->isActiveWindow();
    QColor col1;
    QColor col2;
    if (uiCommon().isInDarkMode())
    {
        col1 = qApp->palette().color(fActive ? QPalette::Active : QPalette::Inactive, QPalette::Window).lighter(130);
        col2 = qApp->palette().color(fActive ? QPalette::Active : QPalette::Inactive, QPalette::Window).lighter(150);
    }
    else
    {
        col1 = qApp->palette().color(fActive ? QPalette::Active : QPalette::Inactive, QPalette::Window).darker(105);
        col2 = qApp->palette().color(fActive ? QPalette::Active : QPalette::Inactive, QPalette::Window).darker(120);
    }

    /* Prepare painter path: */
    const QRect widgetRect = rect();
    QPainterPath path;
    int iRadius = 6;
    QSizeF arcSize(2 * iRadius, 2 * iRadius);
    path.moveTo(widgetRect.x() + iRadius, widgetRect.y());
    path.arcTo(QRectF(path.currentPosition(), arcSize).translated(-iRadius, 0), 90, 90);
    path.lineTo(path.currentPosition().x(), widgetRect.height() - iRadius);
    path.arcTo(QRectF(path.currentPosition(), arcSize).translated(0, -iRadius), 180, 90);
    path.lineTo(widgetRect.width() - iRadius, path.currentPosition().y());
    path.arcTo(QRectF(path.currentPosition(), arcSize).translated(-iRadius, -2 * iRadius), 270, 90);
    path.lineTo(path.currentPosition().x(), widgetRect.y() + iRadius);
    path.arcTo(QRectF(path.currentPosition(), arcSize).translated(-2 * iRadius, -iRadius), 0, 90);
    path.closeSubpath();

    /* Painting stuff: */
    painter.fillPath(path, col1);
    painter.strokePath(path, col2);
}

void UISettingsPageFrame::prepare()
{
    /* Keep minimum vertical size: */
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);

    /* Create main layout: */
    QVBoxLayout *pLayoutMain = new QVBoxLayout(this);
    if (pLayoutMain)
    {
        /* Create name label: */
        m_pLabelName = new QLabel(this);
        if (m_pLabelName)
        {
            QFont fnt = m_pLabelName->font();
            fnt.setBold(true);
            m_pLabelName->setFont(fnt);
            pLayoutMain->addWidget(m_pLabelName);
        }

        /* Create contents widget: */
        m_pWidget = new QWidget(this);
        if (m_pWidget)
        {
            m_pLayout = new QVBoxLayout(m_pWidget);
            if (m_pLayout)
            {
                m_pLayout->setContentsMargins(0, 0, 0, 0);

                m_pLayout->addWidget(m_pPage);
                /// @todo what about removal handling?
                addEditor(m_pPage);
            }
            pLayoutMain->addWidget(m_pWidget);
        }
    }
}
