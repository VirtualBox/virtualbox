/* $Id: UISerialSettingsEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISerialSettingsEditor class implementation.
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
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>

/* GUI includes: */
#include "QIWidgetValidator.h"
#include "UIConverter.h"
#include "UIGlobalSession.h"
#include "UISerialSettingsEditor.h"
#include "UITranslator.h"

/* COM includes: */
#include "CSystemProperties.h"


UISerialSettingsEditor::UISerialSettingsEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_enmPortMode(KPortMode_Disconnected)
    , m_pCheckBoxPort(0)
    , m_pWidgetPortSettings(0)
    , m_pLabelNumber(0)
    , m_pComboNumber(0)
    , m_pLabelIRQ(0)
    , m_pLineEditIRQ(0)
    , m_pLabelIOAddress(0)
    , m_pLineEditIOAddress(0)
    , m_pLabelMode(0)
    , m_pComboMode(0)
    , m_pCheckBoxPipe(0)
    , m_pLabelPath(0)
    , m_pEditorPath(0)
{
    prepare();
}

void UISerialSettingsEditor::setPortOptionsAvailable(bool fAvailable)
{
    if (m_pCheckBoxPort)
        m_pCheckBoxPort->setEnabled(fAvailable);
    if (m_pLabelNumber)
        m_pLabelNumber->setEnabled(fAvailable);
    if (m_pComboNumber)
        m_pComboNumber->setEnabled(fAvailable);
}

void UISerialSettingsEditor::setPortEnabled(bool fEnabled)
{
    if (m_pCheckBoxPort)
    {
        const bool fIssueUpdateAnyway = m_pCheckBoxPort->isChecked() == fEnabled;
        m_pCheckBoxPort->setChecked(fEnabled);
        if (fIssueUpdateAnyway)
            sltHandlePortAvailabilityToggled(m_pCheckBoxPort->isChecked());
    }
}

bool UISerialSettingsEditor::isPortEnabled() const
{
    return m_pCheckBoxPort ? m_pCheckBoxPort->isChecked() : false;
}

void UISerialSettingsEditor::setIRQAndIOAddressOptionsAvailable(bool fAvailable)
{
    if (m_pLabelIRQ)
        m_pLabelIRQ->setEnabled(fAvailable);
    if (m_pLineEditIRQ)
        m_pLineEditIRQ->setEnabled(fAvailable);
    if (m_pLabelIOAddress)
        m_pLabelIOAddress->setEnabled(fAvailable);
    if (m_pLineEditIOAddress)
        m_pLineEditIOAddress->setEnabled(fAvailable);
}

void UISerialSettingsEditor::setPortByIRQAndIOAddress(ulong uIRQ, ulong uIOAddress)
{
    if (m_pComboNumber)
        m_pComboNumber->setCurrentIndex(m_pComboNumber->findText(UITranslator::toCOMPortName(uIRQ, uIOAddress)));
}

bool UISerialSettingsEditor::isPortStandardOne() const
{
    ulong uIRQ, uIOAddress;
    return m_pComboNumber ? UITranslator::toCOMPortNumbers(m_pComboNumber->currentText(), uIRQ, uIOAddress) : false;
}

void UISerialSettingsEditor::setIRQ(ulong uIRQ)
{
    if (m_pLineEditIRQ)
        m_pLineEditIRQ->setText(QString::number(uIRQ));
}

ulong UISerialSettingsEditor::irq() const
{
    return m_pLineEditIRQ ? m_pLineEditIRQ->text().toULong(NULL, 0) : 0;
}

void UISerialSettingsEditor::setIOAddress(ulong uIOAddress)
{
    if (m_pLineEditIOAddress)
        m_pLineEditIOAddress->setText("0x" + QString::number(uIOAddress, 16).toUpper());
}

ulong UISerialSettingsEditor::ioAddress() const
{
    return m_pLineEditIOAddress ? m_pLineEditIOAddress->text().toULong(NULL, 0) : 0;
}

void UISerialSettingsEditor::setHostModeOptionsAvailable(bool fAvailable)
{
    if (m_pLabelMode)
        m_pLabelMode->setEnabled(fAvailable);
    if (m_pComboMode)
        m_pComboMode->setEnabled(fAvailable);
}

void UISerialSettingsEditor::setHostMode(KPortMode enmMode)
{
    m_enmPortMode = enmMode;
    populateCombo();
}

KPortMode UISerialSettingsEditor::hostMode() const
{
    return m_pComboMode ? m_pComboMode->currentData().value<KPortMode>() : m_enmPortMode;
}

void UISerialSettingsEditor::setPipeOptionsAvailable(bool fAvailable)
{
    if (m_pCheckBoxPipe)
        m_pCheckBoxPipe->setEnabled(fAvailable);
}

void UISerialSettingsEditor::setServerEnabled(bool fEnabled)
{
    if (m_pCheckBoxPipe)
        m_pCheckBoxPipe->setChecked(!fEnabled);
}

bool UISerialSettingsEditor::isServerEnabled() const
{
    return m_pCheckBoxPipe ? !m_pCheckBoxPipe->isChecked() : true;
}

void UISerialSettingsEditor::setPathOptionsAvailable(bool fAvailable)
{
    if (m_pLabelPath)
        m_pLabelPath->setEnabled(fAvailable);
    if (m_pEditorPath)
        m_pEditorPath->setEnabled(fAvailable);
}

void UISerialSettingsEditor::setPath(const QString &strPath)
{
    if (m_pEditorPath)
        m_pEditorPath->setText(strPath);
}

QString UISerialSettingsEditor::path() const
{
    return m_pEditorPath ? QDir::toNativeSeparators(m_pEditorPath->text()) : QString();
}

void UISerialSettingsEditor::sltRetranslateUI()
{
    if (m_pCheckBoxPort)
    {
        m_pCheckBoxPort->setText(tr("&Enable Serial Port"));
        m_pCheckBoxPort->setToolTip(tr("When checked, enables the given serial port of the virtual machine."));
    }
    if (m_pLabelNumber)
        m_pLabelNumber->setText(tr("Port &Number:"));
    if (m_pComboNumber)
    {
        m_pComboNumber->setItemText(m_pComboNumber->count() - 1, UITranslator::toCOMPortName(0, 0));
        m_pComboNumber->setToolTip(tr("Selects the serial port number. You can choose one of the standard serial ports or select "
                                      "User-defined and specify port parameters manually."));
    }
    if (m_pLabelIRQ)
        m_pLabelIRQ->setText(tr("&IRQ:"));
    if (m_pLineEditIRQ)
        m_pLineEditIRQ->setToolTip(tr("Holds the IRQ number of this serial port. This should be a whole number between "
                                      "'0' and '255'. Values greater than '15' may only be used if the "
                                      "I/O APIC setting is enabled for this virtual machine."));
    if (m_pLabelIOAddress)
        m_pLabelIOAddress->setText(tr("I/O Po&rt:"));
    if (m_pLineEditIOAddress)
        m_pLineEditIOAddress->setToolTip(tr("Holds the base I/O port address of this serial port. Valid values are integer numbers "
                                            "in range from '0' to '0xFFFF'."));
    if (m_pLabelMode)
        m_pLabelMode->setText(tr("Port &Mode:"));
    if (m_pComboMode)
        m_pComboMode->setToolTip(tr("Selects the working mode of this serial port. If you select Disconnected, the guest "
                                    "OS will detect the serial port but will not be able to operate it."));
    if (m_pCheckBoxPipe)
    {
        m_pCheckBoxPipe->setText(tr("&Connect to existing pipe/socket"));
        m_pCheckBoxPipe->setToolTip(tr("When checked, the virtual machine will assume that the pipe or socket specified in the "
                                       "Path/Address field exists and try to use it. Otherwise, the pipe or socket will "
                                       "be created by the virtual machine when it starts."));
    }
    if (m_pLabelPath)
        m_pLabelPath->setText(tr("&Path/Address:"));
    if (m_pEditorPath)
        m_pEditorPath->setToolTip(tr("In Host Pipe mode: Holds the path to the serial port's pipe on the host. "
                                     "Examples: \"\\\\.\\pipe\\myvbox\" or \"/tmp/myvbox\", for Windows and UNIX-like systems "
                                     "respectively. In Host Device mode: Holds the host serial device name. "
                                     "Examples: \"COM1\" or \"/dev/ttyS0\". In Raw File mode: Holds the file-path "
                                     "on the host system, where the serial output will be dumped. In TCP mode: "
                                     "Holds the TCP \"port\" when in server mode, or \"hostname:port\" when in client mode."));

    /* Translate combo-boxes content: */
    populateCombo();
}

void UISerialSettingsEditor::sltHandlePortAvailabilityToggled(bool fOn)
{
    /* Update availability: */
    if (m_pWidgetPortSettings && m_pCheckBoxPort)
        m_pWidgetPortSettings->setEnabled(m_pCheckBoxPort->isChecked());
    if (fOn)
    {
        if (m_pComboNumber)
            sltHandleStandardPortOptionActivated(m_pComboNumber->currentText());
        if (m_pComboMode)
            sltHandleModeChange(m_pComboMode->currentIndex());
    }

    /* Notify about port availability changed: */
    emit sigPortAvailabilityChanged();
}

void UISerialSettingsEditor::sltHandleStandardPortOptionActivated(const QString &strText)
{
    /* Update availability: */
    ulong uIRQ, uIOAddress;
    bool fStd = UITranslator::toCOMPortNumbers(strText, uIRQ, uIOAddress);
    if (m_pLabelIRQ)
        m_pLabelIRQ->setEnabled(!fStd);
    if (m_pLineEditIRQ)
        m_pLineEditIRQ->setEnabled(!fStd);
    if (m_pLabelIOAddress)
        m_pLabelIOAddress->setEnabled(!fStd);
    if (m_pLineEditIOAddress)
        m_pLineEditIOAddress->setEnabled(!fStd);
    if (fStd)
    {
        if (m_pLineEditIRQ)
            m_pLineEditIRQ->setText(QString::number(uIRQ));
        if (m_pLineEditIOAddress)
            m_pLineEditIOAddress->setText("0x" + QString::number(uIOAddress, 16).toUpper());
    }

    /* Notify about standard port option changed: */
    emit sigStandardPortOptionChanged();
}

void UISerialSettingsEditor::sltHandleModeChange(int iIndex)
{
    /* Update availability: */
    const KPortMode enmMode = m_pComboMode ? m_pComboMode->itemData(iIndex).value<KPortMode>() : m_enmPortMode;
    if (m_pCheckBoxPipe)
        m_pCheckBoxPipe->setEnabled(enmMode == KPortMode_HostPipe || enmMode == KPortMode_TCP);
    if (m_pEditorPath)
        m_pEditorPath->setEnabled(enmMode != KPortMode_Disconnected);
    if (m_pLabelPath)
        m_pLabelPath->setEnabled(enmMode != KPortMode_Disconnected);

    /* Notify about mode changed: */
    emit sigModeChanged();
}

void UISerialSettingsEditor::prepare()
{
    /* Prepare everything: */
    prepareWidgets();
    prepareConnections();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UISerialSettingsEditor::prepareWidgets()
{
    /* Prepare main layout: */
    QGridLayout *pLayout = new QGridLayout(this);
    if (pLayout)
    {
        pLayout->setContentsMargins(0, 0, 0, 0);
        pLayout->setRowStretch(2, 1);

        /* Prepare port check-box: */
        m_pCheckBoxPort = new QCheckBox(this);
        if (m_pCheckBoxPort)
            pLayout->addWidget(m_pCheckBoxPort, 0, 0, 1, 2);

        /* Prepare 20-px shifting spacer: */
        QSpacerItem *pSpacerItem = new QSpacerItem(20, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        if (pSpacerItem)
            pLayout->addItem(pSpacerItem, 1, 0);

        /* Prepare adapter settings widget: */
        m_pWidgetPortSettings = new QWidget(this);
        if (m_pWidgetPortSettings)
        {
            /* Prepare adapter settings widget layout: */
            QGridLayout *pLayoutPortSettings = new QGridLayout(m_pWidgetPortSettings);
            if (pLayoutPortSettings)
            {
                pLayoutPortSettings->setContentsMargins(0, 0, 0, 0);
                pLayoutPortSettings->setColumnStretch(6, 1);

                /* Prepare number label: */
                m_pLabelNumber = new QLabel(m_pWidgetPortSettings);
                if (m_pLabelNumber)
                {
                    m_pLabelNumber->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    pLayoutPortSettings->addWidget(m_pLabelNumber, 0, 0);
                }
                /* Prepare number combo: */
                m_pComboNumber = new QComboBox(m_pWidgetPortSettings);
                if (m_pComboNumber)
                {
                    if (m_pLabelNumber)
                        m_pLabelNumber->setBuddy(m_pComboNumber);
                    m_pComboNumber->insertItem(0, UITranslator::toCOMPortName(0, 0));
                    m_pComboNumber->insertItems(0, UITranslator::COMPortNames());
                    pLayoutPortSettings->addWidget(m_pComboNumber, 0, 1);
                }
                /* Prepare IRQ label: */
                m_pLabelIRQ = new QLabel(m_pWidgetPortSettings);
                if (m_pLabelIRQ)
                    pLayoutPortSettings->addWidget(m_pLabelIRQ, 0, 2);
                /* Prepare IRQ label: */
                m_pLineEditIRQ = new QLineEdit(m_pWidgetPortSettings);
                if (m_pLineEditIRQ)
                {
                    if (m_pLabelIRQ)
                        m_pLabelIRQ->setBuddy(m_pLineEditIRQ);
                    m_pLineEditIRQ->setFixedWidth(m_pLineEditIRQ->fontMetrics().horizontalAdvance("8888"));
                    m_pLineEditIRQ->setValidator(new QIULongValidator(0, 255, this));
                    pLayoutPortSettings->addWidget(m_pLineEditIRQ, 0, 3);
                }
                /* Prepare IO address label: */
                m_pLabelIOAddress = new QLabel(m_pWidgetPortSettings);
                if (m_pLabelIOAddress)
                    pLayoutPortSettings->addWidget(m_pLabelIOAddress, 0, 4);
                /* Prepare IO address label: */
                m_pLineEditIOAddress = new QLineEdit(m_pWidgetPortSettings);
                if (m_pLineEditIOAddress)
                {
                    if (m_pLabelIOAddress)
                        m_pLabelIOAddress->setBuddy(m_pLineEditIOAddress);
                    m_pLineEditIOAddress->setFixedWidth(m_pLineEditIOAddress->fontMetrics().horizontalAdvance("8888888"));
                    m_pLineEditIOAddress->setValidator(new QIULongValidator(0, 0xFFFF, this));
                    pLayoutPortSettings->addWidget(m_pLineEditIOAddress, 0, 5);
                }

                /* Prepare mode label: */
                m_pLabelMode = new QLabel(m_pWidgetPortSettings);
                if (m_pLabelMode)
                {
                    m_pLabelMode->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    pLayoutPortSettings->addWidget(m_pLabelMode, 1, 0);
                }
                /* Prepare mode combo: */
                m_pComboMode = new QComboBox(m_pWidgetPortSettings);
                if (m_pComboMode)
                {
                    if (m_pLabelMode)
                        m_pLabelMode->setBuddy(m_pComboMode);
                    pLayoutPortSettings->addWidget(m_pComboMode, 1, 1);
                }

                /* Prepare pipe check-box: */
                m_pCheckBoxPipe = new QCheckBox(m_pWidgetPortSettings);
                if (m_pCheckBoxPipe)
                    pLayoutPortSettings->addWidget(m_pCheckBoxPipe, 2, 1, 1, 5);

                /* Prepare path label: */
                m_pLabelPath = new QLabel(m_pWidgetPortSettings);
                if (m_pLabelPath)
                {
                    m_pLabelPath->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    pLayoutPortSettings->addWidget(m_pLabelPath, 3, 0);
                }
                /* Prepare path editor: */
                m_pEditorPath = new QLineEdit(m_pWidgetPortSettings);
                if (m_pEditorPath)
                {
                    if (m_pLabelPath)
                        m_pLabelPath->setBuddy(m_pEditorPath);
                    m_pEditorPath->setValidator(new QRegularExpressionValidator(QRegularExpression(".+"), this));
                    pLayoutPortSettings->addWidget(m_pEditorPath, 3, 1, 1, 6);
                }
            }

            pLayout->addWidget(m_pWidgetPortSettings, 1, 1);
        }
    }
}

void UISerialSettingsEditor::prepareConnections()
{
    if (m_pCheckBoxPort)
        connect(m_pCheckBoxPort, &QCheckBox::toggled,
                this, &UISerialSettingsEditor::sltHandlePortAvailabilityToggled);
    if (m_pComboNumber)
        connect(m_pComboNumber, &QComboBox::textActivated,
                this, &UISerialSettingsEditor::sltHandleStandardPortOptionActivated);
    if (m_pLineEditIRQ)
        connect(m_pLineEditIRQ, &QLineEdit::textChanged, this, &UISerialSettingsEditor::sigPortIRQChanged);
    if (m_pLineEditIOAddress)
        connect(m_pLineEditIOAddress, &QLineEdit::textChanged, this, &UISerialSettingsEditor::sigPortIOAddressChanged);
    if (m_pComboMode)
        connect(m_pComboMode, &QComboBox::activated,
                this, &UISerialSettingsEditor::sltHandleModeChange);
    if (m_pEditorPath)
        connect(m_pEditorPath, &QLineEdit::textChanged, this, &UISerialSettingsEditor::sigPathChanged);
}

void UISerialSettingsEditor::populateCombo()
{
    if (m_pComboMode)
    {
        /* Clear the mode combo-box: */
        m_pComboMode->clear();

        /* Load currently supported port moded: */
        CSystemProperties comProperties = gpGlobalSession->virtualBox().GetSystemProperties();
        QVector<KPortMode> supportedModes = comProperties.GetSupportedPortModes();
        /* Take currently requested mode into account if it's sane: */
        if (!supportedModes.contains(m_enmPortMode) && m_enmPortMode != KPortMode_Max)
            supportedModes.prepend(m_enmPortMode);

        /* Populate port modes: */
        int iPortModeIndex = 0;
        foreach (const KPortMode &enmMode, supportedModes)
        {
            m_pComboMode->insertItem(iPortModeIndex, gpConverter->toString(enmMode));
            m_pComboMode->setItemData(iPortModeIndex, QVariant::fromValue(enmMode));
            m_pComboMode->setItemData(iPortModeIndex, m_pComboMode->itemText(iPortModeIndex), Qt::ToolTipRole);
            ++iPortModeIndex;
        }

        /* Choose requested mode: */
        const int iIndex = m_pComboMode->findData(QVariant::fromValue(m_enmPortMode));
        m_pComboMode->setCurrentIndex(iIndex != -1 ? iIndex : 0);
    }
}
