/* $Id: UIApplianceImportEditorWidget.cpp 106061 2024-09-16 14:03:52Z knut.osmundsen@oracle.com $ */
/** @file
 * VBox Qt GUI - UIApplianceImportEditorWidget class implementation.
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

/* Qt includes: */
#include <QTextEdit>

/* GUI includes: */
#include "QITreeView.h"
#include "UIApplianceImportEditorWidget.h"
#include "UIMessageCenter.h"


/** UIApplianceSortProxyModel subclass for Export Appliance wizard. */
class ImportSortProxyModel : public UIApplianceSortProxyModel
{
public:

    /** Constructs proxy model passing @a pParent to the base-class. */
    ImportSortProxyModel(QObject *pParent = 0)
        : UIApplianceSortProxyModel(pParent)
    {
        m_aFilteredList << KVirtualSystemDescriptionType_License;
    }
};


/*********************************************************************************************************************************
*   Class UIApplianceImportEditorWidget implementation.                                                                          *
*********************************************************************************************************************************/

UIApplianceImportEditorWidget::UIApplianceImportEditorWidget(QWidget *pParent /* = 0 */)
    : UIApplianceEditorWidget(pParent)
{
}

void UIApplianceImportEditorWidget::setAppliance(const CAppliance &comAppliance)
{
    /* Cleanup previous stuff: */
    clear();

    /* Call to base-class: */
    UIApplianceEditorWidget::setAppliance(comAppliance);

    /* Prepare model: */
    QVector<CVirtualSystemDescription> vsds = m_comAppliance.GetVirtualSystemDescriptions();
    m_pModel = new UIApplianceModel(vsds, m_pTreeViewSettings);
    if (m_pModel)
    {
        /* Create proxy model: */
        ImportSortProxyModel *pProxy = new ImportSortProxyModel(m_pModel);
        if (pProxy)
        {
            pProxy->setSourceModel(m_pModel);
            pProxy->sort(ApplianceViewSection_Description, Qt::DescendingOrder);

            /* Set our own model: */
            m_pTreeViewSettings->setModel(pProxy);
            /* Set our own delegate: */
            UIApplianceDelegate *pDelegate = new UIApplianceDelegate(pProxy);
            if (pDelegate)
                m_pTreeViewSettings->setItemDelegate(pDelegate);

            /* For now we hide the original column. This data is displayed as tooltip also. */
            m_pTreeViewSettings->setColumnHidden(ApplianceViewSection_OriginalValue, true);
            m_pTreeViewSettings->expandAll();
            /* Set model root index and make it current: */
            m_pTreeViewSettings->setRootIndex(pProxy->mapFromSource(m_pModel->root()));
            m_pTreeViewSettings->setCurrentIndex(pProxy->mapFromSource(m_pModel->root()));
        }
    }

    /* Check for warnings & if there are one display them: */
    const QVector<QString> warnings = m_comAppliance.GetWarnings();
    const bool fWarningsEnabled = warnings.size() > 0;
    foreach (const QString &strText, warnings)
        m_pTextEditWarning->append("- " + strText);
    m_pPaneWarning->setVisible(fWarningsEnabled);
}

void UIApplianceImportEditorWidget::prepareImport()
{
    if (m_comAppliance.isNotNull())
        m_pModel->putBack();
}
