/* $Id: UIHostnameDomainNameEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIHostnameDomainNameEditor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_wizards_editors_UIHostnameDomainNameEditor_h
#define FEQT_INCLUDED_SRC_wizards_editors_UIHostnameDomainNameEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QIcon>
#include <QWidget>

/* Forward declarations: */
class QCheckBox;
class QGridLayout;
class QLabel;
class QILineEdit;
class UIPasswordLineEdit;

class UIHostnameDomainNameEditor : public QWidget
{

    Q_OBJECT;

signals:

    void sigHostnameDomainNameChanged(const QString &strHostNameDomain, bool fIsComplete);
    void sigProductKeyChanged(const QString &strProductKey);
    void sigStartHeadlessChanged(bool fChecked);

public:

    UIHostnameDomainNameEditor(QWidget *pParent = 0);

    QString hostname() const;
    void setHostname(const QString &strHostname);

    QString domainName() const;
    void setDomainName(const QString &strDomain);

    QString hostnameDomainName() const;

    bool isComplete() const;
    void mark();

    void disableEnableProductKeyWidgets(bool fEnabled);

private slots:

    void sltHostnameChanged();
    void sltDomainChanged();
    void sltRetranslateUI();

private:

    void prepare();
    void addLineEdit(int &iRow, QLabel *&pLabel, QILineEdit *&pLineEdit, QGridLayout *pLayout);

    QILineEdit *m_pHostnameLineEdit;
    QILineEdit *m_pDomainNameLineEdit;
    QILineEdit *m_pProductKeyLineEdit;

    QLabel *m_pHostnameLabel;
    QLabel *m_pDomainNameLabel;
    QLabel *m_pProductKeyLabel;
    QGridLayout *m_pMainLayout;
    QCheckBox *m_pStartHeadlessCheckBox;
};

#endif /* !FEQT_INCLUDED_SRC_wizards_editors_UIHostnameDomainNameEditor_h */
