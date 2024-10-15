/* $Id: UIChooserNodeGlobal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooserNodeGlobal class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_chooser_UIChooserNodeGlobal_h
#define FEQT_INCLUDED_SRC_manager_chooser_UIChooserNodeGlobal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIChooserNode.h"


/** UIChooserNode subclass used as interface for invisible tree-view global nodes. */
class UIChooserNodeGlobal : public UIChooserNode
{
    Q_OBJECT;

public:

    /** Constructs chooser node passing @a pParent to the base-class.
      * @param  iPosition  Brings the initial node position.
      * @param  fFavorite  Brings whether the node is favorite.
      * @param  strTip     Brings the dummy tip. */
    UIChooserNodeGlobal(UIChooserNode *pParent,
                        int iPosition,
                        bool fFavorite,
                        const QString &strTip);
    /** Constructs chooser node passing @a pParent to the base-class.
      * @param  iPosition  Brings the initial node position.
      * @param  pCopyFrom  Brings the node to copy data from. */
    UIChooserNodeGlobal(UIChooserNode *pParent,
                        int iPosition,
                        UIChooserNodeGlobal *pCopyFrom);
    /** Destructs chooser node. */
    virtual ~UIChooserNodeGlobal() RT_OVERRIDE;

    /** Returns RTTI node type. */
    virtual UIChooserNodeType type() const RT_OVERRIDE { return UIChooserNodeType_Global; }

    /** Returns node name. */
    virtual QString name() const RT_OVERRIDE;
    /** Returns full node name. */
    virtual QString fullName() const RT_OVERRIDE;
    /** Returns item description. */
    virtual QString description() const RT_OVERRIDE;
    /** Returns item definition.
      * @param  fFull  Brings whether full definition is required
      *                which is used while saving group definitions,
      *                otherwise short definition will be returned,
      *                which is used while saving last chosen node. */
    virtual QString definition(bool fFull = false) const RT_OVERRIDE;

    /** Returns whether there are children of certain @a enmType. */
    virtual bool hasNodes(UIChooserNodeType enmType = UIChooserNodeType_Any) const RT_OVERRIDE;
    /** Returns a list of nodes of certain @a enmType. */
    virtual QList<UIChooserNode*> nodes(UIChooserNodeType enmType = UIChooserNodeType_Any) const RT_OVERRIDE;

    /** Adds passed @a pNode to specified @a iPosition. */
    virtual void addNode(UIChooserNode *pNode, int iPosition) RT_OVERRIDE;
    /** Removes passed @a pNode. */
    virtual void removeNode(UIChooserNode *pNode) RT_OVERRIDE;

    /** Removes all children with specified @a uId recursively. */
    virtual void removeAllNodes(const QUuid &uId) RT_OVERRIDE;
    /** Updates all children with specified @a uId recursively. */
    virtual void updateAllNodes(const QUuid &uId) RT_OVERRIDE;

    /** Returns position of specified node inside this one. */
    virtual int positionOf(UIChooserNode *pNode) RT_OVERRIDE;

    /** Updates the @a matchedItems wrt. @strSearchTerm and @a iSearchFlags. */
    virtual void searchForNodes(const QString &strSearchTerm, int iSearchFlags, QList<UIChooserNode*> &matchedItems) RT_OVERRIDE;

    /** Performs sorting of children nodes. */
    virtual void sortNodes() RT_OVERRIDE;

private slots:

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Holds the node name. */
    QString  m_strName;
};


#endif /* !FEQT_INCLUDED_SRC_manager_chooser_UIChooserNodeGlobal_h */
