/* $Id: UIGuestOSType.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGuestOSType class declaration.
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

#ifndef FEQT_INCLUDED_SRC_globals_UIGuestOSType_h
#define FEQT_INCLUDED_SRC_globals_UIGuestOSType_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMap>
#include <QString>
#include <QVector>

/* COM includes: */
#include "CGuestOSType.h"

/** Holds various Guest OS type helper stuff. */
namespace UIGuestOSTypeHelpers
{
#ifdef VBOX_WITH_3D_ACCELERATION
    /** Returns whether guest OS type with passed @a strGuestOSTypeId is WDDM compatible. */
    SHARED_LIBRARY_STUFF bool isWddmCompatibleOsType(const QString &strGuestOSTypeId);
#endif

    /** Returns the required video memory in bytes for the current desktop
      * resolution at maximum possible screen depth in bpp. */
    SHARED_LIBRARY_STUFF quint64 requiredVideoMemory(const QString &strGuestOSTypeId, int cMonitors = 1);
}

/** Represents guest OS family info. */
struct UIFamilyInfo
{
    /** Constructs empty family info. */
    UIFamilyInfo()
        : m_enmArch(KPlatformArchitecture_None)
        , m_fSupported(false)
    {}

    /** Constructs family info.
      * @param  strId           Brings the family ID.
      * @param  strDescription  Brings the family description.
      * @param  enmArch         Brings the family architecture.
      * @param  fSupported      Brings whether family is supported. */
    UIFamilyInfo(const QString &strId,
                 const QString &strDescription,
                 KPlatformArchitecture enmArch,
                 bool fSupported)
        : m_strId(strId)
        , m_strDescription(strDescription)
        , m_enmArch(enmArch)
        , m_fSupported(fSupported)
    {}

    /** Returns whether this family info has the same id as @a other. */
    bool operator==(const UIFamilyInfo &other) const
    {
        return m_strId == other.m_strId;
    }

    /** Holds family id. */
    QString                m_strId;
    /** Holds family description. */
    QString                m_strDescription;
    /** Holds family architecture. */
    KPlatformArchitecture  m_enmArch;
    /** Holds whether family is supported. */
    bool                   m_fSupported;
};

/** Represents guest OS subtype info. */
struct UISubtypeInfo
{
    /** Constructs empty subtype info. */
    UISubtypeInfo()
        : m_enmArch(KPlatformArchitecture_None)
        , m_fSupported(false)
    {}

    /** Constructs subtype info.
      * @param  strName     Brings the name.
      * @param  enmArch     Brings the architecture type.
      * @param  fSupported  Brings whether subtype is supported. */
    UISubtypeInfo(const QString &strName,
                  KPlatformArchitecture enmArch,
                  bool fSupported)
        : m_strName(strName)
        , m_enmArch(enmArch)
        , m_fSupported(fSupported)
    {}

    /** Returns whether this subtype info has the same name as @a other. */
    bool operator==(const UISubtypeInfo &other) const
    {
        return m_strName == other.m_strName;
    }

    /** Holds the name. */
    QString                m_strName;
    /** Holds the architecture. */
    KPlatformArchitecture  m_enmArch;
    /** Holds whether subtype is supported. */
    bool                   m_fSupported;
};

/** A wrapper around CGuestOSType. Some of the properties are cached here for performance. */
class SHARED_LIBRARY_STUFF UIGuestOSType
{

public:

    UIGuestOSType(const CGuestOSType &comGuestOSType, bool fSupported);
    UIGuestOSType();

    bool isSupported() const;

    const QString &getFamilyId() const;
    const QString &getFamilyDescription() const;
    const QString &getId() const;
    const QString &getSubtype() const;
    const QString &getDescription() const;

    /** @name Wrapper getters for CGuestOSType member.
      * @{ */
        KStorageBus             getRecommendedHDStorageBus() const;
        ULONG                   getRecommendedRAM() const;
        KStorageBus             getRecommendedDVDStorageBus() const;
        ULONG                   getRecommendedCPUCount() const;
        KFirmwareType           getRecommendedFirmware() const;
        bool                    getRecommendedFloppy() const;
        LONG64                  getRecommendedHDD() const;
        KGraphicsControllerType getRecommendedGraphicsController() const;
        KStorageControllerType  getRecommendedDVDStorageController() const;
        bool                    is64Bit() const;
        KPlatformArchitecture   getPlatformArchitecture() const;
    /** @} */

    bool isOk() const;

private:

    CGuestOSType m_comGuestOSType;

    bool m_fSupported;

    /** @name CGuestOSType properties. Cached here for a faster access.
      * @{ */
        mutable QString m_strFamilyId;
        mutable QString m_strFamilyDescription;
        mutable QString m_strId;
        mutable QString m_strSubtype;
        mutable QString m_strDescription;
    /** @} */
};

/** A wrapper and manager class for Guest OS types (IGuestOSType). Logically we structure os types into families
  *  e.g. Window, Linux etc. Some families have so-called subtypes which for Linux corresponds to distros, while some
  *  families have no subtype. Under subtypes (and when no subtype exists direcly under family) we have guest os
  *  types, e.g. Debian12_x64 etc. */
class SHARED_LIBRARY_STUFF UIGuestOSTypeManager
{
public:

    /** OS info pair. 'first' is id and 'second' is description. */
    typedef QPair<QString, QString> UIGuestInfoPair;
    /** A list of all OS families. */
    typedef QVector<UIFamilyInfo> UIGuestOSFamilyInfo;
    /** A list of all OS subtypes. */
    typedef QVector<UISubtypeInfo> UIGuestOSSubtypeInfo;
    /** A list of all OS type pairs. */
    typedef QVector<UIGuestInfoPair> UIGuestOSTypeInfo;

    /** Constructs guest OS type manager. */
    UIGuestOSTypeManager() {}
    /** Prohibits to copy guest OS type manager. */
    UIGuestOSTypeManager(const UIGuestOSTypeManager &other) = delete;

    /** Re-create the guest OS type database. */
    void reCacheGuestOSTypes();

    /** Returns a list of all families.
      * @param  fListAll    Brings whether a list of all families is requested, supported otherwise.
      * @param  exceptions  Brings the list of families to be included even if they are restricted. */
    UIGuestOSFamilyInfo getFamilies(bool fListAll = true,
                                    const QStringList &exceptions = QStringList(),
                                    KPlatformArchitecture enmArch = KPlatformArchitecture_None) const;
    /** Returns the list of subtypes for @p strFamilyId. This may be an empty list.
      * @param  fListAll    Brings whether a list of all subtypes is requested, supported otherwise.
      * @param  exceptions  Brings the list of subtypes to be included even if they are restricted. */
    UIGuestOSSubtypeInfo getSubtypesForFamilyId(const QString &strFamilyId,
                                                bool fListAll = true,
                                                const QStringList &exceptions = QStringList(),
                                                KPlatformArchitecture enmArch = KPlatformArchitecture_None) const;
    /** Returns a list of OS types for the @p strFamilyId.
      * @param  fListAll    Brings whether a list of all types is requested, supported otherwise.
      * @param  exceptions  Brings the list of types to be included even if they are restricted. */
    UIGuestOSTypeInfo getTypesForFamilyId(const QString &strFamilyId,
                                          bool fListAll = true,
                                          const QStringList &exceptions = QStringList(),
                                          KPlatformArchitecture enmArch = KPlatformArchitecture_None) const;
    /** Returns a list of OS types for the @p strSubtype.
      * @param  fListAll    Brings whether a list of all types is requested, supported otherwise.
      * @param  exceptions  Brings the list of types to be included even if they are restricted. */
    UIGuestOSTypeInfo getTypesForSubtype(const QString &strSubtype,
                                         bool fListAll = true,
                                         const QStringList &exceptions = QStringList(),
                                         KPlatformArchitecture enmArch = KPlatformArchitecture_None) const;

    /* Returns true if @p strTypeId is supported by the host. */
    bool isGuestOSTypeIDSupported(const QString &strTypeId) const;

    /** Returns whether specified @a strOSTypeId is of DOS type. */
    static bool isDOSType(const QString &strOSTypeId);

    /** @name Getters for UIGuestOSType properties.
      * They utilize a map for faster access to UIGuestOSType instance with @p strTypeId.
      * @{ */
        QString                 getFamilyId(const QString &strTypeId) const;
        QString                 getSubtype(const QString  &strTypeId) const;
        KGraphicsControllerType getRecommendedGraphicsController(const QString &strTypeId) const;
        ULONG                   getRecommendedRAM(const QString &strTypeId) const;
        ULONG                   getRecommendedCPUCount(const QString &strTypeId) const;
        KFirmwareType           getRecommendedFirmware(const QString &strTypeId) const;
        QString                 getDescription(const QString &strTypeId) const;
        LONG64                  getRecommendedHDD(const QString &strTypeId) const;
        KStorageBus             getRecommendedHDStorageBus(const QString &strTypeId) const;
        KStorageBus             getRecommendedDVDStorageBus(const QString &strTypeId) const;
        bool                    getRecommendedFloppy(const QString &strTypeId) const;
        KStorageControllerType  getRecommendedDVDStorageController(const QString &strTypeId) const;
        bool                    isLinux(const QString &strTypeId) const;
        bool                    isWindows(const QString &strTypeId) const;
        bool                    is64Bit(const QString &strOSTypeId) const;
        KPlatformArchitecture   getPlatformArchitecture(const QString &strTypeId) const;
    /** @} */

private:

    /** Adds certain @a comType to internal cache. */
    void addGuestOSType(const CGuestOSType &comType);

    /** Holds the list of supported platform architectures. */
    QVector<KPlatformArchitecture>  m_supportedArchTypes;

    /** Holds the list of supported guest OS type IDs. */
    QStringList  m_supportedGuestOSTypeIDs;

    /** The type list. Here it is a pointer to QVector to delay definition of UIGuestOSType. */
    QVector<UIGuestOSType> m_guestOSTypes;
    /** A map to prevent linear search of UIGuestOSType instances wrt. typeId. Key is typeId and value
      * is index to m_guestOSTypes list. */
    QMap<QString, int> m_typeIdIndexMap;

    /** Hold the list of guest OS family info. */
    UIGuestOSFamilyInfo m_guestOSFamilies;
    /** Hold the list of guest OS subtype info. */
    QMap<QString, UIGuestOSSubtypeInfo> m_guestOSSubtypes;
};

#endif /* !FEQT_INCLUDED_SRC_globals_UIGuestOSType_h */
