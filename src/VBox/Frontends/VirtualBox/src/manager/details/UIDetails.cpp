/* $Id: UIDetails.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetails class implementation.
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
#include "UICommon.h"
#include "UIDetails.h"
#include "UIDetailsModel.h"
#include "UIDetailsView.h"
#include "UIExtraDataManager.h"

/* Other VBox includes: */
#include "iprt/assert.h"


UIDetails::UIDetails(QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_pMainLayout(0)
    , m_pDetailsModel(0)
    , m_pDetailsView(0)
{
    prepare();
}

void UIDetails::setItems(const QList<UIVirtualMachineItem*> &items)
{
    /* Propagate to details-model: */
    m_pDetailsModel->setItems(items);
}

void UIDetails::prepare()
{
    /* Prepare everything: */
    prepareContents();
    prepareConnections();

    /* Configure context-sensitive help: */
    uiCommon().setHelpKeyword(this, "vm-details-tool");

    /* Init model finally: */
    initModel();
}

void UIDetails::prepareContents()
{
    /* Prepare main-layout: */
    m_pMainLayout = new QVBoxLayout(this);
    if (m_pMainLayout)
    {
        m_pMainLayout->setContentsMargins(0, 0, 0, 0);
        m_pMainLayout->setSpacing(0);

        /* Prepare model: */
        prepareModel();
    }
}

void UIDetails::prepareModel()
{
    /* Prepare model: */
    m_pDetailsModel = new UIDetailsModel(this);
    if (m_pDetailsModel)
        prepareView();
}

void UIDetails::prepareView()
{
    AssertPtrReturnVoid(m_pDetailsModel);
    AssertPtrReturnVoid(m_pMainLayout);

    /* Prepare view: */
    m_pDetailsView = new UIDetailsView(this);
    if (m_pDetailsView)
    {
        m_pDetailsView->setScene(m_pDetailsModel->scene());
        m_pDetailsView->show();
        setFocusProxy(m_pDetailsView);

        /* Add into layout: */
        m_pMainLayout->addWidget(m_pDetailsView);
    }
}

void UIDetails::prepareConnections()
{
    /* Extra-data events connections: */
    connect(gEDataManager, &UIExtraDataManager::sigDetailsCategoriesChange,
            m_pDetailsModel, &UIDetailsModel::sltHandleExtraDataCategoriesChange);
    connect(gEDataManager, &UIExtraDataManager::sigDetailsOptionsChange,
            m_pDetailsModel, &UIDetailsModel::sltHandleExtraDataOptionsChange);

    /* Model connections: */
    connect(m_pDetailsModel, &UIDetailsModel::sigRootItemMinimumWidthHintChanged,
            m_pDetailsView, &UIDetailsView::sltMinimumWidthHintChanged);
    connect(m_pDetailsModel, &UIDetailsModel::sigLinkClicked,
            this, &UIDetails::sigLinkClicked);
    connect(this, &UIDetails::sigToggleStarted,
            m_pDetailsModel, &UIDetailsModel::sltHandleToggleStarted);
    connect(this, &UIDetails::sigToggleFinished,
            m_pDetailsModel, &UIDetailsModel::sltHandleToggleFinished);

    /* View connections: */
    connect(m_pDetailsView, &UIDetailsView::sigResized,
            m_pDetailsModel, &UIDetailsModel::sltHandleViewResize);
}

void UIDetails::initModel()
{
    m_pDetailsModel->init();
}
