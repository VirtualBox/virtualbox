/* $Id: UIVMCloseDialog.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVMCloseDialog class implementation.
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
#include <QButtonGroup>
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QStyle>
#include <QVBoxLayout>
#include <QWindow>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "UICommon.h"
#include "UIConverter.h"
#include "UIExtraDataManager.h"
#include "UIIconPool.h"
#include "UIMachine.h"
#include "UIMessageCenter.h"
#include "UIShortcutPool.h"
#include "UITranslationEventListener.h"
#include "UIVMCloseDialog.h"


UIVMCloseDialog::UIVMCloseDialog(QWidget *pParent, UIMachine *pMachine,
                                 bool fIsACPIEnabled, MachineCloseAction restictedCloseActions)
    : QIDialog(pParent)
    , m_pMachine(pMachine)
    , m_fIsACPIEnabled(fIsACPIEnabled)
    , m_restictedCloseActions(restictedCloseActions)
    , m_fValid(false)
    , m_pMainLayout(0)
    , m_pTopLayout(0)
    , m_pTopLeftLayout(0)
    , m_pTopRightLayout(0)
    , m_pChoiceLayout(0)
    , m_pLabelIcon(0), m_pLabelText(0)
    , m_pLabelIconDetach(0), m_pRadioButtonDetach(0)
    , m_pLabelIconSave(0), m_pRadioButtonSave(0)
    , m_pLabelIconShutdown(0), m_pRadioButtonShutdown(0)
    , m_pLabelIconPowerOff(0), m_pRadioButtonPowerOff(0)
    , m_pCheckBoxDiscard(0)
    , m_enmLastCloseAction(MachineCloseAction_Invalid)
{
    prepare();
}

void UIVMCloseDialog::setIcon(const QIcon &icon)
{
    /* Make sure icon is valid: */
    if (icon.isNull())
        return;

    /* Remember it: */
    m_icon = icon;
    /* Update pixmaps: */
    updatePixmaps();
}

bool UIVMCloseDialog::eventFilter(QObject *pObject, QEvent *pEvent)
{
    /* Handle events realted to our radio-buttons only: */
    if (   pObject != m_pRadioButtonDetach
        && pObject != m_pRadioButtonSave
        && pObject != m_pRadioButtonShutdown
        && pObject != m_pRadioButtonPowerOff)
        return QIDialog::eventFilter(pObject, pEvent);

    /* For now we are interested in double-click events only: */
    if (pEvent->type() == QEvent::MouseButtonDblClick)
    {
        /* Since on double-click the button will be also selected
         * we are just calling for the *accept* slot: */
        accept();
    }

    /* Call to base-class: */
    return QIDialog::eventFilter(pObject, pEvent);
}

bool UIVMCloseDialog::event(QEvent *pEvent)
{
    /* Pre-process in base-class: */
    const bool fResult = QIDialog::event(pEvent);

    /* Post-process know event types: */
    switch (pEvent->type())
    {
        case QEvent::ScreenChangeInternal:
        {
            /* Update pixmaps: */
            updatePixmaps();
            break;
        }
        default:
            break;
    }

    /* Return pre-processed result: */
    return fResult;
}

void UIVMCloseDialog::showEvent(QShowEvent *pEvent)
{
    /* Update pixmaps: */
    updatePixmaps();

    /* Call to base-class: */
    QIDialog::showEvent(pEvent);
}

void UIVMCloseDialog::sltRetranslateUI()
{
    /* Translate title: */
    setWindowTitle(tr("Close Virtual Machine"));

    /* Translate text label: */
    m_pLabelText->setText(tr("You want to:"));

    /* Translate radio-buttons: */
    m_pRadioButtonDetach->setText(tr("&Continue running in the background"));
    m_pRadioButtonDetach->setWhatsThis(tr("<p>Close the virtual machine windows but keep the virtual machine running.</p>"
                                          "<p>You can use the VirtualBox Manager to return to running the virtual machine "
                                          "in a window.</p>"));
    m_pRadioButtonSave->setText(tr("&Save the machine state"));
    m_pRadioButtonSave->setWhatsThis(tr("<p>Saves the current execution state of the virtual machine to the physical hard disk "
                                        "of the host PC.</p>"
                                        "<p>Next time this machine is started, it will be restored from the saved state and "
                                        "continue execution from the same place you saved it at, which will let you continue "
                                        "your work immediately.</p>"
                                        "<p>Note that saving the machine state may take a long time, depending on the guest "
                                        "operating system type and the amount of memory you assigned to the virtual "
                                        "machine.</p>"));
    m_pRadioButtonShutdown->setText(tr("S&end the shutdown signal"));
    m_pRadioButtonShutdown->setWhatsThis(tr("<p>Sends the ACPI Power Button press event to the virtual machine.</p>"
                                            "<p>Normally, the guest operating system running inside the virtual machine will "
                                            "detect this event and perform a clean shutdown procedure. This is a recommended "
                                            "way to turn off the virtual machine because all applications running inside it "
                                            "will get a chance to save their data and state.</p>"
                                            "<p>If the machine doesn't respond to this action then the guest operating system "
                                            "may be misconfigured or doesn't understand ACPI Power Button events at all. In "
                                            "this case you should select the <b>Power off the machine</b> action to stop "
                                            "virtual machine execution.</p>"));
    m_pRadioButtonPowerOff->setText(tr("&Power off the machine"));
    m_pRadioButtonPowerOff->setWhatsThis(tr("<p>Turns off the virtual machine.</p>"
                                            "<p>Note that this action will stop machine execution immediately so that the guest "
                                            "operating system running inside it will not be able to perform a clean shutdown "
                                            "procedure which may result in <i>data loss</i> inside the virtual machine. "
                                            "Selecting this action is recommended only if the virtual machine does not respond "
                                            "to the <b>Send the shutdown signal</b> action.</p>"));

    /* Translate check-box: */
    m_pCheckBoxDiscard->setText(tr("&Restore current snapshot '%1'").arg(m_strDiscardCheckBoxText));
    m_pCheckBoxDiscard->setWhatsThis(tr("<p>When checked, the machine will be returned to the state stored in the current "
                                        "snapshot after it is turned off. This is useful if you are sure that you want to "
                                        "discard the results of your last sessions and start again at that snapshot.</p>"));
}

void UIVMCloseDialog::sltUpdateWidgetAvailability()
{
    /* Discard option should be enabled only on power-off action: */
    m_pCheckBoxDiscard->setEnabled(m_pRadioButtonPowerOff->isChecked());
}

void UIVMCloseDialog::accept()
{
    /* Calculate result: */
    if (m_pRadioButtonDetach->isChecked())
        setResult(MachineCloseAction_Detach);
    else if (m_pRadioButtonSave->isChecked())
        setResult(MachineCloseAction_SaveState);
    else if (m_pRadioButtonShutdown->isChecked())
        setResult(MachineCloseAction_Shutdown);
    else if (m_pRadioButtonPowerOff->isChecked())
    {
        if (!m_pCheckBoxDiscard->isChecked() || !m_pCheckBoxDiscard->isVisible())
            setResult(MachineCloseAction_PowerOff);
        else
            setResult(MachineCloseAction_PowerOff_RestoringSnapshot);
    }

    /* Memorize the last user's choice for the given VM: */
    MachineCloseAction newCloseAction = static_cast<MachineCloseAction>(result());
    /* But make sure 'Shutdown' is preserved if temporary unavailable: */
    if (newCloseAction == MachineCloseAction_PowerOff &&
        m_enmLastCloseAction == MachineCloseAction_Shutdown && !m_fIsACPIEnabled)
        newCloseAction = MachineCloseAction_Shutdown;
    gEDataManager->setLastMachineCloseAction(newCloseAction, uiCommon().managedVMUuid());

    /* Hide the dialog: */
    hide();
}

void UIVMCloseDialog::setButtonEnabledDetach(bool fEnabled)
{
    m_pLabelIconDetach->setEnabled(fEnabled);
    m_pRadioButtonDetach->setEnabled(fEnabled);
}

void UIVMCloseDialog::setButtonVisibleDetach(bool fVisible)
{
    m_pLabelIconDetach->setVisible(fVisible);
    m_pRadioButtonDetach->setVisible(fVisible);
}

void UIVMCloseDialog::setButtonEnabledSave(bool fEnabled)
{
    m_pLabelIconSave->setEnabled(fEnabled);
    m_pRadioButtonSave->setEnabled(fEnabled);
}

void UIVMCloseDialog::setButtonVisibleSave(bool fVisible)
{
    m_pLabelIconSave->setVisible(fVisible);
    m_pRadioButtonSave->setVisible(fVisible);
}

void UIVMCloseDialog::setButtonEnabledShutdown(bool fEnabled)
{
    m_pLabelIconShutdown->setEnabled(fEnabled);
    m_pRadioButtonShutdown->setEnabled(fEnabled);
}

void UIVMCloseDialog::setButtonVisibleShutdown(bool fVisible)
{
    m_pLabelIconShutdown->setVisible(fVisible);
    m_pRadioButtonShutdown->setVisible(fVisible);
}

void UIVMCloseDialog::setButtonEnabledPowerOff(bool fEnabled)
{
    m_pLabelIconPowerOff->setEnabled(fEnabled);
    m_pRadioButtonPowerOff->setEnabled(fEnabled);
}

void UIVMCloseDialog::setButtonVisiblePowerOff(bool fVisible)
{
    m_pLabelIconPowerOff->setVisible(fVisible);
    m_pRadioButtonPowerOff->setVisible(fVisible);
}

void UIVMCloseDialog::setCheckBoxVisibleDiscard(bool fVisible)
{
    m_pCheckBoxDiscard->setVisible(fVisible);
}

void UIVMCloseDialog::prepare()
{
    /* Choose default dialog icon: */
    m_icon = UIIconPool::iconSet(":/os_unknown.png");

    /* Prepare size-grip token: */
    setSizeGripEnabled(false);

    /* Prepare main layout: */
    prepareMainLayout();

    /* Update pixmaps: */
    updatePixmaps();

    /* Configure: */
    configure();

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIVMCloseDialog::sltRetranslateUI);
}

void UIVMCloseDialog::prepareMainLayout()
{
    /* Create main layout: */
    m_pMainLayout = new QVBoxLayout(this);
    if (m_pMainLayout)
    {
        /* Configure layout: */
#ifdef VBOX_WS_MAC
        m_pMainLayout->setContentsMargins(40, 20, 40, 20);
        m_pMainLayout->setSpacing(15);
#else
        m_pMainLayout->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing) * 2);
#endif

        /* Prepare top layout: */
        prepareTopLayout();

        /* Add stretch between top and bottom: */
        m_pMainLayout->addStretch(1);

        /* Prepare button-box: */
        prepareButtonBox();
    }
}

void UIVMCloseDialog::prepareTopLayout()
{
    /* Create top layout: */
    m_pTopLayout = new QHBoxLayout;
    if (m_pTopLayout)
    {
        /* Configure layout: */
#ifdef VBOX_WS_MAC
        m_pTopLayout->setSpacing(20);
#else
        m_pTopLayout->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing) * 2);
#endif

        /* Prepare top-left layout: */
        prepareTopLeftLayout();
        /* Prepare top-right layout: */
        prepareTopRightLayout();

        /* Add into layout: */
        m_pMainLayout->addLayout(m_pTopLayout);
    }
}

void UIVMCloseDialog::prepareTopLeftLayout()
{
    /* Create top-left layout: */
    m_pTopLeftLayout = new QVBoxLayout;
    if (m_pTopLeftLayout)
    {
        /* Create icon label: */
        m_pLabelIcon = new QLabel;
        if (m_pLabelIcon)
        {
            /* Configure label: */
            m_pLabelIcon->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

            /* Add into layout: */
            m_pTopLeftLayout->addWidget(m_pLabelIcon);
        }

        /* Add vertical stretch under icon label: */
        m_pTopLeftLayout->addStretch();

        /* Add into layout: */
        m_pTopLayout->addLayout(m_pTopLeftLayout);
    }
}

void UIVMCloseDialog::prepareTopRightLayout()
{
    /* Create top-right layout: */
    m_pTopRightLayout = new QVBoxLayout;
    if (m_pTopRightLayout)
    {
        /* Configure layout: */
#ifdef VBOX_WS_MAC
        m_pTopRightLayout->setSpacing(10);
#else
        m_pTopRightLayout->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing));
#endif

        /* Create text label: */
        m_pLabelText = new QLabel;
        if (m_pLabelText)
        {
            /* Add into layout: */
            m_pTopRightLayout->addWidget(m_pLabelText);
        }

        /* Prepare choice layout: */
        prepareChoiceLayout();

        /* Add into layout: */
        m_pTopLayout->addLayout(m_pTopRightLayout);
    }
}

void UIVMCloseDialog::prepareChoiceLayout()
{
    /* Create 'choice' layout: */
    m_pChoiceLayout = new QGridLayout;
    if (m_pChoiceLayout)
    {
        /* Configure layout: */
#ifdef VBOX_WS_MAC
        m_pChoiceLayout->setSpacing(10);
#else
        m_pChoiceLayout->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing));
#endif

        /* Create button-group: */
        QButtonGroup *pButtonGroup = new QButtonGroup(this);
        if (pButtonGroup)
            connect(pButtonGroup, &QButtonGroup::buttonClicked,
                    this, &UIVMCloseDialog::sltUpdateWidgetAvailability);

        /* Create 'detach' icon label: */
        m_pLabelIconDetach = new QLabel;
        if (m_pLabelIconDetach)
        {
            /* Configure label: */
            m_pLabelIconDetach->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            /* Add into layout: */
            m_pChoiceLayout->addWidget(m_pLabelIconDetach, 0, 0);
        }
        /* Create 'detach' radio-button: */
        m_pRadioButtonDetach = new QRadioButton;
        if (m_pRadioButtonDetach)
        {
            /* Configure button: */
            m_pRadioButtonDetach->installEventFilter(this);
            /* Add into group/layout: */
            pButtonGroup->addButton(m_pRadioButtonDetach);
            m_pChoiceLayout->addWidget(m_pRadioButtonDetach, 0, 1);
        }

        /* Create 'save' icon label: */
        m_pLabelIconSave = new QLabel;
        if (m_pLabelIconSave)
        {
            /* Configure label: */
            m_pLabelIconSave->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            /* Add into layout: */
            m_pChoiceLayout->addWidget(m_pLabelIconSave, 1, 0);
        }
        /* Create 'save' radio-button: */
        m_pRadioButtonSave = new QRadioButton;
        if (m_pRadioButtonSave)
        {
            /* Configure button: */
            m_pRadioButtonSave->installEventFilter(this);
            /* Add into group/layout: */
            pButtonGroup->addButton(m_pRadioButtonSave);
            m_pChoiceLayout->addWidget(m_pRadioButtonSave, 1, 1);
        }

        /* Create 'shutdown' icon label: */
        m_pLabelIconShutdown = new QLabel;
        if (m_pLabelIconShutdown)
        {
            /* Configure label: */
            m_pLabelIconShutdown->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            /* Add into layout: */
            m_pChoiceLayout->addWidget(m_pLabelIconShutdown, 2, 0);
        }
        /* Create 'shutdown' radio-button: */
        m_pRadioButtonShutdown = new QRadioButton;
        if (m_pRadioButtonShutdown)
        {
            /* Configure button: */
            m_pRadioButtonShutdown->installEventFilter(this);
            /* Add into group/layout: */
            pButtonGroup->addButton(m_pRadioButtonShutdown);
            m_pChoiceLayout->addWidget(m_pRadioButtonShutdown, 2, 1);
        }

        /* Create 'power-off' icon label: */
        m_pLabelIconPowerOff = new QLabel;
        if (m_pLabelIconPowerOff)
        {
            /* Configure label: */
            m_pLabelIconPowerOff->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            /* Add into layout: */
            m_pChoiceLayout->addWidget(m_pLabelIconPowerOff, 3, 0);
        }
        /* Create 'shutdown' radio-button: */
        m_pRadioButtonPowerOff = new QRadioButton;
        if (m_pRadioButtonPowerOff)
        {
            /* Configure button: */
            m_pRadioButtonPowerOff->installEventFilter(this);
            /* Add into group/layout: */
            pButtonGroup->addButton(m_pRadioButtonPowerOff);
            m_pChoiceLayout->addWidget(m_pRadioButtonPowerOff, 3, 1);
        }

        /* Create 'discard' check-box: */
        m_pCheckBoxDiscard = new QCheckBox;
        if (m_pCheckBoxDiscard)
        {
            /* Add into layout: */
            m_pChoiceLayout->addWidget(m_pCheckBoxDiscard, 4, 1);
        }

        /* Add into layout: */
        m_pTopRightLayout->addLayout(m_pChoiceLayout);
    }
}

void UIVMCloseDialog::prepareButtonBox()
{
    /* Create button-box: */
    QIDialogButtonBox *pButtonBox = new QIDialogButtonBox;
    if (pButtonBox)
    {
        /* Configure button-box: */
        pButtonBox->setStandardButtons(  QDialogButtonBox::Cancel
                                       | QDialogButtonBox::Help
                                       | QDialogButtonBox::NoButton
                                       | QDialogButtonBox::Ok);
        connect(pButtonBox, &QIDialogButtonBox::accepted, this, &UIVMCloseDialog::accept);
        connect(pButtonBox, &QIDialogButtonBox::rejected, this, &UIVMCloseDialog::reject);
        uiCommon().setHelpKeyword(pButtonBox->button(QIDialogButtonBox::Help), "intro-save-machine-state");
        connect(pButtonBox->button(QIDialogButtonBox::Help), &QPushButton::pressed,
                pButtonBox, &QIDialogButtonBox::sltHandleHelpRequest);
        pButtonBox->button(QIDialogButtonBox::Help)->setShortcut(UIShortcutPool::standardSequence(QKeySequence::HelpContents));

        /* Add into layout: */
        m_pMainLayout->addWidget(pButtonBox);
    }
}

void UIVMCloseDialog::configure()
{
    /* Get actual machine-state: */
    KMachineState enmActualState = KMachineState_Null;
    m_pMachine->acquireLiveMachineState(enmActualState);

    /* Check which close-actions are resticted: */
    bool fIsDetachAllowed = uiCommon().isSeparateProcess() && !(m_restictedCloseActions & MachineCloseAction_Detach);
    bool fIsStateSavingAllowed = !(m_restictedCloseActions & MachineCloseAction_SaveState);
    bool fIsACPIShutdownAllowed = !(m_restictedCloseActions & MachineCloseAction_Shutdown);
    bool fIsPowerOffAllowed = !(m_restictedCloseActions & MachineCloseAction_PowerOff);
    bool fIsPowerOffAndRestoreAllowed = fIsPowerOffAllowed && !(m_restictedCloseActions & MachineCloseAction_PowerOff_RestoringSnapshot);

    /* Make 'Detach' button visible/hidden depending on restriction: */
    setButtonVisibleDetach(fIsDetachAllowed);
    /* Make 'Detach' button enabled/disabled depending on machine-state: */
    setButtonEnabledDetach(enmActualState != KMachineState_Stuck);

    /* Make 'Save state' button visible/hidden depending on restriction: */
    setButtonVisibleSave(fIsStateSavingAllowed);
    /* Make 'Save state' button enabled/disabled depending on machine-state: */
    setButtonEnabledSave(enmActualState != KMachineState_Stuck);

    /* Make 'Shutdown' button visible/hidden depending on restriction: */
    setButtonVisibleShutdown(fIsACPIShutdownAllowed);
    /* Make 'Shutdown' button enabled/disabled depending on console and machine-state: */
    setButtonEnabledShutdown(m_fIsACPIEnabled && enmActualState != KMachineState_Stuck);

    /* Make 'Power off' button visible/hidden depending on restriction: */
    setButtonVisiblePowerOff(fIsPowerOffAllowed);
    /* Make the Restore Snapshot checkbox visible/hidden depending on snapshot count & restrictions: */
    ulong uSnapshotCount = 0;
    m_pMachine->acquireSnapshotCount(uSnapshotCount);
    setCheckBoxVisibleDiscard(fIsPowerOffAndRestoreAllowed && uSnapshotCount > 0);
    /* Assign Restore Snapshot checkbox text: */
    if (uSnapshotCount > 0)
    {
        QString strCurrentSnapshotName;
        m_pMachine->acquireCurrentSnapshotName(strCurrentSnapshotName);
        m_strDiscardCheckBoxText = strCurrentSnapshotName;
    }

    /* Check which radio-button should be initially chosen: */
    QRadioButton *pRadioButtonToChoose = 0;
    /* If choosing 'last choice' is possible: */
    m_enmLastCloseAction = gEDataManager->lastMachineCloseAction(uiCommon().managedVMUuid());
    if (m_enmLastCloseAction == MachineCloseAction_Detach && fIsDetachAllowed)
    {
        pRadioButtonToChoose = m_pRadioButtonDetach;
    }
    else if (m_enmLastCloseAction == MachineCloseAction_SaveState && fIsStateSavingAllowed)
    {
        pRadioButtonToChoose = m_pRadioButtonSave;
    }
    else if (m_enmLastCloseAction == MachineCloseAction_Shutdown && fIsACPIShutdownAllowed && m_fIsACPIEnabled)
    {
        pRadioButtonToChoose = m_pRadioButtonShutdown;
    }
    else if (m_enmLastCloseAction == MachineCloseAction_PowerOff && fIsPowerOffAllowed)
    {
        pRadioButtonToChoose = m_pRadioButtonPowerOff;
    }
    else if (m_enmLastCloseAction == MachineCloseAction_PowerOff_RestoringSnapshot && fIsPowerOffAndRestoreAllowed)
    {
        pRadioButtonToChoose = m_pRadioButtonPowerOff;
    }
    /* Else 'default choice' will be used: */
    else
    {
        if (fIsDetachAllowed)
            pRadioButtonToChoose = m_pRadioButtonDetach;
        else if (fIsStateSavingAllowed)
            pRadioButtonToChoose = m_pRadioButtonSave;
        else if (fIsACPIShutdownAllowed && m_fIsACPIEnabled)
            pRadioButtonToChoose = m_pRadioButtonShutdown;
        else if (fIsPowerOffAllowed)
            pRadioButtonToChoose = m_pRadioButtonPowerOff;
    }

    /* If some radio-button chosen: */
    if (pRadioButtonToChoose)
    {
        /* Check and focus it: */
        pRadioButtonToChoose->setChecked(true);
        pRadioButtonToChoose->setFocus();
        sltUpdateWidgetAvailability();
        m_fValid = true;
    }
}

void UIVMCloseDialog::updatePixmaps()
{
    /* Acquire hints: */
    const int iMetricSmall = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
    const int iMetricLarge = QApplication::style()->pixelMetric(QStyle::PM_LargeIconSize);

    /* Re-apply pixmap: */
    const qreal fDevicePixelRatio = windowHandle() ? windowHandle()->devicePixelRatio() : 1;
    m_pLabelIcon->setPixmap(m_icon.pixmap(QSize(iMetricLarge, iMetricLarge), fDevicePixelRatio));

    QIcon icon;
    icon = UIIconPool::iconSet(":/vm_create_shortcut_16px.png");
    m_pLabelIconDetach->setPixmap(icon.pixmap(QSize(iMetricSmall, iMetricSmall), fDevicePixelRatio));
    icon = UIIconPool::iconSet(":/vm_save_state_16px.png");
    m_pLabelIconSave->setPixmap(icon.pixmap(QSize(iMetricSmall, iMetricSmall), fDevicePixelRatio));
    icon = UIIconPool::iconSet(":/vm_shutdown_16px.png");
    m_pLabelIconShutdown->setPixmap(icon.pixmap(QSize(iMetricSmall, iMetricSmall), fDevicePixelRatio));
    icon = UIIconPool::iconSet(":/vm_poweroff_16px.png");
    m_pLabelIconPowerOff->setPixmap(icon.pixmap(QSize(iMetricSmall, iMetricSmall), fDevicePixelRatio));
}
