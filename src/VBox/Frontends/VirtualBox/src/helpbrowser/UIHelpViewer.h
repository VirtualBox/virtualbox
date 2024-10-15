/* $Id: UIHelpViewer.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIHelpViewer class declaration.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_helpbrowser_UIHelpViewer_h
#define FEQT_INCLUDED_SRC_helpbrowser_UIHelpViewer_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QTextBrowser>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QHelpEngine;
class QGraphicsBlurEffect;
class QLabel;
class UIFindInPageWidget;

/** A QTextBrowser extension used as poor man's html viewer. Since we were not happy with the quality of QTextBrowser's image
  * rendering and didn't want to use WebKit module, this extension redraws the document images as overlays with improved QPainter
  * parameters. There is also a small hack to render clicked image 1:1 (and the rest of the document blurred)
  * for a zoom-in-image functionality. This extension can also scale the images while scaling the document. In contrast,
  * QTextBrowser scales only fonts. */
class UIHelpViewer : public QTextBrowser
{

    Q_OBJECT;

public:

    enum ZoomOperation
    {
        ZoomOperation_In = 0,
        ZoomOperation_Out,
        ZoomOperation_Reset,
        ZoomOperation_Max
    };

signals:

    void sigOpenLinkInNewTab(const QUrl &url, bool fBackground);
    void sigFindInPageWidgetToogle(bool fVisible);
    void sigFontPointSizeChanged(int iFontPointSize);
    void sigGoBackward();
    void sigGoForward();
    void sigGoHome();
    void sigAddBookmark();
    void sigMouseOverImage(const QString &strImageName);
    void sigZoomRequest(ZoomOperation enmZoomOperation);

public:

    UIHelpViewer(const QHelpEngine *pHelpEngine, QWidget *pParent = 0);
    virtual QVariant loadResource(int type, const QUrl &name) RT_OVERRIDE;
    void emitHistoryChangedSignal();
    void setFont(const QFont &);
    bool isFindInPageWidgetVisible() const;
    void setZoomPercentage(int iZoomPercentage);
    void setHelpFileList(const QList<QUrl> &helpFileList);
    bool hasSelectedText() const;
    static const QPair<int, int> zoomPercentageMinMax;
    void toggleFindInPageWidget(bool fVisible);

public slots:

    void sltSelectPreviousMatch();
    void sltSelectNextMatch();
    virtual void reload() RT_OVERRIDE;

protected:

    virtual void contextMenuEvent(QContextMenuEvent *event) RT_OVERRIDE;
    virtual void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE;
    virtual void wheelEvent(QWheelEvent *pEvent) RT_OVERRIDE;
    virtual void mouseReleaseEvent(QMouseEvent *pEvent) RT_OVERRIDE;
    virtual void mousePressEvent(QMouseEvent *pEvent) RT_OVERRIDE;
    virtual void mouseMoveEvent(QMouseEvent *pEvent) RT_OVERRIDE;
    virtual void mouseDoubleClickEvent(QMouseEvent *pEvent) RT_OVERRIDE;
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE;
    virtual bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE;
    virtual void keyPressEvent(QKeyEvent *pEvent) RT_OVERRIDE;
    virtual void doSetSource(const QUrl &url, QTextDocument::ResourceType type = QTextDocument::UnknownResource) RT_OVERRIDE;

private slots:

    void sltOpenLinkInNewTab();
    void sltOpenLink();
    void sltCopyLink();
    void sltFindWidgetDrag(const QPoint &delta);
    void sltFindInPageSearchTextChange(const QString &strSearchText);
    void sltToggleFindInPageWidget(bool fVisible);
    void sltCloseFindInPageWidget();
    void sltUpdateHighlightedURL(const QUrl &url);

private:

    struct DocumentImage
    {
        qreal m_fInitialWidth;
        qreal m_fScaledWidth;
        QTextCursor m_textCursor;
        QPixmap m_pixmap;
        QString m_strName;
    };

    bool isRectInside(const QRect &rect, int iMargin) const;
    void moveFindWidgetIn(int iMargin);
    void findAllMatches(const QString &searchString);
    void highlightFinds(int iSearchTermLength);
    void selectMatch(int iMatchIndex, int iSearchStringLength);
    /** Scans the document and finds all the images, whose pixmap data is retrieved from QHelp system to be used in overlay draw. */
    void iterateDocumentImages();
    void scaleFont();
    void scaleImages();
    void loadImage(const QUrl &imageFileUrl);
    void clearOverlay();
    void enableOverlay();
    bool isImage(const QString &strLink);

    const QHelpEngine* m_pHelpEngine;
    UIFindInPageWidget *m_pFindInPageWidget;
    /** Initilized as false and set to true once the user drag moves the find widget. */
    bool m_fFindWidgetDragged;
    int m_iMarginForFindWidget;
    /** Document positions of the cursors within the document for all matches. */
    QVector<int>   m_matchedCursorPosition;
    int m_iSelectedMatchIndex;
    int m_iSearchTermLength;
    int m_iInitialFontPointSize;
    /** A container to store the original image sizes/positions in the document. key is image name value is DocumentImage. */
    QHash<QString, DocumentImage> m_imageMap;
    /** We need this list from th QHelp system to obtain information of images. */
    QList<QUrl> m_helpFileList;
    QPixmap m_overlayPixmap;
    bool m_fOverlayMode;
    QLabel *m_pOverlayLabel;
    QGraphicsBlurEffect *m_pOverlayBlurEffect;
    int m_iZoomPercentage;
    QUrl m_highlightedUrl;
};

#endif /* !FEQT_INCLUDED_SRC_helpbrowser_UIHelpViewer_h */
