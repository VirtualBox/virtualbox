/* $Id: UIMachineSettingsSerial.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineSettingsSerial class implementation.
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
#include <QVBoxLayout>

/* GUI includes: */
#include "QITabWidget.h"
#include "UIErrorString.h"
#include "UIGlobalSession.h"
#include "UIMachineSettingsSerial.h"
#include "UISerialSettingsEditor.h"
#include "UITranslator.h"

/* COM includes: */
#include "CPlatformProperties.h"
#include "CSerialPort.h"


/** Machine settings: Serial Port tab data structure. */
struct UIDataSettingsMachineSerialPort
{
    /** Constructs data. */
    UIDataSettingsMachineSerialPort()
        : m_iSlot(-1)
        , m_fPortEnabled(false)
        , m_uIRQ(0)
        , m_uIOAddress(0)
        , m_hostMode(KPortMode_Disconnected)
        , m_fServer(false)
        , m_strPath(QString())
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataSettingsMachineSerialPort &other) const
    {
        return true
               && (m_iSlot == other.m_iSlot)
               && (m_fPortEnabled == other.m_fPortEnabled)
               && (m_uIRQ == other.m_uIRQ)
               && (m_uIOAddress == other.m_uIOAddress)
               && (m_hostMode == other.m_hostMode)
               && (m_fServer == other.m_fServer)
               && (m_strPath == other.m_strPath)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsMachineSerialPort &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataSettingsMachineSerialPort &other) const { return !equal(other); }

    /** Holds the serial port slot number. */
    int        m_iSlot;
    /** Holds whether the serial port is enabled. */
    bool       m_fPortEnabled;
    /** Holds the serial port IRQ. */
    ulong      m_uIRQ;
    /** Holds the serial port IO address. */
    ulong      m_uIOAddress;
    /** Holds the serial port host mode. */
    KPortMode  m_hostMode;
    /** Holds whether the serial port is server. */
    bool       m_fServer;
    /** Holds the serial port path. */
    QString    m_strPath;
};


/** Machine settings: Serial page data structure. */
struct UIDataSettingsMachineSerial
{
    /** Constructs data. */
    UIDataSettingsMachineSerial() {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsMachineSerial & /* other */) const { return true; }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataSettingsMachineSerial & /* other */) const { return false; }
};


/*********************************************************************************************************************************
*   Class UIMachineSettingsSerial implementation.                                                                                *
*********************************************************************************************************************************/

UIMachineSettingsSerial::UIMachineSettingsSerial()
    : m_pCache(0)
    , m_pTabWidget(0)
{
    prepare();
}

UIMachineSettingsSerial::~UIMachineSettingsSerial()
{
    cleanup();
}

bool UIMachineSettingsSerial::changed() const
{
    return m_pCache ? m_pCache->wasChanged() : false;
}

void UIMachineSettingsSerial::loadToCacheFrom(QVariant &data)
{
    /* Sanity check: */
    if (   !m_pCache
        || !m_pTabWidget)
        return;

    /* Fetch data to machine: */
    UISettingsPageMachine::fetchData(data);

    /* Clear cache initially: */
    m_pCache->clear();

    /* Cache lists: */
    refreshPorts();
    refreshPaths();

    /* Prepare old data: */
    UIDataSettingsMachineSerial oldSerialData;

    /* For each serial port: */
    for (int iSlot = 0; iSlot < m_pTabWidget->count(); ++iSlot)
    {
        /* Prepare old data: */
        UIDataSettingsMachineSerialPort oldPortData;

        /* Check whether port is valid: */
        const CSerialPort &comPort = m_machine.GetSerialPort(iSlot);
        if (!comPort.isNull())
        {
            /* Gather old data: */
            oldPortData.m_iSlot = iSlot;
            oldPortData.m_fPortEnabled = comPort.GetEnabled();
            oldPortData.m_uIRQ = comPort.GetIRQ();
            oldPortData.m_uIOAddress = comPort.GetIOAddress();
            oldPortData.m_hostMode = comPort.GetHostMode();
            oldPortData.m_fServer = comPort.GetServer();
            oldPortData.m_strPath = comPort.GetPath();
        }

        /* Cache old data: */
        m_pCache->child(iSlot).cacheInitialData(oldPortData);
    }

    /* Cache old data: */
    m_pCache->cacheInitialData(oldSerialData);

    /* Upload machine to data: */
    UISettingsPageMachine::uploadData(data);
}

void UIMachineSettingsSerial::getFromCache()
{
    /* Sanity check: */
    if (   !m_pCache
        || !m_pTabWidget)
        return;

    /* Load old data from cache: */
    for (int iSlot = 0; iSlot < m_pTabWidget->count(); ++iSlot)
        getFromCache(iSlot, m_pCache->child(iSlot));

    /* Apply language settings: */
    sltRetranslateUI();

    /* Polish page finally: */
    polishPage();

    /* Revalidate: */
    revalidate();
}

void UIMachineSettingsSerial::putToCache()
{
    /* Sanity check: */
    if (   !m_pCache
        || !m_pTabWidget)
        return;

    /* Prepare new data: */
    UIDataSettingsMachineSerial newSerialData;

    /* Gather new data to cache: */
    for (int iSlot = 0; iSlot < m_pTabWidget->count(); ++iSlot)
        putToCache(iSlot, m_pCache->child(iSlot));

    /* Cache new data: */
    m_pCache->cacheCurrentData(newSerialData);
}

void UIMachineSettingsSerial::saveFromCacheTo(QVariant &data)
{
    /* Fetch data to machine: */
    UISettingsPageMachine::fetchData(data);

    /* Update data and failing state: */
    setFailed(!saveData());

    /* Upload machine to data: */
    UISettingsPageMachine::uploadData(data);
}

bool UIMachineSettingsSerial::validate(QList<UIValidationMessage> &messages)
{
    /* Sanity check: */
    if (!m_pTabWidget)
        return false;

    /* Pass by default: */
    bool fValid = true;

    /* Delegate validation to adapter tabs: */
    for (int iSlot = 0; iSlot < m_pTabWidget->count(); ++iSlot)
        if (!validate(iSlot, messages))
            fValid = false;

    /* Return result: */
    return fValid;
}

void UIMachineSettingsSerial::sltRetranslateUI()
{
    /* Sanity check: */
    if (!m_pTabWidget)
        return;

    for (int iSlot = 0; iSlot < m_pTabWidget->count(); ++iSlot)
        m_pTabWidget->setTabText(iSlot, tabTitle(iSlot));
}

void UIMachineSettingsSerial::polishPage()
{
    /* Sanity check: */
    if (   !m_pCache
        || !m_pTabWidget)
        return;

    for (int iSlot = 0; iSlot < m_pTabWidget->count(); ++iSlot)
    {
        m_pTabWidget->setTabEnabled(iSlot,
                                    isMachineOffline() ||
                                    (isMachineInValidMode() &&
                                     m_pCache->childCount() > iSlot &&
                                     m_pCache->child(iSlot).base().m_fPortEnabled));
        polishTab(iSlot);
    }
}

void UIMachineSettingsSerial::sltHandlePortChange()
{
    refreshPorts();
    revalidate();
}

void UIMachineSettingsSerial::sltHandlePathChange()
{
    refreshPaths();
    revalidate();
}

void UIMachineSettingsSerial::prepare()
{
    /* Prepare cache: */
    m_pCache = new UISettingsCacheMachineSerial;
    AssertPtrReturnVoid(m_pCache);

    /* Prepare everything: */
    prepareWidgets();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIMachineSettingsSerial::prepareWidgets()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayoutMain = new QVBoxLayout(this);
    if (pLayoutMain)
    {
        /* Prepare tab-widget: */
        m_pTabWidget = new QITabWidget(this);
        if (m_pTabWidget)
        {
            /* How many ports to display: */
            const ulong uCount = gpGlobalSession->virtualBox().GetPlatformProperties(KPlatformArchitecture_x86).GetSerialPortCount();

            /* Create corresponding port tabs: */
            for (ulong uSlot = 0; uSlot < uCount; ++uSlot)
                prepareTab();

            pLayoutMain->addWidget(m_pTabWidget);
        }
    }
}

void UIMachineSettingsSerial::prepareTab()
{
    /* Prepare tab: */
    UIEditor *pTab = new UIEditor(m_pTabWidget);
    if (pTab)
    {
        /* Prepare tab layout: */
        QVBoxLayout *pLayout = new QVBoxLayout(pTab);
        if (pLayout)
        {
#ifdef VBOX_WS_MAC
            /* On Mac OS X we can do a bit of smoothness: */
            int iLeft, iTop, iRight, iBottom;
            pLayout->getContentsMargins(&iLeft, &iTop, &iRight, &iBottom);
            pLayout->setContentsMargins(iLeft / 2, iTop / 2, iRight / 2, iBottom / 2);
#endif

            /* Create port tab-editor: */
            UISerialSettingsEditor *pEditor = new UISerialSettingsEditor(this);
            if (pEditor)
            {
                m_tabEditors << pEditor;
                prepareConnections(pEditor);
                pTab->addEditor(pEditor);
                pLayout->addWidget(pEditor);
            }

            pLayout->addStretch();
        }

        addEditor(pTab);
        m_pTabWidget->addTab(pTab, QString());
    }
}

void UIMachineSettingsSerial::prepareConnections(UISerialSettingsEditor *pTabEditor)
{
    /* Tab connections: */
    connect(pTabEditor, &UISerialSettingsEditor::sigPortAvailabilityChanged,
            this, &UIMachineSettingsSerial::sltHandlePortChange);
    connect(pTabEditor, &UISerialSettingsEditor::sigPortAvailabilityChanged,
            this, &UIMachineSettingsSerial::sltHandlePathChange);
    connect(pTabEditor, &UISerialSettingsEditor::sigStandardPortOptionChanged,
            this, &UIMachineSettingsSerial::revalidate);
    connect(pTabEditor, &UISerialSettingsEditor::sigPortIRQChanged,
            this, &UIMachineSettingsSerial::sltHandlePortChange);
    connect(pTabEditor, &UISerialSettingsEditor::sigPortIOAddressChanged,
            this, &UIMachineSettingsSerial::sltHandlePortChange);
    connect(pTabEditor, &UISerialSettingsEditor::sigModeChanged,
            this, &UIMachineSettingsSerial::revalidate);
    connect(pTabEditor, &UISerialSettingsEditor::sigPathChanged,
            this, &UIMachineSettingsSerial::sltHandlePathChange);
}

void UIMachineSettingsSerial::cleanup()
{
    /* Cleanup cache: */
    delete m_pCache;
    m_pCache = 0;
}

void UIMachineSettingsSerial::polishTab(int iSlot)
{
    /* Acquire tab-editor: */
    UISerialSettingsEditor *pTabEditor = m_tabEditors.at(iSlot);
    AssertPtrReturnVoid(pTabEditor);

    /* Polish port page: */
    const bool fStd = pTabEditor->isPortStandardOne();
    const KPortMode enmMode = pTabEditor->hostMode();
    pTabEditor->setPortOptionsAvailable(isMachineOffline());
    pTabEditor->setIRQAndIOAddressOptionsAvailable(!fStd && isMachineOffline());
    pTabEditor->setHostModeOptionsAvailable(isMachineOffline());
    pTabEditor->setPipeOptionsAvailable(   (enmMode == KPortMode_HostPipe || enmMode == KPortMode_TCP)
                                        && isMachineOffline());
    pTabEditor->setPathOptionsAvailable(   enmMode != KPortMode_Disconnected
                                        && isMachineOffline());
}

void UIMachineSettingsSerial::getFromCache(int iSlot, const UISettingsCacheMachineSerialPort &portCache)
{
    /* Acquire tab-editor: */
    UISerialSettingsEditor *pTabEditor = m_tabEditors.at(iSlot);
    AssertPtrReturnVoid(pTabEditor);

    /* Get old data: */
    const UIDataSettingsMachineSerialPort &oldPortData = portCache.base();

    /* Load port data: */
    pTabEditor->setPortByIRQAndIOAddress(oldPortData.m_uIRQ, oldPortData.m_uIOAddress);
    pTabEditor->setIRQ(oldPortData.m_uIRQ);
    pTabEditor->setIOAddress(oldPortData.m_uIOAddress);
    pTabEditor->setHostMode(oldPortData.m_hostMode);
    pTabEditor->setServerEnabled(oldPortData.m_fServer);
    pTabEditor->setPath(oldPortData.m_strPath);
    // Should be done in th end to finalize availability:
    pTabEditor->setPortEnabled(oldPortData.m_fPortEnabled);
}

void UIMachineSettingsSerial::putToCache(int iSlot, UISettingsCacheMachineSerialPort &portCache)
{
    /* Acquire tab-editor: */
    UISerialSettingsEditor *pTabEditor = m_tabEditors.at(iSlot);
    AssertPtrReturnVoid(pTabEditor);

    /* Prepare new data: */
    UIDataSettingsMachineSerialPort newPortData;

    /* Save port number: */
    newPortData.m_iSlot = iSlot;

    /* Save port data: */
    newPortData.m_fPortEnabled = pTabEditor->isPortEnabled();
    newPortData.m_uIRQ = pTabEditor->irq();
    newPortData.m_uIOAddress = pTabEditor->ioAddress();
    newPortData.m_fServer = pTabEditor->isServerEnabled();
    newPortData.m_hostMode = pTabEditor->hostMode();
    newPortData.m_strPath = pTabEditor->path();

    /* Cache new data: */
    portCache.cacheCurrentData(newPortData);
}

QString UIMachineSettingsSerial::irq(int iSlot) const
{
    /* Acquire tab-editor: */
    UISerialSettingsEditor *pTabEditor = m_tabEditors.at(iSlot);
    AssertPtrReturn(pTabEditor, QString());
    return QString::number(pTabEditor->irq());
}

QString UIMachineSettingsSerial::ioAddress(int iSlot) const
{
    /* Acquire tab-editor: */
    UISerialSettingsEditor *pTabEditor = m_tabEditors.at(iSlot);
    AssertPtrReturn(pTabEditor, QString());
    return QString::number(pTabEditor->ioAddress());
}

bool UIMachineSettingsSerial::validate(int iSlot, QList<UIValidationMessage> &messages)
{
    /* Acquire tab-editor: */
    UISerialSettingsEditor *pTabEditor = m_tabEditors.at(iSlot);
    AssertPtrReturn(pTabEditor, false);

    /* Pass by default: */
    bool fPass = true;

    /* Prepare message: */
    UIValidationMessage message;
    message.first = UITranslator::removeAccelMark(tabTitle(iSlot));

    /* Validate enabled port only: */
    if (pTabEditor->isPortEnabled())
    {
        /* Check the port attribute emptiness & uniqueness: */
        const QString strIRQ = irq(iSlot);
        const QString strIOAddress = ioAddress(iSlot);
        const QPair<QString, QString> port = qMakePair(strIRQ, strIOAddress);

        if (strIRQ.isEmpty())
        {
            message.second << tr("No IRQ is currently specified.");
            fPass = false;
        }
        if (strIOAddress.isEmpty())
        {
            message.second << tr("No I/O port is currently specified.");
            fPass = false;
        }
        if (   !strIRQ.isEmpty()
            && !strIOAddress.isEmpty())
        {
            QVector<QPair<QString, QString> > currentPorts;
            currentPorts = ports();
            currentPorts.removeAt(iSlot);
            if (currentPorts.contains(port))
            {
                message.second << tr("Two or more ports have the same settings.");
                fPass = false;
            }
        }

        const KPortMode enmMode = pTabEditor->hostMode();
        if (enmMode != KPortMode_Disconnected)
        {
            const QString strPath = pTabEditor->path();

            if (strPath.isEmpty())
            {
                message.second << tr("No port path is currently specified.");
                fPass = false;
            }
            else
            {
                QVector<QString> currentPaths;
                currentPaths = paths();
                currentPaths.removeAt(iSlot);
                if (currentPaths.contains(strPath))
                {
                    message.second << tr("There are currently duplicate port paths specified.");
                    fPass = false;
                }
            }
        }
    }

    /* Serialize message: */
    if (!message.second.isEmpty())
        messages << message;

    /* Return result: */
    return fPass;
}

void UIMachineSettingsSerial::refreshPorts()
{
    /* Sanity check: */
    if (!m_pTabWidget)
        return;

    /* Reload port list: */
    m_ports.clear();
    m_ports.resize(m_pTabWidget->count());
    /* Append port list with data from all the tabs: */
    for (int iSlot = 0; iSlot < m_pTabWidget->count(); ++iSlot)
    {
        UISerialSettingsEditor *pTabEditor = m_tabEditors.at(iSlot);
        AssertPtrReturnVoid(pTabEditor);
        m_ports[iSlot] = pTabEditor->isPortEnabled() ? qMakePair(irq(iSlot), ioAddress(iSlot)) : qMakePair(QString(), QString());
    }
}

void UIMachineSettingsSerial::refreshPaths()
{
    /* Sanity check: */
    if (!m_pTabWidget)
        return;

    /* Reload path list: */
    m_paths.clear();
    m_paths.resize(m_pTabWidget->count());
    /* Append path list with data from all the tabs: */
    for (int iSlot = 0; iSlot < m_pTabWidget->count(); ++iSlot)
    {
        UISerialSettingsEditor *pTabEditor = m_tabEditors.at(iSlot);
        AssertPtrReturnVoid(pTabEditor);
        m_paths[iSlot] = pTabEditor->isPortEnabled() ? pTabEditor->path() : QString();
    }
}

/* static */
QString UIMachineSettingsSerial::tabTitle(int iSlot)
{
    return QString(tr("Port %1", "serial ports")).arg(QString("&%1").arg(iSlot + 1));
}

bool UIMachineSettingsSerial::saveData()
{
    /* Sanity check: */
    if (   !m_pCache
        || !m_pTabWidget)
        return false;

    /* Prepare result: */
    bool fSuccess = true;
    /* Save serial settings from cache: */
    if (fSuccess && isMachineInValidMode() && m_pCache->wasChanged())
    {
        /* For each port: */
        for (int iSlot = 0; fSuccess && iSlot < m_pTabWidget->count(); ++iSlot)
            fSuccess = savePortData(iSlot);
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsSerial::savePortData(int iSlot)
{
    /* Sanity check: */
    if (!m_pCache)
        return false;

    /* Prepare result: */
    bool fSuccess = true;
    /* Save adapter settings from cache: */
    if (fSuccess && m_pCache->child(iSlot).wasChanged())
    {
        /* Get old data from cache: */
        const UIDataSettingsMachineSerialPort &oldPortData = m_pCache->child(iSlot).base();
        /* Get new data from cache: */
        const UIDataSettingsMachineSerialPort &newPortData = m_pCache->child(iSlot).data();

        /* Get serial port for further activities: */
        CSerialPort comPort = m_machine.GetSerialPort(iSlot);
        fSuccess = m_machine.isOk() && comPort.isNotNull();

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
        else
        {
            // This *must* be first.
            // If the requested host mode is changed to disconnected we should do it first.
            // That allows to automatically fulfill the requirements for some of the settings below.
            /* Save port host mode: */
            if (   fSuccess && isMachineOffline()
                && newPortData.m_hostMode != oldPortData.m_hostMode
                && newPortData.m_hostMode == KPortMode_Disconnected)
            {
                comPort.SetHostMode(newPortData.m_hostMode);
                fSuccess = comPort.isOk();
            }
            /* Save whether the port is enabled: */
            if (fSuccess && isMachineOffline() && newPortData.m_fPortEnabled != oldPortData.m_fPortEnabled)
            {
                comPort.SetEnabled(newPortData.m_fPortEnabled);
                fSuccess = comPort.isOk();
            }
            /* Save port IRQ: */
            if (fSuccess && isMachineOffline() && newPortData.m_uIRQ != oldPortData.m_uIRQ)
            {
                comPort.SetIRQ(newPortData.m_uIRQ);
                fSuccess = comPort.isOk();
            }
            /* Save port IO address: */
            if (fSuccess && isMachineOffline() && newPortData.m_uIOAddress != oldPortData.m_uIOAddress)
            {
                comPort.SetIOAddress(newPortData.m_uIOAddress);
                fSuccess = comPort.isOk();
            }
            /* Save whether the port is server: */
            if (fSuccess && isMachineOffline() && newPortData.m_fServer != oldPortData.m_fServer)
            {
                comPort.SetServer(newPortData.m_fServer);
                fSuccess = comPort.isOk();
            }
            /* Save port path: */
            if (fSuccess && isMachineOffline() && newPortData.m_strPath != oldPortData.m_strPath)
            {
                comPort.SetPath(newPortData.m_strPath);
                fSuccess = comPort.isOk();
            }
            // This *must* be last.
            // The host mode will be changed to disconnected if some of the necessary
            // settings above will not meet the requirements for the selected mode.
            /* Save port host mode: */
            if (   fSuccess && isMachineOffline()
                && newPortData.m_hostMode != oldPortData.m_hostMode
                && newPortData.m_hostMode != KPortMode_Disconnected)
            {
                comPort.SetHostMode(newPortData.m_hostMode);
                fSuccess = comPort.isOk();
            }

            /* Show error message if necessary: */
            if (!fSuccess)
                notifyOperationProgressError(UIErrorString::formatErrorInfo(comPort));
        }
    }
    /* Return result: */
    return fSuccess;
}
