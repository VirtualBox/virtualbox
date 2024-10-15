/* $Id: UIImageTools.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Implementation of utility classes and functions for image manipulation.
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

/* Qt includes: */
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPalette>

/* GUI include */
#include "UIDesktopWidgetWatchdog.h"
#include "UIImageTools.h"

/* External includes: */
#include <math.h>


QImage UIImageTools::toGray(const QImage &image)
{
    QImage result = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < result.height(); ++y)
    {
        QRgb *pScanLine = (QRgb*)result.scanLine(y);
        for (int x = 0; x < result.width(); ++x)
        {
            const int g = qGray(pScanLine[x]);
            pScanLine[x] = qRgba(g, g, g, qAlpha(pScanLine[x]));
        }
    }
    return result;
}

void UIImageTools::dimImage(QImage &image)
{
    for (int y = 0; y < image.height(); ++y)
    {
        QRgb *pScanLine = (QRgb*)image.scanLine(y);
        if (y % 2)
        {
            if (image.depth() == 32)
            {
                for (int x = 0; x < image.width(); ++x)
                {
                    const int iGray = qGray(pScanLine[x]) / 2;
                    pScanLine[x] = qRgba(iGray, iGray, iGray, qAlpha(pScanLine[x]));
                }
            }
            else
                ::memset(pScanLine, 0, image.bytesPerLine());
        }
        else
        {
            if (image.depth() == 32)
            {
                for (int x = 0; x < image.width(); ++x)
                {
                    const int iGray = (2 * qGray(pScanLine[x])) / 3;
                    pScanLine[x] = qRgba(iGray, iGray, iGray, qAlpha(pScanLine[x]));
                }
            }
        }
    }
}

void UIImageTools::blurImage(const QImage &source, QImage &destination, int iRadius)
{
    /* Blur in two steps, first horizontal and then vertical: */
    QImage tmpImage(source.size(), QImage::Format_ARGB32);
    blurImageHorizontal(source, tmpImage, iRadius);
    blurImageVertical(tmpImage, destination, iRadius);
}

void UIImageTools::blurImageHorizontal(const QImage &source, QImage &destination, int iRadius)
{
    QSize s = source.size();
    for (int y = 0; y < s.height(); ++y)
    {
        int rt = 0;
        int gt = 0;
        int bt = 0;
        int at = 0;

        /* In the horizontal case we can just use the scanline, which is
         * much faster than accessing every pixel with the QImage::pixel
         * method. Unfortunately this doesn't work in the vertical case. */
        QRgb *ssl = (QRgb*)source.scanLine(y);
        QRgb *dsl = (QRgb*)destination.scanLine(y);
        /* First process the horizontal zero line at once: */
        int b = iRadius + 1;
        for (int x1 = 0; x1 <= iRadius; ++x1)
        {
            QRgb rgba = ssl[x1];
            rt += qRed(rgba);
            gt += qGreen(rgba);
            bt += qBlue(rgba);
            at += qAlpha(rgba);
        }
        /* Set the new weighted pixel: */
        dsl[0] = qRgba(rt / b, gt / b, bt / b, at / b);

        /* Now process the rest */
        for (int x = 1; x < s.width(); ++x)
        {
            /* Subtract the pixel which fall out of our blur matrix: */
            int x1 = x - iRadius - 1;
            if (x1 >= 0)
            {
                /* Adjust the weight (necessary for the border case): */
                --b;
                QRgb rgba = ssl[x1];
                rt -= qRed(rgba);
                gt -= qGreen(rgba);
                bt -= qBlue(rgba);
                at -= qAlpha(rgba);
            }

            /* Add the pixel which get into our blur matrix: */
            int x2 = x + iRadius;
            if (x2 < s.width())
            {
                /* Adjust the weight (necessary for the border case): */
                ++b;
                QRgb rgba = ssl[x2];
                rt += qRed(rgba);
                gt += qGreen(rgba);
                bt += qBlue(rgba);
                at += qAlpha(rgba);
            }
            /* Set the new weighted pixel: */
            dsl[x] = qRgba(rt / b, gt / b, bt / b, at / b);
        }
    }
}

void UIImageTools::blurImageVertical(const QImage &source, QImage &destination, int iRadius)
{
    QSize s = source.size();
    destination = QImage(s, source.format());
    for (int x = 0; x < s.width(); ++x)
    {
        int rt = 0;
        int gt = 0;
        int bt = 0;
        int at = 0;

        /* First process the vertical zero line at once: */
        int b = iRadius + 1;
        for (int y1 = 0; y1 <= iRadius; ++y1)
        {
            QRgb rgba = source.pixel(x, y1);
            rt += qRed(rgba);
            gt += qGreen(rgba);
            bt += qBlue(rgba);
            at += qAlpha(rgba);
        }
        /* Set the new weighted pixel: */
        destination.setPixel(x, 0, qRgba(rt / b, gt / b, bt / b, at / b));

        /* Now process the rest: */
        for (int y = 1; y < s.height(); ++y)
        {
            /* Subtract the pixel which fall out of our blur matrix: */
            int y1 = y - iRadius - 1;
            if (y1 >= 0)
            {
                --b; /* Adjust the weight (necessary for the border case): */
                QRgb rgba = source.pixel(x, y1);
                rt -= qRed(rgba);
                gt -= qGreen(rgba);
                bt -= qBlue(rgba);
                at -= qAlpha(rgba);
            }

            /* Add the pixel which get into our blur matrix: */
            int y2 = y + iRadius;
            if (y2 < s.height())
            {
                ++b; /* Adjust the weight (necessary for the border case): */
                QRgb rgba = source.pixel(x, y2);
                rt += qRed(rgba);
                gt += qGreen(rgba);
                bt += qBlue(rgba);
                at += qAlpha(rgba);
            }
            /* Set the new weighted pixel: */
            destination.setPixel(x, y, qRgba(rt / b, gt / b, bt / b, at / b));
        }
    }
}

static QImage betaLabelImage(QSize size, QWidget *pHint)
{
    /* Calculate device pixel ratio: */
    const double dDpr = pHint ? UIDesktopWidgetWatchdog::devicePixelRatio(pHint) : UIDesktopWidgetWatchdog::devicePixelRatio(-1);
    if (dDpr > 1.0)
        size *= dDpr;

    /* Beta label: */
    QColor bgc(246, 179, 0);
    QImage i(size, QImage::Format_ARGB32);
    i.fill(Qt::transparent);
    QPainter p(&i);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    p.setPen(Qt::NoPen);

    /* Background: */
    p.setBrush(bgc);
    p.drawRect(0, 0, size.width(), size.height());

    /* The black stripes: */
    p.setPen(QPen(QColor(70, 70, 70), 5));
    float c = ((float)size.width() / size.height()) + 1;
    float g = (size.width() / (c - 1));
    for (int i = 0; i < c; ++i)
        p.drawLine((int)(-g / 2 + g * i), size.height(), (int)(-g / 2 + g * (i + 1)), 0);

    /* The text: */
    QFont f = p.font();
    if (dDpr > 1.0)
        f.setPointSize(f.pointSize() * dDpr);
    f.setBold(true);
    QPainterPath tp;
    tp.addText(0, 0, f, "BETA");
    QRectF r = tp.boundingRect();

    /* Center the text path: */
    p.translate((size.width() - r.width()) / 2, size.height() - (size.height() - r.height()) / 2);
    QPainterPathStroker pps;
    QPainterPath pp = pps.createStroke(tp);
    p.setPen(QPen(bgc.darker(80), 2, Qt::SolidLine, Qt::RoundCap));
    p.drawPath(pp);
    p.setBrush(Qt::black);
    p.setPen(Qt::NoPen);
    p.drawPath(tp);
    p.end();

    /* Smoothing: */
    QImage i1(size, QImage::Format_ARGB32);
    i1.fill(Qt::transparent);
    QPainter p1(&i1);
    p1.setCompositionMode(QPainter::CompositionMode_Source);
    p1.drawImage(0, 0, i);
    p1.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    QLinearGradient lg(0, 0, size.width(), 0);
    lg.setColorAt(0, QColor(Qt::transparent));
    lg.setColorAt(0.20, QColor(Qt::white));
    lg.setColorAt(0.80, QColor(Qt::white));
    lg.setColorAt(1, QColor(Qt::transparent));
    p1.fillRect(0, 0, size.width(), size.height(), lg);
    p1.end();
    if (dDpr > 1.0)
        i1.setDevicePixelRatio(dDpr);

    return i1;
}

QPixmap UIImageTools::betaLabel(const QSize &size /* = QSize(80, 16) */, QWidget *pHint /* = 0 */)
{
    return QPixmap::fromImage(betaLabelImage(size, pHint));
}

QColor UIImageTools::suitableForegroundColor(const QPalette &pal, const QColor &background)
{
    /* Get possible foreground colors: */
    const QColor simpleText = pal.color(QPalette::Active, QPalette::Text);
    const QColor highlightText = pal.color(QPalette::Active, QPalette::HighlightedText);
    QColor lightText = simpleText.black() < highlightText.black() ? simpleText : highlightText;
    QColor darkText = simpleText.black() > highlightText.black() ? simpleText : highlightText;
    if (lightText.black() > 128)
        lightText = QColor(Qt::white);
    if (darkText.black() < 128)
        darkText = QColor(Qt::black);

    /* Measure background luminance: */
    double dLuminance = (0.299 * background.red() + 0.587 * background.green() + 0.114 * background.blue()) / 255;
    //printf("luminance = %f\n", dLuminance);

    /* Gather foreground color for background one: */
    return dLuminance > 0.5 ? darkText : lightText;
}
