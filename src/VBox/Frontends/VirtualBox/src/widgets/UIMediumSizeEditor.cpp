/* $Id: UIMediumSizeEditor.cpp 108768 2025-03-17 14:04:09Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMediumSizeEditor class implementation.
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
#include <QAccessibleWidget>
#include <QGridLayout>
#include <QLabel>
#include <QRegularExpressionValidator>

/* GUI includes: */
#include "QILineEdit.h"
#include "UIConverter.h"
#include "UIGlobalSession.h"
#include "UIMediumSizeEditor.h"
#include "UITranslator.h"
#include "UITranslationEventListener.h"

/* COM includes: */
#include "CSystemProperties.h"


/** QAccessibleWidget extension used as an accessibility interface for UIMediumSizeSlider. */
class UIAccessibilityInterfaceForUIMediumSizeSlider : public QAccessibleWidget
{
public:

    /** Returns an accessibility interface for passed @a strClassname and @a pObject. */
    static QAccessibleInterface *pFactory(const QString &strClassname, QObject *pObject)
    {
        /* Creating QIRichTextLabel accessibility interface: */
        if (pObject && strClassname == QLatin1String("UIMediumSizeSlider"))
            return new UIAccessibilityInterfaceForUIMediumSizeSlider(qobject_cast<QWidget*>(pObject));

        /* Null by default: */
        return 0;
    }

    /** Constructs an accessibility interface passing @a pWidget to the base-class. */
    UIAccessibilityInterfaceForUIMediumSizeSlider(QWidget *pWidget)
        : QAccessibleWidget(pWidget, QAccessible::Slider)
    {}

    /** Returns a text for the passed @a enmTextRole. */
    virtual QString text(QAccessible::Text enmTextRole) const RT_OVERRIDE
    {
        /* Make sure label still alive: */
        AssertPtrReturn(slider(), QString());

        /* Non-macOS screen-readers using QAccessible::Value for slider: */
        if (enmTextRole == QAccessible::Value)
            return slider()->scaledValueToString();

        /* Call to base-class: */
        return QAccessibleWidget::text(enmTextRole);
    }

private:

    /** Returns corresponding UIMediumSizeSlider. */
    UIMediumSizeSlider *slider() const
    {
        return qobject_cast<UIMediumSizeSlider*>(widget());
    }
};


/*********************************************************************************************************************************
*   Class UIMediumSizeSlider implementation.                                                                                     *
*********************************************************************************************************************************/

UIMediumSizeSlider::UIMediumSizeSlider(qulonglong uSizeMax, QWidget *pParent /* = 0 */)
    : QSlider(pParent)
    , m_iSliderScale(calculateSliderScale(uSizeMax))
    , m_uScaledMinimum(0)
    , m_uScaledMaximum(100)
    , m_uScaledValue(0)
{
    /* Install UIMediumSizeSlider accessibility interface factory: */
    QAccessible::installFactory(UIAccessibilityInterfaceForUIMediumSizeSlider::pFactory);

    /* Configure basic properties: */
    setFocusPolicy(Qt::StrongFocus);
    setOrientation(Qt::Horizontal);
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);

    /* Configure tick look&feel: */
    setTickPosition(QSlider::TicksBelow);
    setTickInterval(0);

    /* Configure scaling: */
    setPageStep(m_iSliderScale);
    setSingleStep(m_iSliderScale / 8);

    /* Connection converting usual value into scaled one: */
    connect(this, &UIMediumSizeSlider::valueChanged,
            this, &UIMediumSizeSlider::sltValueChanged);
}

void UIMediumSizeSlider::setScaledMinimum(qulonglong uMinimum)
{
    /* Make sure something got changed and save the change: */
    if (m_uScaledMinimum == uMinimum)
        return;
    m_uScaledMinimum = uMinimum;
    /* Call to base-class: */
    QSlider::setMinimum(sizeMBToSlider(m_uScaledMinimum, m_iSliderScale));
}

void UIMediumSizeSlider::setScaledMaximum(qulonglong uMaximum)
{
    /* Make sure something got changed and save the change: */
    if (m_uScaledMaximum == uMaximum)
        return;
    m_uScaledMaximum = uMaximum;
    /* Call to base-class: */
    QSlider::setMaximum(sizeMBToSlider(m_uScaledMaximum, m_iSliderScale));
}

void UIMediumSizeSlider::setScaledValue(qulonglong uValue)
{
    /* Make sure something got changed and save the change: */
    if (m_uScaledValue == uValue)
        return;
    m_uScaledValue = uValue;
    /* Call to base-class: */
    QSlider::setValue(sizeMBToSlider(m_uScaledValue, m_iSliderScale));
}

QString UIMediumSizeSlider::scaledValueToString()
{
    return UITranslator::formatSize(m_uScaledValue);
}

void UIMediumSizeSlider::sltValueChanged(int iValue)
{
    /* Convert from usual to scaled value: */
    emit sigScaledValueChanged(sliderToSizeMB(iValue, m_iSliderScale));
}

/* static */
int UIMediumSizeSlider::calculateSliderScale(qulonglong uMaximumMediumSize)
{
    /* Detect how many steps to recognize between adjacent powers of 2
     * to ensure that the last slider step is exactly that we need: */
    int iSliderScale = 0;
    const int iPower = log2i(uMaximumMediumSize);
    qulonglong uTickMB = (qulonglong)1 << iPower;
    if (uTickMB < uMaximumMediumSize)
    {
        qulonglong uTickMBNext = (qulonglong)1 << (iPower + 1);
        qulonglong uGap = uTickMBNext - uMaximumMediumSize;
        iSliderScale = (int)((uTickMBNext - uTickMB) / uGap);
#ifdef VBOX_WS_MAC
        // WORKAROUND:
        // There is an issue with Qt5 QSlider under OSX:
        // Slider tick count (maximum - minimum) is limited with some
        // "magical number" - 588351, having it more than that brings
        // unpredictable results like slider token jumping and disappearing,
        // so we are limiting tick count by lowering slider-scale 128 times.
        iSliderScale /= 128;
#endif /* VBOX_WS_MAC */
    }
    return qMax(iSliderScale, 8);
}

/* static */
int UIMediumSizeSlider::log2i(qulonglong uValue)
{
    if (!uValue)
        return 0;
    int iPower = -1;
    while (uValue)
    {
        ++iPower;
        uValue >>= 1;
    }
    return iPower;
}

/* static */
int UIMediumSizeSlider::sizeMBToSlider(qulonglong uValue, int iSliderScale)
{
    /* Make sure *any* slider value is multiple of s_uSectorSize: */
    uValue /= UIMediumSizeEditor::s_uSectorSize;

    /* Calculate result: */
    const int iPower = log2i(uValue);
    const qulonglong uTickMB = qulonglong (1) << iPower;
    const qulonglong uTickMBNext = qulonglong (1) << (iPower + 1);
    const int iStep = (uValue - uTickMB) * iSliderScale / (uTickMBNext - uTickMB);
    const int iResult = iPower * iSliderScale + iStep;

    /* Return result: */
    return iResult;
}

/* static */
qulonglong UIMediumSizeSlider::sliderToSizeMB(int uValue, int iSliderScale)
{
    /* Calculate result: */
    const int iPower = uValue / iSliderScale;
    const int iStep = uValue % iSliderScale;
    const qulonglong uTickMB = qulonglong (1) << iPower;
    const qulonglong uTickMBNext = qulonglong (1) << (iPower + 1);
    qulonglong uResult = uTickMB + (uTickMBNext - uTickMB) * iStep / iSliderScale;

    /* Make sure *any* slider value is multiple of s_uSectorSize: */
    uResult *= UIMediumSizeEditor::s_uSectorSize;

    /* Return result: */
    return uResult;
}


/*********************************************************************************************************************************
*   Class UIMediumSizeEditor implementation.                                                                                     *
*********************************************************************************************************************************/

/* static */
const qulonglong UIMediumSizeEditor::s_uSectorSize = 512;

UIMediumSizeEditor::UIMediumSizeEditor(QWidget *pParent, bool fEnableEditorLabel, qulonglong uMinimumSize /* = _4M */)
    : QWidget(pParent)
    , m_uSizeMin(uMinimumSize)
    , m_uSizeMax(gpGlobalSession->virtualBox().GetSystemProperties().GetInfoVDSize())
    , m_uSize(0)
    , m_pSlider(0)
    , m_pLabelMinSize(0)
    , m_pLabelMaxSize(0)
    , m_pEditor(0)
    , m_pEditorLabel(0)
    , m_pLayout(0)
{
    prepare(fEnableEditorLabel);
}

void UIMediumSizeEditor::setMediumSize(qulonglong uSize)
{
    /* Remember the new size: */
    m_uSize = uSize;

    /* And assign it to the slider & editor: */
    m_pSlider->blockSignals(true);
    m_pSlider->setScaledValue(m_uSize);
    m_pSlider->blockSignals(false);
    m_pEditor->blockSignals(true);
    m_pEditor->setText(UITranslator::formatSize(m_uSize));
    m_strSizeSuffix = gpConverter->toString(UITranslator::parseSizeSuffix(m_pEditor->text()));
    m_pEditor->blockSignals(false);
    updateSizeToolTips(m_uSize);
}

int UIMediumSizeEditor::minimumLabelHorizontalHint() const
{
    if (m_pEditorLabel)
        return m_pEditorLabel->minimumSizeHint().width();
    return 0;
}

void UIMediumSizeEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UIMediumSizeEditor::sltRetranslateUI()
{
    /* Translate labels: */
    m_pLabelMinSize->setText(UITranslator::formatSize(m_uSizeMin));
    m_pLabelMaxSize->setText(UITranslator::formatSize(m_uSizeMax));
    if (m_pEditorLabel)
        m_pEditorLabel->setText(tr("D&isk Size"));

    /* Translate fields: */
    m_pSlider->setToolTip(tr("Medium size"));
    m_pEditor->setToolTip(tr("Medium size"));
    m_pLabelMinSize->setToolTip(tr("Minimum possible disk size"));
    m_pLabelMaxSize->setToolTip(tr("Maximum possible disk size"));
}

void UIMediumSizeEditor::sltSizeSliderChanged(qulonglong uValue)
{
    /* Update the current size: */
    m_uSize = uValue;
    /* Update the other widget: */
    m_pEditor->blockSignals(true);
    m_pEditor->setText(UITranslator::formatSize(m_uSize));
    m_strSizeSuffix = gpConverter->toString(UITranslator::parseSizeSuffix(m_pEditor->text()));
    m_pEditor->blockSignals(false);
    updateSizeToolTips(m_uSize);
    /* Notify the listeners: */
    emit sigSizeChanged(m_uSize);
}

void UIMediumSizeEditor::sltSizeEditorTextChanged()
{
    QString strSizeString = ensureSizeSuffix(m_pEditor->text());

    m_pEditor->blockSignals(true);
    int iCursorPosition = m_pEditor->cursorPosition();
    m_pEditor->setText(strSizeString);
    m_pEditor->setCursorPosition(iCursorPosition);
    m_pEditor->blockSignals(false);

    /* Update the current size: */
    m_uSize = checkSectorSizeAlignment(UITranslator::parseSize(strSizeString));

    /* Update the other widget: */
    m_pSlider->blockSignals(true);
    m_pSlider->setScaledValue(m_uSize);
    m_pSlider->blockSignals(false);
    updateSizeToolTips(m_uSize);
    /* Notify the listeners: */
    emit sigSizeChanged(m_uSize);
}

void UIMediumSizeEditor::prepare(bool fEnableEditorLabel)
{
    /* Configure reg-exp: */
    m_regExNonDigitOrSeparator = QRegularExpression(QString("[^\\d%1]").arg(UITranslator::decimalSep()));

    /* Create editor label: */
    if (fEnableEditorLabel)
    {
        m_pEditorLabel = new QLabel(this);
        m_pEditorLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        AssertReturnVoid(m_pEditorLabel);
    }

    /* Create size slider: */
    m_pSlider = new UIMediumSizeSlider(m_uSizeMax, this);
    AssertPtrReturnVoid(m_pSlider);
    /* Configure slider: */
    m_pSlider->setScaledMinimum(m_uSizeMin);
    m_pSlider->setScaledMaximum(m_uSizeMax);
    connect(m_pSlider, &UIMediumSizeSlider::sigScaledValueChanged,
            this, &UIMediumSizeEditor::sltSizeSliderChanged);

    /* Create minimum size label: */
    m_pLabelMinSize = new QLabel;
    AssertPtrReturnVoid(m_pLabelMinSize);
    /* Configure label: */
    m_pLabelMinSize->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    /* Create maximum size label: */
    m_pLabelMaxSize = new QLabel;
    AssertPtrReturnVoid(m_pLabelMaxSize);
    /* Configure label: */
    m_pLabelMaxSize->setAlignment(Qt::AlignRight | Qt::AlignVCenter);


    /* Create size editor: */
    m_pEditor = new QILineEdit;
    AssertPtrReturnVoid(m_pEditor);
    if (m_pEditorLabel)
        m_pEditorLabel->setBuddy(m_pEditorLabel);

    /* Configure editor: */
    m_pEditor->installEventFilter(this);
    m_pEditor->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_pEditor->setFixedWidthByText("88888.88 MB");
    m_pEditor->setAlignment(Qt::AlignRight);
    m_pEditor->setValidator(new QRegularExpressionValidator(QRegularExpression(UITranslator::sizeRegexp()), this));
    connect(m_pEditor, &QILineEdit::textChanged,
            this, &UIMediumSizeEditor::sltSizeEditorTextChanged);

    /* Create layout: */
    m_pLayout = new QGridLayout(this);
    AssertPtrReturnVoid(m_pLayout);

    /* Configure layout: */
    m_pLayout->setContentsMargins(0, 0, 0, 0);
    if (m_pEditorLabel)
    {
        m_pLayout->setColumnStretch(1, 1);
        m_pLayout->setColumnStretch(2, 1);
        m_pLayout->setColumnStretch(3, 0);

        m_pLayout->addWidget(m_pEditorLabel,  0, 0, 1, 1);
        m_pLayout->addWidget(m_pSlider,       0, 1, 1, 2, Qt::AlignTop);
        m_pLayout->addWidget(m_pLabelMinSize, 1, 1);
        m_pLayout->addWidget(m_pLabelMaxSize, 1, 2);
        m_pLayout->addWidget(m_pEditor,       0, 3, Qt::AlignTop);
    }
    else
    {
        m_pLayout->setColumnStretch(0, 1);
        m_pLayout->setColumnStretch(1, 1);
        m_pLayout->setColumnStretch(2, 0);

        m_pLayout->addWidget(m_pSlider, 0, 0, 1, 2, Qt::AlignTop);
        m_pLayout->addWidget(m_pLabelMinSize, 1, 0);
        m_pLayout->addWidget(m_pLabelMaxSize, 1, 1);
        m_pLayout->addWidget(m_pEditor, 0, 2, Qt::AlignTop);
    }

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIMediumSizeEditor::sltRetranslateUI);
}

void UIMediumSizeEditor::updateSizeToolTips(qulonglong uSize)
{
    const QString strToolTip = tr("Disk size set to %1").arg(UITranslator::formatSize(uSize));
    m_pSlider->setToolTip(strToolTip);
    m_pEditor->setToolTip(strToolTip);
}

qulonglong UIMediumSizeEditor::checkSectorSizeAlignment(qulonglong uSize)
{
    if (s_uSectorSize == 0 || uSize % s_uSectorSize == 0)
        return uSize;
    return (uSize / s_uSectorSize) * s_uSectorSize;
}

QString UIMediumSizeEditor::ensureSizeSuffix(const QString &strSizeString)
{
    /* Try to update the m_strSizeSuffix: */
    if (UITranslator::hasSizeSuffix(strSizeString))
        m_strSizeSuffix = gpConverter->toString(UITranslator::parseSizeSuffix(strSizeString));

    /* Remove any chars from the string except digits and decimal separator and then add a space and size suffix: */
    QString strOnlyDigits(strSizeString);
    return QString("%1 %2").arg(strOnlyDigits.remove(m_regExNonDigitOrSeparator)).arg(m_strSizeSuffix);
}
