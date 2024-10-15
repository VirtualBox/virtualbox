/* $Id: UIMedium.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMedium class declaration.
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

#ifndef FEQT_INCLUDED_SRC_medium_UIMedium_h
#define FEQT_INCLUDED_SRC_medium_UIMedium_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMap>
#include <QPixmap>

/* GUI includes: */
#include "UILibraryDefs.h"
#include "UIMediumDefs.h"

/* COM includes: */
#include "CMedium.h"

/** Storage medium cache used to
  * override some UIMedium attributes in the
  * user-friendly "don't show diffs" mode. */
struct NoDiffsCache
{
    /** Constructor. */
    NoDiffsCache() : isSet(false), state(KMediumState_NotCreated) {}

    /** Operator= reimplementation. */
    NoDiffsCache& operator=(const NoDiffsCache &other)
    {
        isSet = other.isSet;
        state = other.state;
        result = other.result;
        toolTip = other.toolTip;
        return *this;
    }

    /** Holds whether the cache is set. */
    bool isSet : 1;

    /** Holds overriden medium state. */
    KMediumState state;
    /** Holds overriden medium acquiring result. */
    COMResult result;
    /** Holds overriden medium tool-tip. */
    QString toolTip;
};

/** Storage medium descriptor wrapping CMedium wrapper for IMedium interface.
  *
  * Maintains the results of the last CMedium state (accessibility) check and precomposes
  * string parameters such as name, location and size which can be used for various GUI tasks.
  *
  * Many getter methods take the boolean @a fNoDiffs argument.
  * Unless explicitly stated otherwise, this argument, when set to @c true,
  * will cause the corresponding property of this object's root medium to be returned instead
  * of its own one. This is useful when hard drive medium is reflected in the user-friendly
  * "don't show diffs" mode. For non-hard drive media, the value of this argument is irrelevant
  * because the root object for such medium is the medium itself.
  *
  * Note that this class "abuses" the KMediumState_NotCreated state value to indicate that the
  * accessibility check of the given medium (see #blockAndQueryState()) has not been done yet
  * and therefore some parameters such as #size() are meaningless because they can be read only
  * from the accessible medium. The real KMediumState_NotCreated state is not necessary because
  * this class is only used with created (existing) media. */
class SHARED_LIBRARY_STUFF UIMedium
{
public:

    /** Default constructor.
      * Creates NULL UIMedium which is not associated with any CMedium. */
    UIMedium();

    /** Lazy wrapping constructor.
      * Creates the UIMedium associated with the given @a medium of the given @a type. */
    UIMedium(const CMedium &medium, UIMediumDeviceType type);

    /** Wrapping constructor with known medium state.
      * Similarly to the previous one it creates the UIMedium associated with the
      * given @a medium of the given @a type but sets the UIMedium @a state to passed one.
      * Suitable when the medium state is known such as right after the medium creation. */
    UIMedium(const CMedium &medium, UIMediumDeviceType type, KMediumState state);

    /** Copy constructor.
      * Creates the UIMedium on the basis of the passed @a other one. */
    UIMedium(const UIMedium &other);

    /** Operator= reimplementation. */
    UIMedium& operator=(const UIMedium &other);

    /** Queries the actual medium state.
      * @note This method blocks for the duration of the state check.
      *       Since this check may take quite a while,
      *       the calling thread must not be the UI thread. */
    void blockAndQueryState();

    /** Refreshes the precomposed user-readable strings.
      * @note Note that some string such as #size() are meaningless if the medium state is
      *       KMediumState_NotCreated (i.e. the medium has not yet been checked for accessibility). */
    void refresh();

    /** Returns the type of UIMedium object. */
    UIMediumDeviceType type() const { return m_type; }

    /** Returns the CMedium wrapped by this UIMedium object. */
    const CMedium& medium() const { return m_medium; }

    /** Returns @c true if CMedium wrapped by this UIMedium object has ID == #nullID().
      * @note   Also make sure wrapped CMedium is NULL object if his ID == #nullID(). */
    bool isNull() const
    {
        AssertReturn(m_uId != nullID() || m_medium.isNull(), true);
        return m_uId == nullID();
    }

    /** Returns the medium state.
      * @param fNoDiffs @c true to enable user-friendly "don't show diffs" mode.
      * @note  In "don't show diffs" mode, this method returns the worst state
      *        (in terms of inaccessibility) detected on the given hard drive chain. */
    KMediumState state(bool fNoDiffs = false) const;

    /** Returns the result of the last blockAndQueryState() call.
      * Indicates an error and contain a proper error info if the last state check fails.
      * @param fNoDiffs @c true to enable user-friendly "don't show diffs" mode.
      * @note  In "don't show diffs" mode, this method returns the worst result
      *        (in terms of inaccessibility) detected on the given hard drive chain. */
    const COMResult& result(bool fNoDiffs = false) const;

    /** Returns the error result of the last blockAndQueryState() call. */
    QString lastAccessError() const { return m_strLastAccessError; }

    /** Returns the medium ID. */
    QUuid id() const { return m_uId; }

    /** Returns the medium root ID. */
    QUuid rootID() const { return m_uRootId; }
    /** Returns the medium parent ID. */
    QUuid parentID() const { return m_uParentId; }

    /** Updates medium parent. */
    void updateParentID();

    /** Returns the medium cache key. */
    QUuid key() const { return m_uKey; }
    /** Defines the medium cache @a uKey. */
    void setKey(const QUuid &uKey) { m_uKey = uKey; }

    /** Returns the medium name.
      * @param fNoDiffs @c true to enable user-friendly "don't show diffs" mode.
      * @note  In "don't show diffs" mode, this method returns the name of root in the given hard drive chain. */
    QString name(bool fNoDiffs = false) const { return fNoDiffs ? root().m_strName : m_strName; }
    /** Returns the medium location.
      * @param fNoDiffs @c true to enable user-friendly "don't show diffs" mode.
      * @note  In "don't show diffs" mode, this method returns the location of root in the given hard drive chain. */
    QString location(bool fNoDiffs = false) const { return fNoDiffs ? root().m_strLocation : m_strLocation; }
    /** Returns the medium description.
      * @param fNoDiffs @c true to enable user-friendly "don't show diffs" mode.
      * @note  In "don't show diffs" mode, this method returns the description of root in the given hard drive chain. */
    QString description(bool fNoDiffs = false) const { return fNoDiffs ? root().m_strDescription : m_strDescription; }

    /** Returns the medium size in bytes.
      * @param fNoDiffs @c true to enable user-friendly "don't show diffs" mode.
      * @note  In "don't show diffs" mode, this method returns the size of root in the given hard drive chain. */
    qulonglong sizeInBytes(bool fNoDiffs = false) const { return fNoDiffs ? root().m_uSize : m_uSize; }
    /** Returns the logical medium size in bytes.
      * @param fNoDiffs @c true to enable user-friendly "don't show diffs" mode.
      * @note  In "don't show diffs" mode, this method returns the size of root in the given hard drive chain. */
    qulonglong logicalSizeInBytes(bool fNoDiffs = false) const { return fNoDiffs ? root().m_uLogicalSize : m_uLogicalSize; }
    /** Returns the medium size.
      * @param fNoDiffs @c true to enable user-friendly "don't show diffs" mode.
      * @note  In "don't show diffs" mode, this method returns the size of root in the given hard drive chain. */
    QString size(bool fNoDiffs = false) const { return fNoDiffs ? root().m_strSize : m_strSize; }
    /** Returns the logical medium size.
      * @param fNoDiffs @c true to enable user-friendly "don't show diffs" mode.
      * @note  In "don't show diffs" mode, this method returns the logical size of root in the given hard drive chain. */
    QString logicalSize(bool fNoDiffs = false) const { return fNoDiffs ? root().m_strLogicalSize : m_strLogicalSize; }

    /** Returns the medium disk type.
      * @param fNoDiffs @c true to enable user-friendly "don't show diffs" mode.
      * @note  In "don't show diffs" mode, this method returns the disk type of root in the given hard drive chain. */
    KMediumType mediumType(bool fNoDiffs = false) const { return fNoDiffs ? root().m_enmMediumType : m_enmMediumType; }
    /** Returns the medium disk variant.
      * @param fNoDiffs @c true to enable user-friendly "don't show diffs" mode.
      * @note  In "don't show diffs" mode, this method returns the disk variant of root in the given hard drive chain. */
    KMediumVariant mediumVariant(bool fNoDiffs = false) const { return fNoDiffs ? root().m_enmMediumVariant : m_enmMediumVariant; }

    /** Returns the hard drive medium disk type.
      * @param fNoDiffs @c true to enable user-friendly "don't show diffs" mode.
      * @note  In "don't show diffs" mode, this method returns the disk type of root in the given hard drive chain. */
    QString hardDiskType(bool fNoDiffs = false) const { return fNoDiffs ? root().m_strHardDiskType : m_strHardDiskType; }
    /** Returns the hard drive medium disk format.
      * @param fNoDiffs @c true to enable user-friendly "don't show diffs" mode.
      * @note  In "don't show diffs" mode, this method returns the disk format of root in the given hard drive chain. */
    QString hardDiskFormat(bool fNoDiffs = false) const { return fNoDiffs ? root().m_strHardDiskFormat : m_strHardDiskFormat; }

    /** Returns whether the hard drive medium disk has childred.
      * @param fNoDiffs @c true to enable user-friendly "don't show diffs" mode.
      * @note  In "don't show diffs" mode, this method returns the disk format of root in the given hard drive chain. */
    bool hasChildren(bool fNoDiffs = false) const { return fNoDiffs ? root().m_fHasChildren : m_fHasChildren; }

    /** Returns the hard drive medium storage details. */
    QString storageDetails() const { return m_strStorageDetails; }
    /** Returns the hard drive medium encryption password ID. */
    QString encryptionPasswordID() const { return m_strEncryptionPasswordID; }

    /** Returns the medium usage data.
      * @param fNoDiffs @c true to enable user-friendly "don't show diffs" mode.
      * @note  In "don't show diffs" mode, this method returns the usage data of root in the given hard drive chain. */
    QString usage(bool fNoDiffs = false) const { return fNoDiffs ? root().m_strUsage : m_strUsage; }

    /** Returns the short version of medium tool-tip. */
    QString tip() const { return m_strToolTip; }

    /** Returns the full version of medium tool-tip.
      * @param fNoDiffs     @c true to enable user-friendly "don't show diffs" mode.
      * @param fCheckRO     @c true to perform the #readOnly() check and add a notice accordingly.
      * @param fNullAllowed @c true to allow NULL medium description to be mentioned in the tool-tip.
      * @note  In "don't show diffs" mode (where the attributes of the base hard drive are shown instead
      *        of the attributes of the differencing hard drive), extra information will be added to the
      *        tooltip to give the user a hint that the medium is actually a differencing hard drive. */
    QString toolTip(bool fNoDiffs = false, bool fCheckRO = false, bool fNullAllowed = false) const;

    /** Shortcut to <tt>#toolTip(fNoDiffs, true, fNullAllowed)</tt>. */
    QString toolTipCheckRO(bool fNoDiffs = false, bool fNullAllowed = false) const { return toolTip(fNoDiffs, true, fNullAllowed); }

    /** Returns an icon corresponding to the medium state.
      * Distinguishes between the Inaccessible state and the situation when querying the state itself failed.
      * @param fNoDiffs @c true to enable user-friendly "don't show diffs" mode.
      * @param fCheckRO @c true to perform the #readOnly() check and change the icon accordingly.
      * @note  In "don't show diffs" mode (where the attributes of the base hard drive are shown instead
      *        of the attributes of the differencing hard drive), the most worst medium state on the given
      *        hard drive chain will be used to select the medium icon. */
    QPixmap icon(bool fNoDiffs = false, bool fCheckRO = false) const;

    /** Shortcut to <tt>#icon(fNoDiffs, true)</tt>. */
    QPixmap iconCheckRO(bool fNoDiffs = false) const { return icon(fNoDiffs, true); }

    /** Returns the details of this medium as a single-line string.
      * @param fNoDiffs     @c true to enable user-friendly "don't show diffs" mode.
      * @param fPredictDiff @c true to mark the hard drive as differencing if attaching
      *                             it would create a differencing hard drive.
      * @param fUseHTML     @c true to allow for emphasizing using bold and italics.
      * @note  For hard drives, the details include the location, type and the logical size of the hard drive.
      *        Note that if @a fNoDiffs is @c true, these properties are queried on the root hard drive of the
      *        given hard drive because the primary purpose of the returned string is to be human readable
      *        (so that seeing a complex diff hard drive name is usually not desirable).
      * @note  For other medium types, the location and the actual size are returned.
      *        Arguments @a fPredictDiff and @a fNoDiffs are ignored in this case.
      * @note  Use #detailsHTML() instead of passing @c true for @a fUseHTML.
      * @note  The medium object may become uninitialized by a third party while this method is reading its properties.
      *        In this case, the method will return an empty string. */
    QString details(bool fNoDiffs = false, bool fPredictDiff = false, bool fUseHTML = false) const;

    /** Shortcut to <tt>#details(fNoDiffs, fPredictDiff, true)</tt>. */
    QString detailsHTML(bool fNoDiffs = false, bool fPredictDiff = false) const { return details(fNoDiffs, fPredictDiff, true); }

    /** Returns the medium cache for "don't show diffs" mode. */
    const NoDiffsCache& cache() const { return m_noDiffs; }

    /** Returns whether this medium is hidden.
      * @note The medium is considered 'hidden' if it has corresponding
      *       medium property or is connected to 'hidden' VMs only. */
    bool isHidden() const { return m_fHidden || m_fUsedByHiddenMachinesOnly; }

    /** Returns whether this medium is read-only
      * (either because it is Immutable or because it has child hard drives).
      * @note Read-only medium can only be attached indirectly. */
    bool isReadOnly() const { return m_fReadOnly; }

    /** Returns whether this medium is attached to any VM in any snapshot. */
    bool isUsedInSnapshots() const { return m_fUsedInSnapshots; }

    /** Returns whether this medium corresponds to real host drive. */
    bool isHostDrive() const { return m_fHostDrive; }

    /** Returns whether this medium is encrypted. */
    bool isEncrypted() const { return m_fEncrypted; }

    /** Returns whether this medium is attached to any VM (in the current state or in a snapshot) in which case
      * #usage() will contain a string with comma-separated VM names (with snapshot names, if any, in parenthesis). */
    bool isUsed() const { return !m_strUsage.isNull(); }

    /** Returns whether this medium is attached to the given machine in the current state. */
    bool isAttachedInCurStateTo(const QUuid &uMachineId) const { return m_curStateMachineIds.indexOf(uMachineId) >= 0; }

    /** Returns a vector of IDs of all machines this medium is attached to. */
    const QList<QUuid>& machineIds() const { return m_machineIds; }
    /** Returns a vector of IDs of all machines this medium is attached to
      * in their current state (i.e. excluding snapshots). */
    const QList<QUuid>& curStateMachineIds() const { return m_curStateMachineIds; }

    /** Returns NULL medium ID. */
    static QUuid nullID();

    /** Returns passed @a uID if it's valid or #nullID() overwise. */
    static QUuid normalizedID(const QUuid &uID);

    /** Determines if passed @a medium is attached to hidden machines only. */
    static bool isMediumAttachedToHiddenMachinesOnly(const UIMedium &medium);

private:

    /** Returns medium root. */
    UIMedium root() const;
    /** Returns medium parent. */
    UIMedium parent() const;

    /** Checks if m_noDiffs is filled in and does it if not.
      * @param fNoDiffs @if false, this method immediately returns. */
    void checkNoDiffs(bool fNoDiffs);

    /** Returns string representation for passed @a comMedium type. */
    static QString mediumTypeToString(const CMedium &comMedium);

    /** Holds the type of UIMedium object. */
    UIMediumDeviceType m_type;

    /** Holds the CMedium wrapped by this UIMedium object. */
    CMedium m_medium;

    /** Holds the medium state. */
    KMediumState m_state;
    /** Holds the result of the last blockAndQueryState() call. */
    COMResult m_result;
    /** Holds the error result of the last blockAndQueryState() call. */
    QString m_strLastAccessError;

    /** Holds the medium ID. */
    QUuid m_uId;
    /** Holds the medium root ID. */
    QUuid m_uRootId;
    /** Holds the medium parent ID. */
    QUuid m_uParentId;

    /** Holds the medium cache key. */
    QUuid m_uKey;

    /** Holds the medium name. */
    QString m_strName;
    /** Holds the medium location. */
    QString m_strLocation;
    /** Holds the medium description. */
    QString m_strDescription;

    /** Holds the medium size in bytes. */
    qulonglong m_uSize;
    /** Holds the logical medium size in bytes. */
    qulonglong m_uLogicalSize;
    /** Holds the medium size. */
    QString m_strSize;
    /** Holds the logical medium size. */
    QString m_strLogicalSize;

    /** Holds the medium disk type. */
    KMediumType m_enmMediumType;
    /** Holds the medium disk variant. */
    KMediumVariant m_enmMediumVariant;

    /** Holds the hard drive medium disk type. */
    QString m_strHardDiskType;
    /** Holds the hard drive medium disk format. */
    QString m_strHardDiskFormat;
    /** Holds whether the hard drive medium disk has children. */
    bool m_fHasChildren;
    /** Holds the hard drive medium storage details. */
    QString m_strStorageDetails;
    /** Holds the hard drive medium encryption password ID. */
    QString m_strEncryptionPasswordID;

    /** Holds the medium usage. */
    QString m_strUsage;
    /** Holds the medium tool-tip. */
    QString m_strToolTip;
    /** Holds the vector of IDs of all machines this medium is attached to. */
    QList<QUuid> m_machineIds;
    /** Hodls the vector of IDs of all machines this medium is attached to
      * in their current state (i.e. excluding snapshots). */
    QList<QUuid> m_curStateMachineIds;

    /** Holds the medium cache for "don't show diffs" mode. */
    NoDiffsCache m_noDiffs;

    /** Holds whether this medium is 'hidden' by the corresponding medium property. */
    bool m_fHidden                   : 1;
    /** Holds whether this medium is 'hidden' because it's used by 'hidden' VMs only. */
    bool m_fUsedByHiddenMachinesOnly : 1;
    /** Holds whether this medium is read-only. */
    bool m_fReadOnly                 : 1;
    /** Holds whether this medium is attached to any VM in any snapshot. */
    bool m_fUsedInSnapshots          : 1;
    /** Holds whether this medium corresponds to real host drive. */
    bool m_fHostDrive                : 1;
    /** Holds whether this medium is encrypted. */
    bool m_fEncrypted                : 1;

    /** Holds the NULL medium ID. */
    static QUuid m_uNullID;
    /** Holds the medium tool-tip table template. */
    static QString m_sstrTable;
    /** Holds the medium tool-tip table row template. */
    static QString m_sstrRow;
};
Q_DECLARE_METATYPE(UIMedium);

typedef QMap<QUuid, UIMedium> UIMediumMap;

#endif /* !FEQT_INCLUDED_SRC_medium_UIMedium_h */
