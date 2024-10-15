/* $Id: UIMonitorCommon.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMonitorCommon class implementation.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
#include <QXmlStreamReader>


/* GUI includes: */
#include "UIMonitorCommon.h"

/* COM includes: */
#include "CForm.h"
#include "CFormValue.h"
#include "CRangedIntegerFormValue.h"
#include "CMachineDebugger.h"
#include "CPerformanceCollector.h"
#include <VBox/com/VirtualBox.h>


/*********************************************************************************************************************************
*   UIProgressTaskReadCloudMachineMetricList implementation.                                                                     *
*********************************************************************************************************************************/

UIProgressTaskReadCloudMachineMetricList::UIProgressTaskReadCloudMachineMetricList(QObject *pParent, CCloudMachine comCloudMachine)
    :UIProgressTask(pParent)
    , m_comCloudMachine(comCloudMachine)
{
}

CProgress UIProgressTaskReadCloudMachineMetricList::createProgress()
{
    if (!m_comCloudMachine.isOk())
        return CProgress();
    return m_comCloudMachine.ListMetricNames(m_metricNamesArray);
}

void UIProgressTaskReadCloudMachineMetricList::handleProgressFinished(CProgress &comProgress)
{
    if (!comProgress.isOk())
        return;
    emit sigMetricListReceived(m_metricNamesArray.GetValues());
}


/*********************************************************************************************************************************
*   UIProgressTaskReadCloudMachineMetricData implementation.                                                                     *
*********************************************************************************************************************************/

UIProgressTaskReadCloudMachineMetricData::UIProgressTaskReadCloudMachineMetricData(QObject *pParent,
                                                                                   CCloudMachine comCloudMachine,
                                                                                   KMetricType enmMetricType,
                                                                                   ULONG uDataPointsCount)
    :UIProgressTask(pParent)
    , m_comCloudMachine(comCloudMachine)
    , m_enmMetricType(enmMetricType)
    , m_uDataPointsCount(uDataPointsCount)
{
}

CProgress UIProgressTaskReadCloudMachineMetricData::createProgress()
{
    if (!m_comCloudMachine.isOk())
        return CProgress();

    CStringArray aUnit;
    return m_comCloudMachine.EnumerateMetricData(m_enmMetricType, m_uDataPointsCount, m_metricData, m_timeStamps, aUnit);
}


void UIProgressTaskReadCloudMachineMetricData::handleProgressFinished(CProgress &comProgress)
{
    if (!comProgress.isOk())
        return;
    if (m_metricData.isOk() && m_timeStamps.isOk())
        emit sigMetricDataReceived(m_enmMetricType, m_metricData.GetValues(), m_timeStamps.GetValues());
}

/* static */
void UIMonitorCommon::getNetworkLoad(CMachineDebugger &debugger, quint64 &uOutNetworkReceived, quint64 &uOutNetworkTransmitted)
{
    uOutNetworkReceived = 0;
    uOutNetworkTransmitted = 0;
    QVector<UIDebuggerMetricData> xmlData = getAndParseStatsFromDebugger(debugger, "/Public/NetAdapter/*/Bytes*");
    foreach (const UIDebuggerMetricData &data, xmlData)
    {
        if (data.m_strName.endsWith("BytesReceived"))
            uOutNetworkReceived += data.m_counter;
        else if (data.m_strName.endsWith("BytesTransmitted"))
            uOutNetworkTransmitted += data.m_counter;
        else
            AssertMsgFailed(("name=%s\n", data.m_strName.toLocal8Bit().data()));
    }
}

/* static */
void UIMonitorCommon::getDiskLoad(CMachineDebugger &debugger, quint64 &uOutDiskWritten, quint64 &uOutDiskRead)
{
    uOutDiskWritten = 0;
    uOutDiskRead = 0;
    QVector<UIDebuggerMetricData> xmlData = getAndParseStatsFromDebugger(debugger, "/Public/Storage/*/Port*/Bytes*");
    foreach (const UIDebuggerMetricData &data, xmlData)
    {
        if (data.m_strName.endsWith("BytesWritten"))
            uOutDiskWritten += data.m_counter;
        else if (data.m_strName.endsWith("BytesRead"))
            uOutDiskRead += data.m_counter;
        else
            AssertMsgFailed(("name=%s\n", data.m_strName.toLocal8Bit().data()));
    }
}

/* static */
void UIMonitorCommon::getVMMExitCount(CMachineDebugger &debugger, quint64 &uOutVMMExitCount)
{
    uOutVMMExitCount = 0;
    QVector<UIDebuggerMetricData> xmlData = getAndParseStatsFromDebugger(debugger, "/PROF/CPU*/EM/RecordedExits");
    foreach (const UIDebuggerMetricData &data, xmlData)
    {
        if (data.m_strName.endsWith("RecordedExits"))
            uOutVMMExitCount += data.m_counter;
        else
            AssertMsgFailed(("name=%s\n", data.m_strName.toLocal8Bit().data()));
    }
}


/* static */
QVector<UIDebuggerMetricData> UIMonitorCommon::getAndParseStatsFromDebugger(CMachineDebugger &debugger, const QString &strQuery)
{
    QVector<UIDebuggerMetricData> xmlData;
    if (strQuery.isEmpty())
        return xmlData;
    QString strStats = debugger.GetStats(strQuery, false);
    QXmlStreamReader xmlReader;
    xmlReader.addData(strStats);
    if (xmlReader.readNextStartElement())
    {
        while (xmlReader.readNextStartElement())
        {
            if (xmlReader.name() == QLatin1String("Counter"))
            {
                QXmlStreamAttributes attributes = xmlReader.attributes();
                quint64 iCounter = attributes.value("c").toULongLong();
                xmlData.push_back(UIDebuggerMetricData(attributes.value("name").toString(), iCounter));
            }
            else if (xmlReader.name() == QLatin1String("U64"))
            {
                QXmlStreamAttributes attributes = xmlReader.attributes();
                quint64 iCounter = attributes.value("val").toULongLong();
                xmlData.push_back(UIDebuggerMetricData(attributes.value("name").toString(), iCounter));
            }
            xmlReader.skipCurrentElement();
        }
    }
    return xmlData;
}

/* static */
void UIMonitorCommon::getRAMLoad(CPerformanceCollector &comPerformanceCollector, QVector<QString> &nameList,
                                 QVector<CUnknown>& objectList, quint64 &iOutTotalRAM, quint64 &iOutFreeRAM)
{
    iOutTotalRAM = 0;
    iOutFreeRAM = 0;
    QVector<QString>  aReturnNames;
    QVector<CUnknown>  aReturnObjects;
    QVector<QString>  aReturnUnits;
    QVector<ULONG>  aReturnScales;
    QVector<ULONG>  aReturnSequenceNumbers;
    QVector<ULONG>  aReturnDataIndices;
    QVector<ULONG>  aReturnDataLengths;
    /* Make a query to CPerformanceCollector to fetch some metrics (e.g RAM usage): */
    QVector<LONG> returnData = comPerformanceCollector.QueryMetricsData(nameList,
                                                                        objectList,
                                                                        aReturnNames,
                                                                        aReturnObjects,
                                                                        aReturnUnits,
                                                                        aReturnScales,
                                                                        aReturnSequenceNumbers,
                                                                        aReturnDataIndices,
                                                                        aReturnDataLengths);
    /* Parse the result we get from CPerformanceCollector to get respective values: */
    for (int i = 0; i < aReturnNames.size(); ++i)
    {
        if (aReturnDataLengths[i] == 0)
            continue;
        /* Read the last of the return data disregarding the rest since we are caching the data in GUI side: */
        float fData = returnData[aReturnDataIndices[i] + aReturnDataLengths[i] - 1] / (float)aReturnScales[i];
        if (aReturnNames[i].contains("RAM", Qt::CaseInsensitive) && !aReturnNames[i].contains(":"))
        {
            if (aReturnNames[i].contains("Total", Qt::CaseInsensitive))
                iOutTotalRAM = (quint64)fData;
            if (aReturnNames[i].contains("Free", Qt::CaseInsensitive))
                iOutFreeRAM = (quint64)fData;
        }
    }
}

/* static */
QPainterPath UIMonitorCommon::doughnutSlice(const QRectF &outerRectangle, const QRectF &innerRectangle, float fStartAngle, float fSweepAngle)
{
    QPainterPath subPath1;
    subPath1.moveTo(outerRectangle.center());
    subPath1.arcTo(outerRectangle, fStartAngle,
                   -1.f * fSweepAngle);
    subPath1.closeSubpath();

    QPainterPath subPath2;
    subPath2.moveTo(innerRectangle.center());
    subPath2.arcTo(innerRectangle, fStartAngle,
                   -1.f * fSweepAngle);
    subPath2.closeSubpath();

    return subPath1.subtracted(subPath2);
}

/* static */
QPainterPath UIMonitorCommon::wholeArc(const QRectF &rectangle)
{
    QPainterPath arc;
    arc.addEllipse(rectangle);
    return arc;
}

/* static */
void UIMonitorCommon::drawCombinedDoughnutChart(quint64 data1, const QColor &data1Color,
                               quint64 data2, const QColor &data2Color,
                               QPainter &painter, quint64  iMaximum,
                               const QRectF &chartRect, const QRectF &innerRect, int iOverlayAlpha)
{
    (void)data2;
    (void)data2Color;
    (void)iOverlayAlpha;
    /* Draw two arcs. one for the inner the other for the outer circle: */
    painter.setPen(QPen(QColor(100, 100, 100, iOverlayAlpha), 1));
    painter.drawArc(chartRect, 0, 3600 * 16);
    painter.drawArc(innerRect, 0, 3600 * 16);

    /* Draw a translucent white background: */
    QPainterPath background = wholeArc(chartRect).subtracted(wholeArc(innerRect));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, iOverlayAlpha));
    painter.drawPath(background);

    /* Draw a doughnut slice for the first data series: */
    float fAngle = 360.f * data1 / (float)iMaximum;
    painter.setBrush(data1Color);
    painter.drawPath(doughnutSlice(chartRect, innerRect, 90, fAngle));

    float fAngle2 = 360.f * data2 / (float)iMaximum;
    painter.setBrush(data2Color);
    painter.drawPath(doughnutSlice(chartRect, innerRect, 90 - fAngle, fAngle2));
}

/* static */
QRectF UIMonitorCommon::getScaledRect(const QRectF &outerFrame, float fScaleX, float fScaleY)
{
    if (!outerFrame.isValid())
        return QRectF();
    QPointF center = outerFrame.center();
    float iWidth = fScaleX * outerFrame.width();
    float iHeight = fScaleY * outerFrame.height();
    return QRectF(QPointF(center.x() - 0.5 * iWidth, center.y() - 0.5 * iHeight),
                 QSizeF(iWidth, iHeight));
}

/* static */
void UIMonitorCommon::drawDoughnutChart(QPainter &painter, quint64 iMaximum, quint64 data,
                                        const QRectF &chartRect, const QRectF &innerRect, int iOverlayAlpha, const QColor &color)
{
    /* Draw a whole non-filled circle: */
    painter.setPen(QPen(QColor(100, 100, 100, iOverlayAlpha), 1));
    painter.drawArc(chartRect, 0, 3600 * 16);
    painter.drawArc(innerRect, 0, 3600 * 16);

    /* Draw a white filled circle and the arc for data: */
    QPainterPath background = UIMonitorCommon::wholeArc(chartRect).subtracted(UIMonitorCommon::wholeArc(innerRect));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, iOverlayAlpha));
    painter.drawPath(background);

    /* Draw the doughnut slice for the data: */
    float fAngle = 360.f * data / (float)iMaximum;
    painter.setBrush(color);
    painter.drawPath(UIMonitorCommon::doughnutSlice(chartRect, innerRect, 90, fAngle));
}

/* static */
quint64 UIMonitorCommon::determineTotalRAMAmount(CCloudMachine &comCloudMachine)
{
    quint64 iTotalRAM = 0;
    CForm comForm = comCloudMachine.GetDetailsForm();
    /* Ignore cloud machine errors: */
    if (comCloudMachine.isOk())
    {
        /* Common anchor for all fields: */
        const QString strAnchorType = "cloud";

        /* For each form value: */
        const QVector<CFormValue> values = comForm.GetValues();
        foreach (const CFormValue &comIteratedValue, values)
        {
            /* Ignore invisible values: */
            if (!comIteratedValue.GetVisible())
                continue;

            /* Acquire label: */
            const QString strLabel = comIteratedValue.GetLabel();
            if (strLabel != "RAM")
                continue;

            AssertReturn((comIteratedValue.GetType() == KFormValueType_RangedInteger), 0);

            CRangedIntegerFormValue comValue(comIteratedValue);
            iTotalRAM = comValue.GetInteger();
            QString strRAMUnit = comValue.GetSuffix();
            if (strRAMUnit.compare("gb", Qt::CaseInsensitive) == 0)
                iTotalRAM *= _1G / _1K;
            else if (strRAMUnit.compare("mb", Qt::CaseInsensitive) == 0)
                iTotalRAM *= _1M / _1K;
            if (!comValue.isOk())
                iTotalRAM = 0;
        }
    }
    return iTotalRAM;
}
