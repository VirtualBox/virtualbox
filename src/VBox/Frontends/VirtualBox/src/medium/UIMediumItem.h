/* $Id: UIMediumItem.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMediumItem class declaration.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_medium_UIMediumItem_h
#define FEQT_INCLUDED_SRC_medium_UIMediumItem_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "QITreeWidget.h"
#include "UIMedium.h"
#include "UIMediumDetailsWidget.h"

/** QITreeWidgetItem extension representing Media Manager item. */
class SHARED_LIBRARY_STUFF UIMediumItem : public QITreeWidgetItem, public UIDataMedium
{
    Q_OBJECT;

public:

    /** Constructs top-level item.
      * @param  guiMedium  Brings the medium to wrap around.
      * @param  pParent    Brings the parent tree reference. */
    UIMediumItem(const UIMedium &guiMedium, QITreeWidget *pParent);
    /** Constructs sub-level item.
      * @param  guiMedium  Brings the medium to wrap around.
      * @param  pParent    Brings the parent item reference. */
    UIMediumItem(const UIMedium &guiMedium, UIMediumItem *pParent);
    /** Constructs sub-level item under a QITreeWidgetItem.
      * @param  guiMedium  Brings the medium to wrap around.
      * @param  pParent    Brings the parent item reference. */
    UIMediumItem(const UIMedium &guiMedium, QITreeWidgetItem *pParent);

    /** Moves UIMedium wrapped by <i>this</i> item. */
    virtual bool move();
    /** Removes UIMedium wrapped by <i>this</i> item. */
    virtual bool remove(bool fShowMessageBox) = 0;
    /** Releases UIMedium wrapped by <i>this</i> item.
      * @param  fInduced  Brings whether this action is caused by other user's action,
      *                   not a direct order to release particularly selected medium. */
    virtual bool release(bool fShowMessageBox, bool fInduced);

    /** Refreshes item fully. */
    void refreshAll();

    /** Returns UIMedium wrapped by <i>this</i> item. */
    const UIMedium &medium() const { return m_guiMedium; }
    /** Defines UIMedium wrapped by <i>this</i> item. */
    void setMedium(const UIMedium &guiMedium);

    /** Returns UIMediumDeviceType of the wrapped UIMedium. */
    UIMediumDeviceType mediumType() const { return m_guiMedium.type(); }

    /** Returns KMediumState of the wrapped UIMedium. */
    KMediumState state() const { return m_guiMedium.state(); }

    /** Returns QUuid <i>ID</i> of the wrapped UIMedium. */
    QUuid id() const { return m_guiMedium.id(); }
    /** Returns QString <i>name</i> of the wrapped UIMedium. */
    QString name() const { return m_guiMedium.name(); }

    /** Returns QString <i>location</i> of the wrapped UIMedium. */
    QString location() const { return m_guiMedium.location(); }

    /** Returns QString <i>hard-disk format</i> of the wrapped UIMedium. */
    QString hardDiskFormat() const { return m_guiMedium.hardDiskFormat(); }
    /** Returns QString <i>hard-disk type</i> of the wrapped UIMedium. */
    QString hardDiskType() const { return m_guiMedium.hardDiskType(); }

    /** Returns QString <i>storage details</i> of the wrapped UIMedium. */
    QString details() const { return m_guiMedium.storageDetails(); }
    /** Returns QString <i>encryption password ID</i> of the wrapped UIMedium. */
    QString encryptionPasswordID() const { return m_guiMedium.encryptionPasswordID(); }

    /** Returns QString <i>tool-tip</i> of the wrapped UIMedium. */
    QString toolTip() const { return m_guiMedium.toolTip(); }

    /** Returns a vector of IDs of all machines wrapped UIMedium is attached to. */
    const QList<QUuid> &machineIds() const { return m_guiMedium.machineIds(); }
    /** Returns QString <i>usage</i> of the wrapped UIMedium. */
    QString usage() const { return m_guiMedium.usage(); }
    /** Returns whether wrapped UIMedium is used. */
    bool isUsed() const { return m_guiMedium.isUsed(); }
    /** Returns whether wrapped UIMedium is used in snapshots. */
    bool isUsedInSnapshots() const { return m_guiMedium.isUsedInSnapshots(); }

    /** Returns whether <i>this</i> item is less than @a other one. */
    bool operator<(const QTreeWidgetItem &other) const RT_OVERRIDE;
    /** Returns whether the medium can be modified. For
      * simplicity's sake this returns false if one of the attached vms is not
      * in PoweredOff or Aborted state. */
    bool isMediumModifiable() const;
    /** Returns true if the medium is attached to the vm with @p uId. */
    bool isMediumAttachedTo(QUuid uId);
    bool changeMediumType(KMediumType enmNewType);

protected:

    /** Release UIMedium wrapped by <i>this</i> item from virtual @a comMachine. */
    virtual bool releaseFrom(CMachine comMachine) = 0;

    /** Returns default text. */
    virtual QString defaultText() const RT_OVERRIDE;

protected slots:

    /** Handles medium move progress result. */
    void sltHandleMoveProgressFinished();

    /** Handles @a comMedium remove request. */
    void sltHandleMediumRemoveRequest(CMedium comMedium);

private:

    /** A simple struct used to save some parameters of machine device attachment.
      * Used for re-attaching the medium to VMs after a medium type change. */
    struct AttachmentCache
    {
        /** Holds the machine ID. */
        QUuid        m_uMachineId;
        /** Holds the controller name. */
        QString      m_strControllerName;
        /** Holds the controller bus. */
        KStorageBus  m_enmControllerBus;
        /** Holds the attachment port. */
        LONG         m_iAttachmentPort;
        /** Holds the attachment device. */
        LONG         m_iAttachmentDevice;
    };

    /** Refreshes item information such as icon, text and tool-tip. */
    void refresh();

    /** Releases UIMedium wrapped by <i>this</i> item from virtual machine with @a uMachineId. */
    bool releaseFrom(const QUuid &uMachineId);
    /** Is called by detaching the medium and modifiying it to restore the attachement. */
    bool attachTo(const AttachmentCache &attachmentCache);

    /** Formats field text. */
    static QString formatFieldText(const QString &strText, bool fCompact = true, const QString &strElipsis = "middle");

    /** Holds the UIMedium wrapped by <i>this</i> item. */
    UIMedium m_guiMedium;
};


/** UIMediumItem extension representing hard-disk item. */
class SHARED_LIBRARY_STUFF UIMediumItemHD : public UIMediumItem
{
public:

    /** Constructs top-level item.
      * @param  guiMedium  Brings the medium to wrap around.
      * @param  pParent    Brings the parent tree reference. */
    UIMediumItemHD(const UIMedium &guiMedium, QITreeWidget *pParent);
    /** Constructs sub-level item.
      * @param  guiMedium  Brings the medium to wrap around.
      * @param  pParent    Brings the parent item reference. */
    UIMediumItemHD(const UIMedium &guiMedium, UIMediumItem *pParent);
    /** Constructs sub-level item under a QITreeWidgetItem.
      * @param  guiMedium  Brings the medium to wrap around.
      * @param  pParent    Brings the parent item reference. */
    UIMediumItemHD(const UIMedium &guiMedium, QITreeWidgetItem *pParent);

protected:

    /** Removes UIMedium wrapped by <i>this</i> item. */
    virtual bool remove(bool fShowMessageBox) RT_OVERRIDE;
    /** Releases UIMedium wrapped by <i>this</i> item from virtual @a comMachine. */
    virtual bool releaseFrom(CMachine comMachine) RT_OVERRIDE;

private:

    /** Proposes user to remove CMedium storage wrapped by <i>this</i> item. */
    bool maybeRemoveStorage();
};

/** UIMediumItem extension representing optical-disk item. */
class SHARED_LIBRARY_STUFF UIMediumItemCD : public UIMediumItem
{
public:

    /** Constructs top-level item.
      * @param  guiMedium  Brings the medium to wrap around.
      * @param  pParent    Brings the parent tree reference. */
    UIMediumItemCD(const UIMedium &guiMedium, QITreeWidget *pParent);
    /** Constructs sub-level item under a QITreeWidgetItem.
      * @param  guiMedium  Brings the medium to wrap around.
      * @param  pParent    Brings the parent item reference. */
    UIMediumItemCD(const UIMedium &guiMedium, QITreeWidgetItem *pParent);

protected:

    /** Removes UIMedium wrapped by <i>this</i> item. */
    virtual bool remove(bool fShowMessageBox) RT_OVERRIDE;
    /** Releases UIMedium wrapped by <i>this</i> item from virtual @a comMachine. */
    virtual bool releaseFrom(CMachine comMachine) RT_OVERRIDE;
};

/** UIMediumItem extension representing floppy-disk item. */
class SHARED_LIBRARY_STUFF UIMediumItemFD : public UIMediumItem
{
public:

    /** Constructs top-level item.
      * @param  guiMedium  Brings the medium to wrap around.
      * @param  pParent    Brings the parent tree reference. */
    UIMediumItemFD(const UIMedium &guiMedium, QITreeWidget *pParent);
    /** Constructs sub-level item under a QITreeWidgetItem.
      * @param  guiMedium  Brings the medium to wrap around.
      * @param  pParent    Brings the parent item reference. */
    UIMediumItemFD(const UIMedium &guiMedium, QITreeWidgetItem *pParent);

protected:

    /** Removes UIMedium wrapped by <i>this</i> item. */
    virtual bool remove(bool fShowMessageBox) RT_OVERRIDE;
    /** Releases UIMedium wrapped by <i>this</i> item from virtual @a comMachine. */
    virtual bool releaseFrom(CMachine comMachine) RT_OVERRIDE;
};

#endif /* !FEQT_INCLUDED_SRC_medium_UIMediumItem_h */
