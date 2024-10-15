/* $Id: UISettingsSelector.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISettingsSelector class declaration.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_settings_UISettingsSelector_h
#define FEQT_INCLUDED_SRC_settings_UISettingsSelector_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QObject>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QSortFilterProxyModel;
class UISelectorItem;
class UISelectorModel;
class UISelectorTreeView;
class UISettingsPage;


/** QObject subclass providing settings dialog
  * with the means to switch between settings pages. */
class SHARED_LIBRARY_STUFF UISettingsSelector : public QObject
{
    Q_OBJECT;

signals:

    /** Notifies listeners about selector @a iCategory changed. */
    void sigCategoryChanged(int iCategory);

public:

    /** Constructs settings selector passing @a pParent to the base-class. */
    UISettingsSelector(QWidget *pParent = 0);
    /** Destructs settings selector. */
    virtual ~UISettingsSelector() RT_OVERRIDE;

    /** Returns the widget selector operates on. */
    virtual QWidget *widget() const = 0;

    /** Adds a new selector item.
      * @param  strBigIcon     Brings the big icon reference.
      * @param  strMediumIcon  Brings the medium icon reference.
      * @param  strSmallIcon   Brings the small icon reference.
      * @param  iID            Brings the selector section ID.
      * @param  strLink        Brings the selector section link.
      * @param  pPage          Brings the selector section page reference.
      * @param  iParentID      Brings the parent section ID or -1 if there is no parent. */
    virtual QWidget *addItem(const QString &strBigIcon, const QString &strMediumIcon, const QString &strSmallIcon,
                             int iID, const QString &strLink, UISettingsPage *pPage = 0, int iParentID = -1) = 0;

    /** Defines whether section with @a iID is @a fVisible. */
    virtual void setItemVisible(int iID, bool fVisible) { Q_UNUSED(iID); Q_UNUSED(fVisible); }

    /** Defines the @a strText for section with @a iID. */
    virtual void setItemText(int iID, const QString &strText);
    /** Returns the text for section with @a iID. */
    virtual QString itemText(int iID) const = 0;
    /** Returns the text for section with @a pPage. */
    virtual QString itemTextByPage(UISettingsPage *pPage) const;

    /** Returns the current selector ID. */
    virtual int currentId() const = 0;

    /** Returns the section ID for passed @a strLink. */
    virtual int linkToId(const QString &strLink) const = 0;

    /** Returns the section page for passed @a iID. */
    virtual QWidget *idToPage(int iID) const;

    /** Make the section with @a iID current. */
    virtual void selectById(int iID, bool fSilently = false) = 0;
    /** Make the section with @a strLink current. */
    virtual void selectByLink(const QString &strLink) { selectById(linkToId(strLink)); }

    /** Returns the list of all selector pages. */
    virtual QList<UISettingsPage*> settingPages() const;

    /** Performs selector polishing. */
    virtual void polish() {}

    /** Returns minimum selector width. */
    virtual int minWidth() const { return 0; }

protected:

    /** Searches for item with passed @a iID. */
    UISelectorItem *findItem(int iID) const;
    /** Searches for item with passed @a strLink. */
    UISelectorItem *findItemByLink(const QString &strLink) const;
    /** Searches for item with passed @a pPage. */
    UISelectorItem *findItemByPage(UISettingsPage *pPage) const;

    /** Holds the selector item instances. */
    QList<UISelectorItem*> m_list;
};


/** UISettingsSelector subclass providing settings dialog
  * with the means to switch between settings pages.
  * This one represented as tree-view. */
class SHARED_LIBRARY_STUFF UISettingsSelectorTreeView : public UISettingsSelector
{
    Q_OBJECT;

public:

    /** Constructs settings selector passing @a pParent to the base-class. */
    UISettingsSelectorTreeView(QWidget *pParent);
    /** Destructs settings selector. */
    virtual ~UISettingsSelectorTreeView() RT_OVERRIDE;

    /** Returns the widget selector operates on. */
    virtual QWidget *widget() const RT_OVERRIDE;

    /** Adds a new selector item.
      * @param  strBigIcon     Brings the big icon reference.
      * @param  strMediumIcon  Brings the medium icon reference.
      * @param  strSmallIcon   Brings the small icon reference.
      * @param  iID            Brings the selector section ID.
      * @param  strLink        Brings the selector section link.
      * @param  pPage          Brings the selector section page reference.
      * @param  iParentID      Brings the parent section ID or -1 if there is no parent. */
    virtual QWidget *addItem(const QString &strBigIcon, const QString &strMediumIcon, const QString &strSmallIcon,
                             int iID, const QString &strLink, UISettingsPage *pPage = 0, int iParentID = -1) RT_OVERRIDE;

    /** Defines whether section with @a iID is @a fVisible. */
    virtual void setItemVisible(int iID, bool fVisible) RT_OVERRIDE;

    /** Defines the @a strText for section with @a iID. */
    virtual void setItemText(int iID, const QString &strText) RT_OVERRIDE;
    /** Returns the text for section with @a iID. */
    virtual QString itemText(int iID) const RT_OVERRIDE;

    /** Returns the current selector ID. */
    virtual int currentId() const RT_OVERRIDE;

    /** Returns the section ID for passed @a strLink. */
    virtual int linkToId(const QString &strLink) const RT_OVERRIDE;

    /** Make the section with @a iID current. */
    virtual void selectById(int iID, bool fSilently) RT_OVERRIDE;

private slots:

    /** Handles selector section change from @a pPrevItem to @a pItem. */
    void sltHandleCurrentChanged(const QModelIndex &current, const QModelIndex &previous);

private:

    /** Prepares all. */
    void prepare();
    /** Cleanups all. */
    void cleanup();

    /** Holds the tree-view instance. */
    UISelectorTreeView    *m_pTreeView;
    /** Holds the model instance. */
    UISelectorModel       *m_pModel;
    /** Holds the proxy-model instance. */
    QSortFilterProxyModel *m_pModelProxy;
};


#endif /* !FEQT_INCLUDED_SRC_settings_UISettingsSelector_h */
