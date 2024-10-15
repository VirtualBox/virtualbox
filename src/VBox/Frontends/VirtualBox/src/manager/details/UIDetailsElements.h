/* $Id: UIDetailsElements.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsElement[Name] classes declaration.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_manager_details_UIDetailsElements_h
#define FEQT_INCLUDED_SRC_manager_details_UIDetailsElements_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMutex>

/* GUI includes: */
#include "UIDetailsElement.h"
#include "UILibraryDefs.h"
#include "UITask.h"

/* COM includes: */
#include "CCloudMachine.h"
#include "CMachine.h"

/* Forward declarations: */
class UIMachinePreview;
class CNetworkAdapter;


/** UITask extension used as update task for the details-element. */
class UIDetailsUpdateTask : public UITask
{
    Q_OBJECT;

public:

    /** Constructs update task taking @a comMachine as data. */
    UIDetailsUpdateTask(const CMachine &comMachine);
    /** Constructs update task taking @a comCloudMachine as data. */
    UIDetailsUpdateTask(const CCloudMachine &comCloudMachine);

    /** Returns the machine. */
    CMachine machine() const;
    /** Returns the cloud machine. */
    CCloudMachine cloudMachine() const;

    /** Returns the table. */
    UITextTable table() const;
    /** Defines the @a guiTable. */
    void setTable(const UITextTable &guiTable);

private:

    /** Holds the mutex to access m_comMachine and m_comCloudMachine members. */
    mutable QMutex  m_machineMutex;
    /** Holds the machine being processed. */
    CMachine        m_comMachine;
    /** Holds the cloud machine being processed. */
    CCloudMachine   m_comCloudMachine;

    /** Holds the mutex to access m_guiTable member. */
    mutable QMutex  m_tableMutex;
    /** Holds the machine being filled. */
    UITextTable     m_guiTable;
};

/** UIDetailsElement extension used as a wrapping interface to
  * extend base-class with async functionality performed by the COM worker-threads. */
class UIDetailsElementInterface : public UIDetailsElement
{
    Q_OBJECT;

public:

    /** Constructs details-element interface for passed @a pParent set.
      * @param type    brings the details-element type this element belongs to.
      * @param fOpened brings whether the details-element should be visually opened. */
    UIDetailsElementInterface(UIDetailsSet *pParent, DetailsElementType type, bool fOpened);

protected:

    /** Updates appearance. */
    virtual void updateAppearance() RT_OVERRIDE;

    /** Creates update task. */
    virtual UITask *createUpdateTask() = 0;

private slots:

    /** Handles the signal about update @a pTask is finished. */
    virtual void sltUpdateAppearanceFinished(UITask *pTask);

    /** Performs translation. */
    void sltRetranslateUI();

private:

    /** Holds the instance of the update task. */
    UITask *m_pTask;
};


/** UIDetailsElementInterface extension for the details-element type 'Preview'. */
class UIDetailsElementPreview : public UIDetailsElement
{
    Q_OBJECT;

public:

    /** Constructs details-element interface for passed @a pParent set.
      * @param fOpened brings whether the details-element should be opened. */
    UIDetailsElementPreview(UIDetailsSet *pParent, bool fOpened);

    /** Updates layout. */
    virtual void updateLayout() RT_OVERRIDE;

private slots:

    /** Handles preview size-hint changes. */
    void sltPreviewSizeHintChanged();

    /** Performs translation. */
    void sltRetranslateUI();

private:

    /** Returns minimum width hint. */
    int minimumWidthHint() const RT_OVERRIDE;
    /** Returns minimum height hint.
      * @param fClosed allows to specify whether the hint should
      *                be calculated for the closed element. */
    int minimumHeightHintForElement(bool fClosed) const RT_OVERRIDE;

    /** Updates appearance. */
    void updateAppearance() RT_OVERRIDE;

    /** Holds the instance of VM preview. */
    UIMachinePreview *m_pPreview;
};


/** UITask extension used as update task for the details-element type 'General'. */
class UIDetailsUpdateTaskGeneral : public UIDetailsUpdateTask
{
    Q_OBJECT;

public:

    /** Constructs update task passing @a comMachine to the base-class. */
    UIDetailsUpdateTaskGeneral(const CMachine &comMachine, UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral fOptions)
        : UIDetailsUpdateTask(comMachine), m_fOptions(fOptions) {}

private:

    /** Contains update task body. */
    void run() RT_OVERRIDE RT_FINAL;

    /** Holds the options. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral m_fOptions;
};

/** UITask extension used as update task for the details-element type 'General' of cloud VM. */
class UIDetailsUpdateTaskGeneralCloud : public UIDetailsUpdateTask
{
    Q_OBJECT;

public:

    /** Constructs update task passing @a comCloudMachine to the base-class. */
    UIDetailsUpdateTaskGeneralCloud(const CCloudMachine &comCloudMachine, UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral fOptions)
        : UIDetailsUpdateTask(comCloudMachine), m_fOptions(fOptions) {}

private:

    /** Contains update task body. */
    void run() RT_OVERRIDE RT_FINAL;

    /** Holds the options. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral m_fOptions;
};

/** UIDetailsElementInterface extension for the details-element type 'General'. */
class UIDetailsElementGeneral : public UIDetailsElementInterface
{
    Q_OBJECT;

public:

    /** Constructs details-element object for passed @a pParent set.
      * @param fOpened brings whether the details-element should be visually opened. */
    UIDetailsElementGeneral(UIDetailsSet *pParent, bool fOpened)
        : UIDetailsElementInterface(pParent, DetailsElementType_General, fOpened) {}

private:

    /** Creates update task for this element. */
    virtual UITask *createUpdateTask() RT_OVERRIDE;
};


/** UITask extension used as update task for the details-element type 'System'. */
class UIDetailsUpdateTaskSystem : public UIDetailsUpdateTask
{
    Q_OBJECT;

public:

    /** Constructs update task passing @a comMachine to the base-class. */
    UIDetailsUpdateTaskSystem(const CMachine &comMachine, UIExtraDataMetaDefs::DetailsElementOptionTypeSystem fOptions)
        : UIDetailsUpdateTask(comMachine), m_fOptions(fOptions) {}

private:

    /** Contains update task body. */
    void run() RT_OVERRIDE RT_FINAL;

    /** Holds the options. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeSystem m_fOptions;
};

/** UIDetailsElementInterface extension for the details-element type 'System'. */
class UIDetailsElementSystem : public UIDetailsElementInterface
{
    Q_OBJECT;

public:

    /** Constructs details-element object for passed @a pParent set.
      * @param fOpened brings whether the details-element should be visually opened. */
    UIDetailsElementSystem(UIDetailsSet *pParent, bool fOpened)
        : UIDetailsElementInterface(pParent, DetailsElementType_System, fOpened) {}

private:

    /** Creates update task for this element. */
    virtual UITask *createUpdateTask() RT_OVERRIDE;
};


/** UITask extension used as update task for the details-element type 'Display'. */
class UIDetailsUpdateTaskDisplay : public UIDetailsUpdateTask
{
    Q_OBJECT;

public:

    /** Constructs update task passing @a comMachine to the base-class. */
    UIDetailsUpdateTaskDisplay(const CMachine &comMachine, UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay fOptions)
        : UIDetailsUpdateTask(comMachine), m_fOptions(fOptions) {}

private:

    /** Contains update task body. */
    void run() RT_OVERRIDE RT_FINAL;

    /** Holds the options. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay m_fOptions;
};

/** UIDetailsElementInterface extension for the details-element type 'Display'. */
class UIDetailsElementDisplay : public UIDetailsElementInterface
{
    Q_OBJECT;

public:

    /** Constructs details-element object for passed @a pParent set.
      * @param fOpened brings whether the details-element should be visually opened. */
    UIDetailsElementDisplay(UIDetailsSet *pParent, bool fOpened)
        : UIDetailsElementInterface(pParent, DetailsElementType_Display, fOpened) {}

private:

    /** Creates update task for this element. */
    virtual UITask *createUpdateTask() RT_OVERRIDE;
};


/** UITask extension used as update task for the details-element type 'Storage'. */
class UIDetailsUpdateTaskStorage : public UIDetailsUpdateTask
{
    Q_OBJECT;

public:

    /** Constructs update task passing @a comMachine to the base-class. */
    UIDetailsUpdateTaskStorage(const CMachine &comMachine, UIExtraDataMetaDefs::DetailsElementOptionTypeStorage fOptions)
        : UIDetailsUpdateTask(comMachine), m_fOptions(fOptions) {}

private:

    /** Contains update task body. */
    void run() RT_OVERRIDE RT_FINAL;

    /** Holds the options. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeStorage m_fOptions;
};

/** UIDetailsElementInterface extension for the details-element type 'Storage'. */
class UIDetailsElementStorage : public UIDetailsElementInterface
{
    Q_OBJECT;

public:

    /** Constructs details-element object for passed @a pParent set.
      * @param fOpened brings whether the details-element should be visually opened. */
    UIDetailsElementStorage(UIDetailsSet *pParent, bool fOpened)
        : UIDetailsElementInterface(pParent, DetailsElementType_Storage, fOpened) {}

private:

    /** Creates update task for this element. */
    virtual UITask *createUpdateTask() RT_OVERRIDE;
};


/** UITask extension used as update task for the details-element type 'Audio'. */
class UIDetailsUpdateTaskAudio : public UIDetailsUpdateTask
{
    Q_OBJECT;

public:

    /** Constructs update task passing @a comMachine to the base-class. */
    UIDetailsUpdateTaskAudio(const CMachine &comMachine, UIExtraDataMetaDefs::DetailsElementOptionTypeAudio fOptions)
        : UIDetailsUpdateTask(comMachine), m_fOptions(fOptions) {}

private:

    /** Contains update task body. */
    void run() RT_OVERRIDE RT_FINAL;

    /** Holds the options. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeAudio m_fOptions;
};

/** UIDetailsElementInterface extension for the details-element type 'Audio'. */
class UIDetailsElementAudio : public UIDetailsElementInterface
{
    Q_OBJECT;

public:

    /** Constructs details-element object for passed @a pParent set.
      * @param fOpened brings whether the details-element should be visually opened. */
    UIDetailsElementAudio(UIDetailsSet *pParent, bool fOpened)
        : UIDetailsElementInterface(pParent, DetailsElementType_Audio, fOpened) {}

private:

    /** Creates update task for this element. */
    virtual UITask *createUpdateTask() RT_OVERRIDE;
};


/** UITask extension used as update task for the details-element type 'Network'. */
class UIDetailsUpdateTaskNetwork : public UIDetailsUpdateTask
{
    Q_OBJECT;

public:

    /** Constructs update task passing @a comMachine to the base-class. */
    UIDetailsUpdateTaskNetwork(const CMachine &comMachine, UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork fOptions)
        : UIDetailsUpdateTask(comMachine), m_fOptions(fOptions) {}

private:

    /** Contains update task body. */
    void run() RT_OVERRIDE RT_FINAL;

    /** Holds the options. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork m_fOptions;
};

/** UIDetailsElementInterface extension for the details-element type 'Network'. */
class UIDetailsElementNetwork : public UIDetailsElementInterface
{
    Q_OBJECT;

public:

    /** Constructs details-element object for passed @a pParent set.
      * @param fOpened brings whether the details-element should be visually opened. */
    UIDetailsElementNetwork(UIDetailsSet *pParent, bool fOpened)
        : UIDetailsElementInterface(pParent, DetailsElementType_Network, fOpened) {}

private:

    /** Creates update task for this element. */
    virtual UITask *createUpdateTask() RT_OVERRIDE;
};


/** UITask extension used as update task for the details-element type 'Serial'. */
class UIDetailsUpdateTaskSerial : public UIDetailsUpdateTask
{
    Q_OBJECT;

public:

    /** Constructs update task passing @a comMachine to the base-class. */
    UIDetailsUpdateTaskSerial(const CMachine &comMachine, UIExtraDataMetaDefs::DetailsElementOptionTypeSerial fOptions)
        : UIDetailsUpdateTask(comMachine), m_fOptions(fOptions) {}

private:

    /** Contains update task body. */
    void run() RT_OVERRIDE RT_FINAL;

    /** Holds the options. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeSerial m_fOptions;
};

/** UIDetailsElementInterface extension for the details-element type 'Serial'. */
class UIDetailsElementSerial : public UIDetailsElementInterface
{
    Q_OBJECT;

public:

    /** Constructs details-element object for passed @a pParent set.
      * @param fOpened brings whether the details-element should be visually opened. */
    UIDetailsElementSerial(UIDetailsSet *pParent, bool fOpened)
        : UIDetailsElementInterface(pParent, DetailsElementType_Serial, fOpened) {}

private:

    /** Creates update task for this element. */
    virtual UITask *createUpdateTask() RT_OVERRIDE;
};


/** UITask extension used as update task for the details-element type 'USB'. */
class UIDetailsUpdateTaskUSB : public UIDetailsUpdateTask
{
    Q_OBJECT;

public:

    /** Constructs update task passing @a comMachine to the base-class. */
    UIDetailsUpdateTaskUSB(const CMachine &comMachine, UIExtraDataMetaDefs::DetailsElementOptionTypeUsb fOptions)
        : UIDetailsUpdateTask(comMachine), m_fOptions(fOptions) {}

private:

    /** Contains update task body. */
    void run() RT_OVERRIDE RT_FINAL;

    /** Holds the options. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeUsb m_fOptions;
};

/** UIDetailsElementInterface extension for the details-element type 'USB'. */
class UIDetailsElementUSB : public UIDetailsElementInterface
{
    Q_OBJECT;

public:

    /** Constructs details-element object for passed @a pParent set.
      * @param fOpened brings whether the details-element should be visually opened. */
    UIDetailsElementUSB(UIDetailsSet *pParent, bool fOpened)
        : UIDetailsElementInterface(pParent, DetailsElementType_USB, fOpened) {}

private:

    /** Creates update task for this element. */
    virtual UITask *createUpdateTask() RT_OVERRIDE;
};


/** UITask extension used as update task for the details-element type 'SF'. */
class UIDetailsUpdateTaskSF : public UIDetailsUpdateTask
{
    Q_OBJECT;

public:

    /** Constructs update task passing @a comMachine to the base-class. */
    UIDetailsUpdateTaskSF(const CMachine &comMachine, UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders fOptions)
        : UIDetailsUpdateTask(comMachine), m_fOptions(fOptions) {}

private:

    /** Contains update task body. */
    void run() RT_OVERRIDE RT_FINAL;

    /** Holds the options. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders m_fOptions;
};

/** UIDetailsElementInterface extension for the details-element type 'SF'. */
class UIDetailsElementSF : public UIDetailsElementInterface
{
    Q_OBJECT;

public:

    /** Constructs details-element object for passed @a pParent set.
      * @param fOpened brings whether the details-element should be visually opened. */
    UIDetailsElementSF(UIDetailsSet *pParent, bool fOpened)
        : UIDetailsElementInterface(pParent, DetailsElementType_SF, fOpened) {}

private:

    /** Creates update task for this element. */
    virtual UITask *createUpdateTask() RT_OVERRIDE;
};


/** UITask extension used as update task for the details-element type 'UI'. */
class UIDetailsUpdateTaskUI : public UIDetailsUpdateTask
{
    Q_OBJECT;

public:

    /** Constructs update task passing @a comMachine to the base-class. */
    UIDetailsUpdateTaskUI(const CMachine &comMachine, UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface fOptions)
        : UIDetailsUpdateTask(comMachine), m_fOptions(fOptions) {}

private:

    /** Contains update task body. */
    void run() RT_OVERRIDE RT_FINAL;

    /** Holds the options. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface m_fOptions;
};

/** UIDetailsElementInterface extension for the details-element type 'UI'. */
class UIDetailsElementUI : public UIDetailsElementInterface
{
    Q_OBJECT;

public:

    /** Constructs details-element object for passed @a pParent set.
      * @param fOpened brings whether the details-element should be visually opened. */
    UIDetailsElementUI(UIDetailsSet *pParent, bool fOpened)
        : UIDetailsElementInterface(pParent, DetailsElementType_UI, fOpened) {}

private:

    /** Creates update task for this element. */
    virtual UITask *createUpdateTask() RT_OVERRIDE;
};


/** UITask extension used as update task for the details-element type 'Description'. */
class UIDetailsUpdateTaskDescription : public UIDetailsUpdateTask
{
    Q_OBJECT;

public:

    /** Constructs update task passing @a comMachine to the base-class. */
    UIDetailsUpdateTaskDescription(const CMachine &comMachine, UIExtraDataMetaDefs::DetailsElementOptionTypeDescription fOptions)
        : UIDetailsUpdateTask(comMachine), m_fOptions(fOptions) {}

private:

    /** Contains update task body. */
    void run() RT_OVERRIDE RT_FINAL;

    /** Holds the options. */
    UIExtraDataMetaDefs::DetailsElementOptionTypeDescription m_fOptions;
};

/** UIDetailsElementInterface extension for the details-element type 'Description'. */
class UIDetailsElementDescription : public UIDetailsElementInterface
{
    Q_OBJECT;

public:

    /** Constructs details-element object for passed @a pParent set.
      * @param fOpened brings whether the details-element should be visually opened. */
    UIDetailsElementDescription(UIDetailsSet *pParent, bool fOpened)
        : UIDetailsElementInterface(pParent, DetailsElementType_Description, fOpened) {}

private:

    /** Creates update task for this element. */
    virtual UITask *createUpdateTask() RT_OVERRIDE;
};

#endif /* !FEQT_INCLUDED_SRC_manager_details_UIDetailsElements_h */
