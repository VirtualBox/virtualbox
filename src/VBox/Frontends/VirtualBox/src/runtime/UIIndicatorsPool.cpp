/* $Id: UIIndicatorsPool.cpp 110192 2025-07-10 14:13:01Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIIndicatorsPool class implementation.
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

/* Qt includes: */
#include <QApplication>
#include <QAccessibleWidget>
#include <QHBoxLayout>
#include <QPainter>
#include <QRegularExpression>
#include <QStyle>
#include <QTimer>

/* GUI includes: */
#include "QIStatusBarIndicator.h"
#include "UIAnimationFramework.h"
#include "UICommon.h"
#include "UIConverter.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIHostComboEditor.h"
#include "UIIconPool.h"
#include "UIIndicatorsPool.h"
#include "UIMachine.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include "iprt/assert.h"


/** QIStateStatusBarIndicator extension for Runtime UI. */
class UISessionStateStatusBarIndicator : public QIStateStatusBarIndicator
{
    Q_OBJECT;

public:

    /** Constructor which remembers passed @a session object. */
    UISessionStateStatusBarIndicator(IndicatorType enmType, UIMachine *pMachine);

    /** Returns the indicator type. */
    IndicatorType type() const { return m_enmType; }

    /** Returns the indicator description. */
    virtual QString description() const { return m_strDescription; }

public slots:

    /** Abstract update routine. */
    virtual void updateAppearance() = 0;

protected slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI();

protected:

    /** Holds the indicator type. */
    const IndicatorType m_enmType;

    /** Holds the machine UI reference. */
    UIMachine *m_pMachine;

    /** Holds the indicator description. */
    QString m_strDescription;

    /** Holds the table format. */
    static const QString s_strTable;
    /** Holds the table row format 1. */
    static const QString s_strTableRow1;
    /** Holds the table row format 2. */
    static const QString s_strTableRow2;
    /** Holds the table row format 3. */
    static const QString s_strTableRow3;
    /** Holds the table row format 4. */
    static const QString s_strTableRow4;
};


/** QAccessibleWidget extension used as an accessibility interface for UISessionStateStatusBarIndicator. */
class QIAccessibilityInterfaceForUISessionStateStatusBarIndicator : public QAccessibleWidget
{
public:

    /** Returns an accessibility interface for passed @a strClassname and @a pObject. */
    static QAccessibleInterface *pFactory(const QString &strClassname, QObject *pObject)
    {
        /* Creating UISessionStateStatusBarIndicator accessibility interface: */
        if (pObject && strClassname == QLatin1String("UISessionStateStatusBarIndicator"))
            return new QIAccessibilityInterfaceForUISessionStateStatusBarIndicator(qobject_cast<QWidget*>(pObject));

        /* Null by default: */
        return 0;
    }

    /** Constructs an accessibility interface passing @a pWidget to the base-class. */
    QIAccessibilityInterfaceForUISessionStateStatusBarIndicator(QWidget *pWidget)
        : QAccessibleWidget(pWidget, QAccessible::Button)
    {}

    /** Returns a text for the passed @a enmTextRole. */
    virtual QString text(QAccessible::Text /* enmTextRole */) const RT_OVERRIDE
    {
        /* Sanity check: */
        AssertPtrReturn(indicator(), 0);

        /* Return the indicator description: */
        return indicator()->description();
    }

private:

    /** Returns corresponding UISessionStateStatusBarIndicator. */
    UISessionStateStatusBarIndicator *indicator() const { return qobject_cast<UISessionStateStatusBarIndicator*>(widget()); }
};


/* static */
const QString UISessionStateStatusBarIndicator::s_strTable = QString("<table cellspacing=5 style='white-space:pre'>%1</table>");
/* static */
const QString UISessionStateStatusBarIndicator::s_strTableRow1 = QString("<tr><td colspan='2'><nobr><b>%1</b></nobr></td></tr>");
/* static */
const QString UISessionStateStatusBarIndicator::s_strTableRow2 = QString("<tr><td><nobr>%1:</nobr></td><td><nobr>%2</nobr></td></tr>");
/* static */
const QString UISessionStateStatusBarIndicator::s_strTableRow3 = QString("<tr><td><nobr>%1</nobr></td><td><nobr>%2</nobr></td></tr>");
/* static */
const QString UISessionStateStatusBarIndicator::s_strTableRow4 = QString("<tr><td><nobr>&nbsp;%1:</nobr></td><td><nobr>%2</nobr></td></tr>");


UISessionStateStatusBarIndicator::UISessionStateStatusBarIndicator(IndicatorType enmType, UIMachine *pMachine)
    : m_enmType(enmType)
    , m_pMachine(pMachine)
{
    /* Install UISessionStateStatusBarIndicator accessibility interface factory: */
    QAccessible::installFactory(QIAccessibilityInterfaceForUISessionStateStatusBarIndicator::pFactory);
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UISessionStateStatusBarIndicator::sltRetranslateUI);
}

void UISessionStateStatusBarIndicator::sltRetranslateUI()
{
    /* Translate description: */
    m_strDescription = tr("%1 status-bar indicator", "like 'hard-disk status-bar indicator'")
                         .arg(gpConverter->toString(type()));
}


/** QITextStatusBarIndicator extension for Runtime UI. */
class UISessionTextStatusBarIndicator : public QITextStatusBarIndicator
{
    Q_OBJECT;

public:

    /** Constructor which remembers passed @a session object. */
    UISessionTextStatusBarIndicator(IndicatorType enmType);

    /** Returns the indicator type. */
    IndicatorType type() const { return m_enmType; }

    /** Returns the indicator description. */
    virtual QString description() const { return m_strDescription; }

public slots:

    /** Abstract update routine. */
    virtual void updateAppearance() = 0;

protected slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI();

protected:

    /** Holds the indicator type. */
    const IndicatorType m_enmType;

    /** Holds the indicator description. */
    QString m_strDescription;
};


/** QAccessibleWidget extension used as an accessibility interface for UISessionTextStatusBarIndicator. */
class QIAccessibilityInterfaceForUISessionTextStatusBarIndicator : public QAccessibleWidget
{
public:

    /** Returns an accessibility interface for passed @a strClassname and @a pObject. */
    static QAccessibleInterface *pFactory(const QString &strClassname, QObject *pObject)
    {
        /* Creating UISessionTextStatusBarIndicator accessibility interface: */
        if (pObject && strClassname == QLatin1String("UISessionTextStatusBarIndicator"))
            return new QIAccessibilityInterfaceForUISessionTextStatusBarIndicator(qobject_cast<QWidget*>(pObject));

        /* Null by default: */
        return 0;
    }

    /** Constructs an accessibility interface passing @a pWidget to the base-class. */
    QIAccessibilityInterfaceForUISessionTextStatusBarIndicator(QWidget *pWidget)
        : QAccessibleWidget(pWidget, QAccessible::Button)
    {}

    /** Returns a text for the passed @a enmTextRole. */
    virtual QString text(QAccessible::Text /* enmTextRole */) const RT_OVERRIDE
    {
        /* Sanity check: */
        AssertPtrReturn(indicator(), 0);

        /* Return the indicator description: */
        return indicator()->description();
    }

private:

    /** Returns corresponding UISessionTextStatusBarIndicator. */
    UISessionTextStatusBarIndicator *indicator() const { return qobject_cast<UISessionTextStatusBarIndicator*>(widget()); }
};


UISessionTextStatusBarIndicator::UISessionTextStatusBarIndicator(IndicatorType enmType)
    : m_enmType(enmType)
{
    /* Install UISessionTextStatusBarIndicator accessibility interface factory: */
    QAccessible::installFactory(QIAccessibilityInterfaceForUISessionTextStatusBarIndicator::pFactory);
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UISessionTextStatusBarIndicator::sltRetranslateUI);
}

void UISessionTextStatusBarIndicator::sltRetranslateUI()
{
    /* Translate description: */
    m_strDescription = tr("%1 status-bar indicator", "like 'hard-disk status-bar indicator'")
                         .arg(gpConverter->toString(type()));
}


/** UISessionStateStatusBarIndicator extension for Runtime UI: Hard-drive indicator. */
class UIIndicatorHardDrive : public UISessionStateStatusBarIndicator
{
    Q_OBJECT;

public:

    /** Constructs indicator passing @a pMachine to the base-class. */
    UIIndicatorHardDrive(UIMachine *pMachine)
        : UISessionStateStatusBarIndicator(IndicatorType_HardDisks, pMachine)
        , m_cAttachmentsCount(0)
    {
        /* Assign state-icons: */
        setStateIcon(KDeviceActivity_Idle,    UIIconPool::iconSet(":/hd_16px.png"));
        setStateIcon(KDeviceActivity_Reading, UIIconPool::iconSet(":/hd_read_16px.png"));
        setStateIcon(KDeviceActivity_Writing, UIIconPool::iconSet(":/hd_write_16px.png"));
        setStateIcon(KDeviceActivity_Null,    UIIconPool::iconSet(":/hd_disabled_16px.png"));
        /* Configure connection: */
        connect(m_pMachine, &UIMachine::sigMachineStateChange,
                this, &UIIndicatorHardDrive::updateAppearance);
        connect(m_pMachine, &UIMachine::sigStorageDeviceChange,
                this, &UIIndicatorHardDrive::updateAppearance);
        connect(m_pMachine, &UIMachine::sigMediumChange,
                this, &UIIndicatorHardDrive::updateAppearance);
        /* Update & translate finally: */
        updateAppearance();
    }

protected slots:

    /** Update routine. */
    virtual void updateAppearance() RT_OVERRIDE
    {
        /* Acquire data: */
        QString strFullData;
        m_cAttachmentsCount = 0;
        m_pMachine->acquireHardDiskStatusInfo(strFullData, m_cAttachmentsCount);

        /* Show/hide indicator if there are no attachments
         * and parent is visible already: */
        if (   parentWidget()
            && parentWidget()->isVisible())
            setVisible(m_cAttachmentsCount);

        /* Update tool-tip: */
        if (!strFullData.isEmpty())
            setToolTip(s_strTable.arg(strFullData));
        /* Update indicator state: */
        setState(m_cAttachmentsCount ? KDeviceActivity_Idle : KDeviceActivity_Null);

        /* Retranslate finally: */
        sltRetranslateUI();
    }

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE
    {
        /* Call to base-class: */
        UISessionStateStatusBarIndicator::sltRetranslateUI();

        /* Append description with more info: */
        const QString strAttachmentStatus = tr("%1 disks attached").arg(m_cAttachmentsCount);
        m_strDescription = QString("%1, %2").arg(m_strDescription, strAttachmentStatus);
    }

private:

    /** Holds the last cached attachment count. */
    uint m_cAttachmentsCount;
};


/** UISessionStateStatusBarIndicator extension for Runtime UI: Optical-drive indicator. */
class UIIndicatorOpticalDisks : public UISessionStateStatusBarIndicator
{
    Q_OBJECT;

public:

    /** Constructs indicator passing @a pMachine to the base-class. */
    UIIndicatorOpticalDisks(UIMachine *pMachine)
        : UISessionStateStatusBarIndicator(IndicatorType_OpticalDisks, pMachine)
        , m_cAttachmentsCount(0)
        , m_cAttachmentsMountedCount(0)
    {
        /* Assign state-icons: */
        setStateIcon(KDeviceActivity_Idle,    UIIconPool::iconSet(":/cd_16px.png"));
        setStateIcon(KDeviceActivity_Reading, UIIconPool::iconSet(":/cd_read_16px.png"));
        setStateIcon(KDeviceActivity_Writing, UIIconPool::iconSet(":/cd_write_16px.png"));
        setStateIcon(KDeviceActivity_Null,    UIIconPool::iconSet(":/cd_disabled_16px.png"));
        /* Configure connection: */
        connect(m_pMachine, &UIMachine::sigMachineStateChange,
                this, &UIIndicatorOpticalDisks::updateAppearance);
        connect(m_pMachine, &UIMachine::sigStorageDeviceChange,
                this, &UIIndicatorOpticalDisks::updateAppearance);
        connect(m_pMachine, &UIMachine::sigMediumChange,
                this, &UIIndicatorOpticalDisks::updateAppearance);
        /* Update & translate finally: */
        updateAppearance();
    }

protected slots:

    /** Update routine. */
    virtual void updateAppearance() RT_OVERRIDE
    {
        QString strFullData;
        m_cAttachmentsCount = 0;
        m_cAttachmentsMountedCount = 0;
        m_pMachine->acquireOpticalDiskStatusInfo(strFullData, m_cAttachmentsCount, m_cAttachmentsMountedCount);

        /* Show/hide indicator if there are no attachments
         * and parent is visible already: */
        if (   parentWidget()
            && parentWidget()->isVisible())
            setVisible(m_cAttachmentsCount);

        /* Update tool-tip: */
        if (!strFullData.isEmpty())
            setToolTip(s_strTable.arg(strFullData));
        /* Update indicator state: */
        setState(m_cAttachmentsMountedCount ? KDeviceActivity_Idle : KDeviceActivity_Null);

        /* Retranslate finally: */
        sltRetranslateUI();
    }

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE
    {
        /* Call to base-class: */
        UISessionStateStatusBarIndicator::sltRetranslateUI();

        /* Append description with more info: */
        const QString strAttachmentStatus = tr("%1 drives attached").arg(m_cAttachmentsCount);
        const QString strMountingStatus = tr("%1 images mounted").arg(m_cAttachmentsMountedCount);
        m_strDescription = QString("%1, %2, %3").arg(m_strDescription, strAttachmentStatus, strMountingStatus);
    }

private:

    /** Holds the last cached attachment count. */
    uint m_cAttachmentsCount;
    /** Holds the last cached mounted attachment count. */
    uint m_cAttachmentsMountedCount;
};


/** UISessionStateStatusBarIndicator extension for Runtime UI: Floppy-drive indicator. */
class UIIndicatorFloppyDisks : public UISessionStateStatusBarIndicator
{
    Q_OBJECT;

public:

    /** Constructs indicator passing @a pMachine to the base-class. */
    UIIndicatorFloppyDisks(UIMachine *pMachine)
        : UISessionStateStatusBarIndicator(IndicatorType_FloppyDisks, pMachine)
        , m_cAttachmentsCount(0)
        , m_cAttachmentsMountedCount(0)
    {
        /* Assign state-icons: */
        setStateIcon(KDeviceActivity_Idle,    UIIconPool::iconSet(":/fd_16px.png"));
        setStateIcon(KDeviceActivity_Reading, UIIconPool::iconSet(":/fd_read_16px.png"));
        setStateIcon(KDeviceActivity_Writing, UIIconPool::iconSet(":/fd_write_16px.png"));
        setStateIcon(KDeviceActivity_Null,    UIIconPool::iconSet(":/fd_disabled_16px.png"));
        /* Configure connection: */
        connect(m_pMachine, &UIMachine::sigMachineStateChange,
                this, &UIIndicatorFloppyDisks::updateAppearance);
        connect(m_pMachine, &UIMachine::sigStorageDeviceChange,
                this, &UIIndicatorFloppyDisks::updateAppearance);
        connect(m_pMachine, &UIMachine::sigMediumChange,
                this, &UIIndicatorFloppyDisks::updateAppearance);
        /* Update & translate finally: */
        updateAppearance();
    }

protected slots:

    /** Update routine. */
    virtual void updateAppearance() RT_OVERRIDE
    {
        QString strFullData;
        m_cAttachmentsCount = 0;
        m_cAttachmentsMountedCount = 0;
        m_pMachine->acquireFloppyDiskStatusInfo(strFullData, m_cAttachmentsCount, m_cAttachmentsMountedCount);

        /* Show/hide indicator if there are no attachments
         * and parent is visible already: */
        if (   parentWidget()
            && parentWidget()->isVisible())
            setVisible(m_cAttachmentsCount);

        /* Update tool-tip: */
        if (!strFullData.isEmpty())
            setToolTip(s_strTable.arg(strFullData));
        /* Update indicator state: */
        setState(m_cAttachmentsMountedCount ? KDeviceActivity_Idle : KDeviceActivity_Null);

        /* Retranslate finally: */
        sltRetranslateUI();
    }

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE
    {
        /* Call to base-class: */
        UISessionStateStatusBarIndicator::sltRetranslateUI();

        /* Append description with more info: */
        const QString strAttachmentStatus = tr("%1 drives attached").arg(m_cAttachmentsCount);
        const QString strMountingStatus = tr("%1 images mounted").arg(m_cAttachmentsMountedCount);
        m_strDescription = QString("%1, %2, %3").arg(m_strDescription, strAttachmentStatus, strMountingStatus);
    }

private:

    /** Holds the last cached attachment count. */
    uint m_cAttachmentsCount;
    /** Holds the last cached mounted attachment count. */
    uint m_cAttachmentsMountedCount;
};


/** UISessionStateStatusBarIndicator extension for Runtime UI: Audio indicator. */
class UIIndicatorAudio : public UISessionStateStatusBarIndicator
{
    Q_OBJECT;

public:

    /** Audio states. */
    enum AudioState
    {
        AudioState_AllOff   = 0,
        AudioState_OutputOn = RT_BIT(0),
        AudioState_InputOn  = RT_BIT(1),
        AudioState_AllOn    = AudioState_InputOn | AudioState_OutputOn
    };

    /** Constructs indicator passing @a pMachine to the base-class. */
    UIIndicatorAudio(UIMachine *pMachine)
        : UISessionStateStatusBarIndicator(IndicatorType_Audio, pMachine)
        , m_fOutputEnabled(false)
        , m_fInputEnabled(false)
    {
        /* Assign state-icons: */
        setStateIcon(AudioState_AllOff, UIIconPool::iconSet(":/audio_all_off_16px.png"));
        setStateIcon(AudioState_OutputOn, UIIconPool::iconSet(":/audio_input_off_16px.png"));
        setStateIcon(AudioState_InputOn, UIIconPool::iconSet(":/audio_output_off_16px.png"));
        setStateIcon(AudioState_AllOn, UIIconPool::iconSet(":/audio_16px.png"));
        /* Configure connection: */
        connect(m_pMachine, &UIMachine::sigMachineStateChange,
                this, &UIIndicatorAudio::updateAppearance);
        connect(m_pMachine, &UIMachine::sigInitialized,
                this, &UIIndicatorAudio::updateAppearance);
        connect(m_pMachine, &UIMachine::sigAudioAdapterChange,
                this, &UIIndicatorAudio::updateAppearance);
        /* Update & translate finally: */
        updateAppearance();
    }

protected slots:

    /** Update routine. */
    virtual void updateAppearance() RT_OVERRIDE
    {
        QString strFullData;
        bool fAudioEnabled = false;
        m_fOutputEnabled = false;
        m_fInputEnabled = false;
        m_pMachine->acquireAudioStatusInfo(strFullData, fAudioEnabled, m_fOutputEnabled, m_fInputEnabled);

        /* Show/hide indicator if there are no attachments
         * and parent is visible already: */
        if (   parentWidget()
            && parentWidget()->isVisible())
            setVisible(fAudioEnabled);

        /* Update tool-tip: */
        if (!strFullData.isEmpty())
            setToolTip(s_strTable.arg(strFullData));
        /* Update indicator state: */
        AudioState enmState = AudioState_AllOff;
        if (m_fOutputEnabled)
            enmState = (AudioState)(enmState | AudioState_OutputOn);
        if (m_fInputEnabled)
            enmState = (AudioState)(enmState | AudioState_InputOn);
        setState(enmState);

        /* Retranslate finally: */
        sltRetranslateUI();
    }

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE
    {
        /* Call to base-class: */
        UISessionStateStatusBarIndicator::sltRetranslateUI();

        /* Append description with more info: */
        const QString strOutputStatus = m_fOutputEnabled ? tr("Output enabled") : tr("Output disabled");
        const QString strInputStatus = m_fInputEnabled ? tr("Input enabled") : tr("Input disabled");
        m_strDescription = QString("%1, %2, %3").arg(m_strDescription, strOutputStatus, strInputStatus);
    }

private:

    /** Holds whether audio output enabled. */
    bool  m_fOutputEnabled;
    /** Holds whether audio input enabled. */
    bool  m_fInputEnabled;
};


/** UISessionStateStatusBarIndicator extension for Runtime UI: Network indicator. */
class UIIndicatorNetwork : public UISessionStateStatusBarIndicator
{
    Q_OBJECT;

public:

    /** Constructs indicator passing @a pMachine to the base-class. */
    UIIndicatorNetwork(UIMachine *pMachine)
        : UISessionStateStatusBarIndicator(IndicatorType_Network, pMachine)
        , m_fAdaptersPresent(false)
        , m_fCablesDisconnected(true)
    {
        /* Assign state-icons: */
        setStateIcon(KDeviceActivity_Idle,    UIIconPool::iconSet(":/nw_16px.png"));
        setStateIcon(KDeviceActivity_Reading, UIIconPool::iconSet(":/nw_read_16px.png"));
        setStateIcon(KDeviceActivity_Writing, UIIconPool::iconSet(":/nw_write_16px.png"));
        setStateIcon(KDeviceActivity_Null,    UIIconPool::iconSet(":/nw_disabled_16px.png"));
        /* Configure connection: */
        connect(m_pMachine, &UIMachine::sigMachineStateChange,
                this, &UIIndicatorNetwork::updateAppearance);
        connect(m_pMachine, &UIMachine::sigNetworkAdapterChange,
                this, &UIIndicatorNetwork::updateAppearance);
        /* Update & translate finally: */
        updateAppearance();
    }

protected slots:

    /** Update routine. */
    virtual void updateAppearance() RT_OVERRIDE
    {
        QString strFullData;
        m_fAdaptersPresent = false;
        m_fCablesDisconnected = true;
        m_pMachine->acquireNetworkStatusInfo(strFullData, m_fAdaptersPresent, m_fCablesDisconnected);

        /* Show/hide indicator if there are no attachments
         * and parent is visible already: */
        if (   parentWidget()
            && parentWidget()->isVisible())
            setVisible(m_fAdaptersPresent);

        /* Update tool-tip: */
        if (!strFullData.isEmpty())
            setToolTip(s_strTable.arg(strFullData));
        /* Update indicator state: */
        setState(m_fAdaptersPresent && !m_fCablesDisconnected ? KDeviceActivity_Idle : KDeviceActivity_Null);

        /* Retranslate finally: */
        sltRetranslateUI();
    }

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE
    {
        /* Call to base-class: */
        UISessionStateStatusBarIndicator::sltRetranslateUI();

        /* Append description with more info: */
        QStringList info;
        info << (m_fAdaptersPresent ? tr("Adapters present") : tr("No network adapters"));
        if (m_fCablesDisconnected)
            info << tr("All cables disconnected");
        m_strDescription = QString("%1, %2").arg(m_strDescription, info.join(", "));
    }

private:

    /** Holds whether adapters present. */
    bool  m_fAdaptersPresent;
    /** Holds whether cables disconnected. */
    bool  m_fCablesDisconnected;
};


/** UISessionStateStatusBarIndicator extension for Runtime UI: USB indicator. */
class UIIndicatorUSB : public UISessionStateStatusBarIndicator
{
    Q_OBJECT;

public:

    /** Constructs indicator passing @a pMachine to the base-class. */
    UIIndicatorUSB(UIMachine *pMachine)
        : UISessionStateStatusBarIndicator(IndicatorType_USB, pMachine)
        , m_fUsbEnabled(false)
        , m_cUsbFilterCount(0)
    {
        /* Assign state-icons: */
        setStateIcon(KDeviceActivity_Idle,    UIIconPool::iconSet(":/usb_16px.png"));
        setStateIcon(KDeviceActivity_Reading, UIIconPool::iconSet(":/usb_read_16px.png"));
        setStateIcon(KDeviceActivity_Writing, UIIconPool::iconSet(":/usb_write_16px.png"));
        setStateIcon(KDeviceActivity_Null,    UIIconPool::iconSet(":/usb_disabled_16px.png"));
        /* Configure connection: */
        connect(m_pMachine, &UIMachine::sigMachineStateChange,
                this, &UIIndicatorUSB::updateAppearance);
        connect(m_pMachine, &UIMachine::sigUSBControllerChange,
                this, &UIIndicatorUSB::updateAppearance);
        connect(m_pMachine, &UIMachine::sigUSBDeviceStateChange,
                this, &UIIndicatorUSB::updateAppearance);
        /* Update & translate finally: */
        updateAppearance();
    }

protected slots:

    /** Update routine. */
    virtual void updateAppearance() RT_OVERRIDE
    {
        QString strFullData;
        m_fUsbEnabled = false;
        m_cUsbFilterCount = 0;
        m_pMachine->acquireUsbStatusInfo(strFullData, m_fUsbEnabled, m_cUsbFilterCount);

        /* Show/hide indicator if there are no attachments
         * and parent is visible already: */
        if (   parentWidget()
            && parentWidget()->isVisible())
            setVisible(m_fUsbEnabled);

        /* Update tool-tip: */
        if (!strFullData.isEmpty())
            setToolTip(s_strTable.arg(strFullData));
        /* Update indicator state: */
        setState(m_fUsbEnabled ? KDeviceActivity_Idle : KDeviceActivity_Null);

        /* Retranslate finally: */
        sltRetranslateUI();
    }

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE
    {
        /* Call to base-class: */
        UISessionStateStatusBarIndicator::sltRetranslateUI();

        /* Append description with more info: */
        const QString strUsbStatus = m_fUsbEnabled ? tr("USB enabled") : tr("USB disabled");
        const QString strFilterCount = m_cUsbFilterCount ? tr("%1 USB devices attached").arg(m_cUsbFilterCount)
                                                         : tr("No USB devices attached");
        m_strDescription = QString("%1, %2, %3").arg(m_strDescription, strUsbStatus, strFilterCount);
    }

private:

    /** Holds whether USB subsystem is enabled. */
    bool  m_fUsbEnabled;
    /** Holds USB device filter count. */
    uint  m_cUsbFilterCount;
};


/** UISessionStateStatusBarIndicator extension for Runtime UI: Shared-folders indicator. */
class UIIndicatorSharedFolders : public UISessionStateStatusBarIndicator
{
    Q_OBJECT;

public:

    /** Constructs indicator passing @a pMachine to the base-class. */
    UIIndicatorSharedFolders(UIMachine *pMachine)
        : UISessionStateStatusBarIndicator(IndicatorType_SharedFolders, pMachine)
        , m_cFoldersCount(0)
    {
        /* Assign state-icons: */
        setStateIcon(KDeviceActivity_Idle,    UIIconPool::iconSet(":/sf_16px.png"));
        setStateIcon(KDeviceActivity_Reading, UIIconPool::iconSet(":/sf_read_16px.png"));
        setStateIcon(KDeviceActivity_Writing, UIIconPool::iconSet(":/sf_write_16px.png"));
        setStateIcon(KDeviceActivity_Null,    UIIconPool::iconSet(":/sf_disabled_16px.png"));
        /* Configure connection: */
        connect(m_pMachine, &UIMachine::sigMachineStateChange,
                this, &UIIndicatorSharedFolders::updateAppearance);
        connect(m_pMachine, &UIMachine::sigSharedFolderChange,
                this, &UIIndicatorSharedFolders::updateAppearance);
        /* Update & translate finally: */
        updateAppearance();
    }

protected slots:

    /** Update routine. */
    virtual void updateAppearance() RT_OVERRIDE
    {
        QString strFullData;
        m_cFoldersCount = 0;
        m_pMachine->acquireSharedFoldersStatusInfo(strFullData, m_cFoldersCount);

        /* Update tool-tip: */
        if (!strFullData.isEmpty())
            setToolTip(s_strTable.arg(strFullData));
        /* Update indicator state: */
        setState(m_cFoldersCount ? KDeviceActivity_Idle : KDeviceActivity_Null);

        /* Retranslate finally: */
        sltRetranslateUI();
    }

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE
    {
        /* Call to base-class: */
        UISessionStateStatusBarIndicator::sltRetranslateUI();

        /* Append description with more info: */
        const QString strFoldersCount = m_cFoldersCount ? tr("%1 shared folders").arg(m_cFoldersCount)
                                                         : tr("No shared folders");
        m_strDescription = QString("%1, %2").arg(m_strDescription, strFoldersCount);
    }

private:

    /** Holds the amount of folders. */
    uint  m_cFoldersCount;
};


/** UISessionStateStatusBarIndicator extension for Runtime UI: Display indicator. */
class UIIndicatorDisplay : public UISessionStateStatusBarIndicator
{
    Q_OBJECT;

public:

    /** Display states. */
    enum DisplayState
    {
        DisplayState_Unavailable = 0,
        DisplayState_Software = 1,
        DisplayState_Hardware = 2
    };

    /** Constructs indicator passing @a pMachine to the base-class. */
    UIIndicatorDisplay(UIMachine *pMachine)
        : UISessionStateStatusBarIndicator(IndicatorType_Display, pMachine)
        , m_uVRAMSize(0)
        , m_cMonitorCount(0)
        , m_fAcceleration3D(false)
    {
        /* Assign state-icons: */
        setStateIcon(DisplayState_Unavailable, UIIconPool::iconSet(":/display_software_disabled_16px.png"));
        setStateIcon(DisplayState_Software,    UIIconPool::iconSet(":/display_software_16px.png"));
        setStateIcon(DisplayState_Hardware,    UIIconPool::iconSet(":/display_hardware_16px.png"));
        /* Configure connection: */
        connect(m_pMachine, &UIMachine::sigMachineStateChange,
                this, &UIIndicatorDisplay::updateAppearance);
        /* Update & translate finally: */
        updateAppearance();
    }

protected slots:

    /** Update routine. */
    virtual void updateAppearance() RT_OVERRIDE
    {
        QString strFullData;
        m_uVRAMSize = 0;
        m_cMonitorCount = 0;
        m_fAcceleration3D = false;
        m_pMachine->acquireDisplayStatusInfo(strFullData, m_uVRAMSize, m_cMonitorCount, m_fAcceleration3D);

        /* Update tool-tip: */
        if (!strFullData.isEmpty())
            setToolTip(s_strTable.arg(strFullData));
        /* Update indicator state: */
        DisplayState enmState = DisplayState_Unavailable;
        if (m_pMachine->machineState() != KMachineState_Null)
        {
            if (!m_fAcceleration3D)
                enmState = DisplayState_Software;
            else
                enmState = DisplayState_Hardware;
        }
        setState(enmState);

        /* Retranslate finally: */
        sltRetranslateUI();
    }

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE
    {
        /* Call to base-class: */
        UISessionStateStatusBarIndicator::sltRetranslateUI();

        /* Append description with more info: */
        QStringList info;
        info << tr("%1 MB").arg(m_uVRAMSize);
        if (m_cMonitorCount > 1)
            info << tr("%1 monitors connected").arg(m_cMonitorCount);
        if (m_fAcceleration3D)
            info << tr("3D acceleration enabled");
        m_strDescription = QString("%1, %2").arg(m_strDescription, info.join(", "));
    }

private:

    /** Holds the VRAM size. */
    uint  m_uVRAMSize;
    /** Holds the monitor count. */
    uint  m_cMonitorCount;
    /** Holds whether 3D acceleration is enabled. */
    bool  m_fAcceleration3D;
};


/** UISessionStateStatusBarIndicator extension for Runtime UI: Recording indicator. */
class UIIndicatorRecording : public UISessionStateStatusBarIndicator
{
    Q_OBJECT;
    Q_PROPERTY(int alphaFactorMin READ alphaFactorMin);
    Q_PROPERTY(int alphaFactorMax READ alphaFactorMax);
    Q_PROPERTY(int alphaFactor READ alphaFactor WRITE setAlphaFactor);

    /** Recording states. */
    enum RecordingState
    {
        RecordingState_Unavailable = 0,
        RecordingState_Disabled    = 1,
        RecordingState_Enabled     = 2,
        RecordingState_Paused      = 3
    };

public:

    /** Constructs indicator passing @a pMachine to the base-class. */
    UIIndicatorRecording(UIMachine *pMachine)
        : UISessionStateStatusBarIndicator(IndicatorType_Recording, pMachine)
        , m_pAnimation(0)
        , m_iAlphaFactor(0)
        , m_enmState(RecordingState_Unavailable)
    {
        /* Assign state-icons: */
        setStateIcon(RecordingState_Unavailable, UIIconPool::iconSet(":/video_capture_disabled_16px.png"));
        setStateIcon(RecordingState_Disabled,    UIIconPool::iconSet(":/video_capture_16px.png"));
        setStateIcon(RecordingState_Enabled,     UIIconPool::iconSet(":/movie_reel_16px.png"));
        setStateIcon(RecordingState_Paused,      UIIconPool::iconSet(":/movie_reel_16px.png"));
        /* Configure connection: */
        connect(m_pMachine, &UIMachine::sigMachineStateChange,
                this, &UIIndicatorRecording::updateAppearance);
        connect(m_pMachine, &UIMachine::sigRecordingChange,
                this, &UIIndicatorRecording::updateAppearance);
        /* Create *enabled* state animation: */
        m_pAnimation = UIAnimationLoop::installAnimationLoop(this, "alphaFactor",
                                                             "alphaFactorMin", "alphaFactorMax",
                                                             2000);
        /* Update & translate finally: */
        updateAppearance();
    }

protected:

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE
    {
        /* For Disabled indicator => Call to base-class: */
        if (state() == RecordingState_Disabled)
            return UISessionStateStatusBarIndicator::paintEvent(pEvent);

        /* Create and configure painter: */
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        /* Default alpha is alphaFactorMin(), for Paused state: */
        int iAlpha = alphaFactorMin();

        /* Configure alpha for Enabled state: */
        if (state() == RecordingState_Enabled)
        {
            /* UIAnimationFramework returns an value in increasing order.
             * Convert it to a value between min and max that ping pongs between min and max: */

            /* First get normalized value: */
            double dAlpha = (double)(alphaFactor() - alphaFactorMin()) / (alphaFactorMax() - alphaFactorMin());
            /* Convert it ping-pong value between 0-1: */
            dAlpha = 1 - qAbs(2 * dAlpha - 1);
            /* Map it to the original min-max range: */
            iAlpha = alphaFactorMin() + (alphaFactorMax() - alphaFactorMin()) * dAlpha;
        }

        /* Configure brush (red circle with black border): */
        QPen pen(Qt::black);
        pen.setWidth(0);
        painter.setPen(pen);
        QColor bgc(Qt::red);
        bgc.setAlpha(iAlpha);
        painter.setBrush(bgc);
        const int iRadius = 0.4 * qMin(width(), height());
        painter.drawEllipse(contentsRect().center() + QPoint(0, 1), iRadius, iRadius);
    }

protected slots:

    /** Update routine. */
    virtual void updateAppearance() RT_OVERRIDE
    {
        QString strFullData;
        bool fRecordingEnabled = false;
        bool fMachinePaused = false;
        m_pMachine->acquireRecordingStatusInfo(strFullData, fRecordingEnabled, fMachinePaused);

        /* Update tool-tip: */
        if (!strFullData.isEmpty())
            setToolTip(s_strTable.arg(strFullData));
        /* Set initial indicator state: */
        m_enmState = RecordingState_Unavailable;
        if (m_pMachine->machineState() != KMachineState_Null)
        {
            if (!fRecordingEnabled)
                m_enmState = RecordingState_Disabled;
            else if (!fMachinePaused)
                m_enmState = RecordingState_Enabled;
            else
                m_enmState = RecordingState_Paused;
        }
        setState(m_enmState);

        /* Retranslate finally: */
        sltRetranslateUI();
    }

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE
    {
        /* Call to base-class: */
        UISessionStateStatusBarIndicator::sltRetranslateUI();

        /* Append description with more info: */
        switch (m_enmState)
        {
            case RecordingState_Disabled:
                m_strDescription = QString("%1, %2").arg(m_strDescription, tr("Recording stopped")); break;
            case RecordingState_Enabled:
                m_strDescription = QString("%1, %2").arg(m_strDescription, tr("Recording started")); break;
            case RecordingState_Paused:
                m_strDescription = QString("%1, %2").arg(m_strDescription, tr("Recording paused")); break;
            default:
                break;
        }
    }

private slots:

    /** Handles state change. */
    void setState(int iState) RT_OVERRIDE
    {
        /* Update animation state: */
        switch (iState)
        {
            case RecordingState_Disabled:
                m_pAnimation->stop();
                m_iAlphaFactor = 0;
                break;
            case RecordingState_Enabled:
                m_pAnimation->start();
                break;
            case RecordingState_Paused:
                m_pAnimation->stop();
                break;
            default:
                break;
        }
        /* Call to base-class: */
        QIStateStatusBarIndicator::setState(iState);
    }

private:

    /** Returns minimum alpha factor. */
    int alphaFactorMin() const { return 55; }
    /** Returns maximum alpha factor. */
    int alphaFactorMax() const { return 255; }
    /** Returns current alpha factor. */
    int alphaFactor() const { return m_iAlphaFactor; }
    /** Defines current alpha factor. */
    void setAlphaFactor(int iAlphaFactor) { m_iAlphaFactor = iAlphaFactor; update(); }

    /** Holds the animation instance. */
    UIAnimationLoop *m_pAnimation;
    /** Holds current alpha factor. */
    int              m_iAlphaFactor;

    /** Holds the recording state. */
    RecordingState  m_enmState;
};


/** UISessionStateStatusBarIndicator extension for Runtime UI: Features indicator. */
class UIIndicatorFeatures : public UISessionStateStatusBarIndicator
{
    Q_OBJECT;

public:

    /** Constructs indicator passing @a pMachine to the base-class. */
    UIIndicatorFeatures(UIMachine *pMachine)
        : UISessionStateStatusBarIndicator(IndicatorType_Features, pMachine)
        , m_pTimerAutoUpdate(0)
        , m_uEffectiveCPULoad(0)
    {
        /* Assign state-icons: */
        /** @todo  The vtx_amdv_disabled_16px.png icon isn't really approprate anymore (no raw-mode),
         * might want to get something different for KVMExecutionEngine_Emulated or reuse the
         * vm_execution_engine_native_api_16px.png one... @bugref{9898} */
        setStateIcon(KVMExecutionEngine_NotSet, UIIconPool::iconSet(":/vtx_amdv_disabled_16px.png"));
        setStateIcon(KVMExecutionEngine_Interpreter, UIIconPool::iconSet(":/vtx_amdv_disabled_16px.png"));
        setStateIcon(KVMExecutionEngine_Recompiler, UIIconPool::iconSet(":/vtx_amdv_disabled_16px.png"));
        setStateIcon(KVMExecutionEngine_HwVirt, UIIconPool::iconSet(":/vtx_amdv_16px.png"));
        setStateIcon(KVMExecutionEngine_NativeApi, UIIconPool::iconSet(":/vm_execution_engine_native_api_16px.png"));
        /* Configure connection: */
        connect(m_pMachine, &UIMachine::sigMachineStateChange,
                this, &UIIndicatorFeatures::updateAppearance);
        connect(m_pMachine, &UIMachine::sigInitialized,
                this, &UIIndicatorFeatures::updateAppearance);
        connect(m_pMachine, &UIMachine::sigCPUExecutionCapChange,
                this, &UIIndicatorFeatures::updateAppearance);
        /* Configure CPU load update timer: */
        m_pTimerAutoUpdate = new QTimer(this);
        if (m_pTimerAutoUpdate)
            connect(m_pTimerAutoUpdate, &QTimer::timeout,
                    this, &UIIndicatorFeatures::sltHandleTimeout);
        /* Update & translate finally: */
        updateAppearance();
    }

protected:

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE
    {
        /* Call to base-class: */
        UISessionStateStatusBarIndicator::paintEvent(pEvent);

        /* Create new painter: */
        QPainter painter(this);

        /* Be a paranoid: */
        ulong uCPU = qMin(m_uEffectiveCPULoad, static_cast<ulong>(100));

        int iBorderThickness = 1;
        /** Draw a black rectangle as background to the right hand side of 'this'.
          * A smaller and colored rectangle will be drawn on top of this:  **/
        QPoint topLeftOut(0.76 * width() ,0);
        QPoint bottomRightOut(width() - 1, height() - 1);
        QRect outRect(topLeftOut, bottomRightOut);

        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        painter.drawRect(outRect);

        /* Draw a colored rectangle. Its color and height is dynamically computed from CPU usage: */
        int inFullHeight = outRect.height() - 2 * iBorderThickness;
        int inWidth = outRect.width() - 2 * iBorderThickness;
        int inHeight = inFullHeight * uCPU / 100.;
        QPoint topLeftIn(topLeftOut.x() + iBorderThickness,
                         topLeftOut.y() + iBorderThickness + (inFullHeight - inHeight));

        QRect inRect(topLeftIn, QSize(inWidth, inHeight));
        painter.setPen(Qt::NoPen);

        /* Compute color as HSV: */
        int iH = 120 * (1 - uCPU / 100.);
        QColor fillColor;
        fillColor.setHsv(iH, 255 /*saturation */, 255 /* value */);

        painter.setBrush(fillColor);
        painter.drawRect(inRect);
    }

protected slots:

    /** Update routine. */
    virtual void updateAppearance() RT_OVERRIDE
    {
        m_strFullData.clear();
        KVMExecutionEngine enmEngine = KVMExecutionEngine_NotSet;
        m_pMachine->acquireFeaturesStatusInfo(m_strFullData, enmEngine);

        /* Update tool-tip: */
        if (!m_strFullData.isEmpty())
            setToolTip(s_strTable.arg(m_strFullData));
        /* Update indicator state: */
        setState(enmEngine);

        /* Start or stop CPU load update timer: */
        if (   m_pTimerAutoUpdate
            && m_pMachine->machineState() == KMachineState_Running)
            m_pTimerAutoUpdate->start(1000);
        else
            m_pTimerAutoUpdate->stop();

        /* Retranslate finally: */
        sltRetranslateUI();
    }

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE
    {
        /* Call to base-class: */
        UISessionStateStatusBarIndicator::sltRetranslateUI();

        /* Append description with more info: */
        QString strFullData = m_strFullData;
        strFullData.remove(QRegularExpression("<tr>|<td>|<nobr>"));
        strFullData.replace("</nobr></td></tr>", ", ");
        strFullData.replace("</nobr></td>", " ");
        m_strDescription = QString("%1, %2").arg(m_strDescription, strFullData);
    }

private slots:

    /** Handles timer timeout with CPU load percentage update. */
    void sltHandleTimeout()
    {
        m_pMachine->acquireEffectiveCPULoad(m_uEffectiveCPULoad);
        update();
    }

private:

    /** Holds the auto-update timer instance. */
    QTimer *m_pTimerAutoUpdate;

    /** Holds the effective CPU load. Expected to be in range [0, 100] */
    ulong  m_uEffectiveCPULoad;

    /** Holds the serialized tool-tip data. */
    QString  m_strFullData;
};


/** UISessionStateStatusBarIndicator extension for Runtime UI: Mouse indicator. */
class UIIndicatorMouse : public UISessionStateStatusBarIndicator
{
    Q_OBJECT;

    /** Possible indicator states. */
    enum
    {
        State_NotCaptured         = 0, // 000 == !MouseCaptured
        State_Captured            = 1, // 001 == MouseCaptured
        State_AbsoluteNotCaptured = 2, // 010 == !MouseCaptured & MouseSupportsAbsolute
        State_AbsoluteCaptured    = 3, // 011 == MouseCaptured & MouseSupportsAbsolute
        State_Integrated          = 6, // 110 == MouseSupportsAbsolute & MouseIntegrated
    };

public:

    /** Constructor, using @a pMachine for state-update routine. */
    UIIndicatorMouse(UIMachine *pMachine)
        : UISessionStateStatusBarIndicator(IndicatorType_Mouse, pMachine)
    {
        /* Assign state-icons: */
        setStateIcon(State_NotCaptured,         UIIconPool::iconSet(":/mouse_disabled_16px.png"));                // 000
        setStateIcon(State_Captured,            UIIconPool::iconSet(":/mouse_16px.png"));                         // 001
        setStateIcon(State_AbsoluteNotCaptured, UIIconPool::iconSet(":/mouse_can_seamless_uncaptured_16px.png")); // 010
        setStateIcon(State_AbsoluteCaptured,    UIIconPool::iconSet(":/mouse_can_seamless_16px.png"));            // 011
        setStateIcon(State_Integrated,          UIIconPool::iconSet(":/mouse_seamless_16px.png"));                // 110

        /* Configure machine connection: */
        connect(m_pMachine, &UIMachine::sigMouseStateChange, this, &UIIndicatorMouse::setState);

        /* Update & translate finally: */
        updateAppearance();
    }

protected slots:

    /** Update routine. */
    virtual void updateAppearance() RT_OVERRIDE
    {
        /* Retranslate finally: */
        sltRetranslateUI();
    }

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE
    {
        /* Call to base-class: */
        UISessionStateStatusBarIndicator::sltRetranslateUI();

        /* Update tool-tip: */
        const QString strToolTip = tr("Indicates whether the host mouse pointer is "
                                      "captured by the guest OS:%1", "Mouse tooltip");
        QString strFullData;
        strFullData += s_strTableRow3
            .arg(QString("<img src=:/mouse_disabled_16px.png/>"))
            .arg(tr("Pointer is not captured", "Mouse tooltip"));
        strFullData += s_strTableRow3
            .arg(QString("<img src=:/mouse_16px.png/>"))
            .arg(tr("Pointer is captured", "Mouse tooltip"));
        strFullData += s_strTableRow3
            .arg(QString("<img src=:/mouse_can_seamless_uncaptured_16px.png/>"))
            .arg(tr("Mouse integration is Off, pointer is not captured", "Mouse tooltip"));
        strFullData += s_strTableRow3
            .arg(QString("<img src=:/mouse_can_seamless_16px.png/>"))
            .arg(tr("Mouse integration is Off, pointer is captured", "Mouse tooltip"));
        strFullData += s_strTableRow3
            .arg(QString("<img src=:/mouse_seamless_16px.png/>"))
            .arg(tr("Mouse integration is On", "Mouse tooltip"));
        strFullData = s_strTable.arg(strFullData);
        strFullData += tr("Note that the mouse integration feature requires Guest "
                          "Additions to be installed in the guest OS.", "Mouse tooltip");
        setToolTip(strToolTip.arg(strFullData));

        /* Append description with more info: */
        QString strState;
        switch (state())
        {
            case State_NotCaptured:
                strState = tr("Pointer is not captured", "Mouse tooltip"); break;
            case State_Captured:
                strState = tr("Pointer is captured", "Mouse tooltip"); break;
            case State_AbsoluteNotCaptured:
                strState = tr("Mouse integration is Off, pointer is not captured", "Mouse tooltip"); break;
            case State_AbsoluteCaptured:
                strState = tr("Mouse integration is Off, pointer is captured", "Mouse tooltip"); break;
            case State_Integrated:
                strState = tr("Mouse integration is On", "Mouse tooltip"); break;
            default:
                break;
        }
        if (!strState.isNull())
            m_strDescription = QString("%1, %2").arg(m_strDescription, strState);
    }

private slots:

    /** Handles state change. */
    void setState(int iState) RT_OVERRIDE
    {
        /* Call to base-class: */
        QIStateStatusBarIndicator::setState(iState);

        /* Update everything: */
        updateAppearance();
    }
};


/** UISessionStateStatusBarIndicator extension for Runtime UI: Keyboard indicator. */
class UIIndicatorKeyboard : public UISessionStateStatusBarIndicator
{
    Q_OBJECT;

    /** Possible indicator states. */
    enum
    {
        State_KeyboardUnavailable           = 0,  // 0000 == KeyboardUnavailable
        State_KeyboardAvailable             = 1,  // 0001 == KeyboardAvailable
        State_HostKeyCaptured               = 3,  // 0011 == KeyboardAvailable & KeyboardCaptured
        State_HostKeyPressed                = 5,  // 0101 == KeyboardAvailable & !KeyboardCaptured & HostKeyPressed
        State_HostKeyCapturedPressed        = 7,  // 0111 == KeyboardAvailable & KeyboardCaptured & HostKeyPressed
        State_HostKeyChecked                = 9,  // 1001 == KeyboardAvailable & !KeyboardCaptured & !HostKeyPressed & HostKeyPressedInsertion
        State_HostKeyCapturedChecked        = 11, // 1011 == KeyboardAvailable & KeyboardCaptured & !HostKeyPressed & HostKeyPressedInsertion
        State_HostKeyPressedChecked         = 13, // 1101 == KeyboardAvailable & !KeyboardCaptured & HostKeyPressed & HostKeyPressedInsertion
        State_HostKeyCapturedPressedChecked = 15, // 1111 == KeyboardAvailable & KeyboardCaptured & HostKeyPressed & HostKeyPressedInsertion
    };

public:

    /** Constructor, using @a pMachine for state-update routine. */
    UIIndicatorKeyboard(UIMachine *pMachine)
        : UISessionStateStatusBarIndicator(IndicatorType_Keyboard, pMachine)
    {
        /* Assign state-icons: */
        setStateIcon(State_KeyboardUnavailable,           UIIconPool::iconSet(":/hostkey_disabled_16px.png"));
        setStateIcon(State_KeyboardAvailable,             UIIconPool::iconSet(":/hostkey_16px.png"));
        setStateIcon(State_HostKeyCaptured,               UIIconPool::iconSet(":/hostkey_captured_16px.png"));
        setStateIcon(State_HostKeyPressed,                UIIconPool::iconSet(":/hostkey_pressed_16px.png"));
        setStateIcon(State_HostKeyCapturedPressed,        UIIconPool::iconSet(":/hostkey_captured_pressed_16px.png"));
        setStateIcon(State_HostKeyChecked,                UIIconPool::iconSet(":/hostkey_checked_16px.png"));
        setStateIcon(State_HostKeyCapturedChecked,        UIIconPool::iconSet(":/hostkey_captured_checked_16px.png"));
        setStateIcon(State_HostKeyPressedChecked,         UIIconPool::iconSet(":/hostkey_pressed_checked_16px.png"));
        setStateIcon(State_HostKeyCapturedPressedChecked, UIIconPool::iconSet(":/hostkey_captured_pressed_checked_16px.png"));

        /* Configure machine connection: */
        connect(m_pMachine, &UIMachine::sigKeyboardStateChange, this, &UIIndicatorKeyboard::setState);

        /* Update & translate finally: */
        updateAppearance();
    }

protected slots:

    /** Update routine. */
    virtual void updateAppearance() RT_OVERRIDE
    {
        /* Retranslate finally: */
        sltRetranslateUI();
    }

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE
    {
        /* Call to base-class: */
        UISessionStateStatusBarIndicator::sltRetranslateUI();

        /* Update tool-tip: */
        const QString strToolTip = tr("Indicates whether the host keyboard is "
                                      "captured by the guest OS:%1", "Keyboard tooltip");
        QString strFullData;
        strFullData += s_strTableRow3
            .arg(QString("<img src=:/hostkey_16px.png/>"))
            .arg(tr("Keyboard is not captured", "Keyboard tooltip"));
        strFullData += s_strTableRow3
            .arg(QString("<img src=:/hostkey_captured_16px.png/>"))
            .arg(tr("Keyboard is captured", "Keyboard tooltip"));
        strFullData += s_strTableRow3
            .arg(QString("<img src=:/hostkey_pressed_16px.png/>"))
            .arg(tr("Keyboard is not captured, host-combo being pressed", "Keyboard tooltip"));
        strFullData += s_strTableRow3
            .arg(QString("<img src=:/hostkey_captured_pressed_16px.png/>"))
            .arg(tr("Keyboard is captured, host-combo being pressed", "Keyboard tooltip"));
        strFullData += s_strTableRow3
            .arg(QString("<img src=:/hostkey_checked_16px.png/>"))
            .arg(tr("Keyboard is not captured, host-combo to be inserted", "Keyboard tooltip"));
        strFullData += s_strTableRow3
            .arg(QString("<img src=:/hostkey_captured_checked_16px.png/>"))
            .arg(tr("Keyboard is captured, host-combo to be inserted", "Keyboard tooltip"));
        strFullData += s_strTableRow3
            .arg(QString("<img src=:/hostkey_pressed_checked_16px.png/>"))
            .arg(tr("Keyboard is not captured, host-combo being pressed and to be inserted", "Keyboard tooltip"));
        strFullData += s_strTableRow3
            .arg(QString("<img src=:/hostkey_captured_pressed_checked_16px.png/>"))
            .arg(tr("Keyboard is captured, host-combo being pressed and to be inserted", "Keyboard tooltip"));
        strFullData = s_strTable.arg(strFullData);
        setToolTip(strToolTip.arg(strFullData));

        /* Append description with more info: */
        const QString strStatus = state() & UIKeyboardStateType_KeyboardCaptured
                                ? tr("Keyboard is captured", "Keyboard tooltip")
                                : tr("Keyboard is not captured", "Keyboard tooltip");
        m_strDescription = QString("%1, %2").arg(m_strDescription, strStatus);
    }

private slots:

    /** Handles state change. */
    void setState(int iState) RT_OVERRIDE
    {
        /* Call to base-class: */
        QIStateStatusBarIndicator::setState(iState);

        /* Update everything: */
        updateAppearance();
    }
};


/** UISessionTextStatusBarIndicator extension for Runtime UI: Keyboard-extension indicator. */
class UIIndicatorKeyboardExtension : public UISessionTextStatusBarIndicator
{
    Q_OBJECT;

public:

    /** Constructor. */
    UIIndicatorKeyboardExtension()
        : UISessionTextStatusBarIndicator(IndicatorType_KeyboardExtension)
    {
        /* Make sure host-combination label will be updated: */
        connect(gEDataManager, &UIExtraDataManager::sigRuntimeUIHostKeyCombinationChange,
                this, &UIIndicatorKeyboardExtension::updateAppearance);
        connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
                this, &UIIndicatorKeyboardExtension::sltRetranslateUI);

        /* Update & translate finally: */
        updateAppearance();
    }

protected slots:

    /** Update routine. */
    void updateAppearance()
    {
        /* Update combination: */
        setText(UIHostCombo::toReadableString(gEDataManager->hostKeyCombination()));

        /* Retranslate finally: */
        sltRetranslateUI();
    }

    /** Retranslation routine. */
    void sltRetranslateUI()
    {
        /* Call to base-class: */
        UISessionTextStatusBarIndicator::sltRetranslateUI();

        setToolTip(tr("Shows the currently assigned Host Key Combo.<br>"
                      "This key combo, when pressed alone, toggles the keyboard and mouse "
                      "capture state. It can also be used in combination with other keys "
                      "to quickly perform actions from the main menu."));

        /* Host-combo info: */
        const QString strHostCombo = tr("Host Key Combo: %1").arg(text());
        m_strDescription = QString("%1, %2").arg(m_strDescription, strHostCombo);
    }
};


UIIndicatorsPool::UIIndicatorsPool(UIMachine *pMachine, QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_pMachine(pMachine)
    , m_fEnabled(false)
    , m_pMainLayout(0)
    , m_pTimerAutoUpdate(0)
{
    prepare();
}

UIIndicatorsPool::~UIIndicatorsPool()
{
    cleanup();
}

void UIIndicatorsPool::updateAppearance(IndicatorType indicatorType)
{
    /* Skip missed indicators: */
    if (!m_pool.contains(indicatorType))
        return;

    /* Get indicator: */
    QIStatusBarIndicator *pIndicator = m_pool.value(indicatorType);

    /* Assert indicators with NO appearance: */
    UISessionStateStatusBarIndicator *pSessionStateIndicator =
        qobject_cast<UISessionStateStatusBarIndicator*>(pIndicator);
    AssertPtrReturnVoid(pSessionStateIndicator);

    /* Update indicator appearance: */
    pSessionStateIndicator->updateAppearance();
}

void UIIndicatorsPool::setAutoUpdateIndicatorStates(bool fEnabled)
{
    /* Make sure auto-update timer exists: */
    AssertPtrReturnVoid(m_pTimerAutoUpdate);

    /* Start/stop timer: */
    if (fEnabled)
        m_pTimerAutoUpdate->start(100);
    else
        m_pTimerAutoUpdate->stop();
}

QPoint UIIndicatorsPool::mapIndicatorPositionToGlobal(IndicatorType enmIndicatorType, const QPoint &indicatorPosition)
{
    if (m_pool.contains(enmIndicatorType))
        return m_pool.value(enmIndicatorType)->mapToGlobal(indicatorPosition);
    return QPoint(0, 0);
}

void UIIndicatorsPool::sltHandleConfigurationChange(const QUuid &uMachineID)
{
    /* Skip unrelated machine IDs: */
    if (uiCommon().managedVMUuid() != uMachineID)
        return;

    /* Update pool: */
    updatePool();
}

void UIIndicatorsPool::sltAutoUpdateIndicatorStates()
{
    /* We should update states for following indicators: */
    QVector<KDeviceType> deviceTypes;
    if (m_pool.contains(IndicatorType_HardDisks))
        deviceTypes.append(KDeviceType_HardDisk);
    if (m_pool.contains(IndicatorType_OpticalDisks))
        deviceTypes.append(KDeviceType_DVD);
    if (m_pool.contains(IndicatorType_FloppyDisks))
        deviceTypes.append(KDeviceType_Floppy);
    if (m_pool.contains(IndicatorType_USB))
        deviceTypes.append(KDeviceType_USB);
    if (m_pool.contains(IndicatorType_Network))
        deviceTypes.append(KDeviceType_Network);
    if (m_pool.contains(IndicatorType_SharedFolders))
        deviceTypes.append(KDeviceType_SharedFolder);
    if (m_pool.contains(IndicatorType_Display))
        deviceTypes.append(KDeviceType_Graphics3D);

    /* Acquire current device states from the machine: */
    QVector<KDeviceActivity> states;
    m_pMachine->acquireDeviceActivity(deviceTypes, states);

    /* Update indicators with the acquired states: */
    for (int iIndicator = 0; iIndicator < states.size(); ++iIndicator)
    {
        QIStatusBarIndicator *pIndicator = 0;
        switch (deviceTypes[iIndicator])
        {
            case KDeviceType_HardDisk:     pIndicator = m_pool.value(IndicatorType_HardDisks); break;
            case KDeviceType_DVD:          pIndicator = m_pool.value(IndicatorType_OpticalDisks); break;
            case KDeviceType_Floppy:       pIndicator = m_pool.value(IndicatorType_FloppyDisks); break;
            case KDeviceType_USB:          pIndicator = m_pool.value(IndicatorType_USB); break;
            case KDeviceType_Network:      pIndicator = m_pool.value(IndicatorType_Network); break;
            case KDeviceType_SharedFolder: pIndicator = m_pool.value(IndicatorType_SharedFolders); break;
            case KDeviceType_Graphics3D:   pIndicator = m_pool.value(IndicatorType_Display); break;
            default: AssertFailed(); break;
        }
        if (pIndicator)
            updateIndicatorStateForDevice(pIndicator, states[iIndicator]);
    }
}

void UIIndicatorsPool::sltContextMenuRequest(QIStatusBarIndicator *pIndicator, QContextMenuEvent *pEvent)
{
    /* If that is one of pool indicators: */
    foreach (IndicatorType indicatorType, m_pool.keys())
        if (m_pool[indicatorType] == pIndicator)
        {
            /* Notify listener: */
            emit sigContextMenuRequest(indicatorType, pEvent->pos());
            return;
        }
}

void UIIndicatorsPool::prepare()
{
    /* Prepare connections: */
    prepareConnections();
    /* Prepare contents: */
    prepareContents();
    /* Prepare auto-update timer: */
    prepareUpdateTimer();
}

void UIIndicatorsPool::prepareConnections()
{
    /* Listen for the status-bar configuration changes: */
    connect(gEDataManager, &UIExtraDataManager::sigStatusBarConfigurationChange,
            this, &UIIndicatorsPool::sltHandleConfigurationChange);
}

void UIIndicatorsPool::prepareContents()
{
    /* Create main-layout: */
    m_pMainLayout = new QHBoxLayout(this);
    AssertPtrReturnVoid(m_pMainLayout);
    {
        /* Configure main-layout: */
        m_pMainLayout->setContentsMargins(0, 0, 0, 0);
#ifdef VBOX_WS_MAC
        m_pMainLayout->setSpacing(5);
#else
        m_pMainLayout->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing) / 2);
#endif
        /* Update pool: */
        updatePool();
    }
}

void UIIndicatorsPool::prepareUpdateTimer()
{
    /* Create auto-update timer: */
    m_pTimerAutoUpdate = new QTimer(this);
    AssertPtrReturnVoid(m_pTimerAutoUpdate);
    {
        /* Configure auto-update timer: */
        connect(m_pTimerAutoUpdate, &QTimer::timeout,
                this, &UIIndicatorsPool::sltAutoUpdateIndicatorStates);
        setAutoUpdateIndicatorStates(true);
    }
}

void UIIndicatorsPool::updatePool()
{
    /* Acquire status-bar availability: */
    m_fEnabled = gEDataManager->statusBarEnabled(uiCommon().managedVMUuid());
    /* If status-bar is not enabled: */
    if (!m_fEnabled)
    {
        /* Remove all indicators: */
        while (!m_pool.isEmpty())
        {
            const IndicatorType firstType = m_pool.keys().first();
            delete m_pool.value(firstType);
            m_pool.remove(firstType);
        }
        /* And return: */
        return;
    }

    /* Acquire status-bar restrictions: */
    m_restrictions = gEDataManager->restrictedStatusBarIndicators(uiCommon().managedVMUuid());
    /* Make sure 'Recording' is restricted as well if no features supported: */
    if (   !m_restrictions.contains(IndicatorType_Recording)
        && !gpGlobalSession->supportedRecordingFeatures())
        m_restrictions << IndicatorType_Recording;

    /* Remove restricted indicators: */
    foreach (const IndicatorType &indicatorType, m_restrictions)
    {
        if (m_pool.contains(indicatorType))
        {
            delete m_pool.value(indicatorType);
            m_pool.remove(indicatorType);
        }
    }

    /* Acquire status-bar order: */
    m_order = gEDataManager->statusBarIndicatorOrder(uiCommon().managedVMUuid());
    /* Make sure the order is complete taking restrictions into account: */
    for (int iType = IndicatorType_Invalid; iType < IndicatorType_Max; ++iType)
    {
        /* Get iterated type: */
        IndicatorType type = (IndicatorType)iType;
        /* Skip invalid type: */
        if (type == IndicatorType_Invalid)
            continue;
        /* Take restriction/presence into account: */
        bool fRestricted = m_restrictions.contains(type);
        bool fPresent = m_order.contains(type);
        if (fRestricted && fPresent)
            m_order.removeAll(type);
        else if (!fRestricted && !fPresent)
            m_order << type;
    }

    /* Add/Update allowed indicators: */
    foreach (const IndicatorType &indicatorType, m_order)
    {
        /* Indicator exists: */
        if (m_pool.contains(indicatorType))
        {
            /* Get indicator: */
            QIStatusBarIndicator *pIndicator = m_pool.value(indicatorType);
            /* Make sure it have valid position: */
            const int iWantedIndex = indicatorPosition(indicatorType);
            const int iActualIndex = m_pMainLayout->indexOf(pIndicator);
            if (iActualIndex != iWantedIndex)
            {
                /* Re-inject indicator into main-layout at proper position: */
                m_pMainLayout->removeWidget(pIndicator);
                m_pMainLayout->insertWidget(iWantedIndex, pIndicator);
            }
        }
        /* Indicator missed: */
        else
        {
            /* Create indicator: */
            switch (indicatorType)
            {
                case IndicatorType_HardDisks:         m_pool[indicatorType] = new UIIndicatorHardDrive(m_pMachine);     break;
                case IndicatorType_OpticalDisks:      m_pool[indicatorType] = new UIIndicatorOpticalDisks(m_pMachine);  break;
                case IndicatorType_FloppyDisks:       m_pool[indicatorType] = new UIIndicatorFloppyDisks(m_pMachine);   break;
                case IndicatorType_Audio:             m_pool[indicatorType] = new UIIndicatorAudio(m_pMachine);         break;
                case IndicatorType_Network:           m_pool[indicatorType] = new UIIndicatorNetwork(m_pMachine);       break;
                case IndicatorType_USB:               m_pool[indicatorType] = new UIIndicatorUSB(m_pMachine);           break;
                case IndicatorType_SharedFolders:     m_pool[indicatorType] = new UIIndicatorSharedFolders(m_pMachine); break;
                case IndicatorType_Display:           m_pool[indicatorType] = new UIIndicatorDisplay(m_pMachine);       break;
                case IndicatorType_Recording:         m_pool[indicatorType] = new UIIndicatorRecording(m_pMachine);     break;
                case IndicatorType_Features:          m_pool[indicatorType] = new UIIndicatorFeatures(m_pMachine);      break;
                case IndicatorType_Mouse:             m_pool[indicatorType] = new UIIndicatorMouse(m_pMachine);         break;
                case IndicatorType_Keyboard:          m_pool[indicatorType] = new UIIndicatorKeyboard(m_pMachine);      break;
                case IndicatorType_KeyboardExtension: m_pool[indicatorType] = new UIIndicatorKeyboardExtension;         break;
                default: break;
            }
            /* Configure indicator: */
            connect(m_pool.value(indicatorType), &QIStatusBarIndicator::sigContextMenuRequest,
                    this, &UIIndicatorsPool::sltContextMenuRequest);
            /* Insert indicator into main-layout at proper position: */
            m_pMainLayout->insertWidget(indicatorPosition(indicatorType), m_pool.value(indicatorType));
        }
    }
}

void UIIndicatorsPool::cleanupUpdateTimer()
{
    /* Destroy auto-update timer: */
    AssertPtrReturnVoid(m_pTimerAutoUpdate);
    {
        m_pTimerAutoUpdate->stop();
        delete m_pTimerAutoUpdate;
        m_pTimerAutoUpdate = 0;
    }
}

void UIIndicatorsPool::cleanupContents()
{
    /* Cleanup indicators: */
    while (!m_pool.isEmpty())
    {
        const IndicatorType firstType = m_pool.keys().first();
        delete m_pool.value(firstType);
        m_pool.remove(firstType);
    }
}

void UIIndicatorsPool::cleanup()
{
    /* Cleanup auto-update timer: */
    cleanupUpdateTimer();
    /* Cleanup indicators: */
    cleanupContents();
}

void UIIndicatorsPool::contextMenuEvent(QContextMenuEvent *pEvent)
{
    /* Do not pass-through context menu events,
     * otherwise they will raise the underlying status-bar context-menu. */
    pEvent->accept();
}

int UIIndicatorsPool::indicatorPosition(IndicatorType indicatorType) const
{
    int iPosition = 0;
    foreach (const IndicatorType &iteratedIndicatorType, m_order)
        if (iteratedIndicatorType == indicatorType)
            return iPosition;
        else
            ++iPosition;
    return iPosition;
}

void UIIndicatorsPool::updateIndicatorStateForDevice(QIStatusBarIndicator *pIndicator, KDeviceActivity enmState)
{
    /* Assert indicators with NO state: */
    QIStateStatusBarIndicator *pStateIndicator = qobject_cast<QIStateStatusBarIndicator*>(pIndicator);
    AssertPtrReturnVoid(pStateIndicator);

    /* Skip indicators with NULL state: */
    if (pStateIndicator->state() == KDeviceActivity_Null)
        return;

    /* Paused VM have all indicator states set to IDLE: */
    if (m_pMachine->isPaused())
    {
        /* If current state differs from IDLE => set the IDLE one:  */
        if (pStateIndicator->state() != KDeviceActivity_Idle)
            pStateIndicator->setState(KDeviceActivity_Idle);
    }
    else
    {
        /* If current state differs from actual => set the actual one: */
        if (pStateIndicator->state() != enmState)
            pStateIndicator->setState(enmState);
    }
}

#include "UIIndicatorsPool.moc"
