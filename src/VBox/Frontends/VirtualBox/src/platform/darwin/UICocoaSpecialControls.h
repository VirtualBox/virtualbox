/* $Id: UICocoaSpecialControls.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UICocoaSpecialControls class declaration.
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

#ifndef FEQT_INCLUDED_SRC_platform_darwin_UICocoaSpecialControls_h
#define FEQT_INCLUDED_SRC_platform_darwin_UICocoaSpecialControls_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif
#ifdef VBOX_DARWIN_USE_NATIVE_CONTROLS

/* Qt includes: */
#include <QWidget>
#ifndef VBOX_IS_QT6_OR_LATER /** @todo qt6: ... */
# include <QMacCocoaViewContainer>
#endif

/* GUI includes: */
#include "VBoxCocoaHelper.h"
#include "UILibraryDefs.h"

/* Add typedefs for Cocoa types: */
ADD_COCOA_NATIVE_REF(NSButton);

/** QMacCocoaViewContainer extension,
  * used as cocoa button container. */
class SHARED_LIBRARY_STUFF UICocoaButton
#ifdef VBOX_IS_QT6_OR_LATER /** @todo qt6: ... */
    : public QWidget
#else
    : public QMacCocoaViewContainer
#endif
{
    Q_OBJECT

signals:

    /** Notifies about button click and whether it's @a fChecked. */
    void clicked(bool fChecked = false);

public:

    /** Cocoa button types. */
    enum CocoaButtonType
    {
        HelpButton,
        CancelButton,
        ResetButton
    };

    /** Constructs cocoa button passing @a pParent to the base-class.
      * @param  enmType  Brings the button type. */
    UICocoaButton(QWidget *pParent, CocoaButtonType enmType);
    /** Destructs cocoa button. */
    ~UICocoaButton();

    /** Returns size-hint. */
    QSize sizeHint() const;

    /** Defines button @a strText. */
    void setText(const QString &strText);
    /** Defines button @a strToolTip. */
    void setToolTip(const QString &strToolTip);

    /** Handles button click. */
    void onClicked();

private:

    /** Returns native cocoa button reference. */
    NativeNSButtonRef nativeRef() const { return static_cast<NativeNSButtonRef>(cocoaView()); }
};

#endif /* VBOX_DARWIN_USE_NATIVE_CONTROLS */
#endif /* !FEQT_INCLUDED_SRC_platform_darwin_UICocoaSpecialControls_h */

