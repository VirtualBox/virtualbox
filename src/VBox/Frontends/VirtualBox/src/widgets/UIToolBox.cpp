/* $Id: UIToolBox.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIToolBox class implementation.
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

/* Qt includes: */
#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QPainter>
#include <QStyle>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIRichTextLabel.h"
#include "QIToolButton.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIIconPool.h"
#include "UIToolBox.h"
#include "UITranslationEventListener.h"


/*********************************************************************************************************************************
*   UIToolPageButton definition.                                                                                                 *
*********************************************************************************************************************************/
/** A QAbstractButton extension used to show collapse/expand icons. More importantly
  * it is buddy to the title label which may include some mnemonics. This makes it possible
  * to expand pages via keyboard. */
class UIToolPageButton : public QAbstractButton
{

    Q_OBJECT;

public:

    UIToolPageButton(QWidget *pParent = 0);
    void setPixmap(const QPixmap &pixmap);

protected:

    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE;
    virtual QSize sizeHint() const RT_OVERRIDE;

private:

    /** Holds the pixmap of the expand/collapser icon. We keep
      * QPixmap instead of QIcon since it is rotated when the
      * the page is expanded and end product of rotation is a pixmap.
      * and we use QPainter to draw pixmap.*/
    QPixmap m_pixmap;
};


/*********************************************************************************************************************************
*   UIToolBoxPage definition.                                                                                                    *
*********************************************************************************************************************************/

class UIToolBoxPage : public QWidget
{

    Q_OBJECT;

signals:

    void sigShowPageWidget();

public:

    UIToolBoxPage(bool fEnableCheckBoxEnabled = false, QWidget *pParent = 0);
    void setTitle(const QString &strTitle);
    void setTitleBackgroundColor(const QColor &color);
    void setExpanded(bool fExpanded);
    int index() const;
    void setIndex(int iIndex);
    int totalHeight() const;
    int titleHeight() const;
    QSize pageWidgetSize() const;
    void setTitleIcon(const QIcon &icon, const QString &strToolTip);

protected:

    virtual bool eventFilter(QObject *pWatched, QEvent *pEvent) RT_OVERRIDE;

private slots:

    void sltHandleEnableToggle(int iState);
    void sltRetranslateUI();

private:

    void prepare(bool fEnableCheckBoxEnabled);
    void setExpandCollapseIcon();
    /* @p pWidget's ownership is transferred to the page. */
    void setWidget(QWidget *pWidget);

    bool         m_fExpanded;
    QVBoxLayout *m_pLayout;
    QWidget     *m_pTitleContainerWidget;
    QLabel      *m_pTitleLabel;
    QLabel      *m_pIconLabel;
    QCheckBox   *m_pEnableCheckBox;

    QWidget     *m_pWidget;
    int          m_iIndex;
    bool         m_fExpandCollapseIconVisible;
    QIcon        m_expandCollapseIcon;
    UIToolPageButton *m_pTitleButton;
    QString      m_strTitle;
    friend class UIToolBox;
};


/*********************************************************************************************************************************
*   UIToolPageButton implementation.                                                                                             *
*********************************************************************************************************************************/

UIToolPageButton::UIToolPageButton(QWidget *pParent /* = 0 */)
    : QAbstractButton(pParent)
{
}

void UIToolPageButton::paintEvent(QPaintEvent *pEvent)
{
    Q_UNUSED(pEvent);
    if (!m_pixmap.isNull())
    {
        QPainter painter(this);
        painter.drawPixmap(0 /* origin X */,
                           0 /* origin Y */,
                           m_pixmap.width() / m_pixmap.devicePixelRatio() /* width */,
                           m_pixmap.height() / m_pixmap.devicePixelRatio() /* height */,
                           m_pixmap /* pixmap itself */);
    }
}

void UIToolPageButton::setPixmap(const QPixmap &pixmap)
{
    m_pixmap = pixmap;
    updateGeometry();
    update();
}

QSize UIToolPageButton::sizeHint() const
{
    if (m_pixmap.isNull())
        return QSize(0,0);
    return m_pixmap.size() / m_pixmap.devicePixelRatio();
}


/*********************************************************************************************************************************
*   UIToolBoxPage implementation.                                                                                                *
*********************************************************************************************************************************/

UIToolBoxPage::UIToolBoxPage(bool fEnableCheckBoxEnabled /* = false */, QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_fExpanded(false)
    , m_pLayout(0)
    , m_pTitleContainerWidget(0)
    , m_pTitleLabel(0)
    , m_pIconLabel(0)
    , m_pEnableCheckBox(0)
    , m_pWidget(0)
    , m_iIndex(0)
    , m_fExpandCollapseIconVisible(true)
    , m_pTitleButton(0)
{
    prepare(fEnableCheckBoxEnabled);
}

void UIToolBoxPage::setTitle(const QString &strTitle)
{
    m_strTitle = strTitle;
    if (!m_pTitleLabel)
        return;
    m_pTitleLabel->setText(strTitle);
    sltRetranslateUI();
}

void UIToolBoxPage::prepare(bool fEnableCheckBoxEnabled)
{
    m_expandCollapseIcon = UIIconPool::iconSet(":/expanding_collapsing_16px.png");

    m_pLayout = new QVBoxLayout(this);
    m_pLayout->setContentsMargins(0, 0, 0, 0);

    m_pTitleContainerWidget = new QWidget;
    m_pTitleContainerWidget->installEventFilter(this);
    QHBoxLayout *pTitleLayout = new QHBoxLayout(m_pTitleContainerWidget);
    pTitleLayout->setContentsMargins(qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin),
                                     .4f * qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin),
                                     qApp->style()->pixelMetric(QStyle::PM_LayoutRightMargin),
                                     .4f * qApp->style()->pixelMetric(QStyle::PM_LayoutBottomMargin));

    m_pTitleButton = new UIToolPageButton;
    pTitleLayout->addWidget(m_pTitleButton);
    connect(m_pTitleButton, &QAbstractButton::clicked, this, &UIToolBoxPage::sigShowPageWidget);


    if (fEnableCheckBoxEnabled)
    {
        m_pEnableCheckBox = new QCheckBox;
        pTitleLayout->addWidget(m_pEnableCheckBox);
        connect(m_pEnableCheckBox, &QCheckBox::stateChanged, this, &UIToolBoxPage::sltHandleEnableToggle);
    }

    m_pTitleLabel = new QLabel;
    m_pTitleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_pTitleLabel->setBuddy(m_pTitleButton);

    pTitleLayout->addWidget(m_pTitleLabel);
    m_pIconLabel = new QLabel;
    pTitleLayout->addWidget(m_pIconLabel, Qt::AlignLeft);
    pTitleLayout->addStretch();
    m_pTitleContainerWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_pLayout->addWidget(m_pTitleContainerWidget);

    setExpandCollapseIcon();
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &UIToolBoxPage::sltRetranslateUI);
}

void UIToolBoxPage::setWidget(QWidget *pWidget)
{
    if (!m_pLayout || !pWidget)
        return;
    m_pWidget = pWidget;
    m_pLayout->addWidget(m_pWidget);

    if (m_pEnableCheckBox)
        m_pWidget->setEnabled(m_pEnableCheckBox->checkState() == Qt::Checked);

    m_pWidget->hide();
}

void UIToolBoxPage::setTitleBackgroundColor(const QColor &color)
{
    if (!m_pTitleLabel)
        return;
    QPalette palette = m_pTitleContainerWidget->palette();
    palette.setColor(QPalette::Window, color);
    m_pTitleContainerWidget->setPalette(palette);
    m_pTitleContainerWidget->setAutoFillBackground(true);
}

void UIToolBoxPage::setExpanded(bool fVisible)
{
    if (m_pWidget)
        m_pWidget->setVisible(fVisible);
    m_fExpanded = fVisible;
    setExpandCollapseIcon();
}

int UIToolBoxPage::index() const
{
    return m_iIndex;
}

void UIToolBoxPage::setIndex(int iIndex)
{
    m_iIndex = iIndex;
}

int UIToolBoxPage::totalHeight() const
{
    return pageWidgetSize().height() + titleHeight();
}

void UIToolBoxPage::setTitleIcon(const QIcon &icon, const QString &strToolTip)
{
    if (!m_pIconLabel)
        return;
    if (icon.isNull())
    {
        m_pIconLabel->setPixmap(QPixmap());
        return;
    }
    const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
    const qreal fDevicePixelRatio = gpDesktop->devicePixelRatio(m_pIconLabel);
    m_pIconLabel->setPixmap(icon.pixmap(QSize(iIconMetric, iIconMetric), fDevicePixelRatio));
    m_pIconLabel->setToolTip(strToolTip);
}

int UIToolBoxPage::titleHeight() const
{
    if (m_pTitleContainerWidget && m_pTitleContainerWidget->sizeHint().isValid())
        return m_pTitleContainerWidget->sizeHint().height();
    return 0;
}

QSize UIToolBoxPage::pageWidgetSize() const
{
    if (m_pWidget && m_pWidget->sizeHint().isValid())
        return m_pWidget->sizeHint();
    return QSize();
}

bool UIToolBoxPage::eventFilter(QObject *pWatched, QEvent *pEvent)
{
    if (pWatched == m_pTitleContainerWidget)
    {
        if (pEvent->type() == QEvent::MouseButtonPress)
            emit sigShowPageWidget();
    }
    return QWidget::eventFilter(pWatched, pEvent);

}

void UIToolBoxPage::sltHandleEnableToggle(int iState)
{
    if (m_pWidget)
        m_pWidget->setEnabled(iState == Qt::Checked);
}

void UIToolBoxPage::setExpandCollapseIcon()
{
    if (!m_fExpandCollapseIconVisible)
    {
        m_pTitleButton->setVisible(false);
        return;
    }
    const int iMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
    const qreal fDevicePixelRatio = window() && window()->windowHandle() ? window()->windowHandle()->devicePixelRatio() : 1;
    QPixmap basePixmap = m_expandCollapseIcon.pixmap(QSize(iMetric, iMetric), fDevicePixelRatio);
    if (!m_fExpanded)
        m_pTitleButton->setPixmap(basePixmap);
    else
    {
        QTransform transform;
        transform.rotate(90);
        QPixmap transformedPixmap = basePixmap.transformed(transform);
        transformedPixmap.setDevicePixelRatio(basePixmap.devicePixelRatio());
        m_pTitleButton->setPixmap(transformedPixmap);
    }
}

void UIToolBoxPage::sltRetranslateUI()
{
    if (m_pTitleButton)
        m_pTitleButton->setToolTip(UIToolBox::tr("Expands the page \"%1\"").arg(m_strTitle.remove('&')));
}


/*********************************************************************************************************************************
*   UIToolBox implementation.                                                                                                    *
*********************************************************************************************************************************/

UIToolBox::UIToolBox(QWidget *pParent /*  = 0 */)
    : QFrame(pParent)
    , m_iCurrentPageIndex(-1)
    , m_iPageCount(0)
{
    prepare();
}

bool UIToolBox::insertPage(int iIndex, QWidget *pWidget, const QString &strTitle, bool fAddEnableCheckBox /* = false */)
{
    if (m_pages.contains(iIndex))
        return false;

    /* Remove the stretch from the end of the layout: */
    QLayoutItem *pItem = m_pMainLayout->takeAt(m_pMainLayout->count() - 1);
    delete pItem;

    ++m_iPageCount;
    UIToolBoxPage *pNewPage = new UIToolBoxPage(fAddEnableCheckBox, 0);;

    pNewPage->setWidget(pWidget);
    pNewPage->setIndex(iIndex);
    pNewPage->setTitle(strTitle);

    const QPalette pal = QApplication::palette();
    QColor tabBackgroundColor = pal.color(QPalette::Active, QPalette::Highlight).lighter(130);
    pNewPage->setTitleBackgroundColor(tabBackgroundColor);

    m_pages[iIndex] = pNewPage;
    m_pMainLayout->insertWidget(iIndex, pNewPage);

    connect(pNewPage, &UIToolBoxPage::sigShowPageWidget,
            this, &UIToolBox::sltHandleShowPageWidget);

    /* Add stretch at the end: */
    m_pMainLayout->addStretch(1);
    return iIndex;
}

QSize UIToolBox::minimumSizeHint() const
{

    int iMaxPageHeight = 0;
    int iTotalTitleHeight = 0;
    int iWidth = 0;
    foreach(UIToolBoxPage *pPage, m_pages)
    {
        QSize pageWidgetSize(pPage->pageWidgetSize());
        iMaxPageHeight = qMax(iMaxPageHeight, pageWidgetSize.height());
        iTotalTitleHeight += pPage->titleHeight();
        iWidth = qMax(pageWidgetSize.width(), iWidth);
    }
    int iHeight = m_iPageCount * (qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin) +
                                  qApp->style()->pixelMetric(QStyle::PM_LayoutBottomMargin)) +
        iTotalTitleHeight +
        iMaxPageHeight;
    return QSize(iWidth, iHeight);
}

void UIToolBox::setPageEnabled(int iIndex, bool fEnabled)
{
    m_pages.value(iIndex)->setEnabled(fEnabled);
}

void UIToolBox::setPageTitle(int iIndex, const QString &strTitle)
{
    QMap<int, UIToolBoxPage*>::iterator iterator = m_pages.find(iIndex);
    if (iterator == m_pages.end())
        return;
    iterator.value()->setTitle(strTitle);
}

void UIToolBox::setPageTitleIcon(int iIndex, const QIcon &icon, const QString &strIconToolTip /* = QString() */)
{
    QMap<int, UIToolBoxPage*>::iterator iterator = m_pages.find(iIndex);
    if (iterator == m_pages.end())
        return;
    iterator.value()->setTitleIcon(icon, strIconToolTip);
}

void UIToolBox::setCurrentPage(int iIndex)
{
    m_iCurrentPageIndex = iIndex;
    QMap<int, UIToolBoxPage*>::iterator iterator = m_pages.find(iIndex);
    if (iterator == m_pages.end())
        return;
    foreach(UIToolBoxPage *pPage, m_pages)
        pPage->setExpanded(false);

    iterator.value()->setExpanded(true);
}

void UIToolBox::prepare()
{
    m_pMainLayout = new QVBoxLayout(this);
    m_pMainLayout->addStretch();
}

void UIToolBox::sltHandleShowPageWidget()
{
    UIToolBoxPage *pPage = qobject_cast<UIToolBoxPage*>(sender());
    if (!pPage)
        return;
    setCurrentPage(pPage->index());
    update();
}

#include "UIToolBox.moc"
