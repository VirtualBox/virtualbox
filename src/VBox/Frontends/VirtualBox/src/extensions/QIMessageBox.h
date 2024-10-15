/* $Id: QIMessageBox.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIMessageBox class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIMessageBox_h
#define FEQT_INCLUDED_SRC_extensions_QIMessageBox_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMessageBox>

/* GUI includes: */
#include "QIDialog.h"
#include "UILibraryDefs.h"

/* Forward declarations: */
class QCheckBox;
class QLabel;
class QPushButton;
class QIArrowSplitter;
class QIDialogButtonBox;
class QIRichTextLabel;


/** Button types. */
enum AlertButton
{
    AlertButton_NoButton      =  0x0,  /* 00000000 00000000 */
    AlertButton_Ok            =  0x1,  /* 00000000 00000001 */
    AlertButton_Cancel        =  0x2,  /* 00000000 00000010 */
    AlertButton_Choice1       =  0x4,  /* 00000000 00000100 */
    AlertButton_Choice2       =  0x8,  /* 00000000 00001000 */
    AlertButton_Copy          = 0x10,  /* 00000000 00010000 */
    AlertButton_Help          = 0x11,  /* 00000000 00010001 */
    AlertButtonMask           = 0xFF   /* 00000000 11111111 */
};


/** Button options. */
enum AlertButtonOption
{
    AlertButtonOption_Default = 0x100, /* 00000001 00000000 */
    AlertButtonOption_Escape  = 0x200, /* 00000010 00000000 */
    AlertButtonOptionMask     = 0x300  /* 00000011 00000000 */
};


/** Alert options. */
enum AlertOption
{
    AlertOption_AutoConfirmed = 0x400, /* 00000100 00000000 */
    AlertOption_CheckBox      = 0x800, /* 00001000 00000000 */
    AlertOptionMask           = 0xFC00 /* 11111100 00000000 */
};


/** Icon types. */
enum AlertIconType
{
    AlertIconType_NoIcon         = QMessageBox::NoIcon,
    AlertIconType_Information    = QMessageBox::Information,
    AlertIconType_Warning        = QMessageBox::Warning,
    AlertIconType_Critical       = QMessageBox::Critical,
    AlertIconType_Question       = QMessageBox::Question,
    AlertIconType_GuruMeditation
};


/** QIDialog extension representing GUI alerts. */
class SHARED_LIBRARY_STUFF QIMessageBox : public QIDialog
{
    Q_OBJECT;

public:

    /** Constructs message-box passing @a pParent to the base-class.
      * @param  strTitle    Brings the title.
      * @param  strMessage  Brings the message.
      * @param  strMessage  Brings the help keyword for context sensitive help
      * @param  iconType    Brings the icon-type.
      * @param  iButton1    Brings the integer-code for the 1st button.
      * @param  iButton2    Brings the integer-code for the 2nd button.
      * @param  iButton3    Brings the integer-code for the 3rd button. */
    QIMessageBox(const QString &strTitle, const QString &strMessage, AlertIconType iconType,
                 int iButton1 = 0, int iButton2 = 0, int iButton3 = 0, QWidget *pParent = 0,
                 const QString &strHelpKeyword = QString());

    /** Defines details-text. */
    void setDetailsText(const QString &strText);

    /** Returns whether flag is checked. */
    bool flagChecked() const;
    /** Defines whether flag is @a fChecked. */
    void setFlagChecked(bool fChecked);
    /** Defines @a strFlagText. */
    void setFlagText(const QString &strFlagText);

    /** Defines @a iButton @a strText. */
    void setButtonText(int iButton, const QString &strText);

protected:

    /** Handles polish @a pEvent. */
    virtual void polishEvent(QShowEvent *pEvent) RT_OVERRIDE;

    /** Handles close @a pEvent. */
    virtual void closeEvent(QCloseEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Updates dialog size: */
    void sltUpdateSize();

    /** Copy details-text. */
    void sltCopy() const;

    /** Closes dialog like user would press the Cancel button. */
    virtual void reject() RT_OVERRIDE;

    /** Closes dialog like user would press the 1st button. */
    void sltDone1() { m_fDone = true; done(m_iButton1 & AlertButtonMask); }
    /** Closes dialog like user would press the 2nd button. */
    void sltDone2() { m_fDone = true; done(m_iButton2 & AlertButtonMask); }
    /** Closes dialog like user would press the 3rd button. */
    void sltDone3() { m_fDone = true; done(m_iButton3 & AlertButtonMask); }

private:

    /** Prepares all. */
    void prepare();

    /** Prepares focus. */
    void prepareFocus();

    /** Push-button factory. */
    QPushButton *createButton(int iButton);

    /** Visibility update routine for details-container. */
    void updateDetailsContainer();
    /** Visibility update routine for check-box. */
    void updateCheckBox();

    /** Generates standard pixmap for passed @a iconType using @a pWidget as hint. */
    static QPixmap standardPixmap(AlertIconType iconType, QWidget *pWidget = 0);

    /** Compresses @a strText with ellipsis on the basis of certain logic. */
    static QString compressLongWords(QString strText);

    /** Holds the title. */
    QString m_strTitle;

    /** Holds the icon-type. */
    AlertIconType m_iconType;
    /** Holds the icon-label instance. */
    QLabel *m_pLabelIcon;

    /** Holds the message. */
    QString m_strMessage;
    /** Holds the message-label instance. */
    QIRichTextLabel *m_pLabelText;

    /** Holds the flag check-box instance. */
    QCheckBox *m_pFlagCheckBox;

    /** Holds the flag details-container instance. */
    QIArrowSplitter *m_pDetailsContainer;

    /** Holds the integer-code for the 1st button. */
    int m_iButton1;
    /** Holds the integer-code for the 2nd button. */
    int m_iButton2;
    /** Holds the integer-code for the 3rd button. */
    int m_iButton3;
    /** Holds the integer-code of the cancel-button. */
    int m_iButtonEsc;
    /** Holds the 1st button instance. */
    QPushButton *m_pButton1;
    /** Holds the 2nd button instance. */
    QPushButton *m_pButton2;
    /** Holds the 3rd button instance. */
    QPushButton *m_pButton3;
    /** Holds the help-button instance. */
    QPushButton  *m_pButtonHelp;

    /** Holds the button-box instance. */
    QIDialogButtonBox *m_pButtonBox;

    /** Holds the help keyword string. */
    QString m_strHelpKeyword;

    /** Defines whether message was accepted. */
    bool m_fDone : 1;
};


#endif /* !FEQT_INCLUDED_SRC_extensions_QIMessageBox_h */
