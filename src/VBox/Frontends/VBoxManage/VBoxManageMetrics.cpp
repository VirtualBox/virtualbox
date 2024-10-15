/* $Id: VBoxManageMetrics.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManage - The 'metrics' command.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/com/com.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/VirtualBox.h>

#include <iprt/asm.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/thread.h>
#include <VBox/log.h>

#include <set>
#include <utility>

#include "VBoxManage.h"
using namespace com;

DECLARE_TRANSLATION_CONTEXT(Metrics);

// funcs
///////////////////////////////////////////////////////////////////////////////


static HRESULT parseFilterParameters(int argc, char *argv[],
                                     ComPtr<IVirtualBox> aVirtualBox,
                                     ComSafeArrayOut(BSTR, outMetrics),
                                     ComSafeArrayOut(IUnknown *, outObjects))
{
    HRESULT hrc = S_OK;
    com::SafeArray<BSTR> retMetrics(1);
    com::SafeIfaceArray <IUnknown> retObjects;

    Bstr metricNames, baseNames;

    /* Metric list */
    if (argc > 1)
        metricNames = argv[1];
    else
    {
        metricNames = L"*";
        baseNames = L"*";
    }
    metricNames.cloneTo(&retMetrics[0]);

    /* Object name */
    if (argc > 0 && strcmp(argv[0], "*"))
    {
        if (!strcmp(argv[0], "host"))
        {
            ComPtr<IHost> host;
            CHECK_ERROR(aVirtualBox, COMGETTER(Host)(host.asOutParam()));
            retObjects.reset(1);
            host.queryInterfaceTo(&retObjects[0]);
        }
        else
        {
            ComPtr<IMachine> machine;
            hrc = aVirtualBox->FindMachine(Bstr(argv[0]).raw(),
                                           machine.asOutParam());
            if (SUCCEEDED(hrc))
            {
                retObjects.reset(1);
                machine.queryInterfaceTo(&retObjects[0]);
            }
            else
            {
                errorArgument(Metrics::tr("Invalid machine name: '%s'"), argv[0]);
                return hrc;
            }
        }

    }

    retMetrics.detachTo(ComSafeArrayOutArg(outMetrics));
    retObjects.detachTo(ComSafeArrayOutArg(outObjects));

    return hrc;
}

static Bstr toBaseName(Utf8Str& aFullName)
{
    char *pszRaw = aFullName.mutableRaw();
    /*
     * Currently there are two metrics which base name is the same as the
     * sub-metric name: CPU/MHz and Net/<iface>/LinkSpeed.
     */
    if (pszRaw && strcmp(pszRaw, "CPU/MHz") && !RTStrSimplePatternMatch("Net/*/LinkSpeed", pszRaw))
    {
        char *pszSlash = strrchr(pszRaw, '/');
        if (pszSlash)
        {
            *pszSlash = 0;
            aFullName.jolt();
        }
    }
    return Bstr(aFullName);
}

static Bstr getObjectName(ComPtr<IUnknown> aObject)
{
    HRESULT hrc;

    ComPtr<IHost> host = aObject;
    if (!host.isNull())
        return Bstr(Metrics::tr("host"));

    ComPtr<IMachine> machine = aObject;
    if (!machine.isNull())
    {
        Bstr name;
        CHECK_ERROR(machine, COMGETTER(Name)(name.asOutParam()));
        if (SUCCEEDED(hrc))
            return name;
    }
    return Bstr(Metrics::tr("unknown"));
}

static void listAffectedMetrics(ComSafeArrayIn(IPerformanceMetric*, aMetrics))
{
    HRESULT hrc;
    com::SafeIfaceArray<IPerformanceMetric> metrics(ComSafeArrayInArg(aMetrics));
    if (metrics.size())
    {
        ComPtr<IUnknown> object;
        Bstr metricName;
        RTPrintf(Metrics::tr("The following metrics were modified:\n\n"
                             "Object     Metric\n"
                             "---------- --------------------\n"));
        for (size_t i = 0; i < metrics.size(); i++)
        {
            CHECK_ERROR(metrics[i], COMGETTER(Object)(object.asOutParam()));
            CHECK_ERROR(metrics[i], COMGETTER(MetricName)(metricName.asOutParam()));
            RTPrintf("%-10ls %-20ls\n",
                getObjectName(object).raw(), metricName.raw());
        }
        RTPrintf("\n");
    }
    else
    {
        RTMsgError(Metrics::tr("No metrics match the specified filter!"));
    }
}

/**
 * list
 */
static RTEXITCODE handleMetricsList(int argc, char *argv[],
                             ComPtr<IVirtualBox> aVirtualBox,
                             ComPtr<IPerformanceCollector> performanceCollector)
{
    HRESULT hrc;
    com::SafeArray<BSTR>          metrics;
    com::SafeIfaceArray<IUnknown> objects;

    setCurrentSubcommand(HELP_SCOPE_METRICS_LIST);

    hrc = parseFilterParameters(argc - 1, &argv[1], aVirtualBox,
                               ComSafeArrayAsOutParam(metrics),
                               ComSafeArrayAsOutParam(objects));
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    com::SafeIfaceArray<IPerformanceMetric> metricInfo;

    CHECK_ERROR(performanceCollector,
        GetMetrics(ComSafeArrayAsInParam(metrics),
                   ComSafeArrayAsInParam(objects),
                   ComSafeArrayAsOutParam(metricInfo)));

    ComPtr<IUnknown> object;
    Bstr metricName, unit, description;
    ULONG period, count;
    LONG minimum, maximum;
    RTPrintf(Metrics::tr(
"Object          Metric                                   Unit    Minimum    Maximum     Period      Count Description\n"
"--------------- ---------------------------------------- ---- ---------- ---------- ---------- ---------- -----------\n"));
    for (size_t i = 0; i < metricInfo.size(); i++)
    {
        CHECK_ERROR(metricInfo[i], COMGETTER(Object)(object.asOutParam()));
        CHECK_ERROR(metricInfo[i], COMGETTER(MetricName)(metricName.asOutParam()));
        CHECK_ERROR(metricInfo[i], COMGETTER(Period)(&period));
        CHECK_ERROR(metricInfo[i], COMGETTER(Count)(&count));
        CHECK_ERROR(metricInfo[i], COMGETTER(MinimumValue)(&minimum));
        CHECK_ERROR(metricInfo[i], COMGETTER(MaximumValue)(&maximum));
        CHECK_ERROR(metricInfo[i], COMGETTER(Unit)(unit.asOutParam()));
        CHECK_ERROR(metricInfo[i], COMGETTER(Description)(description.asOutParam()));
        RTPrintf("%-15ls %-40ls %-4ls %10d %10d %10u %10u %ls\n",
            getObjectName(object).raw(), metricName.raw(), unit.raw(),
            minimum, maximum, period, count, description.raw());
    }

    return RTEXITCODE_SUCCESS;
}

/**
 * Metrics setup
 */
static RTEXITCODE handleMetricsSetup(int argc, char *argv[],
                                     ComPtr<IVirtualBox> aVirtualBox,
                                     ComPtr<IPerformanceCollector> performanceCollector)
{
    HRESULT hrc;
    com::SafeArray<BSTR>          metrics;
    com::SafeIfaceArray<IUnknown> objects;
    uint32_t period = 1, samples = 1;
    bool listMatches = false;
    int i;

    setCurrentSubcommand(HELP_SCOPE_METRICS_SETUP);

    for (i = 1; i < argc; i++)
    {
        if (   !strcmp(argv[i], "--period")
            || !strcmp(argv[i], "-period"))
        {
            if (argc <= i + 1)
                return errorArgument(Metrics::tr("Missing argument to '%s'"), argv[i]);
            if (   VINF_SUCCESS != RTStrToUInt32Full(argv[++i], 10, &period)
                || !period)
                return errorArgument(Metrics::tr("Invalid value for 'period' parameter: '%s'"), argv[i]);
        }
        else if (   !strcmp(argv[i], "--samples")
                 || !strcmp(argv[i], "-samples"))
        {
            if (argc <= i + 1)
                return errorArgument(Metrics::tr("Missing argument to '%s'"), argv[i]);
            if (   VINF_SUCCESS != RTStrToUInt32Full(argv[++i], 10, &samples)
                || !samples)
                return errorArgument(Metrics::tr("Invalid value for 'samples' parameter: '%s'"), argv[i]);
        }
        else if (   !strcmp(argv[i], "--list")
                 || !strcmp(argv[i], "-list"))
            listMatches = true;
        else
            break; /* The rest of params should define the filter */
    }

    hrc = parseFilterParameters(argc - i, &argv[i], aVirtualBox,
                                ComSafeArrayAsOutParam(metrics),
                                ComSafeArrayAsOutParam(objects));
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    com::SafeIfaceArray<IPerformanceMetric> affectedMetrics;
    CHECK_ERROR(performanceCollector,
        SetupMetrics(ComSafeArrayAsInParam(metrics),
                     ComSafeArrayAsInParam(objects), period, samples,
                     ComSafeArrayAsOutParam(affectedMetrics)));
    if (FAILED(hrc))
        return RTEXITCODE_SYNTAX; /** @todo figure out why we must return 2 here. */

    if (listMatches)
        listAffectedMetrics(ComSafeArrayAsInParam(affectedMetrics));

    return RTEXITCODE_SUCCESS;
}

/**
 * metrics query
 */
static RTEXITCODE handleMetricsQuery(int argc, char *argv[],
                                     ComPtr<IVirtualBox> aVirtualBox,
                                     ComPtr<IPerformanceCollector> performanceCollector)
{
    HRESULT hrc;
    com::SafeArray<BSTR>          metrics;
    com::SafeIfaceArray<IUnknown> objects;

    setCurrentSubcommand(HELP_SCOPE_METRICS_QUERY);

    hrc = parseFilterParameters(argc - 1, &argv[1], aVirtualBox,
                                ComSafeArrayAsOutParam(metrics),
                                ComSafeArrayAsOutParam(objects));
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    com::SafeArray<BSTR>          retNames;
    com::SafeIfaceArray<IUnknown> retObjects;
    com::SafeArray<BSTR>          retUnits;
    com::SafeArray<ULONG>         retScales;
    com::SafeArray<ULONG>         retSequenceNumbers;
    com::SafeArray<ULONG>         retIndices;
    com::SafeArray<ULONG>         retLengths;
    com::SafeArray<LONG>          retData;
    CHECK_ERROR(performanceCollector, QueryMetricsData(ComSafeArrayAsInParam(metrics),
                                                       ComSafeArrayAsInParam(objects),
                                                       ComSafeArrayAsOutParam(retNames),
                                                       ComSafeArrayAsOutParam(retObjects),
                                                       ComSafeArrayAsOutParam(retUnits),
                                                       ComSafeArrayAsOutParam(retScales),
                                                       ComSafeArrayAsOutParam(retSequenceNumbers),
                                                       ComSafeArrayAsOutParam(retIndices),
                                                       ComSafeArrayAsOutParam(retLengths),
                                                       ComSafeArrayAsOutParam(retData)) );

    RTPrintf(Metrics::tr(
                "Object          Metric                                   Values\n"
                "--------------- ---------------------------------------- --------------------------------------------\n"));
    for (unsigned i = 0; i < retNames.size(); i++)
    {
        Bstr metricUnit(retUnits[i]);
        Bstr metricName(retNames[i]);
        RTPrintf("%-15ls %-40ls ", getObjectName(retObjects[i]).raw(), metricName.raw());
        const char *separator = "";
        for (unsigned j = 0; j < retLengths[i]; j++)
        {
            if (retScales[i] == 1)
                RTPrintf("%s%d %ls", separator, retData[retIndices[i] + j], metricUnit.raw());
            else
                RTPrintf("%s%d.%02d%ls", separator, retData[retIndices[i] + j] / retScales[i],
                         (retData[retIndices[i] + j] * 100 / retScales[i]) % 100, metricUnit.raw());
            separator = ", ";
        }
        RTPrintf("\n");
    }

    return RTEXITCODE_SUCCESS;
}

static void getTimestamp(char *pts, size_t tsSize)
{
    *pts = 0;
    AssertReturnVoid(tsSize >= 13); /* 3+3+3+3+1 */
    RTTIMESPEC TimeSpec;
    RTTIME Time;
    RTTimeExplode(&Time, RTTimeNow(&TimeSpec));
    pts += RTStrFormatNumber(pts, Time.u8Hour, 10, 2, 0, RTSTR_F_ZEROPAD);
    *pts++ = ':';
    pts += RTStrFormatNumber(pts, Time.u8Minute, 10, 2, 0, RTSTR_F_ZEROPAD);
    *pts++ = ':';
    pts += RTStrFormatNumber(pts, Time.u8Second, 10, 2, 0, RTSTR_F_ZEROPAD);
    *pts++ = '.';
    pts += RTStrFormatNumber(pts, Time.u32Nanosecond / 1000000, 10, 3, 0, RTSTR_F_ZEROPAD);
    *pts = 0;
}

/** Used by the handleMetricsCollect loop. */
static bool volatile g_fKeepGoing = true;

#ifdef RT_OS_WINDOWS
/**
 * Handler routine for catching Ctrl-C, Ctrl-Break and closing of
 * the console.
 *
 * @returns true if handled, false if not handled.
 * @param   dwCtrlType      The type of control signal.
 *
 * @remarks This is called on a new thread.
 */
static BOOL WINAPI ctrlHandler(DWORD dwCtrlType) RT_NOTHROW_DEF
{
    switch (dwCtrlType)
    {
        /* Ctrl-C or Ctrl-Break or Close */
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            /* Let's shut down gracefully. */
            ASMAtomicWriteBool(&g_fKeepGoing, false);
            return TRUE;
    }
    /* Don't care about the rest -- let it die a horrible death. */
    return FALSE;
}
#endif /* RT_OS_WINDOWS */

/**
 * collect
 */
static RTEXITCODE handleMetricsCollect(int argc, char *argv[],
                                       ComPtr<IVirtualBox> aVirtualBox,
                                       ComPtr<IPerformanceCollector> performanceCollector)
{
    HRESULT hrc;
    com::SafeArray<BSTR>          metrics;
    com::SafeIfaceArray<IUnknown> objects;
    uint32_t period = 1, samples = 1;
    bool isDetached = false, listMatches = false;
    int i;

    setCurrentSubcommand(HELP_SCOPE_METRICS_COLLECT);

    for (i = 1; i < argc; i++)
    {
        if (   !strcmp(argv[i], "--period")
            || !strcmp(argv[i], "-period"))
        {
            if (argc <= i + 1)
                return errorArgument(Metrics::tr("Missing argument to '%s'"), argv[i]);
            if (   VINF_SUCCESS != RTStrToUInt32Full(argv[++i], 10, &period)
                || !period)
                return errorArgument(Metrics::tr("Invalid value for 'period' parameter: '%s'"), argv[i]);
        }
        else if (   !strcmp(argv[i], "--samples")
                 || !strcmp(argv[i], "-samples"))
        {
            if (argc <= i + 1)
                return errorArgument(Metrics::tr("Missing argument to '%s'"), argv[i]);
            if (   VINF_SUCCESS != RTStrToUInt32Full(argv[++i], 10, &samples)
                || !samples)
                return errorArgument(Metrics::tr("Invalid value for 'samples' parameter: '%s'"), argv[i]);
        }
        else if (   !strcmp(argv[i], "--list")
                 || !strcmp(argv[i], "-list"))
            listMatches = true;
        else if (   !strcmp(argv[i], "--detach")
                 || !strcmp(argv[i], "-detach"))
            isDetached = true;
        else
            break; /* The rest of params should define the filter */
    }

    hrc = parseFilterParameters(argc - i, &argv[i], aVirtualBox,
                                ComSafeArrayAsOutParam(metrics),
                                ComSafeArrayAsOutParam(objects));
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    com::SafeIfaceArray<IPerformanceMetric> metricInfo;

    CHECK_ERROR(performanceCollector,
        GetMetrics(ComSafeArrayAsInParam(metrics),
                   ComSafeArrayAsInParam(objects),
                   ComSafeArrayAsOutParam(metricInfo)));

    std::set<std::pair<ComPtr<IUnknown>,Bstr> > baseMetrics;
    ComPtr<IUnknown> objectFiltered;
    Bstr metricNameFiltered;
    for (i = 0; i < (int)metricInfo.size(); i++)
    {
        CHECK_ERROR(metricInfo[i], COMGETTER(Object)(objectFiltered.asOutParam()));
        CHECK_ERROR(metricInfo[i], COMGETTER(MetricName)(metricNameFiltered.asOutParam()));
        Utf8Str baseMetricName(metricNameFiltered);
        baseMetrics.insert(std::make_pair(objectFiltered, toBaseName(baseMetricName)));
    }
    com::SafeArray<BSTR>          baseMetricsFiltered(baseMetrics.size());
    com::SafeIfaceArray<IUnknown> objectsFiltered(baseMetrics.size());
    std::set<std::pair<ComPtr<IUnknown>,Bstr> >::iterator it;
    i = 0;
    for (it = baseMetrics.begin(); it != baseMetrics.end(); ++it)
    {
        it->first.queryInterfaceTo(&objectsFiltered[i]);
        Bstr(it->second).detachTo(&baseMetricsFiltered[i++]);
    }
    com::SafeIfaceArray<IPerformanceMetric> affectedMetrics;
    CHECK_ERROR(performanceCollector,
        SetupMetrics(ComSafeArrayAsInParam(baseMetricsFiltered),
                     ComSafeArrayAsInParam(objectsFiltered), period, samples,
                     ComSafeArrayAsOutParam(affectedMetrics)));
    if (FAILED(hrc))
        return RTEXITCODE_SYNTAX; /** @todo figure out why we must return 2 here. */

    if (listMatches)
        listAffectedMetrics(ComSafeArrayAsInParam(affectedMetrics));
    if (!affectedMetrics.size())
        return RTEXITCODE_FAILURE;

    if (isDetached)
    {
        RTMsgWarning(Metrics::tr("The background process holding collected metrics will shutdown\n"
                                 "in few seconds, discarding all collected data and parameters."));
        return RTEXITCODE_SUCCESS;
    }

#ifdef RT_OS_WINDOWS
    SetConsoleCtrlHandler(ctrlHandler, true);
#endif /* RT_OS_WINDOWS */

    RTPrintf(Metrics::tr("Time stamp   Object     Metric               Value\n"));

    while (g_fKeepGoing)
    {
        RTPrintf("------------ ---------- -------------------- --------------------\n");
        RTThreadSleep(period * 1000); // Sleep for 'period' seconds
        char ts[15];

        getTimestamp(ts, sizeof(ts));
        com::SafeArray<BSTR>          retNames;
        com::SafeIfaceArray<IUnknown> retObjects;
        com::SafeArray<BSTR>          retUnits;
        com::SafeArray<ULONG>         retScales;
        com::SafeArray<ULONG>         retSequenceNumbers;
        com::SafeArray<ULONG>         retIndices;
        com::SafeArray<ULONG>         retLengths;
        com::SafeArray<LONG>          retData;
        CHECK_ERROR(performanceCollector, QueryMetricsData(ComSafeArrayAsInParam(metrics),
                                                           ComSafeArrayAsInParam(objects),
                                                           ComSafeArrayAsOutParam(retNames),
                                                           ComSafeArrayAsOutParam(retObjects),
                                                           ComSafeArrayAsOutParam(retUnits),
                                                           ComSafeArrayAsOutParam(retScales),
                                                           ComSafeArrayAsOutParam(retSequenceNumbers),
                                                           ComSafeArrayAsOutParam(retIndices),
                                                           ComSafeArrayAsOutParam(retLengths),
                                                           ComSafeArrayAsOutParam(retData)) );
        for (unsigned j = 0; j < retNames.size(); j++)
        {
            Bstr metricUnit(retUnits[j]);
            Bstr metricName(retNames[j]);
            RTPrintf("%-12s %-10ls %-20ls ", ts, getObjectName(retObjects[j]).raw(), metricName.raw());
            const char *separator = "";
            for (unsigned k = 0; k < retLengths[j]; k++)
            {
                if (retScales[j] == 1)
                    RTPrintf("%s%d %ls", separator, retData[retIndices[j] + k], metricUnit.raw());
                else
                    RTPrintf("%s%d.%02d%ls", separator, retData[retIndices[j] + k] / retScales[j],
                             (retData[retIndices[j] + k] * 100 / retScales[j]) % 100, metricUnit.raw());
                separator = ", ";
            }
            RTPrintf("\n");
        }
        RTStrmFlush(g_pStdOut);
    }

#ifdef RT_OS_WINDOWS
    SetConsoleCtrlHandler(ctrlHandler, false);
#endif /* RT_OS_WINDOWS */

    return RTEXITCODE_SUCCESS;
}

/**
 * Enable metrics
 */
static RTEXITCODE handleMetricsEnable(int argc, char *argv[],
                                      ComPtr<IVirtualBox> aVirtualBox,
                                      ComPtr<IPerformanceCollector> performanceCollector)
{
    HRESULT hrc;
    com::SafeArray<BSTR>          metrics;
    com::SafeIfaceArray<IUnknown> objects;
    bool listMatches = false;
    int i;

    setCurrentSubcommand(HELP_SCOPE_METRICS_ENABLE);

    for (i = 1; i < argc; i++)
    {
        if (   !strcmp(argv[i], "--list")
            || !strcmp(argv[i], "-list"))
            listMatches = true;
        else
            break; /* The rest of params should define the filter */
    }

    hrc = parseFilterParameters(argc - i, &argv[i], aVirtualBox,
                                ComSafeArrayAsOutParam(metrics),
                                ComSafeArrayAsOutParam(objects));
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    com::SafeIfaceArray<IPerformanceMetric> affectedMetrics;
    CHECK_ERROR(performanceCollector,
        EnableMetrics(ComSafeArrayAsInParam(metrics),
                      ComSafeArrayAsInParam(objects),
                      ComSafeArrayAsOutParam(affectedMetrics)));
    if (FAILED(hrc))
        return RTEXITCODE_SYNTAX; /** @todo figure out why we must return 2 here. */

    if (listMatches)
        listAffectedMetrics(ComSafeArrayAsInParam(affectedMetrics));

    return RTEXITCODE_SUCCESS;
}

/**
 * Disable metrics
 */
static RTEXITCODE handleMetricsDisable(int argc, char *argv[],
                                       ComPtr<IVirtualBox> aVirtualBox,
                                       ComPtr<IPerformanceCollector> performanceCollector)
{
    HRESULT hrc;
    com::SafeArray<BSTR>          metrics;
    com::SafeIfaceArray<IUnknown> objects;
    bool listMatches = false;
    int i;

    setCurrentSubcommand(HELP_SCOPE_METRICS_DISABLE);

    for (i = 1; i < argc; i++)
    {
        if (   !strcmp(argv[i], "--list")
            || !strcmp(argv[i], "-list"))
            listMatches = true;
        else
            break; /* The rest of params should define the filter */
    }

    hrc = parseFilterParameters(argc - i, &argv[i], aVirtualBox,
                                ComSafeArrayAsOutParam(metrics),
                                ComSafeArrayAsOutParam(objects));
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    com::SafeIfaceArray<IPerformanceMetric> affectedMetrics;
    CHECK_ERROR(performanceCollector,
        DisableMetrics(ComSafeArrayAsInParam(metrics),
                       ComSafeArrayAsInParam(objects),
                       ComSafeArrayAsOutParam(affectedMetrics)));
    if (FAILED(hrc))
        return RTEXITCODE_SYNTAX; /** @todo figure out why we must return 2 here. */

    if (listMatches)
        listAffectedMetrics(ComSafeArrayAsInParam(affectedMetrics));

    return RTEXITCODE_SUCCESS;
}


RTEXITCODE handleMetrics(HandlerArg *a)
{
    /* at least one option: subcommand name */
    if (a->argc < 1)
        return errorSyntax(Metrics::tr("Subcommand missing"));

    ComPtr<IPerformanceCollector> performanceCollector;
    CHECK_ERROR2I_RET(a->virtualBox, COMGETTER(PerformanceCollector)(performanceCollector.asOutParam()), RTEXITCODE_FAILURE);

    RTEXITCODE rcExit;
    if (!strcmp(a->argv[0], "list"))
        rcExit = handleMetricsList(a->argc, a->argv, a->virtualBox, performanceCollector);
    else if (!strcmp(a->argv[0], "setup"))
        rcExit = handleMetricsSetup(a->argc, a->argv, a->virtualBox, performanceCollector);
    else if (!strcmp(a->argv[0], "query"))
        rcExit = handleMetricsQuery(a->argc, a->argv, a->virtualBox, performanceCollector);
    else if (!strcmp(a->argv[0], "collect"))
        rcExit = handleMetricsCollect(a->argc, a->argv, a->virtualBox, performanceCollector);
    else if (!strcmp(a->argv[0], "enable"))
        rcExit = handleMetricsEnable(a->argc, a->argv, a->virtualBox, performanceCollector);
    else if (!strcmp(a->argv[0], "disable"))
        rcExit = handleMetricsDisable(a->argc, a->argv, a->virtualBox, performanceCollector);
    else
        return errorSyntax(Metrics::tr("Invalid subcommand '%s'"), a->argv[0]);

    return rcExit;
}
