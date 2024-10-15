/* $Id: UITools.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UITools class implementation.
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

/* Qt includes: */
#include <QVBoxLayout>

/* GUI includes: */
#include "UITools.h"
#include "UIToolsModel.h"
#include "UIToolsView.h"
#include "UIVirtualBoxManagerWidget.h"


UITools::UITools(UIToolClass enmClass, UIVirtualBoxManagerWidget *pParent /* = 0 */)
    : QWidget(pParent, Qt::Popup)
    , m_enmClass(enmClass)
    , m_pManagerWidget(pParent)
    , m_pMainLayout(0)
    , m_pToolsModel(0)
    , m_pToolsView(0)
{
    prepare();
}

UIActionPool *UITools::actionPool() const
{
    return managerWidget()->actionPool();
}

void UITools::setToolsType(UIToolType enmType)
{
    m_pToolsModel->setToolsType(enmType);
}

UIToolType UITools::toolsType() const
{
    return m_pToolsModel->toolsType();
}

void UITools::setItemsEnabled(bool fEnabled)
{
    m_pToolsModel->setItemsEnabled(fEnabled);
}

bool UITools::isItemsEnabled() const
{
    return m_pToolsModel->isItemsEnabled();
}

void UITools::setRestrictedToolTypes(const QList<UIToolType> &types)
{
    m_pToolsModel->setRestrictedToolTypes(types);
}

QList<UIToolType> UITools::restrictedToolTypes() const
{
    return m_pToolsModel->restrictedToolTypes();
}

UIToolsItem *UITools::currentItem() const
{
    return m_pToolsModel->currentItem();
}

void UITools::prepare()
{
    /* Prepare everything: */
    prepareContents();
    prepareConnections();

    /* Init model finally: */
    initModel();
}

void UITools::prepareContents()
{
    /* Setup own layout rules: */
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::MinimumExpanding);

    /* Prepare main-layout: */
    m_pMainLayout = new QVBoxLayout(this);
    if (m_pMainLayout)
    {
        m_pMainLayout->setContentsMargins(1, 1, 1, 1);
        m_pMainLayout->setSpacing(0);

        /* Prepare model: */
        prepareModel();
    }
}

void UITools::prepareModel()
{
    /* Prepare model: */
    m_pToolsModel = new UIToolsModel(m_enmClass, this);
    if (m_pToolsModel)
        prepareView();
}

void UITools::prepareView()
{
    AssertPtrReturnVoid(m_pToolsModel);
    AssertPtrReturnVoid(m_pMainLayout);

    /* Prepare view: */
    m_pToolsView = new UIToolsView(this);
    if (m_pToolsView)
    {
        m_pToolsView->setScene(m_pToolsModel->scene());
        m_pToolsView->show();
        setFocusProxy(m_pToolsView);

        /* Add into layout: */
        m_pMainLayout->addWidget(m_pToolsView);
    }
}

void UITools::prepareConnections()
{
    /* Model connections: */
    connect(m_pToolsModel, &UIToolsModel::sigClose,
            this, &UITools::close);
    connect(m_pToolsModel, &UIToolsModel::sigSelectionChanged,
            this, &UITools::sigSelectionChanged);
    connect(m_pToolsModel, &UIToolsModel::sigExpandingStarted,
            this, &UITools::sigExpandingStarted);
    connect(m_pToolsModel, &UIToolsModel::sigExpandingFinished,
            this, &UITools::sigExpandingFinished);
    connect(m_pToolsModel, &UIToolsModel::sigItemMinimumWidthHintChanged,
            m_pToolsView, &UIToolsView::sltMinimumWidthHintChanged);
    connect(m_pToolsModel, &UIToolsModel::sigItemMinimumHeightHintChanged,
            m_pToolsView, &UIToolsView::sltMinimumHeightHintChanged);
    connect(m_pToolsModel, &UIToolsModel::sigFocusChanged,
            m_pToolsView, &UIToolsView::sltFocusChanged);

    /* View connections: */
    connect(m_pToolsView, &UIToolsView::sigResized,
            m_pToolsModel, &UIToolsModel::sltHandleViewResized);
}

void UITools::initModel()
{
    m_pToolsModel->init();
}
