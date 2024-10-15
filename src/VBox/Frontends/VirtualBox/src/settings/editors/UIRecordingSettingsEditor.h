/* $Id: UIRecordingSettingsEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIRecordingSettingsEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIRecordingSettingsEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIRecordingSettingsEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIEditor.h"
#include "UISettingsDefs.h"

/* Forward declarations: */
class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;
class QWidget;
class QIAdvancedSlider;
class UIFilePathSelector;
class UIFilmContainer;

/** UIEditor sub-class used as a recording settings editor. */
class SHARED_LIBRARY_STUFF UIRecordingSettingsEditor : public UIEditor
{
    Q_OBJECT;

public:

    /** Constructs editor passing @a pParent to the base-class. */
    UIRecordingSettingsEditor(QWidget *pParent = 0);

    /** Defines whether feature is @a fEnabled. */
    void setFeatureEnabled(bool fEnabled);
    /** Returns whether feature is enabled. */
    bool isFeatureEnabled() const;

    /** Defines whether options are @a fAvailable. */
    void setOptionsAvailable(bool fAvailable);
    /** Defines whether screen options are @a fAvailable. */
    void setScreenOptionsAvailable(bool fAvailable);

    /** Defines @a enmMode. */
    void setMode(UISettingsDefs::RecordingMode enmMode);
    /** Return mode. */
    UISettingsDefs::RecordingMode mode() const;

    /** Defines @a strFolder. */
    void setFolder(const QString &strFolder);
    /** Returns folder. */
    QString folder() const;
    /** Defines @a strFilePath. */
    void setFilePath(const QString &strFilePath);
    /** Returns file path. */
    QString filePath() const;

    /** Defines frame @a iWidth. */
    void setFrameWidth(int iWidth);
    /** Returns frame width. */
    int frameWidth() const;
    /** Defines frame @a iHeight. */
    void setFrameHeight(int iHeight);
    /** Returns frame height. */
    int frameHeight() const;

    /** Defines frame @a iRate. */
    void setFrameRate(int iRate);
    /** Returns frame rate. */
    int frameRate() const;

    /** Defines bit @a iRate. */
    void setBitRate(int iRate);
    /** Returns bit rate. */
    int bitRate() const;

    /** Defines audio quality @a iRate. */
    void setAudioQualityRate(int iRate);
    /** Returns audio quality rate. */
    int audioQualityRate() const;

    /** Defines enabled @a screens. */
    void setScreens(const QVector<bool> &screens);
    /** Returns enabled screens. */
    QVector<bool> screens() const;


private slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;
    /** Handles feature toggling. */
    void sltHandleFeatureToggled();
    /** Handles mode change. */
    void sltHandleModeComboChange();
    /** Handles frame size change. */
    void sltHandleVideoFrameSizeComboChange();
    /** Handles frame width change. */
    void sltHandleVideoFrameWidthChange();
    /** Handles frame height change. */
    void sltHandleVideoFrameHeightChange();
    /** Handles frame rate slider change. */
    void sltHandleVideoFrameRateSliderChange();
    /** Handles frame rate spinbox change. */
    void sltHandleVideoFrameRateSpinboxChange();
    /** Handles bit-rate slider change. */
    void sltHandleVideoBitRateSliderChange();
    /** Handles bit-rate spinbox change. */
    void sltHandleVideoBitRateSpinboxChange();

private:

    /** Prepares all. */
    void prepare();
    /** Prepares widgets. */
    void prepareWidgets();
    /** Prepares connections. */
    void prepareConnections();

    /** Populates mode combo-box. */
    void populateComboMode();

    /** Updates widget visibility. */
    void updateWidgetVisibility();
    /** Updates widget availability. */
    void updateWidgetAvailability();
    /** Updates recording file size hint. */
    void updateRecordingFileSizeHint();
    /** Searches for corresponding frame size preset. */
    void lookForCorrespondingFrameSizePreset();

    /** Searches for the @a data field in corresponding @a pComboBox. */
    static void lookForCorrespondingPreset(QComboBox *pComboBox, const QVariant &data);
    /** Calculates recording video bit-rate for passed @a iFrameWidth, @a iFrameHeight, @a iFrameRate and @a iQuality. */
    static int calculateBitRate(int iFrameWidth, int iFrameHeight, int iFrameRate, int iQuality);
    /** Calculates recording video quality for passed @a iFrameWidth, @a iFrameHeight, @a iFrameRate and @a iBitRate. */
    static int calculateQuality(int iFrameWidth, int iFrameHeight, int iFrameRate, int iBitRate);

    /** @name Values
     * @{ */
        /** Holds whether feature is enabled. */
        bool  m_fFeatureEnabled;

        /** Holds whether options are available. */
        bool  m_fOptionsAvailable;
        /** Holds whether screen options are available. */
        bool  m_fScreenOptionsAvailable;

        /** Holds the list of supported modes. */
        QVector<UISettingsDefs::RecordingMode>  m_supportedValues;
        /** Holds the mode. */
        UISettingsDefs::RecordingMode           m_enmMode;

        /** Holds the folder. */
        QString  m_strFolder;
        /** Holds the file path. */
        QString  m_strFilePath;

        /** Holds the frame width. */
        int  m_iFrameWidth;
        /** Holds the frame height. */
        int  m_iFrameHeight;
        /** Holds the frame rate. */
        int  m_iFrameRate;
        /** Holds the bit rate. */
        int  m_iBitRate;
        /** Holds the audio quality rate. */
        int  m_iAudioQualityRate;

        /** Holds the screens. */
        QVector<bool>  m_screens;
    /** @} */

    /** @name Widgets
     * @{ */
        /** Holds the feature check-box instance. */
        QCheckBox          *m_pCheckboxFeature;
        /** Holds the mode label instance. */
        QLabel             *m_pLabelMode;
        /** Holds the mode combo instance. */
        QComboBox          *m_pComboMode;
        /** Holds the file path label instance. */
        QLabel             *m_pLabelFilePath;
        /** Holds the file path editor instance. */
        UIFilePathSelector *m_pEditorFilePath;
        /** Holds the frame size label instance. */
        QLabel             *m_pLabelFrameSize;
        /** Holds the frame size combo instance. */
        QComboBox          *m_pComboFrameSize;
        /** Holds the frame width spinbox instance. */
        QSpinBox           *m_pSpinboxFrameWidth;
        /** Holds the frame height spinbox instance. */
        QSpinBox           *m_pSpinboxFrameHeight;
        /** Holds the frame rate label instance. */
        QLabel             *m_pLabelFrameRate;
        /** Holds the frame rate settings widget instance. */
        QWidget            *m_pWidgetFrameRateSettings;
        /** Holds the frame rate slider instance. */
        QIAdvancedSlider   *m_pSliderFrameRate;
        /** Holds the frame rate spinbox instance. */
        QSpinBox           *m_pSpinboxFrameRate;
        /** Holds the frame rate min label instance. */
        QLabel             *m_pLabelFrameRateMin;
        /** Holds the frame rate max label instance. */
        QLabel             *m_pLabelFrameRateMax;
        /** Holds the video quality label instance. */
        QLabel             *m_pLabelVideoQuality;
        /** Holds the video quality settings widget instance. */
        QWidget            *m_pWidgetVideoQualitySettings;
        /** Holds the video quality slider instance. */
        QIAdvancedSlider   *m_pSliderVideoQuality;
        /** Holds the video quality spinbox instance. */
        QSpinBox           *m_pSpinboxVideoQuality;
        /** Holds the video quality min label instance. */
        QLabel             *m_pLabelVideoQualityMin;
        /** Holds the video quality med label instance. */
        QLabel             *m_pLabelVideoQualityMed;
        /** Holds the video quality max label instance. */
        QLabel             *m_pLabelVideoQualityMax;
        /** Holds the audio quality label instance. */
        QLabel             *m_pLabelAudioQuality;
        /** Holds the audio quality settings widget instance. */
        QWidget            *m_pWidgetAudioQualitySettings;
        /** Holds the audio quality slider instance. */
        QIAdvancedSlider   *m_pSliderAudioQuality;
        /** Holds the audio quality min label instance. */
        QLabel             *m_pLabelAudioQualityMin;
        /** Holds the audio quality med label instance. */
        QLabel             *m_pLabelAudioQualityMed;
        /** Holds the audio quality max label instance. */
        QLabel             *m_pLabelAudioQualityMax;
        /** Holds the size hint label instance. */
        QLabel             *m_pLabelSizeHint;
        /** Holds the screens label instance. */
        QLabel             *m_pLabelScreens;
        /** Holds the screens scroller instance. */
        UIFilmContainer    *m_pScrollerScreens;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIRecordingSettingsEditor_h */
