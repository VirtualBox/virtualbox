/* $Id: UISettingsDefs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Header with definitions and functions related to settings configuration.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_settings_UISettingsDefs_h
#define FEQT_INCLUDED_SRC_settings_UISettingsDefs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMap>
#include <QPair>
#include <QString>

/* GUI includes: */
#include "UILibraryDefs.h"

/* COM includes: */
#include "KSessionState.h"
#include "KMachineState.h"

/** Settings configuration namespace. */
namespace UISettingsDefs
{
    /** Configuration access levels. */
    enum ConfigurationAccessLevel
    {
        /** Configuration is not accessible. */
        ConfigurationAccessLevel_Null,
        /** Configuration is accessible fully. */
        ConfigurationAccessLevel_Full,
        /** Configuration is accessible partially, machine is in @a powered_off state. */
        ConfigurationAccessLevel_Partial_PoweredOff,
        /** Configuration is accessible partially, machine is in @a saved state. */
        ConfigurationAccessLevel_Partial_Saved,
        /** Configuration is accessible partially, machine is in @a running state. */
        ConfigurationAccessLevel_Partial_Running,
    };

    /** Recording mode enum is used in Display setting page to determine the recording mode. */
    enum RecordingMode
    {
        RecordingMode_None       = 0,
        RecordingMode_VideoAudio = 1,
        RecordingMode_VideoOnly  = 2,
        RecordingMode_AudioOnly  = 3,
        RecordingMode_Max        = 4
    };

    /** Determines configuration access level for passed @a enmSessionState and @a enmMachineState. */
    SHARED_LIBRARY_STUFF ConfigurationAccessLevel configurationAccessLevel(KSessionState enmSessionState,
                                                                           KMachineState enmMachineState);
}

Q_DECLARE_METATYPE(UISettingsDefs::RecordingMode);


/** Template organizing settings object cache: */
template <class CacheData> class UISettingsCache
{
public:

    /** Constructs empty object cache. */
    UISettingsCache() { m_value = qMakePair(CacheData(), CacheData()); }

    /** Destructs cache object. */
    virtual ~UISettingsCache() { /* Makes MSC happy */ }

    /** Returns the NON-modifiable REFERENCE to the initial cached data. */
    const CacheData &base() const { return m_value.first; }
    /** Returns the NON-modifiable REFERENCE to the current cached data. */
    const CacheData &data() const { return m_value.second; }
    /** Returns the modifiable REFERENCE to the initial cached data. */
    CacheData &base() { return m_value.first; }
    /** Returns the modifiable REFERENCE to the current cached data. */
    CacheData &data() { return m_value.second; }

    /** Returns whether the cached object was removed.
      * We assume that cached object was removed if
      * initial data was set but current data was NOT set. */
    virtual bool wasRemoved() const { return base() != CacheData() && data() == CacheData(); }

    /** Returns whether the cached object was created.
      * We assume that cached object was created if
      * initial data was NOT set but current data was set. */
    virtual bool wasCreated() const { return base() == CacheData() && data() != CacheData(); }

    /** Returns whether the cached object was updated.
      * We assume that cached object was updated if
      * current and initial data were both set and not equal to each other. */
    virtual bool wasUpdated() const { return base() != CacheData() && data() != CacheData() && data() != base(); }

    /** Returns whether the cached object was changed.
      * We assume that cached object was changed if
      * it was 1. removed, 2. created or 3. updated. */
    virtual bool wasChanged() const { return wasRemoved() || wasCreated() || wasUpdated(); }

    /** Defines initial cached object data. */
    void cacheInitialData(const CacheData &initialData) { m_value.first = initialData; }
    /** Defines current cached object data: */
    void cacheCurrentData(const CacheData &currentData) { m_value.second = currentData; }

    /** Resets the initial and the current object data to be both empty. */
    void clear() { m_value.first = CacheData(); m_value.second = CacheData(); }

private:

    /** Holds the cached object data. */
    QPair<CacheData, CacheData> m_value;
};


/** Template organizing settings object cache with children. */
template <class ParentCacheData, class ChildCacheData> class UISettingsCachePool : public UISettingsCache<ParentCacheData>
{
public:

    /** Children map. */
    typedef QMap<QString, ChildCacheData> UISettingsCacheChildMap;
    /** Children map iterator. */
    typedef QMapIterator<QString, ChildCacheData> UISettingsCacheChildIterator;

    /** Constructs empty object cache. */
    UISettingsCachePool() : UISettingsCache<ParentCacheData>() {}

    /** Returns children count. */
    int childCount() const { return m_children.size(); }
    /** Returns the modifiable REFERENCE to the child cached data. */
    ChildCacheData &child(const QString &strChildKey) { return m_children[strChildKey]; }
    /** Wraps method above to return the modifiable REFERENCE to the child cached data. */
    ChildCacheData &child(int iIndex) { return child(indexToKey(iIndex)); }
    /** Returns the NON-modifiable COPY to the child cached data. */
    const ChildCacheData child(const QString &strChildKey) const { return m_children[strChildKey]; }
    /** Wraps method above to return the NON-modifiable COPY to the child cached data. */
    const ChildCacheData child(int iIndex) const { return child(indexToKey(iIndex)); }

    /** Returns whether the cache was updated.
      * We assume that cache object was updated if current and
      * initial data were both set and not equal to each other.
      * Takes into account all the children. */
    bool wasUpdated() const RT_OVERRIDE RT_FINAL
    {
        /* First of all, cache object is considered to be updated if parent data was updated: */
        bool fWasUpdated = UISettingsCache<ParentCacheData>::wasUpdated();
        /* If parent data was NOT updated but also was NOT created or removed too
         * (e.j. was NOT changed at all), we have to check children too: */
        if (!fWasUpdated && !UISettingsCache<ParentCacheData>::wasRemoved() && !UISettingsCache<ParentCacheData>::wasCreated())
        {
            for (int iChildIndex = 0; !fWasUpdated && iChildIndex < childCount(); ++iChildIndex)
                if (child(iChildIndex).wasChanged())
                    fWasUpdated = true;
        }
        return fWasUpdated;
    }

    /** Resets the initial and the current one data to be both empty.
      * Removes all the children. */
    void clear()
    {
        UISettingsCache<ParentCacheData>::clear();
        m_children.clear();
    }

private:

    /** Returns QString representation of passed @a iIndex. */
    QString indexToKey(int iIndex) const
    {
        UISettingsCacheChildIterator childIterator(m_children);
        for (int iChildIndex = 0; childIterator.hasNext(); ++iChildIndex)
        {
            childIterator.next();
            if (iChildIndex == iIndex)
                return childIterator.key();
        }
        return QString("%1").arg(iIndex, 8 /* up to 8 digits */, 10 /* base */, QChar('0') /* filler */);
    }

    /** Holds the children. */
    UISettingsCacheChildMap m_children;
};


/** Template organizing settings object cache with 2 groups of children. */
template <class ParentCacheData, class ChildCacheData1, class ChildCacheData2> class UISettingsCachePoolOfTwo : public UISettingsCache<ParentCacheData>
{
public:

    /** Group 1 children map. */
    typedef QMap<QString, ChildCacheData1> UISettingsCacheChildMap1;
    /** Group 2 children map. */
    typedef QMap<QString, ChildCacheData2> UISettingsCacheChildMap2;
    /** Group 1 children map iterator. */
    typedef QMapIterator<QString, ChildCacheData1> UISettingsCacheChildIterator1;
    /** Group 2 children map iterator. */
    typedef QMapIterator<QString, ChildCacheData2> UISettingsCacheChildIterator2;

    /** Constructs empty cache object. */
    UISettingsCachePoolOfTwo() : UISettingsCache<ParentCacheData>() {}

    /** Returns group 1 children count. */
    int childCount1() const { return m_children1.size(); }
    /** Returns the modifiable REFERENCE to the group 1 child cached data. */
    ChildCacheData1 &child1(const QString &strChildKey) { return m_children1[strChildKey]; }
    /** Wraps method above to return the modifiable REFERENCE to the group 1 child cached data. */
    ChildCacheData1 &child1(int iIndex) { return child1(indexToKey1(iIndex)); }
    /** Returns the NON-modifiable COPY to the group 1 child cached data. */
    const ChildCacheData1 child1(const QString &strChildKey) const { return m_children1[strChildKey]; }
    /** Wraps method above to return the NON-modifiable COPY to the group 1 child cached data. */
    const ChildCacheData1 child1(int iIndex) const { return child1(indexToKey1(iIndex)); }

    /** Returns group 2 children count. */
    int childCount2() const { return m_children2.size(); }
    /** Returns the modifiable REFERENCE to the group 2 child cached data. */
    ChildCacheData2 &child2(const QString &strChildKey) { return m_children2[strChildKey]; }
    /** Wraps method above to return the modifiable REFERENCE to the group 2 child cached data. */
    ChildCacheData2 &child2(int iIndex) { return child2(indexToKey2(iIndex)); }
    /** Returns the NON-modifiable COPY to the group 2 child cached data. */
    const ChildCacheData2 child2(const QString &strChildKey) const { return m_children2[strChildKey]; }
    /** Wraps method above to return the NON-modifiable COPY to the group 2 child cached data. */
    const ChildCacheData2 child2(int iIndex) const { return child2(indexToKey2(iIndex)); }

    /** Returns whether the cache was updated.
      * We assume that cache object was updated if current and
      * initial data were both set and not equal to each other.
      * Takes into account all the children of both groups. */
    bool wasUpdated() const
    {
        /* First of all, cache object is considered to be updated if parent data was updated: */
        bool fWasUpdated = UISettingsCache<ParentCacheData>::wasUpdated();
        /* If parent data was NOT updated but also was NOT created or removed too
         * (e.j. was NOT changed at all), we have to check children too: */
        if (!fWasUpdated && !UISettingsCache<ParentCacheData>::wasRemoved() && !UISettingsCache<ParentCacheData>::wasCreated())
        {
            for (int iChildIndex = 0; !fWasUpdated && iChildIndex < childCount1(); ++iChildIndex)
                if (child1(iChildIndex).wasChanged())
                    fWasUpdated = true;
            for (int iChildIndex = 0; !fWasUpdated && iChildIndex < childCount2(); ++iChildIndex)
                if (child2(iChildIndex).wasChanged())
                    fWasUpdated = true;
        }
        return fWasUpdated;
    }

    /** Resets the initial and the current one data to be both empty.
      * Removes all the children from both groups. */
    void clear()
    {
        UISettingsCache<ParentCacheData>::clear();
        m_children1.clear();
        m_children2.clear();
    }

private:

    /** Returns QString representation of passed @a iIndex inside group 1. */
    QString indexToKey1(int iIndex) const
    {
        UISettingsCacheChildIterator1 childIterator(m_children1);
        for (int iChildIndex = 0; childIterator.hasNext(); ++iChildIndex)
        {
            childIterator.next();
            if (iChildIndex == iIndex)
                return childIterator.key();
        }
        return QString("%1").arg(iIndex, 8 /* up to 8 digits */, 10 /* base */, QChar('0') /* filler */);
    }

    /** Returns QString representation of passed @a iIndex inside group 2. */
    QString indexToKey2(int iIndex) const
    {
        UISettingsCacheChildIterator2 childIterator(m_children2);
        for (int iChildIndex = 0; childIterator.hasNext(); ++iChildIndex)
        {
            childIterator.next();
            if (iChildIndex == iIndex)
                return childIterator.key();
        }
        return QString("%1").arg(iIndex, 8 /* up to 8 digits */, 10 /* base */, QChar('0') /* filler */);
    }

    /** Holds the children of group 1. */
    UISettingsCacheChildMap1 m_children1;
    /** Holds the children of group 2. */
    UISettingsCacheChildMap2 m_children2;
};


#endif /* !FEQT_INCLUDED_SRC_settings_UISettingsDefs_h */
