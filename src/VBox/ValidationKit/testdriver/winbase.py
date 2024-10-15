# -*- coding: utf-8 -*-
# $Id: winbase.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
This module is here to externalize some Windows specifics that gives pychecker
a hard time when running on non-Windows systems.
"""

__copyright__ = \
"""
Copyright (C) 2010-2024 Oracle and/or its affiliates.

This file is part of VirtualBox base platform packages, as
available from https://www.virtualbox.org.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, in version 3 of the
License.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <https://www.gnu.org/licenses>.

The contents of this file may alternatively be used under the terms
of the Common Development and Distribution License Version 1.0
(CDDL), a copy of it is provided in the "COPYING.CDDL" file included
in the VirtualBox distribution, in which case the provisions of the
CDDL are applicable instead of those of the GPL.

You may elect to license modified versions of this file under the
terms and conditions of either the GPL or the CDDL or both.

SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
"""
__version__ = "$Revision: 106320 $"


# Standard Python imports.
import ctypes;
import os;
import sys;

# Windows specific imports.
import pywintypes;          # pylint: disable=import-error
import winerror;            # pylint: disable=import-error
import win32con;            # pylint: disable=import-error
import win32api;            # pylint: disable=import-error
import win32console;        # pylint: disable=import-error
import win32event;          # pylint: disable=import-error
import win32process;        # pylint: disable=import-error

# Validation Kit imports.
from testdriver import reporter;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int;             # pylint: disable=redefined-builtin,invalid-name


#
# Windows specific implementation of base functions.
#

def processInterrupt(uPid):
    """
    The Windows version of base.processInterrupt

    Note! This doesn't work terribly well with a lot of processes.
    """
    try:
        # pylint: disable=no-member
        win32console.GenerateConsoleCtrlEvent(win32con.CTRL_BREAK_EVENT, uPid);         # pylint: disable=c-extension-no-member
        #GenerateConsoleCtrlEvent = ctypes.windll.kernel32.GenerateConsoleCtrlEvent
        #rc = GenerateConsoleCtrlEvent(1, uPid);
        #reporter.log('GenerateConsoleCtrlEvent -> %s' % (rc,));
        fRc = True;
    except:
        reporter.logXcpt('uPid=%s' % (uPid,));
        fRc = False;
    return fRc;

def postThreadMesssageClose(uTid):
    """ Posts a WM_CLOSE message to the specified thread."""
    fRc = False;
    try:
        win32api.PostThreadMessage(uTid, win32con.WM_CLOSE, 0, 0);              # pylint: disable=no-member,c-extension-no-member
        fRc = True;
    except:
        reporter.logXcpt('uTid=%s' % (uTid,));
    return fRc;

def postThreadMesssageQuit(uTid):
    """ Posts a WM_QUIT message to the specified thread."""
    fRc = False;
    try:
        win32api.PostThreadMessage(uTid, win32con.WM_QUIT,                      # pylint: disable=no-member,c-extension-no-member
                                   0x40010004, 0); # DBG_TERMINATE_PROCESS
        fRc = True;
    except:
        reporter.logXcpt('uTid=%s' % (uTid,));
    return fRc;

def processTerminate(uPid):
    """ The Windows version of base.processTerminate """
    # pylint: disable=no-member
    fRc = False;
    try:
        hProcess = win32api.OpenProcess(win32con.PROCESS_TERMINATE,             # pylint: disable=no-member,c-extension-no-member
                                        False, uPid);
    except:
        reporter.logXcpt('uPid=%s' % (uPid,));
    else:
        try:
            win32process.TerminateProcess(hProcess,                             # pylint: disable=no-member,c-extension-no-member
                                          0x40010004); # DBG_TERMINATE_PROCESS
            fRc = True;
        except:
            reporter.logXcpt('uPid=%s' % (uPid,));
        hProcess.Close(); #win32api.CloseHandle(hProcess)
    return fRc;

def processKill(uPid):
    """ The Windows version of base.processKill """
    return processTerminate(uPid);

def processExists(uPid):
    """ The Windows version of base.processExists """
    # We try open the process for waiting since this is generally only forbidden in a very few cases.
    try:
        hProcess = win32api.OpenProcess(win32con.SYNCHRONIZE, False, uPid);     # pylint: disable=no-member,c-extension-no-member
    except pywintypes.error as oXcpt:                                           # pylint: disable=no-member
        if oXcpt.winerror == winerror.ERROR_INVALID_PARAMETER:
            return False;
        if oXcpt.winerror != winerror.ERROR_ACCESS_DENIED:
            reporter.logXcpt('uPid=%s oXcpt=%s' % (uPid, oXcpt));
            return False;
        reporter.logXcpt('uPid=%s oXcpt=%s' % (uPid, oXcpt));
    except Exception as oXcpt:
        reporter.logXcpt('uPid=%s oXcpt=%s' % (uPid, oXcpt));
        return False;
    else:
        hProcess.Close(); #win32api.CloseHandle(hProcess)
    return True;

def processCheckPidAndName(uPid, sName):
    """ The Windows version of base.processCheckPidAndName """
    fRc = processExists(uPid);
    if fRc is True:
        try:
            from win32com.client import GetObject; # pylint: disable=import-error
            oWmi = GetObject('winmgmts:');
            aoProcesses = oWmi.InstancesOf('Win32_Process');
            for oProcess in aoProcesses:
                if long(oProcess.Properties_("ProcessId").Value) == uPid:
                    sCurName = oProcess.Properties_("Name").Value;
                    reporter.log2('uPid=%s sName=%s sCurName=%s' % (uPid, sName, sCurName));
                    sName    = sName.lower();
                    sCurName = sCurName.lower();
                    if os.path.basename(sName) == sName:
                        sCurName = os.path.basename(sCurName);

                    if  sCurName == sName \
                    or sCurName + '.exe' == sName \
                    or sCurName == sName  + '.exe':
                        fRc = True;
                    break;
        except:
            reporter.logXcpt('uPid=%s sName=%s' % (uPid, sName));
    return fRc;

#
# Some helper functions.
#
def processCreate(sName, asArgs):
    """
    Returns a (pid, handle, tid) tuple on success. (-1, None) on failure (logged).
    """

    # Construct a command line.
    sCmdLine = '';
    for sArg in asArgs:
        if sCmdLine == '':
            sCmdLine += '"';
        else:
            sCmdLine += ' "';
        sCmdLine += sArg;
        sCmdLine += '"';

    # Try start the process.
    # pylint: disable=no-member
    dwCreationFlags = win32con.CREATE_NEW_PROCESS_GROUP;
    oStartupInfo    = win32process.STARTUPINFO();                                       # pylint: disable=c-extension-no-member
    try:
        (hProcess, hThread, uPid, uTid) = win32process.CreateProcess(sName,             # pylint: disable=c-extension-no-member
            sCmdLine,                   # CommandLine
            None,                       # ProcessAttributes
            None,                       # ThreadAttibutes
            1,                          # fInheritHandles
            dwCreationFlags,
            None,                       # Environment
            None,                       # CurrentDirectory.
            oStartupInfo);
    except:
        reporter.logXcpt('sName="%s" sCmdLine="%s"' % (sName, sCmdLine));
        return (-1, None, -1);

    # Dispense with the thread handle.
    try:
        hThread.Close(); # win32api.CloseHandle(hThread);
    except:
        reporter.logXcpt();

    # Try get full access to the process.
    try:
        hProcessFullAccess = win32api.DuplicateHandle(                                  # pylint: disable=c-extension-no-member
            win32api.GetCurrentProcess(),                                               # pylint: disable=c-extension-no-member
            hProcess,
            win32api.GetCurrentProcess(),                                               # pylint: disable=c-extension-no-member
            win32con.PROCESS_TERMINATE
            | win32con.PROCESS_QUERY_INFORMATION
            | win32con.SYNCHRONIZE
            | win32con.DELETE,
            False,
            0);
        hProcess.Close(); # win32api.CloseHandle(hProcess);
        hProcess = hProcessFullAccess;
    except:
        reporter.logXcpt();
    reporter.log2('processCreate -> %#x, hProcess=%s %#x' % (uPid, hProcess, hProcess.handle,));
    return (uPid, hProcess, uTid);

def processPollByHandle(hProcess):
    """
    Polls the process handle to see if it has finished (True) or not (False).
    """
    try:
        dwWait = win32event.WaitForSingleObject(hProcess, 0);                   # pylint: disable=no-member,c-extension-no-member
    except:
        reporter.logXcpt('hProcess=%s %#x' % (hProcess, hProcess.handle,));
        return True;
    return dwWait != win32con.WAIT_TIMEOUT; #0x102; #


def processTerminateByHandle(hProcess):
    """
    Terminates the process.
    """
    try:
        win32api.TerminateProcess(hProcess,                                     # pylint: disable=no-member,c-extension-no-member
                                  0x40010004); # DBG_TERMINATE_PROCESS
    except:
        reporter.logXcpt('hProcess=%s %#x' % (hProcess, hProcess.handle,));
        return False;
    return True;

#
# Misc
#

def logMemoryStats():
    """
    Logs windows memory stats.
    """
    class MemoryStatusEx(ctypes.Structure):
        """ MEMORYSTATUSEX """
        kaFields = [
            ( 'dwLength',                    ctypes.c_ulong ),
            ( 'dwMemoryLoad',                ctypes.c_ulong ),
            ( 'ullTotalPhys',                ctypes.c_ulonglong ),
            ( 'ullAvailPhys',                ctypes.c_ulonglong ),
            ( 'ullTotalPageFile',            ctypes.c_ulonglong ),
            ( 'ullAvailPageFile',            ctypes.c_ulonglong ),
            ( 'ullTotalVirtual',             ctypes.c_ulonglong ),
            ( 'ullAvailVirtual',             ctypes.c_ulonglong ),
            ( 'ullAvailExtendedVirtual',     ctypes.c_ulonglong ),
        ];
        _fields_ = kaFields; # pylint: disable=invalid-name

        def __init__(self):
            super(MemoryStatusEx, self).__init__();
            self.dwLength = ctypes.sizeof(self);

    try:
        oStats = MemoryStatusEx();
        ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(oStats));
    except:
        reporter.logXcpt();
        return False;

    reporter.log('Memory statistics:');
    for sField, _ in MemoryStatusEx.kaFields:
        reporter.log('  %32s: %s' % (sField, getattr(oStats, sField)));
    return True;

def checkProcessHeap():
    """
    Calls HeapValidate(GetProcessHeap(), 0, NULL);
    """

    # Get the process heap.
    try:
        hHeap = ctypes.windll.kernel32.GetProcessHeap();
    except:
        reporter.logXcpt();
        return False;

    # Check it.
    try:
        fIsOkay = ctypes.windll.kernel32.HeapValidate(hHeap, 0, None);
    except:
        reporter.logXcpt();
        return False;

    if fIsOkay == 0:
        reporter.log('HeapValidate failed!');

        # Try trigger a dump using c:\utils\procdump64.exe.
        from common import utils;

        iPid = os.getpid();
        asArgs = [ 'e:\\utils\\procdump64.exe', '-ma', '%s' % (iPid,), 'c:\\CrashDumps\\python.exe-%u-heap.dmp' % (iPid,)];
        if utils.getHostArch() != 'amd64':
            asArgs[0] = 'c:\\utils\\procdump.exe'
        reporter.log('Trying to dump this process using: %s' % (asArgs,));
        utils.processCall(asArgs);

        # Generate a crash exception.
        ctypes.windll.msvcrt.strcpy(None, None, 1024);

    return True;

