/* $Id: MsiHackExtension.cs 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * MsiHackExtension - Wix Extension that loads MsiHack.dll
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


using Microsoft.Tools.WindowsInstallerXml;
using System;                           /* For Console. */
using System.Reflection;                /* For Assembly*(). */
using System.Runtime.InteropServices;   /* For DllImport. */
using System.IO;                        /* For Path. */



[assembly: AssemblyTitle("org.virtualbox.wix.msi.speed.hack")]
[assembly: AssemblyDescription("Speeding up MSI.DLL")]
[assembly: AssemblyConfiguration("")]
[assembly: AssemblyCompany("Oracle Corporation")]
[assembly: AssemblyProduct("org.virtualbox.wix.msi.speed.hack")]
[assembly: AssemblyCopyright("Copyright (C) 2016")]
[assembly: AssemblyTrademark("")]
[assembly: AssemblyCulture("")]
[assembly: AssemblyDefaultWixExtension(typeof(MsiHackExtension))]


static class NativeMethods
{
    [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    public static extern IntPtr LoadLibrary(string strPath);
}


public class MsiHackExtension : WixExtension
{
    public MsiHackExtension()
    {
        /* Figure out where we are. */
        string strCodeBase = Assembly.GetExecutingAssembly().CodeBase;
        //Console.WriteLine("MsiHackExtension: strCodeBase={0}", strCodeBase);

        UriBuilder uri = new UriBuilder(strCodeBase);
        string strPath = Uri.UnescapeDataString(uri.Path);
        //Console.WriteLine("MsiHackExtension: strPath={0}", strPath);

        string strDir = Path.GetDirectoryName(strPath);
        //Console.WriteLine("MsiHackExtension: strDir={0}", strDir);

        string strHackDll = strDir + "\\MsiHack.dll";
        //Console.WriteLine("strHackDll={0}", strHackDll);

        try
        {
            IntPtr hHackDll = NativeMethods.LoadLibrary(strHackDll);
            Console.WriteLine("MsiHackExtension: Loaded {0} at {1}!", strHackDll, hHackDll.ToString("X"));
        }
        catch (Exception Xcpt)
        {
            Console.WriteLine("MsiHackExtension: Exception loading {0}: {1}", strHackDll, Xcpt);
        }
    }
}

