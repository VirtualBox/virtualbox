/* $Id: UIPopupPane.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIPopupPane class declaration.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UIPopupPane_h
#define FEQT_INCLUDED_SRC_widgets_UIPopupPane_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMap>
#include <QWidget>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declaration: */
class QEvent;
class QObject;
class QPainter;
class QPaintEvent;
class QRect;
class QShowEvent;
class QSize;
class QString;
class QWidget;
class UIAnimation;
class UIPopupPaneDetails;
class UIPopupPaneMessage;
class UIPopupPaneButtonPane;

/** QWidget extension used as popup-center pane prototype. */
class SHARED_LIBRARY_STUFF UIPopupPane : public QWidget
{
    Q_OBJECT;
    Q_PROPERTY(QSize hiddenSizeHint READ hiddenSizeHint);
    Q_PROPERTY(QSize shownSizeHint READ shownSizeHint);
    Q_PROPERTY(QSize minimumSizeHint READ minimumSizeHint WRITE setMinimumSizeHint);
    Q_PROPERTY(int defaultOpacity READ defaultOpacity);
    Q_PROPERTY(int hoveredOpacity READ hoveredOpacity);
    Q_PROPERTY(int opacity READ opacity WRITE setOpacity);

signals:

    /** Asks to show itself asynchronously. */
    void sigToShow();
    /** Asks to hide itself asynchronously. */
    void sigToHide();
    /** Asks to show itself instantly. */
    void sigShow();
    /** Asks to hide itself instantly. */
    void sigHide();

    /** Notifies about hover enter. */
    void sigHoverEnter();
    /** Notifies about hover leave. */
    void sigHoverLeave();

    /** Notifies about focus enter. */
    void sigFocusEnter();
    /** Notifies about focus leave. */
    void sigFocusLeave();

    /** Proposes pane @a iWidth. */
    void sigProposePaneWidth(int iWidth);
    /** Proposes details pane @a iHeight. */
    void sigProposeDetailsPaneHeight(int iHeight);
    /** Notifies about size-hint changed. */
    void sigSizeHintChanged();

    /** Asks to close with @a iResultCode. */
    void sigDone(int iResultCode) const;

public:

    /** Constructs popup-pane.
      * @param  pParent             Brings the parent.
      * @param  strMessage          Brings the pane message.
      * @param  strDetails          Brings the pane details.
      * @param  buttonDescriptions  Brings the button descriptions. */
    UIPopupPane(QWidget *pParent,
                const QString &strMessage, const QString &strDetails,
                const QMap<int, QString> &buttonDescriptions);

    /** Recalls itself. */
    void recall();

    /** Defines the @a strMessage. */
    void setMessage(const QString &strMessage);
    /** Defines the @a strDetails. */
    void setDetails(const QString &strDetails);

    /** Returns minimum size-hint. */
    QSize minimumSizeHint() const RT_OVERRIDE { return m_minimumSizeHint; }
    /** Defines @a minimumSizeHint. */
    void setMinimumSizeHint(const QSize &minimumSizeHint);
    /** Lays the content out. */
    void layoutContent();

public slots:

    /** Handles proposal for a @a newSize. */
    void sltHandleProposalForSize(QSize newSize);

private slots:

    /** Marks pane as fully shown. */
    void sltMarkAsShown();

    /** Updates size-hint. */
    void sltUpdateSizeHint();

    /** Handles a click of button with @a iButtonID. */
    void sltButtonClicked(int iButtonID);

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** A pair of strings. */
    typedef QPair<QString, QString> QStringPair;
    /** A list of string pairs. */
    typedef QList<QStringPair> QStringPairList;

    /** Prepares all. */
    void prepare();
    /** Prepares background. */
    void prepareBackground();
    /** Prepares content. */
    void prepareContent();
    /** Prepares animation. */
    void prepareAnimation();

    /** Translats tool-tips. */
    void retranslateToolTips();

    /** Pre-handles standard Qt @a pEvent for passed @a pObject. */
    virtual bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE;

    /** Handles show @a pEvent. */
    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;
    /** Handles first show @a pEvent. */
    void polishEvent(QShowEvent *pEvent);

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE;

    /** Assigns clipping of @a rect geometry for passed @a painter. */
    void configureClipping(const QRect &rect, QPainter &painter);
    /** Paints background of @a rect geometry using @a painter. */
    void paintBackground(const QRect &rect, QPainter &painter);
    /** Paints frame of @a rect geometry using @a painter. */
    void paintFrame(QPainter &painter);

    /** Closes pane with @a iResultCode. */
    void done(int iResultCode);

    /** Returns size-hint in hidden state. */
    QSize hiddenSizeHint() const { return m_hiddenSizeHint; }
    /** Returns size-hint in shown state. */
    QSize shownSizeHint() const { return m_shownSizeHint; }

    /** Returns default opacity. */
    int defaultOpacity() const { return m_iDefaultOpacity; }
    /** Returns hovered opacity. */
    int hoveredOpacity() const { return m_iHoveredOpacity; }
    /** Returns current opacity. */
    int opacity() const { return m_iOpacity; }
    /** Defines current @a iOpacity. */
    void setOpacity(int iOpacity) { m_iOpacity = iOpacity; update(); }

    /** Returns details text. */
    QString prepareDetailsText() const;
    /** Prepares passed @a aDetailsList. */
    void prepareDetailsList(QStringPairList &aDetailsList) const;

    /** Holds whether the pane was polished. */
    bool m_fPolished;

    /** Holds the pane ID. */
    const QString m_strId;

    /** Holds the layout margin. */
    const int m_iLayoutMargin;
    /** Holds the layout spacing. */
    const int m_iLayoutSpacing;

    /** Holds the minimum size-hint. */
    QSize m_minimumSizeHint;

    /** Holds the pane message. */
    QString m_strMessage;
    /** Holds the pane details. */
    QString m_strDetails;

    /** Holds the button descriptions. */
    QMap<int, QString> m_buttonDescriptions;

    /** Holds whether the pane is shown fully. */
    bool         m_fShown;
    /** Holds the show/hide animation instance. */
    UIAnimation *m_pShowAnimation;
    /** Holds the size-hint of pane in hidden state. */
    QSize        m_hiddenSizeHint;
    /** Holds the size-hint of pane in shown state. */
    QSize        m_shownSizeHint;

    /** Holds whether the pane can loose focus. */
    bool m_fCanLooseFocus;
    /** Holds whether the pane is focused. */
    bool m_fFocused;

    /** Holds whether the pane is hovered. */
    bool      m_fHovered;
    /** Holds the default opacity. */
    const int m_iDefaultOpacity;
    /** Holds the hovered opacity. */
    const int m_iHoveredOpacity;
    /** Holds the current opacity. */
    int       m_iOpacity;

    /** Holds the message pane instance. */
    UIPopupPaneMessage    *m_pMessagePane;
    /** Holds the details pane instance. */
    UIPopupPaneDetails    *m_pDetailsPane;
    /** Holds the buttons pane instance. */
    UIPopupPaneButtonPane *m_pButtonPane;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UIPopupPane_h */
