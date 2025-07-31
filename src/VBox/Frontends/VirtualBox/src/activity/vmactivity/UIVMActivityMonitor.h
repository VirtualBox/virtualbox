/* $Id: UIVMActivityMonitor.h 110344 2025-07-22 07:46:07Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVMActivityMonitor class declaration.
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

#ifndef FEQT_INCLUDED_SRC_activity_vmactivity_UIVMActivityMonitor_h
#define FEQT_INCLUDED_SRC_activity_vmactivity_UIVMActivityMonitor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QColor>
#include <QWidget>
#include <QMap>
#include <QQueue>
#include <QTextStream>
#include <QHash>

/* COM includes: */
#include "CCloudMachine.h"
#include "CConsole.h"
#include "CEventListener.h"
#include "CGuest.h"
#include "CMachine.h"
#include "CMachineDebugger.h"
#include "CPerformanceCollector.h"
#include "CSession.h"

/* GUI includes: */
#include "QIManagerDialog.h"
#include "UIMainEventListener.h"
#include "UIMonitorCommon.h"

/* Forward declarations: */
class QGridLayout;
class QTimer;
class QVBoxLayout;
class QLabel;
class UIActionPool;
class UIChart;
class UISession;
class UIRuntimeInfoWidget;
class UIProgressTaskReadCloudMachineMetricList;

#define DATA_SERIES_SIZE 2

enum Metric_Type
{
    Metric_Type_CPU = 0,
    Metric_Type_RAM,
    Metric_Type_Disk_InOut,
    Metric_Type_Disk_In,
    Metric_Type_Disk_Out,
    Metric_Type_Network_InOut,
    Metric_Type_Network_In,
    Metric_Type_Network_Out,
    Metric_Type_VM_Exits,
    Metric_Type_Max
};

/** UIMetric represents a performance metric and is used to store data related to the corresponding metric. */
class UIMetric
{

public:

    UIMetric(const QString &strUnit, int iMaximumQueueSize);
    UIMetric();

    void setMaximum(quint64 iMaximum);
    quint64 maximum() const;

    void setUnit(QString strUnit);
    const QString &unit() const;

    void addData(int iDataSeriesIndex, quint64 fData);
    void addData(int iDataSeriesIndex, quint64 fData, const QString &strLabel);

    const QQueue<quint64> *data(int iDataSeriesIndex) const;
    const QQueue<QString> *labels() const;
    bool hasDataLabels() const;

    /** # of the data point of the data series with index iDataSeriesIndex. */
    int dataSize(int iDataSeriesIndex) const;

    void setDataSeriesName(int iDataSeriesIndex, const QString &strName);
    QString dataSeriesName(int iDataSeriesIndex) const;

    void setTotal(int iDataSeriesIndex, quint64 iTotal);
    quint64 total(int iDataSeriesIndex) const;

    bool requiresGuestAdditions() const;
    void setRequiresGuestAdditions(bool fRequiresGAs);

    void setIsInitialized(bool fIsInitialized);
    bool isInitialized() const;

    void reset();
    void toFile(QTextStream &stream) const;

    void setAutoUpdateMaximum(bool fAuto);
    bool autoUpdateMaximum() const;

private:
    void updateMax();

    QString m_strUnit;
    QString m_strDataSeriesName[DATA_SERIES_SIZE];
    quint64 m_iMaximum;
    QQueue<quint64> m_data[DATA_SERIES_SIZE];
    /** We assume m_data[0] and m_data[1] have a common label array. */
    QQueue<QString> m_labels;
    /** The total data (the counter value we get from IMachineDebugger API). For the metrics
      * we get from IMachineDebugger m_data values are computed as deltas of total values t - (t-1) */
    quint64 m_iTotal[DATA_SERIES_SIZE];

    bool m_fRequiresGuestAdditions;
    /** Used for metrices whose data is computed as total deltas. That is we receieve only total value and
      * compute time step data from total deltas. m_isInitialised is true if the total has been set first time. */
    bool m_fIsInitialized;
    /** Maximum is updated as a new data is added to data queue. */
    bool m_fAutoUpdateMaximum;
    int m_iMaximumQueueSize;
};

/** UIVMActivityMonitor class displays some high level performance metrics of the guest system.
  * The values are read in certain periods and cached in the GUI side. Currently we draw some line charts
  * and pie charts (where applicable) alongside with some text. IPerformanceCollector and IMachineDebugger are
  * two sources of the performance metrics. Unfortunately these two have very distinct APIs resulting a bit too much
  * special casing etc.*/
class  SHARED_LIBRARY_STUFF UIVMActivityMonitor : public QWidget
{

    Q_OBJECT;

public:

    UIVMActivityMonitor(EmbedTo enmEmbedding, QWidget *pParent, UIActionPool *pActionPool, int iMaximumQueueSize);
    virtual QUuid machineId() const = 0;
    virtual QString machineName() const = 0;
    void setDataSeriesColor(int iIndex, const QColor &color);

public slots:

    void sltExportMetricsToFile();

protected slots:

    virtual void sltRetranslateUI();

protected:

    virtual void obtainDataAndUpdate() = 0;
    virtual QString defaultMachineFolder() const = 0;
    virtual void reset() = 0;
    virtual void start() = 0;

    QString dataColorString(Metric_Type enmType, int iDataIndex);

    /** @name The following functions reset corresponding info labels
      * @{ */
        virtual void resetCPUInfoLabel() = 0;
        void resetRAMInfoLabel();
    /** @} */

    virtual void prepareWidgets();
    void prepareActions();
    void setInfoLabelWidth();

    QGridLayout            *m_pContainerLayout;
    QTimer                 *m_pTimer;
    quint64                 m_iTimeStep;
    QMap<Metric_Type, UIMetric> m_metrics;

    /** @name The following are used during UIPerformanceCollector::QueryMetricsData(..)
      * @{ */
        QVector<QString> m_nameList;
        QVector<CUnknown> m_objectList;
    /** @} */
    QMap<Metric_Type, UIChart*>  m_charts;
    QMap<Metric_Type, QLabel*>   m_infoLabels;

    /** @name Cached translated strings.
      * @{ */
        /** CPU info label strings. */
        QString m_strCPUInfoLabelTitle;
        QString m_strCPUInfoLabelGuest;
        QString  m_strCPUInfoLabelVMM;
        /** RAM usage info label strings. */
        QString m_strRAMInfoLabelTitle;
        QString m_strRAMInfoLabelTotal;
        QString m_strRAMInfoLabelFree;
        QString m_strRAMInfoLabelUsed;
        /** Net traffic info label strings. */
        QString m_strNetworkInfoLabelReceived;
        QString m_strNetworkInfoLabelTransmitted;
        QString m_strNetworkInfoLabelReceivedTotal;
        QString m_strNetworkInfoLabelTransmittedTotal;
        /** Disk IO info label strings. */
        QString m_strDiskIOInfoLabelTitle;
        QString m_strDiskIOInfoLabelWritten;
        QString m_strDiskIOInfoLabelRead;
        QString m_strDiskIOInfoLabelWrittenTotal;
        QString m_strDiskIOInfoLabelReadTotal;
    /** @} */
    int m_iMaximumLabelLength;
    int m_iMaximumQueueSize;
    QColor m_dataSeriesColor[DATA_SERIES_SIZE];
    UIActionPool *m_pActionPool;
private slots:

    /** Reads the metric values for several sources and calls corresponding update functions. */
    void sltTimeout();
    void sltCreateContextMenu(const QPoint &point);

private:

    bool guestAdditionsAvailable(const char *pszMinimumVersion);

    /** Holds the instance of layout we create. */
    QVBoxLayout *m_pMainLayout;
    EmbedTo m_enmEmbedding;
};

class  SHARED_LIBRARY_STUFF UIVMActivityMonitorLocal : public UIVMActivityMonitor
{

    Q_OBJECT;

public:

    /** Constructs information-tab passing @a pParent to the QWidget base-class constructor.
      * @param machine is machine reference. */
    UIVMActivityMonitorLocal(EmbedTo enmEmbedding, QWidget *pParent, const CMachine &machine, UIActionPool *pActionPool);
    ~UIVMActivityMonitorLocal();
    virtual QUuid machineId() const RT_OVERRIDE;
    virtual QString machineName() const RT_OVERRIDE;

protected slots:

        virtual void sltRetranslateUI() RT_OVERRIDE;

protected:

    virtual void obtainDataAndUpdate() RT_OVERRIDE;
    virtual QString defaultMachineFolder() const RT_OVERRIDE;
    virtual void reset() RT_OVERRIDE;
    virtual void start() RT_OVERRIDE;

private slots:

    /** Stop updating the charts if/when the machine state changes something other than KMachineState_Running. */
    void sltMachineStateChange(const QUuid &uId);
    void sltClearCOMData();
    void sltGuestAdditionsStateChange();

private:

    void setMachine(const CMachine &machine);
    void openSession();
    void prepareMetrics();
    bool guestAdditionsAvailable(const char *pszMinimumVersion);
    void enableDisableGuestAdditionDependedWidgets(bool fEnable);
    void updateCPUChart(ULONG iLoadPercentage, ULONG iOtherPercentage);
    void updateRAMGraphsAndMetric(quint64 iTotalRAM, quint64 iFreeRAM);
    void updateNetworkChart(quint64 uReceiveTotal, quint64 uTransmitTotal);
    void updateDiskIOChart(quint64 uDiskIOTotalWritten, quint64 uDiskIOTotalRead);
    void updateVMExitMetric(quint64 uTotalVMExits);
    void resetVMExitInfoLabel();
    virtual void resetCPUInfoLabel() RT_OVERRIDE;
    void resetNetworkInfoLabel();
    void resetDiskIOInfoLabel();
    virtual void prepareWidgets() RT_OVERRIDE;
    void configureCOMPerformanceCollector();

    bool m_fGuestAdditionsAvailable;
    CMachine m_comMachine;
    CSession m_comSession;
    CGuest   m_comGuest;
    CConsole m_comConsole;
    CPerformanceCollector m_performanceCollector;
    bool                  m_fCOMPerformanceCollectorConfigured;
    CMachineDebugger      m_comMachineDebugger;
    /** VM Exit info label strings. */
    QString m_strVMExitInfoLabelTitle;
    QString m_strVMExitLabelCurrent;
    QString m_strVMExitLabelTotal;
    QString m_strNetworkInfoLabelTitle;
    ComObjPtr<UIMainEventListenerImpl> m_pQtConsoleListener;
    CEventListener m_comConsoleListener;
};

class  SHARED_LIBRARY_STUFF UIVMActivityMonitorCloud : public UIVMActivityMonitor
{

    Q_OBJECT;

public:

    UIVMActivityMonitorCloud(EmbedTo enmEmbedding, QWidget *pParent, const CCloudMachine &machine, UIActionPool *pActionPool);
    virtual QUuid machineId() const RT_OVERRIDE;
    virtual QString machineName() const RT_OVERRIDE;
    /** According to OCI docs returned time stamp is in RFC3339 format. */
    static QString formatCloudTimeStamp(const QString &strInput);

protected slots:

        virtual void sltRetranslateUI() RT_OVERRIDE;

private slots:

    void sltMetricNameListingComplete(QVector<QString> metricNameList);
    void sltMetricDataReceived(KMetricType enmMetricType,
                               const QVector<QString> &data, const QVector<QString> &timeStamps);
    void sltMachineStateUpdateTimeout();

private:
    void setMachine(const CCloudMachine &comMachine);
    virtual void obtainDataAndUpdate() RT_OVERRIDE;

    virtual QString defaultMachineFolder() const RT_OVERRIDE;
    virtual void reset() RT_OVERRIDE;
    virtual void start() RT_OVERRIDE;
    virtual void prepareWidgets() RT_OVERRIDE;
    /** @name The following functions update corresponding metric charts and labels with new values
      * @{ */
        void updateCPUChart(quint64 iLoadPercentage, const QString &strLabel);
        void updateNetworkInChart(quint64 uReceive, const QString &strLabel);
        void updateNetworkOutChart(quint64 uTransmit, const QString &strLabel);
        void updateDiskIOReadChart(quint64 uReadRate, const QString &strLabel);
        void updateDiskIOWrittenChart(quint64 uWriteRate, const QString &strLabel);
        void updateRAMChart(quint64 iUsagePercentage, const QString &strLabel);
    /** @} */
    virtual void resetCPUInfoLabel() RT_OVERRIDE;
    void resetNetworkInInfoLabel();
    void resetNetworkOutInfoLabel();
    void resetDiskIOWrittenInfoLabel();
    void resetDiskIOReadInfoLabel();

    bool findMetric(KMetricType enmMetricType, UIMetric &metric, int &iDataSeriesIndex) const;
    void prepareMetrics();

    CCloudMachine m_comMachine;
    QPointer<UIProgressTaskReadCloudMachineMetricList> m_ReadListProgressTask;

    QVector<KMetricType> m_availableMetricTypes;
    /** Mapping from API enums to internal metric type enum. */
    QHash<KMetricType, Metric_Type> m_metricTypeDict;

    /** Total amount of RAM in kb. */
    quint64 m_uTotalRAM;
    QTimer *m_pMachineStateUpdateTimer;
    KCloudMachineState m_enmMachineState;

    QString m_strNetworkInInfoLabelTitle;
    QString m_strNetworkOutInfoLabelTitle;
};
#endif /* !FEQT_INCLUDED_SRC_activity_vmactivity_UIVMActivityMonitor_h */
