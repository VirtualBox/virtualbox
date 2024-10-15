/* $Id: clienttest.java 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/*!file
 * Sample client for the VirtualBox web service, written in Java (object-oriented bindings).
 *
 * Run the VirtualBox web service server first; see the VirtualBox
 * SDK reference for details.
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

/* Somewhat ugly way to support versioning */
import com.sun.xml.ws.commons.virtualbox{VBOX_API_SUFFIX}.*;

import java.util.*;
import javax.xml.ws.Holder;

public class clienttest
{
    IWebsessionManager mgr;
    IVirtualBox vbox;

    public clienttest()
    {
        mgr = new IWebsessionManager("http://localhost:18083/");
        vbox = mgr.logon("test", "test");
        System.out.println("Initialized connection to VirtualBox version " + vbox.getVersion());
    }

    public void disconnect()
    {
        mgr.disconnect(vbox);
    }

    class Desktop
    {
        String name;
        String uuid;

        Desktop(int n)
        {
            name = "Mach"+n;
            uuid = UUID.randomUUID().toString();
        }
        String getName()
        {
            return name;
        }
        String getId()
        {
            return uuid;
        }
    }

    public void test()
    {
        for (int i=0; i<100; i++)
        {
            String baseFolder =
                    vbox.getSystemProperties().getDefaultMachineFolder();
            Desktop desktop = new Desktop(i);
            IMachine machine =  vbox.createMachine(baseFolder,
                                                   "linux",
                                                   desktop.getName(),
                                                   desktop.getId(),
                                                   true);
            machine.saveSettings();
            mgr.cleanupUnused();
        }
    }

    public void test2()
    {
        ISession session = mgr.getSessionObject(vbox);
        String id = "bc8b6219-2775-42c4-f1b2-b48b3c177294";
        vbox.openSession(session, id);
        IMachine mach = session.getMachine();
        IFirmwareSettings firmware = mach.getFirmwareSettings();
        firmware.setIOAPICEnabled(true);
        mach.saveSettings();
        session.close();
    }


    public void test3()
    {

        IWebsessionManager mgr1 = new IWebsessionManager("http://localhost:18082/");
        IWebsessionManager mgr2 = new IWebsessionManager("http://localhost:18083/");
        IVirtualBox vbox1 = mgr1.logon("test", "test");
        IVirtualBox vbox2 = mgr2.logon("test", "test");


        System.out.println("connection 1 to VirtualBox version " + vbox1.getVersion());
        System.out.println("connection 2 to VirtualBox version " + vbox2.getVersion());
        mgr1.disconnect(vbox1);
        mgr2.disconnect(vbox2);

        mgr1 = new IWebsessionManager("http://localhost:18082/");
        mgr2 = new IWebsessionManager("http://localhost:18083/");
        vbox1 = mgr1.logon("test", "test");
        vbox2 = mgr2.logon("test", "test");

        System.out.println("second connection 1 to VirtualBox version " + vbox1.getVersion());
        System.out.println("second connection 2 to VirtualBox version " + vbox2.getVersion());

        mgr1.disconnect(vbox1);
        mgr2.disconnect(vbox2);
    }

    public void showVMs()
    {
        try
        {
            int i = 0;
            for (IMachine m : vbox.getMachines())
            {
                System.out.println("Machine " + (i++) + ": " + " [" + m.getId() + "]" + " - " + m.getName());
            }
        }
        catch (Exception e)
        {
            e.printStackTrace();
        }
    }

    public void listHostInfo()
    {
        try
        {
            IHost host = vbox.getHost();
            long uProcCount = host.getProcessorCount();
            System.out.println("Processor count: " + uProcCount);

            for (long i=0; i<uProcCount; i++)
            {
                System.out.println("Processor #" + i + " speed: " + host.getProcessorSpeed(i) + "MHz");
            }

            IPerformanceCollector  oCollector = vbox.getPerformanceCollector();

            List<IPerformanceMetric> aMetrics =
                oCollector.getMetrics(Arrays.asList(new String[]{"*"}),
                                      Arrays.asList(new IUnknown[]{host}));

            for (IPerformanceMetric m : aMetrics)
            {
                System.out.println("known metric = "+m.getMetricName());
            }

            Holder<List<String>> names = new Holder<List<String>> ();
            Holder<List<IUnknown>> objects = new Holder<List<IUnknown>>() ;
            Holder<List<String>> units = new Holder<List<String>>();
            Holder<List<Long>> scales =  new Holder<List<Long>>();
            Holder<List<Long>> sequenceNumbers =  new Holder<List<Long>>();
            Holder<List<Long>> indices =  new Holder<List<Long>>();
            Holder<List<Long>> lengths =  new Holder<List<Long>>();

            List<Integer> vals =
                oCollector.queryMetricsData(Arrays.asList(new String[]{"*"}),
                                            Arrays.asList(new IUnknown[]{host}),
                                            names, objects, units, scales,
                                            sequenceNumbers, indices, lengths);

            for (int i=0; i < names.value.size(); i++)
                System.out.println("name: "+names.value.get(i));
        }
        catch (Exception e)
        {
            e.printStackTrace();
        }
    }

    public void startVM(String strVM)
    {
        ISession oSession = null;
        IMachine oMachine = null;

        try
        {
            oSession = mgr.getSessionObject(vbox);

            // first assume we were given a UUID
            try
            {
                oMachine = vbox.getMachine(strVM);
            }
            catch (Exception e)
            {
                try
                {
                    oMachine = vbox.findMachine(strVM);
                }
                catch (Exception e1)
                {
                }
            }

            if (oMachine == null)
            {
                System.out.println("Error: can't find VM \"" + strVM + "\"");
            }
            else
            {
                String uuid = oMachine.getId();
                String sessionType = "gui";
                ArrayList<String> env = new ArrayList<String>();
                env.add("DISPLAY=:0.0");
                IProgress oProgress =
                    oMachine.launchVMProcess(oSession,
                                             sessionType,
                                             env);
                System.out.println("Session for VM " + uuid + " is opening...");
                oProgress.waitForCompletion(10000);

                long rc = oProgress.getResultCode();
                if (rc != 0)
                    System.out.println("Session failed!");
            }
        }
        catch (Exception e)
        {
            e.printStackTrace();
        }
        finally
        {
            if (oSession != null)
            {
                oSession.close();
            }
        }
    }

    public void cleanup()
    {
        try
        {
            if (vbox != null)
            {
                disconnect();
                vbox = null;
                System.out.println("Logged off.");
            }
            mgr.cleanupUnused();
            mgr = null;
        }
        catch (Exception e)
        {
            e.printStackTrace();
        }
    }

    public static void printArgs()
    {
        System.out.println(  "Usage: java clienttest <mode> ..." +
                             "\nwith <mode> being:" +
                             "\n   show vms            list installed virtual machines" +
                             "\n   list hostinfo       list host info" +
                             "\n   startvm <vmname|uuid> start the given virtual machine");
    }

    public static void main(String[] args)
    {
        if (args.length < 1)
        {
            System.out.println("Error: Must specify at least one argument.");
            printArgs();
        }
        else
        {
            clienttest c = new clienttest();
            if (args[0].equals("show"))
            {
                if (args.length == 2)
                {
                    if (args[1].equals("vms"))
                        c.showVMs();
                    else
                        System.out.println("Error: Unknown argument to \"show\": \"" + args[1] + "\".");
                }
                else
                    System.out.println("Error: Missing argument to \"show\" command");
            }
            else if (args[0].equals("list"))
            {
                if (args.length == 2)
                {
                    if (args[1].equals("hostinfo"))
                        c.listHostInfo();
                    else
                        System.out.println("Error: Unknown argument to \"show\": \"" + args[1] + "\".");
                }
                else
                    System.out.println("Error: Missing argument to \"list\" command");
            }
            else if (args[0].equals("startvm"))
            {
                if (args.length == 2)
                {
                    c.startVM(args[1]);
                }
                else
                    System.out.println("Error: Missing argument to \"startvm\" command");
            }
            else if (args[0].equals("test"))
            {
                c.test3();
            }
            else
                System.out.println("Error: Unknown command: \"" + args[0] + "\".");

            c.cleanup();
        }
    }
}
