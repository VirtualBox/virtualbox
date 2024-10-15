/* $Id: QIAdvancedSlider.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIAdvancedSlider class implementation.
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
#include <QPainter>
#include <QSlider>
#include <QStyle>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIAdvancedSlider.h"

/* Qt includes: */
#include <QStyleOptionSlider>

/* External includes: */
#include <math.h>


/** QSlider subclass for our private needs. */
class UIPrivateSlider : public QSlider
{
    Q_OBJECT;

public:

    /** Constructs private-slider passing @a pParent and @a enmOrientation to the base-class. */
    UIPrivateSlider(Qt::Orientation enmOrientation, QWidget *pParent = 0);

    /** Returns suitable position for @a iValue. */
    int positionForValue(int iValue) const;

    /** @todo encapsulate below stuff accordingly.. */

    /** Holds the minimum optimal border. */
    int m_minOpt;
    /** Holds the maximum optimal border. */
    int m_maxOpt;
    /** Holds the minimum warning border. */
    int m_minWrn;
    /** Holds the maximum warning border. */
    int m_maxWrn;
    /** Holds the minimum error border. */
    int m_minErr;
    /** Holds the maximum error border. */
    int m_maxErr;

protected:

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE RT_FINAL;

private:

    /** Holds the optimal color. */
    QColor m_optColor;
    /** Holds the warning color. */
    QColor m_wrnColor;
    /** Holds the error color. */
    QColor m_errColor;
};


/*********************************************************************************************************************************
*   Class UIPrivateSlider implementation.                                                                                        *
*********************************************************************************************************************************/

UIPrivateSlider::UIPrivateSlider(Qt::Orientation enmOrientation, QWidget *pParent /* = 0 */)
    : QSlider(enmOrientation, pParent)
    , m_minOpt(-1)
    , m_maxOpt(-1)
    , m_minWrn(-1)
    , m_maxWrn(-1)
    , m_minErr(-1)
    , m_maxErr(-1)
    , m_optColor(0x0, 0xff, 0x0, 0x3c)
    , m_wrnColor(0xff, 0x54, 0x0, 0x3c)
    , m_errColor(0xff, 0x0, 0x0, 0x3c)
{
    /* Make sure ticks *always* positioned below: */
    setTickPosition(QSlider::TicksBelow);
}

int UIPrivateSlider::positionForValue(int iValue) const
{
    QStyleOptionSlider opt;
    initStyleOption(&opt);
    opt.subControls = QStyle::SC_All;
    int iAvailable = opt.rect.width() - style()->pixelMetric(QStyle::PM_SliderLength, &opt, this);
    return QStyle::sliderPositionFromValue(opt.minimum, opt.maximum, iValue, iAvailable);
}

void UIPrivateSlider::paintEvent(QPaintEvent *pEvent)
{
    QPainter p(this);

    QStyleOptionSlider opt;
    initStyleOption(&opt);
    opt.subControls = QStyle::SC_All;

    int iAvailable = opt.rect.width() - style()->pixelMetric(QStyle::PM_SliderLength, &opt, this);
    QSize s = size();

    /* We want to acquire SC_SliderTickmarks sub-control rectangle
     * and fill it with necessary background colors: */
#ifdef VBOX_WS_MAC
    // WORKAROUND:
    // Under MacOS X SC_SliderTickmarks is not fully reliable
    // source of the information we need, providing us with incorrect width.
    // So we have to calculate tickmarks rectangle ourself.
    QRect ticks = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderTickmarks, this);
    ticks.setRect((s.width() - iAvailable) / 2, s.height() - ticks.y(), iAvailable, ticks.height());
#else /* VBOX_WS_MAC */
    // WORKAROUND:
    // Under Windows SC_SliderTickmarks is fully unreliable
    // source of the information we need, providing us with empty rectangle.
    // Under X11 SC_SliderTickmarks is not fully reliable
    // source of the information we need, providing us with different rectangles
    // (correct or incorrect) under different look&feel styles.
    // So we have to calculate tickmarks rectangle ourself.
    QRect ticks = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this) |
                  style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
    ticks.setRect((s.width() - iAvailable) / 2, ticks.bottom() + 1, iAvailable, s.height() - ticks.bottom() - 1);
#endif /* VBOX_WS_MAC */

    if ((m_minOpt != -1 &&
         m_maxOpt != -1) &&
        m_minOpt != m_maxOpt)
    {
        int iPosMinOpt = QStyle::sliderPositionFromValue(opt.minimum, opt.maximum, m_minOpt, iAvailable);
        int iPosMaxOpt = QStyle::sliderPositionFromValue(opt.minimum, opt.maximum, m_maxOpt, iAvailable);
        p.fillRect(ticks.x() + iPosMinOpt, ticks.y(), iPosMaxOpt - iPosMinOpt + 1, ticks.height(), m_optColor);
    }
    if ((m_minWrn != -1 &&
         m_maxWrn != -1) &&
        m_minWrn != m_maxWrn)
    {
        int iPosMinWrn = QStyle::sliderPositionFromValue(opt.minimum, opt.maximum, m_minWrn, iAvailable);
        int iPosMaxWrn = QStyle::sliderPositionFromValue(opt.minimum, opt.maximum, m_maxWrn, iAvailable);
        p.fillRect(ticks.x() + iPosMinWrn, ticks.y(), iPosMaxWrn - iPosMinWrn + 1, ticks.height(), m_wrnColor);
    }
    if ((m_minErr != -1 &&
         m_maxErr != -1) &&
        m_minErr != m_maxErr)
    {
        int iPosMinErr = QStyle::sliderPositionFromValue(opt.minimum, opt.maximum, m_minErr, iAvailable);
        int iPosMaxErr = QStyle::sliderPositionFromValue(opt.minimum, opt.maximum, m_maxErr, iAvailable);
        p.fillRect(ticks.x() + iPosMinErr, ticks.y(), iPosMaxErr - iPosMinErr + 1, ticks.height(), m_errColor);
    }
    p.end();

    /* Call to base-class: */
    QSlider::paintEvent(pEvent);
}


/*********************************************************************************************************************************
*   Class QIAdvancedSlider implementation.                                                                                       *
*********************************************************************************************************************************/

QIAdvancedSlider::QIAdvancedSlider(QWidget *pParent /* = 0 */)
  : QWidget(pParent)
{
    prepare();
}

QIAdvancedSlider::QIAdvancedSlider(Qt::Orientation enmOrientation, QWidget *pParent /* = 0 */)
  : QWidget(pParent)
{
    prepare(enmOrientation);
}

int QIAdvancedSlider::value() const
{
    return m_pSlider->value();
}

void QIAdvancedSlider::setRange(int iMin, int iMax)
{
    m_pSlider->setRange(iMin, iMax);
}

void QIAdvancedSlider::setMaximum(int iValue)
{
    m_pSlider->setMaximum(iValue);
}

int QIAdvancedSlider::maximum() const
{
    return m_pSlider->maximum();
}

void QIAdvancedSlider::setMinimum(int iValue)
{
    m_pSlider->setMinimum(iValue);
}

int QIAdvancedSlider::minimum() const
{
    return m_pSlider->minimum();
}

void QIAdvancedSlider::setPageStep(int iValue)
{
    m_pSlider->setPageStep(iValue);
}

int QIAdvancedSlider::pageStep() const
{
    return m_pSlider->pageStep();
}

void QIAdvancedSlider::setSingleStep(int iValue)
{
    m_pSlider->setSingleStep(iValue);
}

int QIAdvancedSlider::singelStep() const
{
    return m_pSlider->singleStep();
}

void QIAdvancedSlider::setTickInterval(int iValue)
{
    m_pSlider->setTickInterval(iValue);
}

int QIAdvancedSlider::tickInterval() const
{
    return m_pSlider->tickInterval();
}

Qt::Orientation QIAdvancedSlider::orientation() const
{
    return m_pSlider->orientation();
}

void QIAdvancedSlider::setSnappingEnabled(bool fOn)
{
    m_fSnappingEnabled = fOn;
}

bool QIAdvancedSlider::isSnappingEnabled() const
{
    return m_fSnappingEnabled;
}

void QIAdvancedSlider::setOptimalHint(int iMin, int iMax)
{
    m_pSlider->m_minOpt = iMin;
    m_pSlider->m_maxOpt = iMax;

    update();
}

void QIAdvancedSlider::setWarningHint(int iMin, int iMax)
{
    m_pSlider->m_minWrn = iMin;
    m_pSlider->m_maxWrn = iMax;

    update();
}

void QIAdvancedSlider::setErrorHint(int iMin, int iMax)
{
    m_pSlider->m_minErr = iMin;
    m_pSlider->m_maxErr = iMax;

    update();
}

void QIAdvancedSlider::setToolTip(const QString &strToolTip)
{
    m_pSlider->setToolTip(strToolTip);
}

void QIAdvancedSlider::setOrientation(Qt::Orientation enmOrientation)
{
    m_pSlider->setOrientation(enmOrientation);
}

void QIAdvancedSlider::setValue (int iValue)
{
    m_pSlider->setValue(iValue);
}

void QIAdvancedSlider::sltSliderMoved(int iValue)
{
    iValue = snapValue(iValue);
    m_pSlider->setValue(iValue);
    emit sliderMoved(iValue);
}

void QIAdvancedSlider::prepare(Qt::Orientation enmOrientation /* = Qt::Horizontal */)
{
    m_fSnappingEnabled = false;

    /* Create layout: */
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    if (pMainLayout)
    {
        /* Configure layout: */
        pMainLayout->setContentsMargins(0, 0, 0, 0);

        /* Create private-slider: */
        m_pSlider = new UIPrivateSlider(enmOrientation, this);
        if (m_pSlider)
        {
            connect(m_pSlider, &UIPrivateSlider::sliderMoved,    this, &QIAdvancedSlider::sltSliderMoved);
            connect(m_pSlider, &UIPrivateSlider::valueChanged,   this, &QIAdvancedSlider::valueChanged);
            connect(m_pSlider, &UIPrivateSlider::sliderPressed,  this, &QIAdvancedSlider::sliderPressed);
            connect(m_pSlider, &UIPrivateSlider::sliderReleased, this, &QIAdvancedSlider::sliderReleased);

            /* Add into layout: */
            pMainLayout->addWidget(m_pSlider);
        }
    }
}

int QIAdvancedSlider::snapValue(int iValue)
{
    if (m_fSnappingEnabled &&
        iValue > 2)
    {
        float l2 = log((float)iValue)/log(2.0);
        int iNewVal = (int)pow((float)2, (int)qRound(l2)); /* The value to snap on */
        int iPos = m_pSlider->positionForValue(iValue); /* Get the relative screen pos for the original value */
        int iNewPos = m_pSlider->positionForValue(iNewVal); /* Get the relative screen pos for the snap value */
        if (abs(iNewPos - iPos) < 5) /* 10 pixel snapping range */
        {
            iValue = iNewVal;
            if (iValue > m_pSlider->maximum())
                iValue = m_pSlider->maximum();
            else if (iValue < m_pSlider->minimum())
                iValue = m_pSlider->minimum();
        }
    }
    return iValue;
}


#include "QIAdvancedSlider.moc"
