/* $Id: UIApplianceImportEditorWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIApplianceImportEditorWidget class declaration.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UIApplianceImportEditorWidget_h
#define FEQT_INCLUDED_SRC_widgets_UIApplianceImportEditorWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIApplianceEditorWidget.h"

/* COM includes: */
#include "CAppliance.h"

/** UIApplianceEditorWidget subclass for Import Appliance wizard. */
class UIApplianceImportEditorWidget: public UIApplianceEditorWidget
{
    Q_OBJECT;

public:

    /** Constructs widget passing @a pParent to the base-class. */
    UIApplianceImportEditorWidget(QWidget *pParent = 0);

    /** Assigns @a comAppliance and populates widget contents. */
    virtual void setAppliance(const CAppliance &comAppliance) RT_OVERRIDE RT_FINAL;

    /** Prepares import by pushing edited data back to appliance. */
    void prepareImport();
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UIApplianceImportEditorWidget_h */
