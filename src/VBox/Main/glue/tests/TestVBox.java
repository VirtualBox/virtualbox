/* $Id: TestVBox.java 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/*! file
 * Small sample/testcase which demonstrates that the same source code can
 * be used to connect to the webservice and (XP)COM APIs.
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

import org.virtualbox_6_2.*;
import java.util.List;
import java.util.Arrays;
import java.util.ArrayList;
import java.math.BigInteger;

public class TestVBox
{
    static void processEvent(IEvent ev)
    {
        System.out.println("got event: " + ev);
        VBoxEventType type = ev.getType();
        System.out.println("type = " + type);
        switch (type)
        {
            case OnMachineStateChanged:
            {
                IMachineStateChangedEvent mcse = IMachineStateChangedEvent.queryInterface(ev);
                if (mcse == null)
                    System.out.println("Cannot query an interface");
                else
                    System.out.println("mid=" + mcse.getMachineId());
                break;
            }
        }
    }

    static class EventHandler
    {
        EventHandler() {}
        public void handleEvent(IEvent ev)
        {
            try {
                processEvent(ev);
            } catch (Throwable t) {
                t.printStackTrace();
            }
        }
    }

    static void testEvents(VirtualBoxManager mgr, IEventSource es)
    {
        // active mode for Java doesn't fully work yet, and using passive
        // is more portable (the only mode for MSCOM and WS) and thus generally
        // recommended
        IEventListener listener = es.createListener();

        es.registerListener(listener, Arrays.asList(VBoxEventType.Any), false);

        try {
            for (int i = 0; i < 50; i++)
            {
                System.out.print(".");
                IEvent ev = es.getEvent(listener, 500);
                if (ev != null)
                {
                    processEvent(ev);
                    es.eventProcessed(listener, ev);
                }
                // process system event queue
                mgr.waitForEvents(0);
            }
        } catch (Exception e) {
            e.printStackTrace();
        }

        es.unregisterListener(listener);
    }

    static void testEnumeration(VirtualBoxManager mgr, IVirtualBox vbox)
    {
        List<IMachine> machs = vbox.getMachines();
        for (IMachine m : machs)
        {
            String name;
            Long ram = 0L;
            boolean hwvirtEnabled = false, hwvirtNestedPaging = false;
            boolean paeEnabled = false;
            boolean inaccessible = false;
            try
            {
                name = m.getName();
                ram = m.getMemorySize();
                hwvirtEnabled = m.getHWVirtExProperty(HWVirtExPropertyType.Enabled);
                hwvirtNestedPaging = m.getHWVirtExProperty(HWVirtExPropertyType.NestedPaging);
                paeEnabled = m.getCPUProperty(CPUPropertyType.PAE);
                String osType = m.getOSTypeId();
                IGuestOSType foo = vbox.getGuestOSType(osType);
            }
            catch (VBoxException e)
            {
                name = "<inaccessible>";
                inaccessible = true;
            }
            System.out.println("VM name: " + name);
            if (!inaccessible)
            {
                System.out.println(" RAM size: " + ram + "MB"
                                   + ", HWVirt: " + hwvirtEnabled
                                   + ", Nested Paging: " + hwvirtNestedPaging
                                   + ", PAE: " + paeEnabled);
            }
        }
        // process system event queue
        mgr.waitForEvents(0);
    }

    static boolean progressBar(VirtualBoxManager mgr, IProgress p, long waitMillis)
    {
        long end = System.currentTimeMillis() + waitMillis;
        while (!p.getCompleted())
        {
            // process system event queue
            mgr.waitForEvents(0);
            // wait for completion of the task, but at most 200 msecs
            p.waitForCompletion(200);
            if (System.currentTimeMillis() >= end)
                return false;
        }
        return true;
    }

    static void testStart(VirtualBoxManager mgr, IVirtualBox vbox)
    {
        IMachine m = vbox.getMachines().get(0);
        String name = m.getName();
        System.out.println("\nAttempting to start VM '" + name + "'");

        ISession session = mgr.getSessionObject();
        ArrayList<String> env = new ArrayList<String>();
        IProgress p = m.launchVMProcess(session, "gui", env);
        progressBar(mgr, p, 10000);
        session.unlockMachine();
        // process system event queue
        mgr.waitForEvents(0);
    }

    static void testMultiServer()
    {
        VirtualBoxManager mgr1 = VirtualBoxManager.createInstance(null);
        VirtualBoxManager mgr2 = VirtualBoxManager.createInstance(null);

        try {
            mgr1.connect("http://i7:18083", "", "");
            mgr2.connect("http://main:18083", "", "");

            IMachine m1 = mgr1.getVBox().getMachines().get(0);
            IMachine m2 = mgr2.getVBox().getMachines().get(0);
            String name1 = m1.getName();
            String name2 = m2.getName();
            ISession session1 = mgr1.getSessionObject();
            ISession session2 = mgr2.getSessionObject();
            ArrayList<String> env = new ArrayList<String>();
            IProgress p1 = m1.launchVMProcess(session1, "gui", env);
            IProgress p2 = m2.launchVMProcess(session2, "gui", env);
            progressBar(mgr1, p1, 10000);
            progressBar(mgr2, p2, 10000);
            session1.unlockMachine();
            session2.unlockMachine();
            // process system event queue
            mgr1.waitForEvents(0);
            mgr2.waitForEvents(0);
        } finally {
            mgr1.cleanup();
            mgr2.cleanup();
        }
    }

    static void testReadLog(VirtualBoxManager mgr, IVirtualBox vbox)
    {
        IMachine m =  vbox.getMachines().get(0);
        long logNo = 0;
        long off = 0;
        long size = 16 * 1024;
        while (true)
        {
            byte[] buf = m.readLog(logNo, off, size);
            if (buf.length == 0)
                break;
            System.out.print(new String(buf));
            off += buf.length;
        }
        // process system event queue
        mgr.waitForEvents(0);
    }

    static void printErrorInfo(VBoxException e)
    {
        System.out.println("VBox error: " + e.getMessage());
        System.out.println("Error cause message: " + e.getCause());
        System.out.println("Overall result code: " + Integer.toHexString(e.getResultCode()));
        int i = 1;
        for (IVirtualBoxErrorInfo ei = e.getVirtualBoxErrorInfo(); ei != null; ei = ei.getNext(), i++)
        {
            System.out.println("Detail information #" + i);
            System.out.println("Error mesage: " + ei.getText());
            System.out.println("Result code:  " + Integer.toHexString(ei.getResultCode()));
            // optional, usually provides little additional information:
            System.out.println("Component:    " + ei.getComponent());
            System.out.println("Interface ID: " + ei.getInterfaceID());
        }
    }


    public static void main(String[] args)
    {
        VirtualBoxManager mgr = VirtualBoxManager.createInstance(null);

        boolean ws = false;
        String  url = null;
        String  user = null;
        String  passwd = null;

        for (int i = 0; i < args.length; i++)
        {
            if (args[i].equals("-w"))
                ws = true;
            else if (args[i].equals("-url"))
                url = args[++i];
            else if (args[i].equals("-user"))
                user = args[++i];
            else if (args[i].equals("-passwd"))
                passwd = args[++i];
        }

        if (ws)
        {
            try {
                mgr.connect(url, user, passwd);
            } catch (VBoxException e) {
                e.printStackTrace();
                System.out.println("Cannot connect, start webserver first!");
            }
        }

        try
        {
            IVirtualBox vbox = mgr.getVBox();
            if (vbox != null)
            {
                System.out.println("VirtualBox version: " + vbox.getVersion() + "\n");
                testEnumeration(mgr, vbox);
                testReadLog(mgr, vbox);
                testStart(mgr, vbox);
                testEvents(mgr, vbox.getEventSource());

                System.out.println("done, press Enter...");
                int ch = System.in.read();
            }
        }
        catch (VBoxException e)
        {
            printErrorInfo(e);
            System.out.println("Java stack trace:");
            e.printStackTrace();
        }
        catch (RuntimeException e)
        {
            System.out.println("Runtime error: " + e.getMessage());
            e.printStackTrace();
        }
        catch (java.io.IOException e)
        {
            e.printStackTrace();
        }

        // process system event queue
        mgr.waitForEvents(0);
        if (ws)
        {
            try {
                mgr.disconnect();
            } catch (VBoxException e) {
                e.printStackTrace();
            }
        }

        mgr.cleanup();

    }

}
