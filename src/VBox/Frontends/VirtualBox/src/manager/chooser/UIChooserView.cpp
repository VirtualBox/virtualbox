/* $Id: UIChooserView.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooserView class implementation.
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
#include <QApplication>
#include <QAccessibleWidget>
#include <QScrollBar>

/* GUI includes: */
#include "UIChooserItem.h"
#include "UIChooserModel.h"
#include "UIChooserSearchWidget.h"
#include "UIChooserView.h"
#include "UICommon.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include <iprt/assert.h>

/** QAccessibleWidget extension used as an accessibility interface for Chooser-view. */
class UIAccessibilityInterfaceForUIChooserView : public QAccessibleWidget
{
public:

    /** Returns an accessibility interface for passed @a strClassname and @a pObject. */
    static QAccessibleInterface *pFactory(const QString &strClassname, QObject *pObject)
    {
        /* Creating Chooser-view accessibility interface: */
        if (pObject && strClassname == QLatin1String("UIChooserView"))
            return new UIAccessibilityInterfaceForUIChooserView(qobject_cast<QWidget*>(pObject));

        /* Null by default: */
        return 0;
    }

    /** Constructs an accessibility interface passing @a pWidget to the base-class. */
    UIAccessibilityInterfaceForUIChooserView(QWidget *pWidget)
        : QAccessibleWidget(pWidget, QAccessible::List)
    {}

    /** Returns the number of children. */
    virtual int childCount() const RT_OVERRIDE
    {
        /* Make sure view still alive: */
        AssertPtrReturn(view(), 0);

        /* Return the number of model children if model really assigned: */
        return view()->model() ? view()->model()->root()->items().size() : 0;
    }

    /** Returns the child with the passed @a iIndex. */
    virtual QAccessibleInterface *child(int iIndex) const RT_OVERRIDE
    {
        /* Make sure view still alive: */
        AssertPtrReturn(view(), 0);
        /* Make sure index is valid: */
        AssertReturn(iIndex >= 0 && iIndex < childCount(), 0);

        /* Return the model child with the passed iIndex if model really assigned: */
        return QAccessible::queryAccessibleInterface(view()->model() ? view()->model()->root()->items().at(iIndex) : 0);
    }

    /** Returns the index of passed @a pChild. */
    virtual int indexOfChild(const QAccessibleInterface *pChild) const RT_OVERRIDE
    {
        /* Make sure view still alive: */
        AssertPtrReturn(view(), -1);
        /* Make sure child is valid: */
        AssertReturn(pChild, -1);

        /* Acquire item itself: */
        UIChooserItem *pChildItem = qobject_cast<UIChooserItem*>(pChild->object());

        /* Return the index of item in it's parent: */
        return   pChildItem && pChildItem->parentItem()
               ? pChildItem->parentItem()->items().indexOf(pChildItem)
               : -1;;
    }

    /** Returns a text for the passed @a enmTextRole. */
    virtual QString text(QAccessible::Text enmTextRole) const RT_OVERRIDE
    {
        /* Make sure view still alive: */
        AssertPtrReturn(view(), QString());

        /* Return view tool-tip: */
        Q_UNUSED(enmTextRole);
        return view()->whatsThis();
    }

private:

    /** Returns corresponding Chooser-view. */
    UIChooserView *view() const { return qobject_cast<UIChooserView*>(widget()); }
};


UIChooserView::UIChooserView(QWidget *pParent)
    : QIGraphicsView(pParent)
    , m_pChooserModel(0)
    , m_pSearchWidget(0)
    , m_iMinimumWidthHint(0)
{
    prepare();
}

void UIChooserView::setModel(UIChooserModel *pChooserModel)
{
    m_pChooserModel = pChooserModel;
}

UIChooserModel *UIChooserView::model() const
{
    return m_pChooserModel;
}

bool UIChooserView::isSearchWidgetVisible() const
{
    /* Make sure search widget exists: */
    AssertPtrReturn(m_pSearchWidget, false);

    /* Return widget visibility state: */
    return m_pSearchWidget->isVisible();
}

void UIChooserView::setSearchWidgetVisible(bool fVisible)
{
    /* Make sure search widget exists: */
    AssertPtrReturnVoid(m_pSearchWidget);

    /* Make sure keyboard focus is managed correctly: */
    if (fVisible)
        m_pSearchWidget->setFocus();
    else
        setFocus();

    /* Make sure visibility state is really changed: */
    if (m_pSearchWidget->isVisible() == fVisible)
        return;

    /* Set widget visibility state: */
    m_pSearchWidget->setVisible(fVisible);

    /* Notify listeners: */
    emit sigSearchWidgetVisibilityChanged(fVisible);

    /* Update geometry if widget is visible: */
    if (m_pSearchWidget->isVisible())
        updateSearchWidgetGeometry();

    /* Reset search each time widget visibility changed,
     * Model can be undefined.. */
    if (model())
        model()->resetSearch();
}

void UIChooserView::setSearchResultsCount(int iTotalMatchCount, int iCurrentlyScrolledItemIndex)
{
    /* Make sure search widget exists: */
    AssertPtrReturnVoid(m_pSearchWidget);

    /* Update count of search results and scroll to certain result: */
    m_pSearchWidget->setMatchCount(iTotalMatchCount);
    m_pSearchWidget->setScroolToIndex(iCurrentlyScrolledItemIndex);
}

void UIChooserView::appendToSearchString(const QString &strSearchText)
{
    /* Make sure search widget exists: */
    AssertPtrReturnVoid(m_pSearchWidget);

    /* Update search string with passed text: */
    m_pSearchWidget->appendToSearchString(strSearchText);
}

void UIChooserView::redoSearch()
{
    /* Make sure search widget exists: */
    AssertPtrReturnVoid(m_pSearchWidget);

    /* Pass request to search widget: */
    m_pSearchWidget->redoSearch();
}

void UIChooserView::sltMinimumWidthHintChanged(int iHint)
{
    /* Is there something changed? */
    if (m_iMinimumWidthHint == iHint)
        return;

    /* Remember new value: */
    m_iMinimumWidthHint = iHint;

    /* Set minimum view width according passed width-hint: */
    setMinimumWidth(2 * frameWidth() + m_iMinimumWidthHint + verticalScrollBar()->sizeHint().width());

    /* Update scene rectangle: */
    updateSceneRect();
}

void UIChooserView::sltRedoSearch(const QString &strSearchTerm, int iSearchFlags)
{
    /* Model can be undefined: */
    if (!model())
        return;

    /* Perform search: */
    model()->performSearch(strSearchTerm, iSearchFlags);
}

void UIChooserView::sltHandleScrollToSearchResult(bool fNext)
{
    /* Model can be undefined: */
    if (!model())
        return;

    /* Move to requested search result: */
    model()->selectSearchResult(fNext);
}

void UIChooserView::sltHandleSearchWidgetVisibilityToggle(bool fVisible)
{
    setSearchWidgetVisible(fVisible);
}

void UIChooserView::sltRetranslateUI()
{
    /* Translate this: */
    setWhatsThis(tr("Contains a tree of Virtual Machines and their groups"));
}

void UIChooserView::resizeEvent(QResizeEvent *pEvent)
{
    /* Call to base-class: */
    QIGraphicsView::resizeEvent(pEvent);
    /* Notify listeners: */
    emit sigResized();

    /* Update everything: */
    updateSceneRect();
    updateSearchWidgetGeometry();
}

void UIChooserView::prepare()
{
    /* Install Chooser-view accessibility interface factory: */
    QAccessible::installFactory(UIAccessibilityInterfaceForUIChooserView::pFactory);

    /* Prepare everything: */
    prepareThis();
    prepareWidget();

    /* Update everything: */
    updateSceneRect();
    updateSearchWidgetGeometry();

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIChooserView::sltRetranslateUI);
}

void UIChooserView::prepareThis()
{
    /* Prepare palette: */
    preparePalette();

    /* Prepare frame: */
    setFrameShape(QFrame::NoFrame);
    setFrameShadow(QFrame::Plain);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);

    /* Prepare scroll-bars policy: */
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    /* Prepare connections: */
    connect(&uiCommon(), &UICommon::sigThemeChange,
            this, &UIChooserView::sltUpdatePalette);
}

void UIChooserView::preparePalette()
{
    QPalette pal = QApplication::palette();
    pal.setColor(QPalette::Active, QPalette::Base, pal.color(QPalette::Active, QPalette::Window));
    pal.setColor(QPalette::Inactive, QPalette::Base, pal.color(QPalette::Inactive, QPalette::Window));
    setPalette(pal);
}

void UIChooserView::prepareWidget()
{
    /* Create the search widget (initially hidden): */
    m_pSearchWidget = new UIChooserSearchWidget(this);
    if (m_pSearchWidget)
    {
        m_pSearchWidget->hide();
        connect(m_pSearchWidget, &UIChooserSearchWidget::sigRedoSearch,
                this, &UIChooserView::sltRedoSearch);
        connect(m_pSearchWidget, &UIChooserSearchWidget::sigScrollToMatch,
                this, &UIChooserView::sltHandleScrollToSearchResult);
        connect(m_pSearchWidget, &UIChooserSearchWidget::sigToggleVisibility,
                this, &UIChooserView::sltHandleSearchWidgetVisibilityToggle);
    }
}

void UIChooserView::updateSceneRect()
{
    setSceneRect(0, 0, m_iMinimumWidthHint, height());
}

void UIChooserView::updateSearchWidgetGeometry()
{
    /* Make sure search widget exists: */
    AssertPtrReturnVoid(m_pSearchWidget);

    /* Update visible widget only: */
    if (m_pSearchWidget->isVisible())
    {
        const int iHeight = m_pSearchWidget->height();
        m_pSearchWidget->setGeometry(QRect(0, height() - iHeight, width(), iHeight));
    }
}
