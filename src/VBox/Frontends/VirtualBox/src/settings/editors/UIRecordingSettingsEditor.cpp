/* $Id: UIRecordingSettingsEditor.cpp 168969 2025-05-23 11:22:24Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIRecordingSettingsEditor class implementation.
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
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIAdvancedSlider.h"
#include "UICommon.h"
#include "UIConverter.h"
#include "UIFilePathSelector.h"
#include "UIFilmContainer.h"
#include "UIGlobalSession.h"
#include "UIRecordingSettingsEditor.h"

/* COM includes: */
#include "CSystemProperties.h"

/* Defines: */
#define VIDEO_CAPTURE_BIT_RATE_MIN 32
#define VIDEO_CAPTURE_BIT_RATE_MAX 2048


UIRecordingSettingsEditor::UIRecordingSettingsEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_fFeatureEnabled(false)
    , m_fOptionsAvailable(false)
    , m_fScreenOptionsAvailable(false)
    , m_enmMode(UISettingsDefs::RecordingMode_Max)
    , m_iFrameWidth(0)
    , m_iFrameHeight(0)
    , m_iFrameRate(0)
    , m_iBitRate(0)
    , m_iAudioQualityRate(0)
    , m_pCheckboxFeature(0)
    , m_pLabelMode(0)
    , m_pComboMode(0)
    , m_pLabelFilePath(0)
    , m_pEditorFilePath(0)
    , m_pLabelFrameSize(0)
    , m_pComboFrameSize(0)
    , m_pSpinboxFrameWidth(0)
    , m_pSpinboxFrameHeight(0)
    , m_pLabelFrameRate(0)
    , m_pWidgetFrameRateSettings(0)
    , m_pSliderFrameRate(0)
    , m_pSpinboxFrameRate(0)
    , m_pLabelFrameRateMin(0)
    , m_pLabelFrameRateMax(0)
    , m_pLabelVideoQuality(0)
    , m_pWidgetVideoQualitySettings(0)
    , m_pSliderVideoQuality(0)
    , m_pSpinboxVideoQuality(0)
    , m_pLabelVideoQualityMin(0)
    , m_pLabelVideoQualityMed(0)
    , m_pLabelVideoQualityMax(0)
    , m_pLabelAudioQuality(0)
    , m_pWidgetAudioQualitySettings(0)
    , m_pSliderAudioQuality(0)
    , m_pLabelAudioQualityMin(0)
    , m_pLabelAudioQualityMed(0)
    , m_pLabelAudioQualityMax(0)
    , m_pLabelSizeHint(0)
    , m_pLabelScreens(0)
    , m_pScrollerScreens(0)
{
    prepare();
}

void UIRecordingSettingsEditor::setFeatureEnabled(bool fEnabled)
{
    /* Update cached value and
     * check-box if value has changed: */
    if (m_fFeatureEnabled != fEnabled)
    {
        m_fFeatureEnabled = fEnabled;
        if (m_pCheckboxFeature)
        {
            m_pCheckboxFeature->setChecked(m_fFeatureEnabled);
            sltHandleFeatureToggled();
        }
    }
}

bool UIRecordingSettingsEditor::isFeatureEnabled() const
{
    return m_pCheckboxFeature ? m_pCheckboxFeature->isChecked() : m_fFeatureEnabled;
}

void UIRecordingSettingsEditor::setOptionsAvailable(bool fAvailable)
{
    /* Update cached value and
     * widget availability if value has changed: */
    if (m_fOptionsAvailable != fAvailable)
    {
        m_fOptionsAvailable = fAvailable;
        updateWidgetAvailability();
    }
}

void UIRecordingSettingsEditor::setScreenOptionsAvailable(bool fAvailable)
{
    /* Update cached value and
     * widget availability if value has changed: */
    if (m_fScreenOptionsAvailable != fAvailable)
    {
        m_fScreenOptionsAvailable = fAvailable;
        updateWidgetAvailability();
    }
}

void UIRecordingSettingsEditor::setMode(UISettingsDefs::RecordingMode enmMode)
{
    /* Update cached value and
     * combo if value has changed: */
    if (m_enmMode != enmMode)
    {
        m_enmMode = enmMode;
        populateComboMode();
        updateWidgetVisibility();
    }
}

UISettingsDefs::RecordingMode UIRecordingSettingsEditor::mode() const
{
    return m_pComboMode ? m_pComboMode->currentData().value<UISettingsDefs::RecordingMode>() : m_enmMode;
}

void UIRecordingSettingsEditor::setFolder(const QString &strFolder)
{
    /* Update cached value and
     * file editor if value has changed: */
    if (m_strFolder != strFolder)
    {
        m_strFolder = strFolder;
        if (m_pEditorFilePath)
            m_pEditorFilePath->setInitialPath(m_strFolder);
    }
}

QString UIRecordingSettingsEditor::folder() const
{
    return m_pEditorFilePath ? m_pEditorFilePath->initialPath() : m_strFolder;
}

void UIRecordingSettingsEditor::setFilePath(const QString &strFilePath)
{
    /* Update cached value and
     * file editor if value has changed: */
    if (m_strFilePath != strFilePath)
    {
        m_strFilePath = strFilePath;
        if (m_pEditorFilePath)
            m_pEditorFilePath->setPath(m_strFilePath);
    }
}

QString UIRecordingSettingsEditor::filePath() const
{
    return m_pEditorFilePath ? m_pEditorFilePath->path() : m_strFilePath;
}

void UIRecordingSettingsEditor::setFrameWidth(int iWidth)
{
    /* Update cached value and
     * spin-box if value has changed: */
    if (m_iFrameWidth != iWidth)
    {
        m_iFrameWidth = iWidth;
        if (m_pSpinboxFrameWidth)
            m_pSpinboxFrameWidth->setValue(m_iFrameWidth);
    }
}

int UIRecordingSettingsEditor::frameWidth() const
{
    return m_pSpinboxFrameWidth ? m_pSpinboxFrameWidth->value() : m_iFrameWidth;
}

void UIRecordingSettingsEditor::setFrameHeight(int iHeight)
{
    /* Update cached value and
     * spin-box if value has changed: */
    if (m_iFrameHeight != iHeight)
    {
        m_iFrameHeight = iHeight;
        if (m_pSpinboxFrameHeight)
            m_pSpinboxFrameHeight->setValue(m_iFrameHeight);
    }
}

int UIRecordingSettingsEditor::frameHeight() const
{
    return m_pSpinboxFrameHeight ? m_pSpinboxFrameHeight->value() : m_iFrameHeight;
}

void UIRecordingSettingsEditor::setFrameRate(int iRate)
{
    /* Update cached value and
     * spin-box if value has changed: */
    if (m_iFrameRate != iRate)
    {
        m_iFrameRate = iRate;
        if (m_pSpinboxFrameRate)
            m_pSpinboxFrameRate->setValue(m_iFrameRate);
    }
}

int UIRecordingSettingsEditor::frameRate() const
{
    return m_pSpinboxFrameRate ? m_pSpinboxFrameRate->value() : m_iFrameRate;
}

void UIRecordingSettingsEditor::setBitRate(int iRate)
{
    /* Update cached value and
     * spin-box if value has changed: */
    if (m_iBitRate != iRate)
    {
        m_iBitRate = iRate;
        if (m_pSpinboxVideoQuality)
            m_pSpinboxVideoQuality->setValue(m_iBitRate);
    }
}

int UIRecordingSettingsEditor::bitRate() const
{
    return m_pSpinboxVideoQuality ? m_pSpinboxVideoQuality->value() : m_iBitRate;
}

void UIRecordingSettingsEditor::setAudioQualityRate(int iRate)
{
    /* Update cached value and
     * slider if value has changed: */
    if (m_iAudioQualityRate != iRate)
    {
        m_iAudioQualityRate = iRate;
        if (m_pSliderAudioQuality)
            m_pSliderAudioQuality->setValue(m_iAudioQualityRate);
    }
}

int UIRecordingSettingsEditor::audioQualityRate() const
{
    return m_pSliderAudioQuality ? m_pSliderAudioQuality->value() : m_iAudioQualityRate;
}

void UIRecordingSettingsEditor::setScreens(const QVector<bool> &screens)
{
    /* Update cached value and
     * editor if value has changed: */
    if (m_screens != screens)
    {
        m_screens = screens;
        if (m_pScrollerScreens)
            m_pScrollerScreens->setValue(m_screens);
    }
}

QVector<bool> UIRecordingSettingsEditor::screens() const
{
    return m_pScrollerScreens ? m_pScrollerScreens->value() : m_screens;
}

void UIRecordingSettingsEditor::sltRetranslateUI()
{
    m_pCheckboxFeature->setText(tr("&Enable Recording"));
    m_pCheckboxFeature->setToolTip(tr("VirtualBox will record the virtual machine session as a video file"));

    m_pLabelMode->setText(tr("Recording &Mode"));
    for (int iIndex = 0; iIndex < m_pComboMode->count(); ++iIndex)
    {
        const UISettingsDefs::RecordingMode enmType =
            m_pComboMode->itemData(iIndex).value<UISettingsDefs::RecordingMode>();
        m_pComboMode->setItemText(iIndex, gpConverter->toString(enmType));
    }
    m_pComboMode->setToolTip(tr("The recording mode"));

    m_pLabelFilePath->setText(tr("File &Path"));
    m_pEditorFilePath->setToolTip(tr("The filename VirtualBox uses to save the recorded content"));

    m_pLabelFrameSize->setText(tr("Frame Si&ze"));
    m_pComboFrameSize->setItemText(0, tr("User Defined"));
    m_pComboFrameSize->setToolTip(tr("The resolution (frame size) of the recorded video"));
    m_pSpinboxFrameWidth->setToolTip(tr("The horizontal resolution (frame width) of the recorded video"));
    m_pSpinboxFrameHeight->setToolTip(tr("The vertical resolution (frame height) of the recorded video"));

    m_pLabelFrameRate->setText(tr("Frame R&ate"));
    m_pSliderFrameRate->setToolTip(tr("The maximum number of frames per second. Additional frames "
                                      "will be skipped. Reducing this value will increase the number of skipped "
                                      "frames and reduce the file size."));
    m_pSpinboxFrameRate->setSuffix(QString(" %1").arg(tr("fps")));
    m_pSpinboxFrameRate->setToolTip(tr("The maximum number of frames per second. Additional frames "
                                       "will be skipped. Reducing this value will increase the number of skipped "
                                       "frames and reduce the file size."));
    m_pLabelFrameRateMin->setText(tr("%1 fps").arg(m_pSliderFrameRate->minimum()));
    m_pLabelFrameRateMin->setToolTip(tr("Minimum recording frame rate"));
    m_pLabelFrameRateMax->setText(tr("%1 fps").arg(m_pSliderFrameRate->maximum()));
    m_pLabelFrameRateMax->setToolTip(tr("Maximum recording frame rate"));

    m_pLabelVideoQuality->setText(tr("&Video Quality"));
    m_pSliderVideoQuality->setToolTip(tr("The quality. Increasing this value will make the video "
                                         "look better at the cost of an increased file size."));
    m_pSpinboxVideoQuality->setSuffix(QString(" %1").arg(tr("kbps")));
    m_pSpinboxVideoQuality->setToolTip(tr("The bitrate in kilobits per second. Increasing this value "
                                          "will make the video look better at the cost of an increased file size."));
    m_pLabelVideoQualityMin->setText(tr("low", "quality"));
    m_pLabelVideoQualityMed->setText(tr("medium", "quality"));
    m_pLabelVideoQualityMax->setText(tr("high", "quality"));

    m_pLabelAudioQuality->setText(tr("&Audio Quality"));
    m_pSliderAudioQuality->setToolTip(tr("The quality. Increasing this value will make the audio "
                                         "sound better at the cost of an increased file size."));
    m_pLabelAudioQualityMin->setText(tr("low", "quality"));
    m_pLabelAudioQualityMed->setText(tr("medium", "quality"));
    m_pLabelAudioQualityMax->setText(tr("high", "quality"));

    m_pLabelScreens->setText(tr("Scree&ns"));

    updateRecordingFileSizeHint();
}

void UIRecordingSettingsEditor::sltHandleFeatureToggled()
{
    /* Update widget availability: */
    updateWidgetAvailability();
}

void UIRecordingSettingsEditor::sltHandleModeComboChange()
{
    /* Update widget availability: */
    updateWidgetAvailability();
}

void UIRecordingSettingsEditor::sltHandleVideoFrameSizeComboChange()
{
    /* Get the proposed size: */
    const int iCurrentIndex = m_pComboFrameSize->currentIndex();
    const QSize videoCaptureSize = m_pComboFrameSize->itemData(iCurrentIndex).toSize();

    /* Make sure its valid: */
    if (!videoCaptureSize.isValid())
        return;

    /* Apply proposed size: */
    m_pSpinboxFrameWidth->setValue(videoCaptureSize.width());
    m_pSpinboxFrameHeight->setValue(videoCaptureSize.height());
}

void UIRecordingSettingsEditor::sltHandleVideoFrameWidthChange()
{
    /* Look for preset: */
    lookForCorrespondingFrameSizePreset();
    /* Update quality and bit-rate: */
    sltHandleVideoBitRateSliderChange();
}

void UIRecordingSettingsEditor::sltHandleVideoFrameHeightChange()
{
    /* Look for preset: */
    lookForCorrespondingFrameSizePreset();
    /* Update quality and bit-rate: */
    sltHandleVideoBitRateSliderChange();
}

void UIRecordingSettingsEditor::sltHandleVideoFrameRateSliderChange()
{
    /* Apply proposed frame-rate: */
    m_pSpinboxFrameRate->blockSignals(true);
    m_pSpinboxFrameRate->setValue(m_pSliderFrameRate->value());
    m_pSpinboxFrameRate->blockSignals(false);
    /* Update quality and bit-rate: */
    sltHandleVideoBitRateSliderChange();
}

void UIRecordingSettingsEditor::sltHandleVideoFrameRateSpinboxChange()
{
    /* Apply proposed frame-rate: */
    m_pSliderFrameRate->blockSignals(true);
    m_pSliderFrameRate->setValue(m_pSpinboxFrameRate->value());
    m_pSliderFrameRate->blockSignals(false);
    /* Update quality and bit-rate: */
    sltHandleVideoBitRateSliderChange();
}

void UIRecordingSettingsEditor::sltHandleVideoBitRateSliderChange()
{
    /* Calculate/apply proposed bit-rate: */
    m_pSpinboxVideoQuality->blockSignals(true);
    m_pSpinboxVideoQuality->setValue(calculateBitRate(m_pSpinboxFrameWidth->value(),
                                                               m_pSpinboxFrameHeight->value(),
                                                               m_pSpinboxFrameRate->value(),
                                                               m_pSliderVideoQuality->value()));
    m_pSpinboxVideoQuality->blockSignals(false);
    updateRecordingFileSizeHint();
}

void UIRecordingSettingsEditor::sltHandleVideoBitRateSpinboxChange()
{
    /* Calculate/apply proposed quality: */
    m_pSliderVideoQuality->blockSignals(true);
    m_pSliderVideoQuality->setValue(calculateQuality(m_pSpinboxFrameWidth->value(),
                                                              m_pSpinboxFrameHeight->value(),
                                                              m_pSpinboxFrameRate->value(),
                                                              m_pSpinboxVideoQuality->value()));
    m_pSliderVideoQuality->blockSignals(false);
    updateRecordingFileSizeHint();
}

void UIRecordingSettingsEditor::prepare()
{
    /* Prepare everything: */
    prepareWidgets();
    prepareConnections();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIRecordingSettingsEditor::prepareWidgets()
{
    /* Prepare main layout: */
    QGridLayout *pLayout = new QGridLayout(this);
    if (pLayout)
    {
        pLayout->setContentsMargins(0, 0, 0, 0);
        pLayout->setColumnStretch(1, 1);

        /* Prepare 'feature' check-box: */
        m_pCheckboxFeature = new QCheckBox(this);
        if (m_pCheckboxFeature)
        {
            // this name is used from outside, have a look at UIMachineLogic..
            m_pCheckboxFeature->setObjectName("m_pCheckboxVideoCapture");
            pLayout->addWidget(m_pCheckboxFeature, 0, 0, 1, 2);
        }

        /* Prepare 20-px shifting spacer: */
        QSpacerItem *pSpacerItem = new QSpacerItem(20, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        if (pSpacerItem)
            pLayout->addItem(pSpacerItem, 1, 0);

        /* Prepare 'settings' widget: */
        QWidget *pWidgetSettings = new QWidget(this);
        if (pWidgetSettings)
        {
            /* Prepare recording settings widget layout: */
            QGridLayout *pLayoutSettings = new QGridLayout(pWidgetSettings);
            if (pLayoutSettings)
            {
                pLayoutSettings->setContentsMargins(0, 0, 0, 0);

                /* Prepare recording mode label: */
                m_pLabelMode = new QLabel(pWidgetSettings);
                if (m_pLabelMode)
                {
                    m_pLabelMode->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    pLayoutSettings->addWidget(m_pLabelMode, 0, 0);
                }
                /* Prepare recording mode combo: */
                m_pComboMode = new QComboBox(pWidgetSettings);
                if (m_pComboMode)
                {
                    if (m_pLabelMode)
                        m_pLabelMode->setBuddy(m_pComboMode);
                    m_pComboMode->addItem(QString(), QVariant::fromValue(UISettingsDefs::RecordingMode_VideoAudio));
                    m_pComboMode->addItem(QString(), QVariant::fromValue(UISettingsDefs::RecordingMode_VideoOnly));
                    m_pComboMode->addItem(QString(), QVariant::fromValue(UISettingsDefs::RecordingMode_AudioOnly));

                    pLayoutSettings->addWidget(m_pComboMode, 0, 1, 1, 3);
                }

                /* Prepare recording file path label: */
                m_pLabelFilePath = new QLabel(pWidgetSettings);
                if (m_pLabelFilePath)
                {
                    m_pLabelFilePath->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    pLayoutSettings->addWidget(m_pLabelFilePath, 1, 0);
                }
                /* Prepare recording file path editor: */
                m_pEditorFilePath = new UIFilePathSelector(pWidgetSettings);
                if (m_pEditorFilePath)
                {
                    if (m_pLabelFilePath)
                        m_pLabelFilePath->setBuddy(m_pEditorFilePath);
                    m_pEditorFilePath->setEditable(false);
                    m_pEditorFilePath->setMode(UIFilePathSelector::Mode_File_Save);

                    pLayoutSettings->addWidget(m_pEditorFilePath, 1, 1, 1, 3);
                }

                /* Prepare recording frame size label: */
                m_pLabelFrameSize = new QLabel(pWidgetSettings);
                if (m_pLabelFrameSize)
                {
                    m_pLabelFrameSize->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    pLayoutSettings->addWidget(m_pLabelFrameSize, 2, 0);
                }
                /* Prepare recording frame size combo: */
                m_pComboFrameSize = new QComboBox(pWidgetSettings);
                if (m_pComboFrameSize)
                {
                    if (m_pLabelFrameSize)
                        m_pLabelFrameSize->setBuddy(m_pComboFrameSize);
                    m_pComboFrameSize->setSizePolicy(QSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed));
                    m_pComboFrameSize->addItem(""); /* User Defined */
                    m_pComboFrameSize->addItem("320 x 200 (16:10)",   QSize(320, 200));
                    m_pComboFrameSize->addItem("640 x 480 (4:3)",     QSize(640, 480));
                    m_pComboFrameSize->addItem("720 x 400 (9:5)",     QSize(720, 400));
                    m_pComboFrameSize->addItem("720 x 480 (3:2)",     QSize(720, 480));
                    m_pComboFrameSize->addItem("800 x 600 (4:3)",     QSize(800, 600));
                    m_pComboFrameSize->addItem("1024 x 768 (4:3)",    QSize(1024, 768));
                    m_pComboFrameSize->addItem("1152 x 864 (4:3)",    QSize(1152, 864));
                    m_pComboFrameSize->addItem("1280 x 720 (16:9)",   QSize(1280, 720));
                    m_pComboFrameSize->addItem("1280 x 800 (16:10)",  QSize(1280, 800));
                    m_pComboFrameSize->addItem("1280 x 960 (4:3)",    QSize(1280, 960));
                    m_pComboFrameSize->addItem("1280 x 1024 (5:4)",   QSize(1280, 1024));
                    m_pComboFrameSize->addItem("1366 x 768 (16:9)",   QSize(1366, 768));
                    m_pComboFrameSize->addItem("1440 x 900 (16:10)",  QSize(1440, 900));
                    m_pComboFrameSize->addItem("1440 x 1080 (4:3)",   QSize(1440, 1080));
                    m_pComboFrameSize->addItem("1600 x 900 (16:9)",   QSize(1600, 900));
                    m_pComboFrameSize->addItem("1680 x 1050 (16:10)", QSize(1680, 1050));
                    m_pComboFrameSize->addItem("1600 x 1200 (4:3)",   QSize(1600, 1200));
                    m_pComboFrameSize->addItem("1920 x 1080 (16:9)",  QSize(1920, 1080));
                    m_pComboFrameSize->addItem("1920 x 1200 (16:10)", QSize(1920, 1200));
                    m_pComboFrameSize->addItem("1920 x 1440 (4:3)",   QSize(1920, 1440));
                    m_pComboFrameSize->addItem("2880 x 1800 (16:10)", QSize(2880, 1800));

                    pLayoutSettings->addWidget(m_pComboFrameSize, 2, 1);
                }
                /* Prepare recording frame width spinbox: */
                m_pSpinboxFrameWidth = new QSpinBox(pWidgetSettings);
                if (m_pSpinboxFrameWidth)
                {
                    uiCommon().setMinimumWidthAccordingSymbolCount(m_pSpinboxFrameWidth, 5);
                    m_pSpinboxFrameWidth->setMinimum(16);
                    m_pSpinboxFrameWidth->setMaximum(2880);

                    pLayoutSettings->addWidget(m_pSpinboxFrameWidth, 2, 2);
                }
                /* Prepare recording frame height spinbox: */
                m_pSpinboxFrameHeight = new QSpinBox(pWidgetSettings);
                if (m_pSpinboxFrameHeight)
                {
                    uiCommon().setMinimumWidthAccordingSymbolCount(m_pSpinboxFrameHeight, 5);
                    m_pSpinboxFrameHeight->setMinimum(16);
                    m_pSpinboxFrameHeight->setMaximum(1800);

                    pLayoutSettings->addWidget(m_pSpinboxFrameHeight, 2, 3);
                }

                /* Prepare recording frame rate label: */
                m_pLabelFrameRate = new QLabel(pWidgetSettings);
                if (m_pLabelFrameRate)
                {
                    m_pLabelFrameRate->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    pLayoutSettings->addWidget(m_pLabelFrameRate, 3, 0);
                }
                /* Prepare recording frame rate widget: */
                m_pWidgetFrameRateSettings = new QWidget(pWidgetSettings);
                if (m_pWidgetFrameRateSettings)
                {
                    /* Prepare recording frame rate layout: */
                    QVBoxLayout *pLayoutRecordingFrameRate = new QVBoxLayout(m_pWidgetFrameRateSettings);
                    if (pLayoutRecordingFrameRate)
                    {
                        pLayoutRecordingFrameRate->setContentsMargins(0, 0, 0, 0);

                        /* Prepare recording frame rate slider: */
                        m_pSliderFrameRate = new QIAdvancedSlider(m_pWidgetFrameRateSettings);
                        if (m_pSliderFrameRate)
                        {
                            m_pSliderFrameRate->setOrientation(Qt::Horizontal);
                            m_pSliderFrameRate->setMinimum(1);
                            m_pSliderFrameRate->setMaximum(30);
                            m_pSliderFrameRate->setPageStep(1);
                            m_pSliderFrameRate->setSingleStep(1);
                            m_pSliderFrameRate->setTickInterval(1);
                            m_pSliderFrameRate->setSnappingEnabled(true);
                            m_pSliderFrameRate->setOptimalHint(1, 25);
                            m_pSliderFrameRate->setWarningHint(25, 30);

                            pLayoutRecordingFrameRate->addWidget(m_pSliderFrameRate);
                        }
                        /* Prepare recording frame rate scale layout: */
                        QHBoxLayout *pLayoutRecordingFrameRateScale = new QHBoxLayout;
                        if (pLayoutRecordingFrameRateScale)
                        {
                            pLayoutRecordingFrameRateScale->setContentsMargins(0, 0, 0, 0);

                            /* Prepare recording frame rate min label: */
                            m_pLabelFrameRateMin = new QLabel(m_pWidgetFrameRateSettings);
                            if (m_pLabelFrameRateMin)
                                pLayoutRecordingFrameRateScale->addWidget(m_pLabelFrameRateMin);
                            pLayoutRecordingFrameRateScale->addStretch();
                            /* Prepare recording frame rate max label: */
                            m_pLabelFrameRateMax = new QLabel(m_pWidgetFrameRateSettings);
                            if (m_pLabelFrameRateMax)
                                pLayoutRecordingFrameRateScale->addWidget(m_pLabelFrameRateMax);

                            pLayoutRecordingFrameRate->addLayout(pLayoutRecordingFrameRateScale);
                        }
                    }

                    pLayoutSettings->addWidget(m_pWidgetFrameRateSettings, 3, 1, 2, 1);
                }
                /* Prepare recording frame rate spinbox: */
                m_pSpinboxFrameRate = new QSpinBox(pWidgetSettings);
                if (m_pSpinboxFrameRate)
                {
                    if (m_pLabelFrameRate)
                        m_pLabelFrameRate->setBuddy(m_pSpinboxFrameRate);
                    uiCommon().setMinimumWidthAccordingSymbolCount(m_pSpinboxFrameRate, 3);
                    m_pSpinboxFrameRate->setMinimum(1);
                    m_pSpinboxFrameRate->setMaximum(30);

                    pLayoutSettings->addWidget(m_pSpinboxFrameRate, 3, 2, 1, 2);
                }

                /* Prepare recording video quality label: */
                m_pLabelVideoQuality = new QLabel(pWidgetSettings);
                if (m_pLabelVideoQuality)
                {
                    m_pLabelVideoQuality->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    pLayoutSettings->addWidget(m_pLabelVideoQuality, 5, 0);
                }
                /* Prepare recording video quality widget: */
                m_pWidgetVideoQualitySettings = new QWidget(pWidgetSettings);
                if (m_pWidgetVideoQualitySettings)
                {
                    /* Prepare recording video quality layout: */
                    QVBoxLayout *pLayoutRecordingVideoQuality = new QVBoxLayout(m_pWidgetVideoQualitySettings);
                    if (pLayoutRecordingVideoQuality)
                    {
                        pLayoutRecordingVideoQuality->setContentsMargins(0, 0, 0, 0);

                        /* Prepare recording video quality slider: */
                        m_pSliderVideoQuality = new QIAdvancedSlider(m_pWidgetVideoQualitySettings);
                        if (m_pSliderVideoQuality)
                        {
                            m_pSliderVideoQuality->setOrientation(Qt::Horizontal);
                            m_pSliderVideoQuality->setMinimum(1);
                            m_pSliderVideoQuality->setMaximum(10);
                            m_pSliderVideoQuality->setPageStep(1);
                            m_pSliderVideoQuality->setSingleStep(1);
                            m_pSliderVideoQuality->setTickInterval(1);
                            m_pSliderVideoQuality->setSnappingEnabled(true);
                            m_pSliderVideoQuality->setOptimalHint(1, 5);
                            m_pSliderVideoQuality->setWarningHint(5, 9);
                            m_pSliderVideoQuality->setErrorHint(9, 10);

                            pLayoutRecordingVideoQuality->addWidget(m_pSliderVideoQuality);
                        }
                        /* Prepare recording video quality scale layout: */
                        QHBoxLayout *pLayoutRecordingVideoQialityScale = new QHBoxLayout;
                        if (pLayoutRecordingVideoQialityScale)
                        {
                            pLayoutRecordingVideoQialityScale->setContentsMargins(0, 0, 0, 0);

                            /* Prepare recording video quality min label: */
                            m_pLabelVideoQualityMin = new QLabel(m_pWidgetVideoQualitySettings);
                            if (m_pLabelVideoQualityMin)
                                pLayoutRecordingVideoQialityScale->addWidget(m_pLabelVideoQualityMin);
                            pLayoutRecordingVideoQialityScale->addStretch();
                            /* Prepare recording video quality med label: */
                            m_pLabelVideoQualityMed = new QLabel(m_pWidgetVideoQualitySettings);
                            if (m_pLabelVideoQualityMed)
                                pLayoutRecordingVideoQialityScale->addWidget(m_pLabelVideoQualityMed);
                            pLayoutRecordingVideoQialityScale->addStretch();
                            /* Prepare recording video quality max label: */
                            m_pLabelVideoQualityMax = new QLabel(m_pWidgetVideoQualitySettings);
                            if (m_pLabelVideoQualityMax)
                                pLayoutRecordingVideoQialityScale->addWidget(m_pLabelVideoQualityMax);

                            pLayoutRecordingVideoQuality->addLayout(pLayoutRecordingVideoQialityScale);
                        }
                    }

                    pLayoutSettings->addWidget(m_pWidgetVideoQualitySettings, 5, 1, 2, 1);
                }
                /* Prepare recording video quality spinbox: */
                m_pSpinboxVideoQuality = new QSpinBox(pWidgetSettings);
                if (m_pSpinboxVideoQuality)
                {
                    if (m_pLabelVideoQuality)
                        m_pLabelVideoQuality->setBuddy(m_pSpinboxVideoQuality);
                    uiCommon().setMinimumWidthAccordingSymbolCount(m_pSpinboxVideoQuality, 5);
                    m_pSpinboxVideoQuality->setMinimum(VIDEO_CAPTURE_BIT_RATE_MIN);
                    m_pSpinboxVideoQuality->setMaximum(VIDEO_CAPTURE_BIT_RATE_MAX);

                    pLayoutSettings->addWidget(m_pSpinboxVideoQuality, 5, 2, 1, 2);
                }

                /* Prepare recording audio quality label: */
                m_pLabelAudioQuality = new QLabel(pWidgetSettings);
                if (m_pLabelAudioQuality)
                {
                    m_pLabelAudioQuality->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    pLayoutSettings->addWidget(m_pLabelAudioQuality, 7, 0);
                }
                /* Prepare recording audio quality widget: */
                m_pWidgetAudioQualitySettings = new QWidget(pWidgetSettings);
                if (m_pWidgetAudioQualitySettings)
                {
                    /* Prepare recording audio quality layout: */
                    QVBoxLayout *pLayoutRecordingAudioQuality = new QVBoxLayout(m_pWidgetAudioQualitySettings);
                    if (pLayoutRecordingAudioQuality)
                    {
                        pLayoutRecordingAudioQuality->setContentsMargins(0, 0, 0, 0);

                        /* Prepare recording audio quality slider: */
                        m_pSliderAudioQuality = new QIAdvancedSlider(m_pWidgetAudioQualitySettings);
                        if (m_pSliderAudioQuality)
                        {
                            if (m_pLabelAudioQuality)
                                m_pLabelAudioQuality->setBuddy(m_pSliderAudioQuality);
                            m_pSliderAudioQuality->setOrientation(Qt::Horizontal);
                            m_pSliderAudioQuality->setMinimum(1);
                            m_pSliderAudioQuality->setMaximum(3);
                            m_pSliderAudioQuality->setPageStep(1);
                            m_pSliderAudioQuality->setSingleStep(1);
                            m_pSliderAudioQuality->setTickInterval(1);
                            m_pSliderAudioQuality->setSnappingEnabled(true);
                            m_pSliderAudioQuality->setOptimalHint(1, 2);
                            m_pSliderAudioQuality->setWarningHint(2, 3);

                            pLayoutRecordingAudioQuality->addWidget(m_pSliderAudioQuality);
                        }
                        /* Prepare recording audio quality scale layout: */
                        QHBoxLayout *pLayoutRecordingAudioQualityScale = new QHBoxLayout;
                        if (pLayoutRecordingAudioQualityScale)
                        {
                            pLayoutRecordingAudioQualityScale->setContentsMargins(0, 0, 0, 0);

                            /* Prepare recording audio quality min label: */
                            m_pLabelAudioQualityMin = new QLabel(m_pWidgetAudioQualitySettings);
                            if (m_pLabelAudioQualityMin)
                                pLayoutRecordingAudioQualityScale->addWidget(m_pLabelAudioQualityMin);
                            pLayoutRecordingAudioQualityScale->addStretch();
                            /* Prepare recording audio quality med label: */
                            m_pLabelAudioQualityMed = new QLabel(m_pWidgetAudioQualitySettings);
                            if (m_pLabelAudioQualityMed)
                                pLayoutRecordingAudioQualityScale->addWidget(m_pLabelAudioQualityMed);
                            pLayoutRecordingAudioQualityScale->addStretch();
                            /* Prepare recording audio quality max label: */
                            m_pLabelAudioQualityMax = new QLabel(m_pWidgetAudioQualitySettings);
                            if (m_pLabelAudioQualityMax)
                                pLayoutRecordingAudioQualityScale->addWidget(m_pLabelAudioQualityMax);

                            pLayoutRecordingAudioQuality->addLayout(pLayoutRecordingAudioQualityScale);
                        }
                    }

                    pLayoutSettings->addWidget(m_pWidgetAudioQualitySettings, 7, 1, 2, 1);
                }

                /* Prepare recording size hint label: */
                m_pLabelSizeHint = new QLabel(pWidgetSettings);
                if (m_pLabelSizeHint)
                    pLayoutSettings->addWidget(m_pLabelSizeHint, 9, 1);

                /* Prepare recording screens label: */
                m_pLabelScreens = new QLabel(pWidgetSettings);
                if (m_pLabelScreens)
                {
                    m_pLabelScreens->setAlignment(Qt::AlignRight | Qt::AlignTop);
                    pLayoutSettings->addWidget(m_pLabelScreens, 10, 0);
                }
                /* Prepare recording screens scroller: */
                m_pScrollerScreens = new UIFilmContainer(pWidgetSettings);
                if (m_pScrollerScreens)
                {
                    if (m_pLabelScreens)
                        m_pLabelScreens->setBuddy(m_pScrollerScreens);
                    pLayoutSettings->addWidget(m_pScrollerScreens, 10, 1, 1, 3);
                }
            }

            pLayout->addWidget(pWidgetSettings, 1, 1, 1, 2);
        }
    }

    /* Update widget availability: */
    updateWidgetAvailability();
}

void UIRecordingSettingsEditor::prepareConnections()
{
    connect(m_pCheckboxFeature, &QCheckBox::toggled,
            this, &UIRecordingSettingsEditor::sltHandleFeatureToggled);
    connect(m_pComboMode, &QComboBox::currentIndexChanged,
            this, &UIRecordingSettingsEditor::sltHandleModeComboChange);
    connect(m_pComboFrameSize, &QComboBox:: currentIndexChanged,
            this, &UIRecordingSettingsEditor::sltHandleVideoFrameSizeComboChange);
    connect(m_pSpinboxFrameWidth, &QSpinBox::valueChanged,
            this, &UIRecordingSettingsEditor::sltHandleVideoFrameWidthChange);
    connect(m_pSpinboxFrameHeight, &QSpinBox::valueChanged,
            this, &UIRecordingSettingsEditor::sltHandleVideoFrameHeightChange);
    connect(m_pSliderFrameRate, &QIAdvancedSlider::valueChanged,
            this, &UIRecordingSettingsEditor::sltHandleVideoFrameRateSliderChange);
    connect(m_pSpinboxFrameRate, &QSpinBox::valueChanged,
            this, &UIRecordingSettingsEditor::sltHandleVideoFrameRateSpinboxChange);
    connect(m_pSliderVideoQuality, &QIAdvancedSlider::valueChanged,
            this, &UIRecordingSettingsEditor::sltHandleVideoBitRateSliderChange);
    connect(m_pSpinboxVideoQuality, &QSpinBox::valueChanged,
            this, &UIRecordingSettingsEditor::sltHandleVideoBitRateSpinboxChange);
}

void UIRecordingSettingsEditor::populateComboMode()
{
    if (m_pComboMode)
    {
        /* Clear combo first of all: */
        m_pComboMode->clear();

        /* Load currently supported recording features: */
        const int iSupportedFlag = gpGlobalSession->supportedRecordingFeatures();
        m_supportedValues.clear();
        if (!iSupportedFlag)
            m_supportedValues << UISettingsDefs::RecordingMode_None;
        else
        {
            if (   (iSupportedFlag & KRecordingFeature_Video)
                && (iSupportedFlag & KRecordingFeature_Audio))
                m_supportedValues << UISettingsDefs::RecordingMode_VideoAudio;
            if (iSupportedFlag & KRecordingFeature_Video)
                m_supportedValues << UISettingsDefs::RecordingMode_VideoOnly;
            if (iSupportedFlag & KRecordingFeature_Audio)
                m_supportedValues << UISettingsDefs::RecordingMode_AudioOnly;
        }

        /* Make sure requested value if sane is present as well: */
        if (   m_enmMode != UISettingsDefs::RecordingMode_Max
            && !m_supportedValues.contains(m_enmMode))
            m_supportedValues.prepend(m_enmMode);

        /* Update combo with all the supported values: */
        foreach (const UISettingsDefs::RecordingMode &enmType, m_supportedValues)
            m_pComboMode->addItem(QString(), QVariant::fromValue(enmType));

        /* Look for proper index to choose: */
        const int iIndex = m_pComboMode->findData(QVariant::fromValue(m_enmMode));
        if (iIndex != -1)
            m_pComboMode->setCurrentIndex(iIndex);

        /* Retranslate finally: */
        sltRetranslateUI();
    }
}

void UIRecordingSettingsEditor::updateWidgetVisibility()
{
    /* Only the Audio stuff can be totally disabled, so we will add the code for hiding Audio stuff only: */
    const bool fAudioSettingsVisible =    m_supportedValues.isEmpty()
                                       || m_supportedValues.contains(UISettingsDefs::RecordingMode_AudioOnly);
    m_pWidgetAudioQualitySettings->setVisible(fAudioSettingsVisible);
    m_pLabelAudioQuality->setVisible(fAudioSettingsVisible);
}

void UIRecordingSettingsEditor::updateWidgetAvailability()
{
    const bool fFeatureEnabled = m_pCheckboxFeature->isChecked();
    const UISettingsDefs::RecordingMode enmRecordingMode =
        m_pComboMode->currentData().value<UISettingsDefs::RecordingMode>();
    const bool fRecordVideo =    enmRecordingMode == UISettingsDefs::RecordingMode_VideoOnly
                              || enmRecordingMode == UISettingsDefs::RecordingMode_VideoAudio;
    const bool fRecordAudio =    enmRecordingMode == UISettingsDefs::RecordingMode_AudioOnly
                              || enmRecordingMode == UISettingsDefs::RecordingMode_VideoAudio;

    m_pLabelMode->setEnabled(fFeatureEnabled && m_fOptionsAvailable);
    m_pComboMode->setEnabled(fFeatureEnabled && m_fOptionsAvailable);
    m_pLabelFilePath->setEnabled(fFeatureEnabled && m_fOptionsAvailable);
    m_pEditorFilePath->setEnabled(fFeatureEnabled && m_fOptionsAvailable);

    m_pLabelFrameSize->setEnabled(fFeatureEnabled && m_fOptionsAvailable && fRecordVideo);
    m_pComboFrameSize->setEnabled(fFeatureEnabled && m_fOptionsAvailable && fRecordVideo);
    m_pSpinboxFrameWidth->setEnabled(fFeatureEnabled && m_fOptionsAvailable && fRecordVideo);
    m_pSpinboxFrameHeight->setEnabled(fFeatureEnabled && m_fOptionsAvailable && fRecordVideo);

    m_pLabelFrameRate->setEnabled(fFeatureEnabled && m_fOptionsAvailable && fRecordVideo);
    m_pWidgetFrameRateSettings->setEnabled(fFeatureEnabled && m_fOptionsAvailable && fRecordVideo);
    m_pSpinboxFrameRate->setEnabled(fFeatureEnabled && m_fOptionsAvailable && fRecordVideo);

    m_pLabelVideoQuality->setEnabled(fFeatureEnabled && m_fOptionsAvailable && fRecordVideo);
    m_pWidgetVideoQualitySettings->setEnabled(fFeatureEnabled && m_fOptionsAvailable && fRecordVideo);
    m_pSpinboxVideoQuality->setEnabled(fFeatureEnabled && m_fOptionsAvailable && fRecordVideo);

    m_pLabelAudioQuality->setEnabled(fFeatureEnabled && m_fOptionsAvailable && fRecordAudio);
    m_pWidgetAudioQualitySettings->setEnabled(fFeatureEnabled && m_fOptionsAvailable && fRecordAudio);

    m_pLabelSizeHint->setEnabled(fFeatureEnabled && m_fOptionsAvailable && fRecordVideo);

    m_pLabelScreens->setEnabled(fFeatureEnabled && m_fScreenOptionsAvailable && fRecordVideo);
    m_pScrollerScreens->setEnabled(fFeatureEnabled && m_fScreenOptionsAvailable && fRecordVideo);
}

void UIRecordingSettingsEditor::updateRecordingFileSizeHint()
{
    m_pLabelSizeHint->setText(tr("<i>About %1MB per 5 minute video</i>")
                                 .arg(m_pSpinboxVideoQuality->value() * 300 / 8 / 1024));
}

void UIRecordingSettingsEditor::lookForCorrespondingFrameSizePreset()
{
    lookForCorrespondingPreset(m_pComboFrameSize,
                               QSize(m_pSpinboxFrameWidth->value(),
                                     m_pSpinboxFrameHeight->value()));
}

/* static */
void UIRecordingSettingsEditor::lookForCorrespondingPreset(QComboBox *pComboBox, const QVariant &data)
{
    /* Use passed iterator to look for corresponding preset of passed combo-box: */
    const int iLookupResult = pComboBox->findData(data);
    if (iLookupResult != -1 && pComboBox->currentIndex() != iLookupResult)
        pComboBox->setCurrentIndex(iLookupResult);
    else if (iLookupResult == -1 && pComboBox->currentIndex() != 0)
        pComboBox->setCurrentIndex(0);
}

/* static */
int UIRecordingSettingsEditor::calculateBitRate(int iFrameWidth, int iFrameHeight, int iFrameRate, int iQuality)
{
    /* Linear quality<=>bit-rate scale-factor: */
    const double dResult = (double)iQuality
                         * (double)iFrameWidth * (double)iFrameHeight * (double)iFrameRate
                         / (double)10 /* translate quality to [%] */
                         / (double)1024 /* translate bit-rate to [kbps] */
                         / (double)18.75 /* linear scale factor */;
    return (int)dResult;
}

/* static */
int UIRecordingSettingsEditor::calculateQuality(int iFrameWidth, int iFrameHeight, int iFrameRate, int iBitRate)
{
    /* Linear bit-rate<=>quality scale-factor: */
    const double dResult = (double)iBitRate
                         / (double)iFrameWidth / (double)iFrameHeight / (double)iFrameRate
                         * (double)10 /* translate quality to [%] */
                         * (double)1024 /* translate bit-rate to [kbps] */
                         * (double)18.75 /* linear scale factor */;
    return (int)dResult;
}
