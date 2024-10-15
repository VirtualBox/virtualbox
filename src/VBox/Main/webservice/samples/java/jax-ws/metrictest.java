/* $Id: metrictest.java 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/*!file
 * Sample of performance API usage, written in Java.
 *
 * Don't forget to run VBOX webserver
 * with 'vboxwebsrv -t 1000' command, to calm down watchdog thread.
 *
 * The following license applies to this file only:
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

import com.sun.xml.ws.commons.virtualbox{VBOX_API_SUFFIX}.*;

import java.util.*;
import javax.xml.ws.Holder;

class PerformanceData
{
    public String name;
    public IUnknown object;
    public String unit;
    public Long scale;
    public Long sequenceNumber;
    public List<Long> samples;

    public String getFormattedSamples()
    {
        String out = "[";
        String separator = "";

        if (scale != 1)
        {
            for (Long sample : samples)
            {
                out += separator + (sample.doubleValue() / scale) + " " + unit;
                separator = ", ";
            }
        }
        else
        {
            for (Long sample : samples)
            {
                out += separator + sample.toString() + " " + unit;
                separator = ", ";
            }
        }
        out += "]";
        return out;
    }
}

class PerformanceCollector
{
    private IVirtualBox  _vbox;
    private IPerformanceCollector _collector;

    public PerformanceCollector(IVirtualBox vbox)
    {
        _vbox = vbox;
        _collector = vbox.getPerformanceCollector();
    }

    public void cleanup()
    {
        _collector.releaseRemote();
    }

    public List<IPerformanceMetric> setup(List<String> metricNames, List<IUnknown> objects, Long period, Long samples)
    {
        return _collector.setupMetrics(metricNames, objects, period, samples);
    }

    public List<IPerformanceMetric> enable(List<String> metricNames, List<IUnknown> objects)
    {
        return _collector.enableMetrics(metricNames, objects);
    }

    public List<IPerformanceMetric> disable(List<String> metricNames, List<IUnknown> objects)
    {
        return _collector.disableMetrics(metricNames, objects);
    }

    public List<PerformanceData> query(List<String> filterMetrics, List<IUnknown> filterObjects)
    {
        Holder<List<String>> names = new Holder<List<String>>();
        Holder<List<IUnknown>> objects = new Holder<List<IUnknown>>();
        Holder<List<String>> units = new Holder<List<String>>();
        Holder<List<Long>> scales =  new Holder<List<Long>>();
        Holder<List<Long>> sequenceNumbers =  new Holder<List<Long>>();
        Holder<List<Long>> indices =  new Holder<List<Long>>();
        Holder<List<Long>> lengths =  new Holder<List<Long>>();
        List<Integer> values =
            _collector.queryMetricsData(filterMetrics, filterObjects,
                                        names, objects, units, scales, sequenceNumbers, indices, lengths);
        List<PerformanceData> data = new ArrayList<PerformanceData>(names.value.size());
        for (int i = 0; i < names.value.size(); i++)
        {
            PerformanceData singleMetricData = new PerformanceData();
            singleMetricData.name = names.value.get(i);
            singleMetricData.object = objects.value.get(i);
            singleMetricData.unit = units.value.get(i);
            singleMetricData.scale = scales.value.get(i);
            singleMetricData.sequenceNumber = sequenceNumbers.value.get(i);
            List<Long> samples = new ArrayList<Long>(lengths.value.get(i).intValue());
            for (int j = 0; j < lengths.value.get(i); j++)
            {
                samples.add(values.get(indices.value.get(i).intValue() + j).longValue());
            }
            singleMetricData.samples = samples;
            data.add(singleMetricData);
        }

        return data;
    }
}

public class metrictest implements Runnable
{
    IVirtualBox vbox;
    IWebsessionManager mgr;
    PerformanceCollector perf;

    public metrictest()
    {
        mgr = new IWebsessionManager("http://localhost:18083/");
        vbox = mgr.logon("test", "test");
        System.out.println("Initialized connection to VirtualBox version " + vbox.getVersion());
        perf = new PerformanceCollector(vbox);
    }

    private String getObjectName(IUnknown object)
    {
        try
        {
            String machineName = object.getRemoteWSPort().iMachineGetName(object.getRef());
            return machineName;
        } catch (Exception e)
        {
        }
        return new String("host");
    }

    public void setup()
    {
        perf.setup(Arrays.asList(new String[]{"*"}),
                   new ArrayList<IUnknown>(),
                   new Long(1), new Long(5));
    }

    public void collect()
    {
        try
        {
            List<IUnknown> allObjects = new ArrayList<IUnknown>();
            List<PerformanceData> metricData = perf.query(Arrays.asList(new String[]{"*"}),
                                                          allObjects);
            for (PerformanceData md : metricData)
            {
                System.out.println("(" + getObjectName(md.object) + ") " +
                                   md.name + " " + md.getFormattedSamples());
            }
        }
        catch (Exception e)
        {
            e.printStackTrace();
        }
    }

    public void run()
    {
        // Clean up
        try
        {
            if (perf != null)
            {
                perf.cleanup();
                perf = null;
            }
            if (vbox != null)
            {
                mgr.logoff(vbox);
                vbox = null;
                mgr = null;
                System.out.println("Logged off.");
            }
        }
        catch (Exception e)
        {
            e.printStackTrace();
        }
    }

    public static void main(String[] args) throws InterruptedException
    {
        metrictest c = new metrictest();
        // Add a shutdown handle to clean up
        Runtime.getRuntime().addShutdownHook(new Thread(c));
        // Start metric collection
        c.setup();
        // Obtain and print out stats continuously until ctrl-C is pressed
        while (true)
        {
            Thread.sleep(1000); // Sleep for a second
            c.collect();
        }
    }
}
