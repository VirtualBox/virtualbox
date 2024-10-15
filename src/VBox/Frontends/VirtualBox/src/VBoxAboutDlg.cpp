/* $Id: VBoxAboutDlg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - VBoxAboutDlg class implementation.
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
#include <QEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWindow>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "UIIconPool.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UITranslationEventListener.h"
#include "UIVersion.h"
#include "VBoxAboutDlg.h"

/* Other VBox includes: */
#include <iprt/path.h> /* RTPathExecDir */
#include <VBox/version.h> /* VBOX_VENDOR */


VBoxAboutDlg::VBoxAboutDlg(QWidget *pParent, const QString &strVersion)
#ifdef VBOX_WS_MAC
    // No need for About dialog parent on macOS.
    // First of all, non of other native apps (Safari, App Store, iTunes) centers About dialog according the app itself, they do
    // it according to screen instead, we should do it as well.  Besides that since About dialog is not modal, it will be in
    // conflict with modal dialogs if there will be a parent passed, because the dialog will not have own event-loop in that case.
    : QDialog(0)
    , m_pPseudoParent(pParent)
#else
    // On other hosts we will keep the current behavior for now.
    // First of all it's quite difficult to find native (Metro UI) Windows app which have About dialog at all.  But non-native
    // cross-platform apps (Qt Creator, VLC) centers About dialog according the app exactly.
    : QDialog(pParent)
    , m_pPseudoParent(0)
#endif
    , m_fPolished(false)
    , m_strVersion(strVersion)
    , m_pMainLayout(0)
    , m_pLabel(0)
{
    prepare();
}

void VBoxAboutDlg::showEvent(QShowEvent *pEvent)
{
    /* Call to base-class: */
    QDialog::showEvent(pEvent);

    /* Polish once: */
    if (!m_fPolished)
    {
        m_fPolished = true;
        setFixedSize(m_size);

        QRect geo = geometry();
#ifdef VBOX_WS_MAC
        /* Center according to parent screen: */
        geo.moveCenter(gpDesktop->screenGeometry(m_pPseudoParent).center());
#else
        /* Center according to parent widget: */
        geo.moveCenter(parentWidget()->geometry().center());
#endif
        setGeometry(geo);
    }
}

void VBoxAboutDlg::paintEvent(QPaintEvent *)
{
    /* Draw About-VirtualBox background image: */
    QPainter painter(this);
    painter.drawPixmap(0, 0, m_pixmap);
}

void VBoxAboutDlg::sltRetranslateUI()
{
    setWindowTitle(tr("VirtualBox - About"));

    if (m_pLabel)
    {
        const QString strAboutText = tr("VirtualBox Graphical User Interface");
#ifdef VBOX_BLEEDING_EDGE
        const QString strVersionText = "EXPERIMENTAL build %1 - " + QString(VBOX_BLEEDING_EDGE);
#else
        const QString strVersionText = tr("Version %1");
#endif
#ifdef VBOX_OSE
        m_strAboutText = strAboutText + " " + strVersionText.arg(m_strVersion) + "\n"
                       + QString("%1 2004-" VBOX_C_YEAR " " VBOX_VENDOR).arg(QChar(0xa9));
#else
        m_strAboutText = strAboutText + "\n" + strVersionText.arg(m_strVersion);
#endif
        m_strAboutText = m_strAboutText + QString(" (Qt%1)").arg(qVersion());
        m_strAboutText = m_strAboutText + "\n" + QString("Copyright %1 %2 %3.")
                                                         .arg(QChar(0xa9)).arg(VBOX_C_YEAR).arg(VBOX_VENDOR);
        m_pLabel->setText(m_strAboutText);
    }
}

void VBoxAboutDlg::prepare()
{
    /* Delete dialog on close: */
    setAttribute(Qt::WA_DeleteOnClose);
    /* Do not count that window as important for application,
     * it will NOT be taken into account when other top-level windows will be closed: */
    setAttribute(Qt::WA_QuitOnClose, false);

    /* Make sure the dialog is deleted on pseudo-parent destruction: */
    if (m_pPseudoParent)
        connect(m_pPseudoParent, &QObject::destroyed, this, &VBoxAboutDlg::close);

    /* Choose default image: */
    QString strPath(":/about.png");

    /* Branding: Use a custom about splash picture if set: */
    const QString strSplash = UIVersionInfo::brandingGetKey("UI/AboutSplash");
    if (UIVersionInfo::brandingIsActive() && !strSplash.isEmpty())
    {
        char szExecPath[1024];
        RTPathExecDir(szExecPath, 1024);
        QString strTmpPath = QString("%1/%2").arg(szExecPath).arg(strSplash);
        if (QFile::exists(strTmpPath))
            strPath = strTmpPath;
    }

    /* Load image: */
    const QIcon icon = UIIconPool::iconSet(strPath);
    m_size = QSize(640, 480);
    QWidget *pParent = m_pPseudoParent ? m_pPseudoParent : parentWidget();
    const qreal fDevicePixelRatio = pParent ? pParent->windowHandle()->devicePixelRatio() : 1;
    m_pixmap = icon.pixmap(m_size, fDevicePixelRatio);

    /* Prepare main-layout: */
    prepareMainLayout();

    /* Translate: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &VBoxAboutDlg::sltRetranslateUI);
}

void VBoxAboutDlg::prepareMainLayout()
{
    /* Create main-layout: */
    m_pMainLayout = new QVBoxLayout(this);
    if (m_pMainLayout)
    {
        /* Prepare stuff: */
        prepareLabel();
        prepareCloseButton();
    }
}

void VBoxAboutDlg::prepareLabel()
{
    /* Create label for version text: */
    m_pLabel = new QLabel(this);
    if (m_pLabel)
    {
        /* Prepare label for version text: */
        QPalette palette;
        /* Branding: Set a different text color (because splash also could be white),
         * otherwise use white as default color: */
        const QString strColor = UIVersionInfo::brandingGetKey("UI/AboutTextColor");
        if (!strColor.isEmpty())
            palette.setColor(QPalette::WindowText, QColor(strColor).name());
        else
            palette.setColor(QPalette::WindowText, Qt::white);
        m_pLabel->setPalette(palette);
        m_pLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_pLabel->setFont(font());

        /* Add label to the main-layout: */
        if (m_pMainLayout)
        {
            m_pMainLayout->addWidget(m_pLabel);
            m_pMainLayout->setAlignment(m_pLabel, Qt::AlignRight | Qt::AlignBottom);
        }
    }
}

void VBoxAboutDlg::prepareCloseButton()
{
    /* Create button-box: */
    QIDialogButtonBox *pButtonBox = new QIDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal, this);
    if (pButtonBox)
    {
        /* Prepare close-button: */
        connect(pButtonBox, &QDialogButtonBox::rejected, this, &VBoxAboutDlg::close);

        /* Add button-box to the main-layout: */
        if (m_pMainLayout)
            m_pMainLayout->addWidget(pButtonBox);
    }
}
