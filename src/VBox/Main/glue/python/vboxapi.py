# -*- coding: utf-8 -*-
# $Id: vboxapi.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=import-error -- for cross-platform Win32 imports
# pylint: disable=unused-import
# pylint: disable=protected-access -- for XPCOM _xpcom member
"""
VirtualBox Python API Glue.
"""

__copyright__ = \
"""
Copyright (C) 2009-2024 Oracle and/or its affiliates.

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


# Note! To set Python bitness on OSX use 'export VERSIONER_PYTHON_PREFER_32_BIT=yes'


# Standard Python imports.
import os
import sys
import traceback


if sys.version_info >= (3, 0):
    xrange = range # pylint: disable=invalid-name
    long = int     # pylint: disable=invalid-name

#
# Globals, environment and sys.path changes.
#
import platform
g_sVBoxBinDir = os.environ.get("VBOX_PROGRAM_PATH", None)
g_sVBoxSdkDir = os.environ.get("VBOX_SDK_PATH", None)

if g_sVBoxBinDir is None:
    if platform.system() == 'Darwin':
        g_sVBoxBinDir = '/Applications/VirtualBox.app/Contents/MacOS'
    else: # Will be set by the installer
        g_sVBoxBinDir = "%VBOX_INSTALL_PATH%"
else:
    g_sVBoxBinDir = os.path.abspath(g_sVBoxBinDir)

if g_sVBoxSdkDir is None:
    if platform.system() == 'Darwin':
        g_sVBoxSdkDir = '/Applications/VirtualBox.app/Contents/MacOS/sdk'
    else: # Will be set by the installer
        g_sVBoxSdkDir = "%VBOX_SDK_PATH%"
else:
    g_sVBoxSdkDir = os.path.abspath(g_sVBoxSdkDir)

os.environ["VBOX_PROGRAM_PATH"] = g_sVBoxBinDir
os.environ["VBOX_SDK_PATH"] = g_sVBoxSdkDir
sys.path.append(g_sVBoxBinDir)


#
# Import the generated VirtualBox constants.
#
from .VirtualBox_constants import VirtualBoxReflectionInfo


class PerfCollector(object):
    """ This class provides a wrapper over IPerformanceCollector in order to
    get more 'pythonic' interface.

    To begin collection of metrics use setup() method.

    To get collected data use query() method.

    It is possible to disable metric collection without changing collection
    parameters with disable() method. The enable() method resumes metric
    collection.
    """

    def __init__(self, mgr, vbox):
        """ Initializes the instance.

        """
        self.mgr = mgr
        self.isMscom = mgr.type == 'MSCOM'
        self.collector = vbox.performanceCollector

    def setup(self, names, objects, period, nsamples):
        """ Discards all previously collected values for the specified
        metrics, sets the period of collection and the number of retained
        samples, enables collection.
        """
        self.collector.setupMetrics(names, objects, period, nsamples)

    def enable(self, names, objects):
        """ Resumes metric collection for the specified metrics.
        """
        self.collector.enableMetrics(names, objects)

    def disable(self, names, objects):
        """ Suspends metric collection for the specified metrics.
        """
        self.collector.disableMetrics(names, objects)

    def query(self, names, objects):
        """ Retrieves collected metric values as well as some auxiliary
        information. Returns an array of dictionaries, one dictionary per
        metric. Each dictionary contains the following entries:
        'name': metric name
        'object': managed object this metric associated with
        'unit': unit of measurement
        'scale': divide 'values' by this number to get float numbers
        'values': collected data
        'values_as_string': pre-processed values ready for 'print' statement
        """
        # Get around the problem with input arrays returned in output
        # parameters (see #3953) for MSCOM.
        if self.isMscom:
            (values, names, objects, names_out, objects_out, units, scales, _sequence_numbers,
             indices, lengths) = self.collector.queryMetricsData(names, objects)
        else:
            (values, names_out, objects_out, units, scales, _sequence_numbers,
             indices, lengths) = self.collector.queryMetricsData(names, objects)
        out = []
        for i, _ in enumerate(names_out):
            scale = int(scales[i])
            if scale != 1:
                fmt = '%.2f%s'
            else:
                fmt = '%d %s'
            out.append({
                'name': str(names_out[i]),
                'object': str(objects_out[i]),
                'unit': str(units[i]),
                'scale': scale,
                'values': [int(values[j]) for j in xrange(int(indices[i]), int(indices[i]) + int(lengths[i]))],
                'values_as_string': '[' + ', '.join([fmt % (int(values[j]) / scale, units[i]) for j in
                                                     xrange(int(indices[i]), int(indices[i]) + int(lengths[i]))]) + ']'
            })
        return out


#
# Attribute hacks.
#
def comifyName(name):
    return name[0].capitalize() + name[1:]


## This is for saving the original DispatchBaseClass __getattr__ and __setattr__
#  method references.
_g_dCOMForward = {}


def _CustomGetAttr(self, sAttr):
    """ Our getattr replacement for DispatchBaseClass. """
    # Fastpath.
    oRet = self.__class__.__dict__.get(sAttr)
    if oRet is not None:
        return oRet

    # Try case-insensitivity workaround for class attributes (COM methods).
    sAttrLower = sAttr.lower()
    for k in list(self.__class__.__dict__.keys()):
        if k.lower() == sAttrLower:
            setattr(self.__class__, sAttr, self.__class__.__dict__[k])
            return getattr(self, k)

    # Slow path.
    try:
        return _g_dCOMForward['getattr'](self, comifyName(sAttr))
    except AttributeError:
        return _g_dCOMForward['getattr'](self, sAttr)


def _CustomSetAttr(self, sAttr, oValue):
    """ Our setattr replacement for DispatchBaseClass. """
    try:
        return _g_dCOMForward['setattr'](self, comifyName(sAttr), oValue)
    except AttributeError:
        return _g_dCOMForward['setattr'](self, sAttr, oValue)


class PlatformBase(object):
    """
    Base class for the platform specific code.
    """

    def __init__(self, aoParams):
        _ = aoParams

    def getVirtualBox(self):
        """
        Gets a the IVirtualBox singleton.
        """
        return None

    def getSessionObject(self):
        """
        Get a session object that can be used for opening machine sessions.

        The oIVBox parameter is an getVirtualBox() return value, i.e. an
        IVirtualBox reference.

        See also openMachineSession.
        """
        return None

    def getType(self):
        """ Returns the platform type (class name sans 'Platform'). """
        return None

    def isRemote(self):
        """
        Returns True if remote (web services) and False if local (COM/XPCOM).
        """
        return False

    def getArray(self, oInterface, sAttrib):
        """
        Retrives the value of the array attribute 'sAttrib' from
        interface 'oInterface'.

        This is for hiding platform specific differences in attributes
        returning arrays.
        """
        _ = oInterface
        _ = sAttrib
        return None

    def setArray(self, oInterface, sAttrib, aoArray):
        """
        Sets the value (aoArray) of the array attribute 'sAttrib' in
        interface 'oInterface'.

        This is for hiding platform specific differences in attributes
        setting arrays.
        """
        _ = oInterface
        _ = sAttrib
        _ = aoArray
        return None

    def initPerThread(self):
        """
        Does backend specific initialization for the calling thread.
        """
        return True

    def deinitPerThread(self):
        """
        Does backend specific uninitialization for the calling thread.
        """
        return True

    def createListener(self, oImplClass, dArgs):
        """
        Instantiates and wraps an active event listener class so it can be
        passed to an event source for registration.

        oImplClass is a class (type, not instance) which implements
        IEventListener.

        dArgs is a dictionary with string indexed variables.  This may be
        modified by the method to pass platform specific parameters. Can
        be None.

        This currently only works on XPCOM.  COM support is not possible due to
        shortcuts taken in the COM bridge code, which is not under our control.
        Use passive listeners for COM and web services.
        """
        _ = oImplClass
        _ = dArgs
        raise Exception("No active listeners for this platform")

    def waitForEvents(self, cMsTimeout):
        """
        Wait for events to arrive and process them.

        The timeout (cMsTimeout) is in milliseconds for how long to wait for
        events to arrive.  A negative value means waiting for ever, while 0
        does not wait at all.

        Returns 0 if events was processed.
        Returns 1 if timed out or interrupted in some way.
        Returns 2 on error (like not supported for web services).

        Raises an exception if the calling thread is not the main thread (the one
        that initialized VirtualBoxManager) or if the time isn't an integer.
        """
        _ = cMsTimeout
        return 2

    def interruptWaitEvents(self):
        """
        Interrupt a waitForEvents call.
        This is normally called from a worker thread to wake up the main thread.

        Returns True on success, False on failure.
        """
        return False

    def deinit(self):
        """
        Unitializes the platform specific backend.
        """
        return None

    def queryInterface(self, _oIUnknown, _sClassName):
        """
        IUnknown::QueryInterface wrapper.

        oIUnknown is who to ask.
        sClassName is the name of the interface we're asking for.
        """
        return None

    #
    # Error (exception) access methods.
    #

    def xcptGetStatus(self, _oXcpt):
        """
        Returns the COM status code from the VBox API given exception.
        """
        return None

    def xcptIsDeadInterface(self, _oXcpt):
        """
        Returns True if the exception indicates that the interface is dead, False if not.
        """
        return False

    def xcptIsEqual(self, oXcpt, hrStatus):
        """
        Checks if the exception oXcpt is equal to the COM/XPCOM status code
        hrStatus.

        The oXcpt parameter can be any kind of object, we'll just return True
        if it doesn't behave like a our exception class.

        Will not raise any exception as long as hrStatus and self are not bad.
        """
        try:
            hrXcpt = self.xcptGetStatus(oXcpt) # pylint: disable=assignment-from-none
        except AttributeError:
            return False
        if hrXcpt == hrStatus:
            return True

        # Fudge for 32-bit signed int conversion.
        if 0x7fffffff < hrStatus <= 0xffffffff and hrXcpt < 0:
            if (hrStatus - 0x100000000) == hrXcpt:
                return True
        return False

    def xcptGetMessage(self, _oXcpt):
        """
        Returns the best error message found in the COM-like exception.
        Returns None to fall back on xcptToString.
        Raises exception if oXcpt isn't our kind of exception object.
        """
        return None

    def xcptGetBaseXcpt(self):
        """
        Returns the base exception class.
        """
        return None

    def xcptSetupConstants(self, oDst):
        """
        Copy/whatever all error constants onto oDst.
        """
        return oDst

    @staticmethod
    def xcptCopyErrorConstants(oDst, oSrc):
        """
        Copy everything that looks like error constants from oDst to oSrc.
        """
        for sAttr in dir(oSrc):
            if sAttr[0].isupper() and (sAttr[1].isupper() or sAttr[1] == '_'):
                oAttr = getattr(oSrc, sAttr)
                if isinstance(oAttr, int):
                    setattr(oDst, sAttr, oAttr)
        return oDst


class PlatformMSCOM(PlatformBase):
    """
    Platform specific code for MS COM.
    """

    ## @name VirtualBox COM Typelib definitions (should be generate)
    #
    # @remarks Must be updated when the corresponding VirtualBox.xidl bits
    #          are changed.  Fortunately this isn't very often.
    # @{
    VBOX_TLB_GUID = '{D7569351-1750-46F0-936E-BD127D5BC264}'
    VBOX_TLB_LCID = 0
    VBOX_TLB_MAJOR = 1
    VBOX_TLB_MINOR = 3
    ## @}

    def __init__(self, dParams):
        PlatformBase.__init__(self, dParams)

        #
        # Since the code runs on all platforms, we have to do a lot of
        # importing here instead of at the top of the file where it's normally located.
        #
        from win32com import universal
        from win32com.client import gencache, DispatchBaseClass
        from win32com.client import constants, getevents
        import win32com
        import pythoncom
        import win32api
        import winerror
        from win32con import DUPLICATE_SAME_ACCESS
        from win32api import GetCurrentThread, GetCurrentThreadId, DuplicateHandle, GetCurrentProcess
        import threading

        self.winerror = winerror
        self.oHandle  = None;

        # Setup client impersonation in COM calls.
        try:
            pythoncom.CoInitializeSecurity(None,
                                           None,
                                           None,
                                           pythoncom.RPC_C_AUTHN_LEVEL_DEFAULT,
                                           pythoncom.RPC_C_IMP_LEVEL_IMPERSONATE,
                                           None,
                                           pythoncom.EOAC_NONE,
                                           None)
        except:
            _, oXcpt, _ = sys.exc_info();
            if isinstance(oXcpt, pythoncom.com_error) and self.xcptGetStatus(oXcpt) == -2147417831: # RPC_E_TOO_LATE
                print("Warning: CoInitializeSecurity was already called");
            else:
                print("Warning: CoInitializeSecurity failed: ", oXcpt);

        # Remember this thread ID and get its handle so we can wait on it in waitForEvents().
        self.tid = GetCurrentThreadId()
        pid = GetCurrentProcess()
        self.aoHandles = [DuplicateHandle(pid, GetCurrentThread(), pid, 0, 0, DUPLICATE_SAME_ACCESS),] # type: list[PyHANDLE]

        # Hack the COM dispatcher base class so we can modify method and
        # attribute names to match those in xpcom.
        if 'setattr' not in _g_dCOMForward:
            _g_dCOMForward['getattr'] = DispatchBaseClass.__dict__['__getattr__'] # before setattr which we test for.
            _g_dCOMForward['setattr'] = DispatchBaseClass.__dict__['__setattr__']
            setattr(DispatchBaseClass, '__getattr__', _CustomGetAttr)
            setattr(DispatchBaseClass, '__setattr__', _CustomSetAttr)

        # Hack the exception base class so the users doesn't need to check for
        # XPCOM or COM and do different things.
        ## @todo

        #
        # Make sure the gencache is correct (we don't quite follow the COM
        # versioning rules).
        #
        self.flushGenPyCache(win32com.client.gencache)
        win32com.client.gencache.EnsureDispatch('VirtualBox.Session')
        win32com.client.gencache.EnsureDispatch('VirtualBox.VirtualBox')
        win32com.client.gencache.EnsureDispatch('VirtualBox.VirtualBoxClient')

        self.oClient = None     ##< instance of client used to support lifetime of VBoxSDS
        self.oIntCv = threading.Condition()
        self.fInterrupted = False

        _ = dParams

    def flushGenPyCache(self, oGenCache):
        """
        Flushes VBox related files in the win32com gen_py cache.

        This is necessary since we don't follow the typelib versioning rules
        that everyeone else seems to subscribe to.
        """
        #
        # The EnsureModule method have broken validation code, it doesn't take
        # typelib module directories into account.  So we brute force them here.
        # (It's possible the directory approach is from some older pywin
        # version or the result of runnig makepy or gencache manually, but we
        # need to cover it as well.)
        #
        sName = oGenCache.GetGeneratedFileName(self.VBOX_TLB_GUID, self.VBOX_TLB_LCID,
                                               self.VBOX_TLB_MAJOR, self.VBOX_TLB_MINOR)
        sGenPath = oGenCache.GetGeneratePath()
        if len(sName) > 36 and len(sGenPath) > 5:
            sTypelibPath = os.path.join(sGenPath, sName)
            if os.path.isdir(sTypelibPath):
                import shutil
                shutil.rmtree(sTypelibPath, ignore_errors=True)

        #
        # Ensure that our typelib is valid.
        #
        return oGenCache.EnsureModule(self.VBOX_TLB_GUID, self.VBOX_TLB_LCID, self.VBOX_TLB_MAJOR, self.VBOX_TLB_MINOR)

    def getSessionObject(self):
        import win32com
        from win32com.client import Dispatch
        return win32com.client.Dispatch("VirtualBox.Session")

    def getVirtualBox(self):
        # Caching self.oClient is the trick for SDS. It allows to keep the
        # VBoxSDS in the memory  until the end of PlatformMSCOM lifetme.
        if self.oClient is None:
            import win32com
            from win32com.client import Dispatch
            self.oClient = win32com.client.Dispatch("VirtualBox.VirtualBoxClient")
        return self.oClient.virtualBox

    def getType(self):
        return 'MSCOM'

    def getArray(self, oInterface, sAttrib):
        return oInterface.__getattr__(sAttrib) # pylint: disable=unnecessary-dunder-call

    def setArray(self, oInterface, sAttrib, aoArray):
        #
        # HACK ALERT!
        #
        # With pywin32 build 218, we're seeing type mismatch errors here for
        # IGuestSession::environmentChanges (safearray of BSTRs). The Dispatch
        # object (_oleobj_) seems to get some type conversion wrong and COM
        # gets upset.  So, we redo some of the dispatcher work here, picking
        # the missing type information from the getter.
        #
        oOleObj     = getattr(oInterface, '_oleobj_')
        aPropMapGet = getattr(oInterface, '_prop_map_get_')
        aPropMapPut = getattr(oInterface, '_prop_map_put_')
        sComAttrib  = sAttrib if sAttrib in aPropMapGet else comifyName(sAttrib)
        try:
            aArgs, _aDefaultArgs = aPropMapPut[sComAttrib]
            aGetArgs             = aPropMapGet[sComAttrib]
        except KeyError: # fallback.
            return oInterface.__setattr__(sAttrib, aoArray) # pylint: disable=unnecessary-dunder-call

        import pythoncom
        oOleObj.InvokeTypes(aArgs[0],                   # dispid
                            aArgs[1],                   # LCID
                            aArgs[2],                   # DISPATCH_PROPERTYPUT
                            (pythoncom.VT_HRESULT, 0),  # retType - or void?
                            (aGetArgs[2],),             # argTypes - trick: we get the type from the getter.
                            aoArray,)                   # The array
        return True

    def initPerThread(self):
        import pythoncom
        pythoncom.CoInitializeEx(0)

    def deinitPerThread(self):
        import pythoncom
        pythoncom.CoUninitialize()

    def createListener(self, oImplClass, dArgs):
        _ = oImplClass; _ = dArgs;
        raise Exception('no active listeners on Windows as PyGatewayBase::QueryInterface() '
                        'returns new gateway objects all the time, thus breaking EventQueue '
                        'assumptions about the listener interface pointer being constants between calls ')

    def waitForEvents(self, cMsTimeout):
        from win32api import GetCurrentThreadId
        from win32event import INFINITE
        from win32event import MsgWaitForMultipleObjects, QS_ALLINPUT, WAIT_TIMEOUT, WAIT_OBJECT_0
        from pythoncom import PumpWaitingMessages
        import types

        if not isinstance(cMsTimeout, int):
            raise TypeError("The timeout argument is not an integer")
        if self.tid != GetCurrentThreadId():
            raise Exception("wait for events from the same thread you inited!")

        if cMsTimeout < 0:
            cMsTimeout = INFINITE
        rc = MsgWaitForMultipleObjects(self.aoHandles, 0, cMsTimeout, QS_ALLINPUT)
        if WAIT_OBJECT_0 <= rc < WAIT_OBJECT_0 + len(self.aoHandles):
            # is it possible?
            rc = 2
        elif rc == WAIT_OBJECT_0 + len(self.aoHandles):
            # Waiting messages
            PumpWaitingMessages()
            rc = 0
        else:
            # Timeout
            rc = 1

        # check for interruption
        self.oIntCv.acquire()
        if self.fInterrupted:
            self.fInterrupted = False
            rc = 1
        self.oIntCv.release()

        return rc

    def interruptWaitEvents(self):
        """
        Basically a python implementation of NativeEventQueue::postEvent().

        The magic value must be in sync with the C++ implementation or this
        won't work.

        Note that because of this method we cannot easily make use of a
        non-visible Window to handle the message like we would like to do.
        """
        from win32api import PostThreadMessage
        from win32con import WM_USER

        self.oIntCv.acquire()
        self.fInterrupted = True
        self.oIntCv.release()
        try:
            PostThreadMessage(self.tid, WM_USER, None, 0xf241b819)
        except:
            return False
        return True

    def deinit(self):
        for oHandle in self.aoHandles:
            if oHandle is not None:
                oHandle.Close();
        self.oHandle = None;

        del self.oClient;
        self.oClient = None;

        # This non-sense doesn't pair up with any pythoncom.CoInitialize[Ex].
        # See @bugref{9037}.
        #import pythoncom
        #pythoncom.CoUninitialize()

    def queryInterface(self, oIUnknown, sClassName):
        from win32com.client import CastTo
        return CastTo(oIUnknown, sClassName)

    def xcptGetStatus(self, oXcpt):
        # The DISP_E_EXCEPTION + excptinfo fun needs checking up, only
        # empirical info on it so far.
        hrXcpt = oXcpt.hresult
        if hrXcpt == self.winerror.DISP_E_EXCEPTION:
            try:
                hrXcpt = oXcpt.excepinfo[5]
            except:
                pass
        return hrXcpt

    def xcptIsDeadInterface(self, oXcpt):
        return self.xcptGetStatus(oXcpt) in [
            0x800706ba, -2147023174,  # RPC_S_SERVER_UNAVAILABLE.
            0x800706be, -2147023170,  # RPC_S_CALL_FAILED.
            0x800706bf, -2147023169,  # RPC_S_CALL_FAILED_DNE.
            0x80010108, -2147417848,  # RPC_E_DISCONNECTED.
            0x800706b5, -2147023179,  # RPC_S_UNKNOWN_IF
        ]

    def xcptGetMessage(self, oXcpt):
        if hasattr(oXcpt, 'excepinfo'):
            try:
                if len(oXcpt.excepinfo) >= 3:
                    sRet = oXcpt.excepinfo[2]
                    if len(sRet) > 0:
                        return sRet[0:]
            except:
                pass
        if hasattr(oXcpt, 'strerror'):
            try:
                sRet = oXcpt.strerror
                if len(sRet) > 0:
                    return sRet
            except:
                pass
        return None

    def xcptGetBaseXcpt(self):
        import pythoncom

        return pythoncom.com_error

    def xcptSetupConstants(self, oDst):
        import winerror

        oDst = self.xcptCopyErrorConstants(oDst, winerror)

        # XPCOM compatability constants.
        oDst.NS_OK = oDst.S_OK
        oDst.NS_ERROR_FAILURE = oDst.E_FAIL
        oDst.NS_ERROR_ABORT = oDst.E_ABORT
        oDst.NS_ERROR_NULL_POINTER = oDst.E_POINTER
        oDst.NS_ERROR_NO_INTERFACE = oDst.E_NOINTERFACE
        oDst.NS_ERROR_INVALID_ARG = oDst.E_INVALIDARG
        oDst.NS_ERROR_OUT_OF_MEMORY = oDst.E_OUTOFMEMORY
        oDst.NS_ERROR_NOT_IMPLEMENTED = oDst.E_NOTIMPL
        oDst.NS_ERROR_UNEXPECTED = oDst.E_UNEXPECTED
        return oDst


class PlatformXPCOM(PlatformBase):
    """
    Platform specific code for XPCOM.
    """

    def __init__(self, dParams):
        PlatformBase.__init__(self, dParams)
        sys.path.append(g_sVBoxSdkDir + '/bindings/xpcom/python/')
        import xpcom.vboxxpcom
        import xpcom
        import xpcom.components
        _ = dParams

    def getSessionObject(self):
        import xpcom.components
        return xpcom.components.classes["@virtualbox.org/Session;1"].createInstance()

    def getVirtualBox(self):
        import xpcom.components
        client = xpcom.components.classes["@virtualbox.org/VirtualBoxClient;1"].createInstance()
        return client.virtualBox

    def getType(self):
        return 'XPCOM'

    def getArray(self, oInterface, sAttrib):
        return oInterface.__getattr__('get' + comifyName(sAttrib))() # pylint: disable=unnecessary-dunder-call

    def setArray(self, oInterface, sAttrib, aoArray):
        return oInterface.__getattr__('set' + comifyName(sAttrib))(aoArray) # pylint: disable=unnecessary-dunder-call

    def initPerThread(self):
        import xpcom
        xpcom._xpcom.AttachThread()

    def deinitPerThread(self):
        import xpcom
        xpcom._xpcom.DetachThread()

    def createListener(self, oImplClass, dArgs):
        notDocumentedDict = {}
        notDocumentedDict['BaseClass'] = oImplClass
        notDocumentedDict['dArgs'] = dArgs
        sEval  = ""
        sEval += "import xpcom.components\n"
        sEval += "class ListenerImpl(BaseClass):\n"
        sEval += "   _com_interfaces_ = xpcom.components.interfaces.IEventListener\n"
        sEval += "   def __init__(self): BaseClass.__init__(self, dArgs)\n"
        sEval += "result = ListenerImpl()\n"
        exec(sEval, notDocumentedDict, notDocumentedDict) # pylint: disable=exec-used
        return notDocumentedDict['result']

    def waitForEvents(self, cMsTimeout):
        import xpcom
        return xpcom._xpcom.WaitForEvents(cMsTimeout)

    def interruptWaitEvents(self):
        import xpcom
        return xpcom._xpcom.InterruptWait()

    def deinit(self):
        import xpcom
        xpcom._xpcom.DeinitCOM()

    def queryInterface(self, oIUnknown, sClassName):
        import xpcom.components
        return oIUnknown.queryInterface(getattr(xpcom.components.interfaces, sClassName))

    def xcptGetStatus(self, oXcpt):
        return oXcpt.errno

    def xcptIsDeadInterface(self, oXcpt):
        return self.xcptGetStatus(oXcpt) in [
            0x80004004, -2147467260,  # NS_ERROR_ABORT
            0x800706be, -2147023170,  # NS_ERROR_CALL_FAILED (RPC_S_CALL_FAILED)
        ]

    def xcptGetMessage(self, oXcpt):
        if hasattr(oXcpt, 'msg'):
            try:
                sRet = oXcpt.msg
                if len(sRet) > 0:
                    return sRet
            except:
                pass
        return None

    def xcptGetBaseXcpt(self):
        import xpcom
        return xpcom.Exception

    def xcptSetupConstants(self, oDst):
        import xpcom
        oDst = self.xcptCopyErrorConstants(oDst, xpcom.nsError)

        # COM compatability constants.
        oDst.E_ACCESSDENIED = -2147024891  # see VBox/com/defs.h
        oDst.S_OK = oDst.NS_OK
        oDst.E_FAIL = oDst.NS_ERROR_FAILURE
        oDst.E_ABORT = oDst.NS_ERROR_ABORT
        oDst.E_POINTER = oDst.NS_ERROR_NULL_POINTER
        oDst.E_NOINTERFACE = oDst.NS_ERROR_NO_INTERFACE
        oDst.E_INVALIDARG = oDst.NS_ERROR_INVALID_ARG
        oDst.E_OUTOFMEMORY = oDst.NS_ERROR_OUT_OF_MEMORY
        oDst.E_NOTIMPL = oDst.NS_ERROR_NOT_IMPLEMENTED
        oDst.E_UNEXPECTED = oDst.NS_ERROR_UNEXPECTED
        oDst.DISP_E_EXCEPTION = -2147352567  # For COM compatability only.
        return oDst


class PlatformWEBSERVICE(PlatformBase):
    """
    VirtualBox Web Services API specific code.
    """

    def __init__(self, dParams):
        PlatformBase.__init__(self, dParams)
        # Import web services stuff.  Fix the sys.path the first time.
        sWebServLib = os.path.join(g_sVBoxSdkDir, 'bindings', 'webservice', 'python', 'lib')
        if sWebServLib not in sys.path:
            sys.path.append(sWebServLib)
        import VirtualBox_wrappers
        from VirtualBox_wrappers import IWebsessionManager2

        # Initialize instance variables from parameters.
        if dParams is not None:
            self.user = dParams.get("user", "")
            self.password = dParams.get("password", "")
            self.url = dParams.get("url", "")
        else:
            self.user = ""
            self.password = ""
            self.url = None
        self.vbox = None
        self.wsmgr = None

    #
    # Base class overrides.
    #

    def getSessionObject(self):
        return self.wsmgr.getSessionObject(self.vbox)

    def getVirtualBox(self):
        return self.connect(self.url, self.user, self.password)

    def getType(self):
        return 'WEBSERVICE'

    def isRemote(self):
        """ Returns True if remote VBox host, False if local. """
        return True

    def getArray(self, oInterface, sAttrib):
        return oInterface.__getattr__(sAttrib) # pylint: disable=unnecessary-dunder-call

    def setArray(self, oInterface, sAttrib, aoArray):
        return oInterface.__setattr__(sAttrib, aoArray) # pylint: disable=unnecessary-dunder-call

    def waitForEvents(self, _timeout):
        # Webservices cannot do that yet
        return 2

    def interruptWaitEvents(self):
        # Webservices cannot do that yet
        return False

    def deinit(self):
        try:
            self.disconnect()
        except:
            pass

    def queryInterface(self, oIUnknown, sClassName):
        notDocumentedDict = {}
        notDocumentedDict['oIUnknown'] = oIUnknown
        sEval  = ""
        sEval += "from VirtualBox_wrappers import " + sClassName + "\n"
        sEval += "result = " + sClassName + "(oIUnknown.mgr, oIUnknown.handle)\n"
        # wrong, need to test if class indeed implements this interface
        exec(sEval, notDocumentedDict, notDocumentedDict) # pylint: disable=exec-used
        return notDocumentedDict['result']

    #
    # Web service specific methods.
    #

    def connect(self, url, user, passwd):
        if self.vbox is not None:
            self.disconnect()
        from VirtualBox_wrappers import IWebsessionManager2

        if url is None:
            url = ""
        self.url = url
        if user is None:
            user = ""
        self.user = user
        if passwd is None:
            passwd = ""
        self.password = passwd
        self.wsmgr = IWebsessionManager2(self.url)
        self.vbox = self.wsmgr.logon(self.user, self.password)
        if not self.vbox.handle:
            raise Exception("cannot connect to '" + self.url + "' as '" + self.user + "'")
        return self.vbox

    def disconnect(self):
        if self.vbox is not None and self.wsmgr is not None:
            self.wsmgr.logoff(self.vbox)
            self.vbox = None
            self.wsmgr = None


## The current (last) exception class.
# This is reinitalized whenever VirtualBoxManager is called, so it will hold
# the reference to the error exception class for the last platform/style that
# was used.  Most clients does not talk to multiple VBox instance on different
# platforms at the same time, so this should be sufficent for most uses and
# be way simpler to use than VirtualBoxManager::oXcptClass.
g_oCurXcptClass = None


class VirtualBoxManager(object):
    """
    VirtualBox API manager class.

    The API users will have to instantiate this.  If no parameters are given,
    it will default to interface with the VirtualBox running on the local
    machine.  sStyle can be None (default), MSCOM, XPCOM or WEBSERVICES.  Most
    users will either be specifying None or WEBSERVICES.

    The dPlatformParams is an optional dictionary for passing parameters to the
    WEBSERVICE backend.
    """

    class Statuses(object):
        def __init__(self):
            pass

    def __init__(self, sStyle=None, dPlatformParams=None):

        # Deprecation warning for older Python stuff (< Python 3.x).
        if sys.version_info.major < 3:
            print("\nWarning: Running VirtualBox with Python %d.%d is marked as being deprecated.\n" \
                  "Please upgrade your Python installation to avoid breakage.\n" \
                  % (sys.version_info.major, sys.version_info.minor))

        if sStyle is None:
            if sys.platform == 'win32':
                sStyle = "MSCOM"
            else:
                sStyle = "XPCOM"
        if sStyle == 'XPCOM':
            self.platform = PlatformXPCOM(dPlatformParams)
        elif sStyle == 'MSCOM':
            self.platform = PlatformMSCOM(dPlatformParams)
        elif sStyle == 'WEBSERVICE':
            self.platform = PlatformWEBSERVICE(dPlatformParams)
        else:
            raise Exception('Unknown sStyle=%s' % (sStyle,))
        self.style = sStyle
        self.type = self.platform.getType()
        self.remote = self.platform.isRemote()
        ## VirtualBox API constants (for webservices, enums are symbolic).
        self.constants = VirtualBoxReflectionInfo(sStyle == "WEBSERVICE")

        ## Status constants.
        self.statuses = self.platform.xcptSetupConstants(VirtualBoxManager.Statuses())
        ## @todo Add VBOX_E_XXX to statuses? They're already in constants...
        ## Dictionary for errToString, built on demand.
        self._dErrorValToName = None

        ## Dictionary for resolving enum values to names, two levels of dictionaries.
        ## First level is indexed by enum name, the next by value.
        self._ddEnumValueToName = {};

        ## The exception class for the selected platform.
        self.oXcptClass = self.platform.xcptGetBaseXcpt()
        global g_oCurXcptClass
        g_oCurXcptClass = self.oXcptClass

        # Get the virtualbox singleton.
        try:
            self.platform.getVirtualBox()
        except NameError:
            print("Installation problem: check that appropriate libs in place")
            traceback.print_exc()
            raise
        except Exception:
            _, e, _ = sys.exc_info()
            print("init exception: ", e)
            traceback.print_exc()

    def __del__(self):
        self.deinit()

    def getPythonApiRevision(self):
        """
        Returns a Python API revision number.
        This will be incremented when features are added to this file.
        """
        return 3

    @property
    def mgr(self):
        """
        This used to be an attribute referring to a session manager class with
        only one method called getSessionObject. It moved into this class.
        """
        return self

    #
    # Wrappers for self.platform methods.
    #
    def getVirtualBox(self):
        """ See PlatformBase::getVirtualBox(). """
        return self.platform.getVirtualBox()

    def getSessionObject(self, oIVBox = None):
        """ See PlatformBase::getSessionObject(). """
        # ignore parameter which was never needed
        _ = oIVBox
        return self.platform.getSessionObject()

    def getArray(self, oInterface, sAttrib):
        """ See PlatformBase::getArray(). """
        return self.platform.getArray(oInterface, sAttrib)

    def setArray(self, oInterface, sAttrib, aoArray):
        """ See PlatformBase::setArray(). """
        return self.platform.setArray(oInterface, sAttrib, aoArray)

    def createListener(self, oImplClass, dArgs=None):
        """ See PlatformBase::createListener(). """
        return self.platform.createListener(oImplClass, dArgs)

    def waitForEvents(self, cMsTimeout):
        """ See PlatformBase::waitForEvents(). """
        return self.platform.waitForEvents(cMsTimeout)

    def interruptWaitEvents(self):
        """ See PlatformBase::interruptWaitEvents(). """
        return self.platform.interruptWaitEvents()

    def queryInterface(self, oIUnknown, sClassName):
        """ See PlatformBase::queryInterface(). """
        return self.platform.queryInterface(oIUnknown, sClassName)

    #
    # Init and uninit.
    #
    def initPerThread(self):
        """ See PlatformBase::deinitPerThread(). """
        self.platform.initPerThread()

    def deinitPerThread(self):
        """ See PlatformBase::deinitPerThread(). """
        return self.platform.deinitPerThread()

    def deinit(self):
        """
        For unitializing the manager.
        Do not access it after calling this method.
        """
        if hasattr(self, "platform") and self.platform is not None:
            self.platform.deinit()
            self.platform = None
        return True

    #
    # Utility methods.
    #
    def openMachineSession(self, oIMachine, fPermitSharing=True):
        """
        Attempts to open the a session to the machine.
        Returns a session object on success.
        Raises exception on failure.
        """
        oSession = self.getSessionObject()
        if fPermitSharing:
            eType = self.constants.LockType_Shared
        else:
            eType = self.constants.LockType_Write
        oIMachine.lockMachine(oSession, eType)
        return oSession

    def closeMachineSession(self, oSession):
        """
        Closes a session opened by openMachineSession.
        Ignores None parameters.
        """
        if oSession is not None:
            oSession.unlockMachine()
        return True

    def getPerfCollector(self, oIVBox):
        """
        Returns a helper class (PerfCollector) for accessing performance
        collector goodies.  See PerfCollector for details.
        """
        return PerfCollector(self, oIVBox)

    def getBinDir(self):
        """
        Returns the VirtualBox binary directory.
        """
        return g_sVBoxBinDir

    def getSdkDir(self):
        """
        Returns the VirtualBox SDK directory.
        """
        return g_sVBoxSdkDir

    def getEnumValueName(self, sEnumTypeNm, oEnumValue, fTypePrefix = False):
        """
        Returns the name (string) for the corresponding enum value.
        """
        # Cache lookup:
        dValueNames = self._ddEnumValueToName.get(sEnumTypeNm);
        if dValueNames is not None:
            sValueName = dValueNames.get(oEnumValue);
            if sValueName:
                return sValueName if not fTypePrefix else '%s_%s' % (sEnumTypeNm, sValueName);
        else:
            # Cache miss. Build the reverse lookup dictionary and add it to the cache:
            dNamedValues = self.constants.all_values(sEnumTypeNm);
            if len(dNamedValues) > 0:

                dValueNames = {};
                for sName in dNamedValues:
                    dValueNames[dNamedValues[sName]] = sName;
                self._ddEnumValueToName[sEnumTypeNm] = dValueNames;

                # Lookup the value:
                sValueName = dValueNames.get(oEnumValue);
                if sValueName:
                    return sValueName if not fTypePrefix else '%s_%s' % (sEnumTypeNm, sValueName);

        # Fallback:
        return '%s_Unknown_%s' % (sEnumTypeNm, oEnumValue);

    #
    # Error code utilities.
    #
    ## @todo port to webservices!
    def xcptGetStatus(self, oXcpt=None):
        """
        Gets the status code from an exception.  If the exception parameter
        isn't specified, the current exception is examined.
        """
        if oXcpt is None:
            oXcpt = sys.exc_info()[1]
        return self.platform.xcptGetStatus(oXcpt)

    def xcptIsDeadInterface(self, oXcpt=None):
        """
        Returns True if the exception indicates that the interface is dead,
        False if not.  If the exception parameter isn't specified, the current
        exception is examined.
        """
        if oXcpt is None:
            oXcpt = sys.exc_info()[1]
        return self.platform.xcptIsDeadInterface(oXcpt)

    def xcptIsOurXcptKind(self, oXcpt=None):
        """
        Checks if the exception is one that could come from the VBox API. If
        the exception parameter isn't specified, the current exception is
        examined.
        """
        if self.oXcptClass is None:  # @todo find the exception class for web services!
            return False
        if oXcpt is None:
            oXcpt = sys.exc_info()[1]
        return isinstance(oXcpt, self.oXcptClass)

    def xcptIsEqual(self, oXcpt, hrStatus):
        """
        Checks if the exception oXcpt is equal to the COM/XPCOM status code
        hrStatus.

        The oXcpt parameter can be any kind of object, we'll just return True
        if it doesn't behave like a our exception class.  If it's None, we'll
        query the current exception and examine that.

        Will not raise any exception as long as hrStatus and self are not bad.
        """
        if oXcpt is None:
            oXcpt = sys.exc_info()[1]
        return self.platform.xcptIsEqual(oXcpt, hrStatus)

    def xcptIsNotEqual(self, oXcpt, hrStatus):
        """
        Negated xcptIsEqual.
        """
        return not self.xcptIsEqual(oXcpt, hrStatus)

    def xcptToString(self, hrStatusOrXcpt=None):
        """
        Converts the specified COM status code, or the status code of the
        specified exception, to a C constant string.  If the parameter isn't
        specified (is None), the current exception is examined.
        """

        # Deal with exceptions.
        if hrStatusOrXcpt is None or self.xcptIsOurXcptKind(hrStatusOrXcpt):
            hrStatus = self.xcptGetStatus(hrStatusOrXcpt)
        else:
            hrStatus = hrStatusOrXcpt

        # Build the dictionary on demand.
        if self._dErrorValToName is None:
            dErrorValToName = {}
            for sKey in dir(self.statuses):
                if sKey[0].isupper():
                    oValue = getattr(self.statuses, sKey)
                    if isinstance(oValue, (int, long)):
                        dErrorValToName[int(oValue)] = sKey
            # Always prefer the COM names (see aliasing in platform specific code):
            for sKey in ('S_OK', 'E_FAIL', 'E_ABORT', 'E_POINTER', 'E_NOINTERFACE', 'E_INVALIDARG',
                         'E_OUTOFMEMORY', 'E_NOTIMPL', 'E_UNEXPECTED',):
                oValue = getattr(self.statuses, sKey, None)
                if oValue is not None:
                    dErrorValToName[oValue] = sKey
            self._dErrorValToName = dErrorValToName

        # Do the lookup, falling back on formatting the status number.
        try:
            sStr = self._dErrorValToName[int(hrStatus)]
        except KeyError:
            hrLong = long(hrStatus)
            sStr = '%#x (%d)' % (hrLong & 0xffffffff, hrLong)
        return sStr

    def xcptGetMessage(self, oXcpt=None):
        """
        Returns the best error message found in the COM-like exception. If the
        exception parameter isn't specified, the current exception is examined.
        """
        if oXcpt is None:
            oXcpt = sys.exc_info()[1]
        sRet = self.platform.xcptGetMessage(oXcpt)
        if sRet is None:
            sRet = self.xcptToString(oXcpt)
        return sRet
