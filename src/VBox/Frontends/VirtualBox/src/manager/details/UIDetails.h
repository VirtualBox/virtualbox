/* $Id: UIDetails.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetails class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_details_UIDetails_h
#define FEQT_INCLUDED_SRC_manager_details_UIDetails_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* Forward declartions: */
class QString;
class QVBoxLayout;
class UIDetailsModel;
class UIDetailsView;
class UIVirtualMachineItem;

/** QWidget-based Details pane container. */
class UIDetails : public QWidget
{
    Q_OBJECT;

signals:

    /** @name General stuff.
      * @{ */
        /** Notifies listeners about link click.
          * @param  strCategory  Brings link category.
          * @param  strControl   Brings control name.
          * @param  uId        Brings machine ID. */
        void sigLinkClicked(const QString &strCategory,
                            const QString &strControl,
                            const QUuid &uId);

        /** Notifies listeners about toggling started. */
        void sigToggleStarted();
        /** Notifies listeners about toggling finished. */
        void sigToggleFinished();
    /** @} */

public:

    /** Constructs Details pane passing @a pParent to the base-class. */
    UIDetails(QWidget *pParent = 0);

    /** @name General stuff.
      * @{ */
        /** Return the Details-model instance. */
        UIDetailsModel *model() const { return m_pDetailsModel; }
        /** Return the Details-view instance. */
        UIDetailsView *view() const { return m_pDetailsView; }

        /** Replaces current model @a items. */
        void setItems(const QList<UIVirtualMachineItem*> &items);
    /** @} */

private:

    /** @name Prepare/Cleanup cascade.
      * @{ */
        /** Prepares all. */
        void prepare();
        /** Prepares contents. */
        void prepareContents();
        /** Prepares model. */
        void prepareModel();
        /** Prepares view. */
        void prepareView();
        /** Prepares connections. */
        void prepareConnections();
        /** Inits model. */
        void initModel();
    /** @} */

    /** @name General stuff.
      * @{ */
        /** Holds the main layout instance. */
        QVBoxLayout    *m_pMainLayout;
        /** Holds the details model instance. */
        UIDetailsModel *m_pDetailsModel;
        /** Holds the details view instance. */
        UIDetailsView  *m_pDetailsView;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_manager_details_UIDetails_h */
