/* $Id: UIFileTableNavigationWidget.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFileTableNavigationWidget class definitions.
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
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QStackedWidget>
#include <QToolButton>

/* GUI includes: */
#include "UIFileTableNavigationWidget.h"
#include "UIPathOperations.h"


/*********************************************************************************************************************************
*   UIFileManagerHistoryComboBox definition.                                                                                     *
*********************************************************************************************************************************/
/** A QCombo extension used as location history list in the UIFileTableNavigationWidget. */
class UIFileManagerHistoryComboBox : public QComboBox
{
    Q_OBJECT;

signals:

    void  sigHidePopup();

public:

    UIFileManagerHistoryComboBox(QWidget *pParent = 0);
    /** Emit sigHidePopup as the popup is hidded. */
    virtual void hidePopup() RT_OVERRIDE;
};


/*********************************************************************************************************************************
*   UIFileManagerBreadCrumbs definition.                                                                                         *
*********************************************************************************************************************************/
/** A QLabel extension. It shows the current path as text and hightligts the folder name
  *  as the mouse hovers over it. Clicking on the highlighted folder name make the file table to
  *  navigate to that folder. */
class UIFileManagerBreadCrumbs : public QLabel
{
    Q_OBJECT;

public:

    UIFileManagerBreadCrumbs(QWidget *pParent = 0);
    void setPath(const QString &strPath);
    void setPathSeparator(const QChar &separator);

protected:

    virtual void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE;

private:

    QString m_strPath;
    QChar   m_pathSeparator;
};


/*********************************************************************************************************************************
*   UIFileManagerHistoryComboBox implementation.                                                                                 *
*********************************************************************************************************************************/

UIFileManagerHistoryComboBox::UIFileManagerHistoryComboBox(QWidget *pParent /* = 0 */)
    :QComboBox(pParent)
{

}

void UIFileManagerHistoryComboBox::hidePopup()
{
    QComboBox::hidePopup();
    emit sigHidePopup();
}


/*********************************************************************************************************************************
*   UIFileManagerBreadCrumbs implementation.                                                                                     *
*********************************************************************************************************************************/

UIFileManagerBreadCrumbs::UIFileManagerBreadCrumbs(QWidget *pParent /* = 0 */)
    :QLabel(pParent)
    , m_pathSeparator(UIPathOperations::delimiter)
{
    float fFontMult = 1.f;
    QFont mFont = font();
    if (mFont.pixelSize() == -1)
        mFont.setPointSize(fFontMult * mFont.pointSize());
    else
        mFont.setPixelSize(fFontMult * mFont.pixelSize());
    setFont(mFont);

    setFrameShape(QFrame::Box);
    setLineWidth(1);
    setAutoFillBackground(true);
    QPalette pal = QApplication::palette();
    pal.setColor(QPalette::Active, QPalette::Window, pal.color(QPalette::Active, QPalette::Base));
    setPalette(pal);
    /* Allow the label become smaller than the current text. calling setpath in resizeEvent truncated the text anyway: */
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
}

void UIFileManagerBreadCrumbs::setPath(const QString &strPath)
{
    m_strPath = strPath;

    const QChar separator(UIPathOperations::delimiter);
    clear();

    if (strPath.isEmpty())
        return;

    QStringList folderList = UIPathOperations::pathTrail(strPath);
    if (!strPath.isEmpty() && strPath.at(0) == UIPathOperations::delimiter)
        folderList.push_front(separator);

    QString strLabelText;
    QVector<QString> strPathUpto;
    strPathUpto.resize(folderList.size());

    for (int i = 0; i < folderList.size(); ++i)
    {
        QString strFolder = folderList.at(i);
        if (i != 0)
            strPathUpto[i] = strPathUpto[i - 1];
        strPathUpto[i].append(QString("%1%2").arg(strFolder).arg(separator));
    }

    int iWidth = 0;
    for (int i = folderList.size() - 1; i >= 0; --i)
    {
        QString strFolder = UIPathOperations::removeTrailingDelimiters(folderList.at(i)).replace(UIPathOperations::delimiter, m_pathSeparator);
        QString strWord = QString("<a href=\"%1\" style=\"color:black;text-decoration:none;\">%2</a>").arg(strPathUpto[i]).arg(strFolder);

        if (i < folderList.size() - 1)
        {
            iWidth += fontMetrics().horizontalAdvance(" > ");
            strWord.append("<b> > </b>");
        }
        iWidth += fontMetrics().horizontalAdvance(strFolder);

        if (iWidth < width())
            strLabelText.prepend(strWord);
    }
    setText(strLabelText);
}

void UIFileManagerBreadCrumbs::setPathSeparator(const QChar &separator)
{
    m_pathSeparator = separator;
}

void UIFileManagerBreadCrumbs::resizeEvent(QResizeEvent *pEvent)
{
    /* Truncate the text the way we want: */
    setPath(m_strPath);
    QLabel::resizeEvent(pEvent);
}


/*********************************************************************************************************************************
*   UIFileTableNavigationWidget implementation.                                                                                  *
*********************************************************************************************************************************/

UIFileTableNavigationWidget::UIFileTableNavigationWidget(QWidget *pParent /* = 0 */)
    :QWidget(pParent)
    , m_pContainer(0)
    , m_pBreadCrumbs(0)
    , m_pHistoryComboBox(0)
    , m_pAddressLineEdit(0)
    , m_pSwitchButton(0)
    , m_pathSeparator(UIPathOperations::delimiter)
{
    prepare();
}

void UIFileTableNavigationWidget::setPath(const QString &strLocation)
{
    if (m_strCurrentPath == QDir::fromNativeSeparators(strLocation))
        return;

    m_strCurrentPath = QDir::fromNativeSeparators(strLocation);

    if (m_pBreadCrumbs)
        m_pBreadCrumbs->setPath(strLocation);

    if (m_pHistoryComboBox)
    {
        QString strNativeLocation(strLocation);
        strNativeLocation.replace(UIPathOperations::delimiter, m_pathSeparator);
        int itemIndex = m_pHistoryComboBox->findText(strNativeLocation,
                                                      Qt::MatchExactly | Qt::MatchCaseSensitive);
        if (itemIndex == -1)
        {
            m_pHistoryComboBox->insertItem(m_pHistoryComboBox->count(), strNativeLocation);
            itemIndex = m_pHistoryComboBox->count() - 1;
        }
        m_pHistoryComboBox->setCurrentIndex(itemIndex);
        emit sigHistoryListChanged();
    }
}

void UIFileTableNavigationWidget::reset()
{
    if (m_pHistoryComboBox)
    {
        m_pHistoryComboBox->blockSignals(true);
        m_pHistoryComboBox->clear();
        m_pHistoryComboBox->blockSignals(false);
        emit sigHistoryListChanged();
    }

    if (m_pBreadCrumbs)
        m_pBreadCrumbs->setPath(QString());
}

void UIFileTableNavigationWidget::setPathSeparator(const QChar &separator)
{
    m_pathSeparator = separator;
    if (m_pBreadCrumbs)
        m_pBreadCrumbs->setPathSeparator(m_pathSeparator);
}

bool UIFileTableNavigationWidget::canGoForward() const
{
    if (!m_pHistoryComboBox)
        return false;
    return (m_pHistoryComboBox->currentIndex() < m_pHistoryComboBox->count() - 1);
}

bool UIFileTableNavigationWidget::canGoBackward() const
{
    if (!m_pHistoryComboBox)
        return false;
    return (m_pHistoryComboBox->currentIndex() > 0);
}


void UIFileTableNavigationWidget::goForwardInHistory()
{
    if (!m_pHistoryComboBox || m_pHistoryComboBox->currentIndex() >= m_pHistoryComboBox->count() - 1)
        return;
    m_pHistoryComboBox->setCurrentIndex(m_pHistoryComboBox->currentIndex() + 1);
}

void UIFileTableNavigationWidget::goBackwardInHistory()
{
    if (!m_pHistoryComboBox || m_pHistoryComboBox->currentIndex() <= 0)
        return;
    m_pHistoryComboBox->setCurrentIndex(m_pHistoryComboBox->currentIndex() - 1);
}

void UIFileTableNavigationWidget::prepare()
{
    QHBoxLayout *pLayout = new QHBoxLayout;
    if (!pLayout)
        return;
    pLayout->setSpacing(0);
    pLayout->setContentsMargins(0, 0, 0, 0);

    m_pContainer = new QStackedWidget;
    if (m_pContainer)
    {
        m_pBreadCrumbs = new UIFileManagerBreadCrumbs;
        m_pHistoryComboBox = new UIFileManagerHistoryComboBox;
        m_pAddressLineEdit = new QLineEdit;
        if (m_pBreadCrumbs && m_pHistoryComboBox)
        {
            m_pBreadCrumbs->setIndent(0.5 * qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin));
            m_pBreadCrumbs->installEventFilter(this);
            m_pAddressLineEdit->installEventFilter(this);
            connect(m_pBreadCrumbs, &UIFileManagerBreadCrumbs::linkActivated,
                    this, &UIFileTableNavigationWidget::sltHandlePathChange);
            connect(m_pHistoryComboBox, &UIFileManagerHistoryComboBox::sigHidePopup,
                    this, &UIFileTableNavigationWidget::sltHandleHidePopup);
            connect(m_pHistoryComboBox, &UIFileManagerHistoryComboBox::currentTextChanged,
                    this, &UIFileTableNavigationWidget::sltHandlePathChange);
            connect(m_pAddressLineEdit, &QLineEdit::returnPressed,
                    this, &UIFileTableNavigationWidget::sltAddressLineEdited);
            m_pContainer->insertWidget(StackedWidgets_BreadCrumbs, m_pBreadCrumbs);
            m_pContainer->insertWidget(StackedWidgets_History, m_pHistoryComboBox);
            m_pContainer->insertWidget(StackedWidgets_AddressLine, m_pAddressLineEdit);
            m_pContainer->setCurrentIndex(StackedWidgets_BreadCrumbs);
        }
        pLayout->addWidget(m_pContainer);
    }

    m_pSwitchButton = new QToolButton;
    if (m_pSwitchButton)
    {
        QStyle *pStyle = QApplication::style();
        QIcon buttonIcon;
        if (pStyle)
        {
            buttonIcon = pStyle->standardIcon(QStyle::SP_TitleBarUnshadeButton);
            m_pSwitchButton->setIcon(buttonIcon);
        }
        pLayout->addWidget(m_pSwitchButton);
        connect(m_pSwitchButton, &QToolButton::clicked,
                this, &UIFileTableNavigationWidget::sltHandleSwitch);
    }
    setLayout(pLayout);
}

bool UIFileTableNavigationWidget::eventFilter(QObject *pObject, QEvent *pEvent)
{
    if (pObject == m_pBreadCrumbs && pEvent && pEvent->type() == QEvent::MouseButtonDblClick)
    {
        m_pContainer->setCurrentIndex(StackedWidgets_AddressLine);
        m_pAddressLineEdit->setText(QDir::toNativeSeparators(m_strCurrentPath));
        m_pAddressLineEdit->setFocus();

    }
    else if(pObject == m_pAddressLineEdit && pEvent && pEvent->type() == QEvent::FocusOut)
        m_pContainer->setCurrentIndex(StackedWidgets_BreadCrumbs);

    return QWidget::eventFilter(pObject, pEvent);
}

void UIFileTableNavigationWidget::sltHandleHidePopup()
{
    m_pContainer->setCurrentIndex(StackedWidgets_BreadCrumbs);
}

void UIFileTableNavigationWidget::sltHandlePathChange(const QString &strPath)
{
    emit sigPathChanged(UIPathOperations::replaceDosDelimeter(strPath));
}

void UIFileTableNavigationWidget::sltHandleSwitch()
{
    if (m_pContainer->currentIndex() == StackedWidgets_BreadCrumbs)
    {
        m_pContainer->setCurrentIndex(StackedWidgets_History);
        m_pHistoryComboBox->showPopup();
    }
    else
    {
        m_pContainer->setCurrentIndex(StackedWidgets_BreadCrumbs);
        m_pHistoryComboBox->hidePopup();
    }
}

void UIFileTableNavigationWidget::sltAddressLineEdited()
{
    sigPathChanged(QDir::fromNativeSeparators(m_pAddressLineEdit->text()));
}

#include "UIFileTableNavigationWidget.moc"
