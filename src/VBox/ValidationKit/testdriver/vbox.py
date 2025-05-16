# -*- coding: utf-8 -*-
# $Id: vbox.py 168904 2025-05-16 12:38:14Z knut.osmundsen@oracle.com $
# pylint: disable=too-many-lines

"""
VirtualBox Specific base testdriver.
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
__version__ = "$Revision: 168904 $"

# pylint: disable=unnecessary-semicolon

# Standard Python imports.
import datetime
import os
import platform
import re;
import sys
import threading
import time
import traceback

# Figure out where the validation kit lives and make sure it's in the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0];
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)));
if g_ksValidationKitDir not in sys.path:
    sys.path.append(g_ksValidationKitDir);

# Validation Kit imports.
from common     import utils;
from testdriver import base;
from testdriver import btresolver;
from testdriver import reporter;
from testdriver import vboxcon;
from testdriver import vboxtestvms;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    xrange = range; # pylint: disable=redefined-builtin,invalid-name
    long = int;     # pylint: disable=redefined-builtin,invalid-name

#
# Exception and Error Unification Hacks.
# Note! This is pretty gross stuff. Be warned!
# TODO: Find better ways of doing these things, preferrably in vboxapi.
#

ComException = None;                                                            # pylint: disable=invalid-name
__fnComExceptionGetAttr__ = None;                                               # pylint: disable=invalid-name

def __MyDefaultGetAttr(oSelf, sName):
    """ __getattribute__/__getattr__ default fake."""
    try:
        oAttr = oSelf.__dict__[sName];
    except:
        oAttr = dir(oSelf)[sName];
    return oAttr;

def __MyComExceptionGetAttr(oSelf, sName):
    """ ComException.__getattr__ wrapper - both XPCOM and COM.  """
    try:
        oAttr = __fnComExceptionGetAttr__(oSelf, sName);
    except AttributeError:
        if platform.system() == 'Windows':
            if sName == 'errno':
                oAttr = __fnComExceptionGetAttr__(oSelf, 'hresult');
            elif sName == 'msg':
                oAttr = __fnComExceptionGetAttr__(oSelf, 'strerror');
            else:
                raise;
        else:
            if sName == 'hresult':
                oAttr = __fnComExceptionGetAttr__(oSelf, 'errno');
            elif sName == 'strerror':
                oAttr = __fnComExceptionGetAttr__(oSelf, 'msg');
            elif sName == 'excepinfo':
                oAttr = None;
            elif sName == 'argerror':
                oAttr = None;
            else:
                raise;
    #print '__MyComExceptionGetAttr(,%s) -> "%s"' % (sName, oAttr);
    return oAttr;

def __deployExceptionHacks__(oNativeComExceptionClass):
    """
    Deploys the exception and error hacks that helps unifying COM and XPCOM
    exceptions and errors.
    """
    global ComException                                                         # pylint: disable=invalid-name
    global __fnComExceptionGetAttr__                                            # pylint: disable=invalid-name

    # Hook up our attribute getter for the exception class (ASSUMES new-style).
    if __fnComExceptionGetAttr__ is None:
        try:
            __fnComExceptionGetAttr__ = getattr(oNativeComExceptionClass, '__getattr__');
        except:
            try:
                __fnComExceptionGetAttr__ = getattr(oNativeComExceptionClass, '__getattribute__');
            except:
                __fnComExceptionGetAttr__ = __MyDefaultGetAttr;
        setattr(oNativeComExceptionClass, '__getattr__', __MyComExceptionGetAttr)

    # Make the modified classes accessible (are there better ways to do this?)
    ComException = oNativeComExceptionClass
    return None;



#
# Utility functions.
#

def isIpAddrValid(sIpAddr):
    """
    Checks if a IPv4 address looks valid.   This will return false for
    localhost and similar.
    Returns True / False.
    """
    if sIpAddr is None:                 return False;
    if len(sIpAddr.split('.')) != 4:    return False;
    if sIpAddr.endswith('.0'):          return False;
    if sIpAddr.endswith('.255'):        return False;
    if sIpAddr.startswith('127.'):      return False;
    if sIpAddr.startswith('169.254.'):  return False;
    if sIpAddr.startswith('192.0.2.'):  return False;
    if sIpAddr.startswith('224.0.0.'):  return False;
    return True;

def stringifyErrorInfo(oErrInfo):
    """
    Stringifies the error information in a IVirtualBoxErrorInfo object.

    Returns string with error info.
    """
    try:
        rc          = oErrInfo.resultCode;
        sText       = oErrInfo.text;
        sIid        = oErrInfo.interfaceID;
        sComponent  = oErrInfo.component;
    except:
        sRet = 'bad error object (%s)?' % (oErrInfo,);
        traceback.print_exc();
    else:
        sRet = 'rc=%s text="%s" IID=%s component=%s' % (ComError.toString(rc), sText, sIid, sComponent);
    return sRet;

def reportError(oErr, sText):
    """
    Report a VirtualBox error on oErr.  oErr can be IVirtualBoxErrorInfo
    or IProgress.  Anything else is ignored.

    Returns the same a reporter.error().
    """
    try:
        oErrObj = oErr.errorInfo;   # IProgress.
    except:
        oErrObj = oErr;
    reporter.error(sText);
    return reporter.error(stringifyErrorInfo(oErrObj));

def formatComOrXpComException(oType, oXcpt):
    """
    Callback installed with the reporter to better format COM exceptions.
    Similar to format_exception_only, only it returns None if not interested.
    """
    _ = oType;
    oVBoxMgr = vboxcon.goHackModuleClass.oVBoxMgr;
    if oVBoxMgr is None:
        return None;
    if not oVBoxMgr.xcptIsOurXcptKind(oXcpt):               # pylint: disable=not-callable
        return None;

    if platform.system() == 'Windows':
        hrc = oXcpt.hresult;
        if hrc == ComError.DISP_E_EXCEPTION and oXcpt.excepinfo is not None and len(oXcpt.excepinfo) > 5:
            hrc    = oXcpt.excepinfo[5];
            sWhere = oXcpt.excepinfo[1];
            sMsg   = oXcpt.excepinfo[2];
        else:
            sWhere = None;
            sMsg   = oXcpt.strerror;
    else:
        hrc    = oXcpt.errno;
        sWhere = None;
        sMsg   = oXcpt.msg;

    sHrc = oVBoxMgr.xcptToString(hrc);                      # pylint: disable=not-callable
    if sHrc.find('(') < 0:
        sHrc = '%s (%#x)' % (sHrc, hrc & 0xffffffff,);

    asRet = ['COM-Xcpt: %s' % (sHrc,)];
    if sMsg and sWhere:
        asRet.append('--------- %s: %s' % (sWhere, sMsg,));
    elif sMsg:
        asRet.append('--------- %s' % (sMsg,));
    return asRet;
    #if sMsg and sWhere:
    #    return ['COM-Xcpt: %s - %s: %s' % (sHrc, sWhere, sMsg,)];
    #if sMsg:
    #    return ['COM-Xcpt: %s - %s' % (sHrc, sMsg,)];
    #return ['COM-Xcpt: %s' % (sHrc,)];

#
# Classes
#

class ComError(object):
    """
    Unified COM and XPCOM status code repository.
    This works more like a module than a class since it's replacing a module.
    """

    # The VBOX_E_XXX bits:
    __VBOX_E_BASE = -2135228416;
    VBOX_E_OBJECT_NOT_FOUND         = __VBOX_E_BASE + 1;
    VBOX_E_INVALID_VM_STATE         = __VBOX_E_BASE + 2;
    VBOX_E_VM_ERROR                 = __VBOX_E_BASE + 3;
    VBOX_E_FILE_ERROR               = __VBOX_E_BASE + 4;
    VBOX_E_IPRT_ERROR               = __VBOX_E_BASE + 5;
    VBOX_E_PDM_ERROR                = __VBOX_E_BASE + 6;
    VBOX_E_INVALID_OBJECT_STATE     = __VBOX_E_BASE + 7;
    VBOX_E_HOST_ERROR               = __VBOX_E_BASE + 8;
    VBOX_E_NOT_SUPPORTED            = __VBOX_E_BASE + 9;
    VBOX_E_XML_ERROR                = __VBOX_E_BASE + 10;
    VBOX_E_INVALID_SESSION_STATE    = __VBOX_E_BASE + 11;
    VBOX_E_OBJECT_IN_USE            = __VBOX_E_BASE + 12;
    VBOX_E_DONT_CALL_AGAIN          = __VBOX_E_BASE + 13;

    # Reverse lookup table.
    dDecimalToConst = {}; # pylint: disable=invalid-name

    def __init__(self):
        raise base.GenError('No instances, please');

    @staticmethod
    def copyErrors(oNativeComErrorClass):
        """
        Copy all error codes from oNativeComErrorClass to this class and
        install compatability mappings.
        """

        # First, add the VBOX_E_XXX constants to dDecimalToConst.
        for sAttr in dir(ComError):
            if sAttr.startswith('VBOX_E'):
                oAttr = getattr(ComError, sAttr);
                ComError.dDecimalToConst[oAttr] = sAttr;

        # Copy all error codes from oNativeComErrorClass to this class.
        for sAttr in dir(oNativeComErrorClass):
            if sAttr[0].isupper():
                oAttr = getattr(oNativeComErrorClass, sAttr);
                setattr(ComError, sAttr, oAttr);
                if isinstance(oAttr, int):
                    ComError.dDecimalToConst[oAttr] = sAttr;

        # Install mappings to the other platform.
        if platform.system() == 'Windows':
            ComError.NS_OK = ComError.S_OK;
            ComError.NS_ERROR_FAILURE         = ComError.E_FAIL;
            ComError.NS_ERROR_ABORT           = ComError.E_ABORT;
            ComError.NS_ERROR_NULL_POINTER    = ComError.E_POINTER;
            ComError.NS_ERROR_NO_INTERFACE    = ComError.E_NOINTERFACE;
            ComError.NS_ERROR_INVALID_ARG     = ComError.E_INVALIDARG;
            ComError.NS_ERROR_OUT_OF_MEMORY   = ComError.E_OUTOFMEMORY;
            ComError.NS_ERROR_NOT_IMPLEMENTED = ComError.E_NOTIMPL;
            ComError.NS_ERROR_UNEXPECTED      = ComError.E_UNEXPECTED;
        else:
            ComError.E_ACCESSDENIED           = -2147024891; # see VBox/com/defs.h
            ComError.S_OK                     = ComError.NS_OK;
            ComError.E_FAIL                   = ComError.NS_ERROR_FAILURE;
            ComError.E_ABORT                  = ComError.NS_ERROR_ABORT;
            ComError.E_POINTER                = ComError.NS_ERROR_NULL_POINTER;
            ComError.E_NOINTERFACE            = ComError.NS_ERROR_NO_INTERFACE;
            ComError.E_INVALIDARG             = ComError.NS_ERROR_INVALID_ARG;
            ComError.E_OUTOFMEMORY            = ComError.NS_ERROR_OUT_OF_MEMORY;
            ComError.E_NOTIMPL                = ComError.NS_ERROR_NOT_IMPLEMENTED;
            ComError.E_UNEXPECTED             = ComError.NS_ERROR_UNEXPECTED;
            ComError.DISP_E_EXCEPTION         = -2147352567; # For COM compatability only.
        return True;

    @staticmethod
    def getXcptResult(oXcpt):
        """
        Gets the result code for an exception.
        Returns COM status code (or E_UNEXPECTED).
        """
        if platform.system() == 'Windows':
            # The DISP_E_EXCEPTION + excptinfo fun needs checking up, only
            # empirical info on it so far.
            try:
                hrXcpt = oXcpt.hresult;
            except AttributeError:
                hrXcpt = ComError.E_UNEXPECTED;
            if hrXcpt == ComError.DISP_E_EXCEPTION and oXcpt.excepinfo is not None:
                hrXcpt = oXcpt.excepinfo[5];
        else:
            try:
                hrXcpt = oXcpt.errno;
            except AttributeError:
                hrXcpt = ComError.E_UNEXPECTED;
        return hrXcpt;

    @staticmethod
    def equal(oXcpt, hr):
        """
        Checks if the ComException e is not equal to the COM status code hr.
        This takes DISP_E_EXCEPTION & excepinfo into account.

        This method can be used with any Exception derivate, however it will
        only return True for classes similar to the two ComException variants.
        """
        if platform.system() == 'Windows':
            # The DISP_E_EXCEPTION + excptinfo fun needs checking up, only
            # empirical info on it so far.
            try:
                hrXcpt = oXcpt.hresult;
            except AttributeError:
                return False;
            if hrXcpt == ComError.DISP_E_EXCEPTION and oXcpt.excepinfo is not None:
                hrXcpt = oXcpt.excepinfo[5];
        else:
            try:
                hrXcpt = oXcpt.errno;
            except AttributeError:
                return False;
        return hrXcpt == hr;

    @staticmethod
    def notEqual(oXcpt, hr):
        """
        Checks if the ComException e is not equal to the COM status code hr.
        See equal() for more details.
        """
        return not ComError.equal(oXcpt, hr)

    @staticmethod
    def toString(hr):
        """
        Converts the specified COM status code to a string.
        """
        try:
            sStr = ComError.dDecimalToConst[int(hr)];
        except KeyError:
            hrLong = long(hr);
            sStr = '%#x (%d)' % (hrLong, hrLong);
        return sStr;


class Build(object): # pylint: disable=too-few-public-methods
    """
    A VirtualBox build.

    Note! After dropping the installation of VBox from this code and instead
          realizing that with the vboxinstall.py wrapper driver, this class is
          of much less importance and contains unnecessary bits and pieces.
    """

    def __init__(self, oDriver, strInstallPath):
        """
        Construct a build object from a build file name and/or install path.
        """
        # Initialize all members first.
        self.oDriver      = oDriver;
        self.sInstallPath = strInstallPath;
        self.sSdkPath     = None;
        self.sSrcRoot     = None;
        self.sKind        = None;
        self.sDesignation = None;
        self.sType        = None;
        self.sOs          = None;
        self.sArch        = None;
        self.sGuestAdditionsIso = None;

        # Figure out the values as best we can.
        if strInstallPath is None:
            #
            # Both parameters are None, which means we're falling back on a
            # build in the development tree.
            #
            self.sKind = "development";

            if self.sType is None:
                self.sType = os.environ.get("KBUILD_TYPE",        "release");
            if self.sOs is None:
                self.sOs   = os.environ.get("KBUILD_TARGET",      oDriver.sHost);
            if self.sArch is None:
                self.sArch = os.environ.get("KBUILD_TARGET_ARCH", oDriver.sHostArch);

            sOut = os.path.join('out', self.sOs + '.' + self.sArch, self.sType);
            sSearch = os.environ.get('VBOX_TD_DEV_TREE', os.path.dirname(__file__)); # Env.var. for older trees or testboxscript.
            sCandidat = None;
            for i in range(0, 10):                                          # pylint: disable=unused-variable
                sBldDir = os.path.join(sSearch, sOut);
                if os.path.isdir(sBldDir):
                    sCandidat = os.path.join(sBldDir, 'bin', 'VBoxSVC' + base.exeSuff());
                    if os.path.isfile(sCandidat):
                        self.sSdkPath = os.path.join(sBldDir, 'bin/sdk');
                        break;
                    sCandidat = os.path.join(sBldDir, 'dist/VirtualBox.app/Contents/MacOS/VBoxSVC');
                    if os.path.isfile(sCandidat):
                        self.sSdkPath = os.path.join(sBldDir, 'dist/sdk');
                        break;
                sSearch = os.path.abspath(os.path.join(sSearch, '..'));
            if sCandidat is None or not os.path.isfile(sCandidat):
                raise base.GenError();
            self.sInstallPath = os.path.abspath(os.path.dirname(sCandidat));
            self.sSrcRoot     = os.path.abspath(sSearch);

            self.sDesignation = os.environ.get('TEST_BUILD_DESIGNATION', None);
            if self.sDesignation is None:
                try:
                    oFile = utils.openNoInherit(os.path.join(self.sSrcRoot, sOut, 'revision.kmk'), 'r');
                except:
                    pass;
                else:
                    s = oFile.readline();
                    oFile.close();
                    oMatch = re.search("VBOX_SVN_REV=(\\d+)", s);
                    if oMatch is not None:
                        self.sDesignation = oMatch.group(1);

                if self.sDesignation is None:
                    self.sDesignation = 'XXXXX'
        else:
            #
            # We've been pointed to an existing installation, this could be
            # in the out dir of a svn checkout, untarred VBoxAll or a real
            # installation directory.
            #
            self.sKind        = "preinstalled";
            self.sType        = "release";
            self.sOs          = oDriver.sHost;
            self.sArch        = oDriver.sHostArch;
            self.sInstallPath = os.path.abspath(strInstallPath);
            self.sSdkPath     = os.path.join(self.sInstallPath, 'sdk');
            self.sSrcRoot     = None;
            self.sDesignation = os.environ.get('TEST_BUILD_DESIGNATION', 'XXXXX');
            ## @todo Much more work is required here.

            # Try Determine the build type.
            sVBoxManage = os.path.join(self.sInstallPath, 'VBoxManage' + base.exeSuff());
            if os.path.isfile(sVBoxManage):
                try:
                    (iExit, sStdOut, _) = utils.processOutputUnchecked([sVBoxManage, '--dump-build-type']);
                    sStdOut = sStdOut.strip();
                    if iExit == 0 and sStdOut in ('release', 'debug', 'strict', 'dbgopt', 'asan'):
                        self.sType = sStdOut;
                        reporter.log('Build: Detected build type: %s' % (self.sType));
                    else:
                        reporter.log('Build: --dump-build-type -> iExit=%u sStdOut=%s' % (iExit, sStdOut,));
                except:
                    reporter.logXcpt('Build: Running "%s --dump-build-type" failed!' % (sVBoxManage,));
            else:
                reporter.log3('Build: sVBoxManage=%s not found' % (sVBoxManage,));

            # Do some checks.
            if utils.getHostOs() != 'darwin': # On macOS VMMR0.r0 might not be present anymore, especially on arm64.
                sVMMR0 = os.path.join(self.sInstallPath, 'VMMR0.r0');
                if not os.path.isfile(sVMMR0) and utils.getHostOs() == 'solaris': # solaris is special.
                    sVMMR0 = os.path.join(self.sInstallPath, 'amd64' if utils.getHostArch() == 'amd64' else 'i386', 'VMMR0.r0');
                if not os.path.isfile(sVMMR0):
                    raise base.GenError('%s is missing' % (sVMMR0,));

        # Guest additions location is different on windows for some _stupid_ reason.
        if self.sOs == 'win' and self.sKind != 'development':
            self.sGuestAdditionsIso = '%s/VBoxGuestAdditions.iso' % (self.sInstallPath,);
        elif self.sOs == 'darwin':
            self.sGuestAdditionsIso = '%s/VBoxGuestAdditions.iso' % (self.sInstallPath,);
        elif self.sOs == 'solaris':
            self.sGuestAdditionsIso = '%s/VBoxGuestAdditions.iso' % (self.sInstallPath,);
        else:
            self.sGuestAdditionsIso = '%s/additions/VBoxGuestAdditions.iso' % (self.sInstallPath,);

        # __init__ end;

    def isDevBuild(self):
        """ Returns True if it's development build (kind), otherwise False. """
        return self.sKind == 'development';


class EventHandlerBase(object):
    """
    Base class for both Console and VirtualBox event handlers.
    """

    def __init__(self, dArgs, fpApiVer, sName = None):
        self.oVBoxMgr   = dArgs['oVBoxMgr'];
        self.oEventSrc  = dArgs['oEventSrc']; # Console/VirtualBox for < 3.3
        self.oListener  = dArgs['oListener'];
        self.fPassive   = self.oListener is not None;
        self.sName      = sName
        self.fShutdown  = False;
        self.oThread    = None;
        self.fpApiVer   = fpApiVer;
        self.dEventNo2Name = {};
        for sKey, iValue in self.oVBoxMgr.constants.all_values('VBoxEventType').items():
            self.dEventNo2Name[iValue] = sKey;

    def threadForPassiveMode(self):
        """
        The thread procedure for the event processing thread.
        """
        assert self.fPassive is not None;
        while not self.fShutdown:
            try:
                oEvt = self.oEventSrc.getEvent(self.oListener, 500);
            except:
                if not self.oVBoxMgr.xcptIsDeadInterface(): reporter.logXcpt();
                else: reporter.log('threadForPassiveMode/%s: interface croaked (ignored)' % (self.sName,));
                break;
            if oEvt:
                self.handleEvent(oEvt);
                if not self.fShutdown:
                    try:
                        self.oEventSrc.eventProcessed(self.oListener, oEvt);
                    except:
                        reporter.logXcpt();
                        break;
        self.unregister(fWaitForThread = False);
        return None;

    def startThreadForPassiveMode(self):
        """
        Called when working in passive mode.
        """
        self.oThread = threading.Thread(target = self.threadForPassiveMode, args=(), name='PAS-%s' % (self.sName,) );
        self.oThread.setDaemon(True); # pylint: disable=deprecated-method
        self.oThread.start();
        return None;

    def unregister(self, fWaitForThread = True):
        """
        Unregister the event handler.
        """
        fRc = False;
        if not self.fShutdown:
            self.fShutdown = True;

            if self.oEventSrc is not None:
                if self.fpApiVer < 3.3:
                    try:
                        self.oEventSrc.unregisterCallback(self.oListener);
                        fRc = True;
                    except:
                        reporter.errorXcpt('unregisterCallback failed on %s' % (self.oListener,));
                else:
                    try:
                        self.oEventSrc.unregisterListener(self.oListener);
                        fRc = True;
                    except:
                        if self.oVBoxMgr.xcptIsDeadInterface():
                            reporter.log('unregisterListener failed on %s because of dead interface (%s)'
                                         % (self.oListener, self.oVBoxMgr.xcptToString(),));
                        else:
                            reporter.errorXcpt('unregisterListener failed on %s' % (self.oListener,));

            if    self.oThread is not None \
              and self.oThread != threading.current_thread():
                self.oThread.join();
                self.oThread = None;

        _ = fWaitForThread;
        return fRc;

    def handleEvent(self, oEvt):
        """
        Compatibility wrapper that child classes implement.
        """
        _ = oEvt;
        return None;

    @staticmethod
    def registerDerivedEventHandler(oVBoxMgr, fpApiVer, oSubClass, dArgsCopy, # pylint: disable=too-many-arguments
                                    oSrcParent, sSrcParentNm, sICallbackNm,
                                    fMustSucceed = True, sLogSuffix = '', aenmEvents = None):
        """
        Registers the callback / event listener.
        """
        dArgsCopy['oVBoxMgr'] = oVBoxMgr;
        dArgsCopy['oListener'] = None;
        if fpApiVer < 3.3:
            dArgsCopy['oEventSrc'] = oSrcParent;
            try:
                oRet = oVBoxMgr.createCallback(sICallbackNm, oSubClass, dArgsCopy);
            except:
                reporter.errorXcpt('%s::createCallback(%s) failed%s' % (sSrcParentNm, sICallbackNm, sLogSuffix,));
            else:
                try:
                    oSrcParent.registerCallback(oRet);
                    return oRet;
                except Exception as oXcpt:
                    if fMustSucceed or ComError.notEqual(oXcpt, ComError.E_UNEXPECTED):
                        reporter.errorXcpt('%s::registerCallback(%s)%s' % (sSrcParentNm, oRet, sLogSuffix,));
        else:
            #
            # Scalable event handling introduced in VBox 4.0.
            #
            fPassive = sys.platform == 'win32'; # or webservices.

            if not aenmEvents:
                aenmEvents = (vboxcon.VBoxEventType_Any,);

            try:
                oEventSrc = oSrcParent.eventSource;
                dArgsCopy['oEventSrc'] = oEventSrc;
                if not fPassive:
                    oListener = oRet = oVBoxMgr.createListener(oSubClass, dArgsCopy);
                else:
                    oListener = oEventSrc.createListener();
                    dArgsCopy['oListener'] = oListener;
                    oRet = oSubClass(dArgsCopy);
            except Exception as oXcpt:
                reporter.errorXcpt('%s::eventSource.createListener(%s, %s) failed: %s; fPassive=%s%s'
                                   % (sSrcParentNm, oSubClass, dArgsCopy, oXcpt, fPassive, sLogSuffix));
            else:
                try:
                    oEventSrc.registerListener(oListener, aenmEvents, not fPassive);
                except Exception as oXcpt:
                    if fMustSucceed or ComError.notEqual(oXcpt, ComError.E_UNEXPECTED):
                        reporter.errorXcpt('%s::eventSource.registerListener(%s) failed%s'
                                           % (sSrcParentNm, oListener, sLogSuffix));
                else:
                    if not fPassive:
                        if sys.platform == 'win32':
                            from win32com.server.util import unwrap # pylint: disable=import-error
                            oRet = unwrap(oRet);
                        oRet.oListener = oListener;
                    else:
                        oRet.startThreadForPassiveMode();
                    return oRet;
        return None;




class ConsoleEventHandlerBase(EventHandlerBase):
    """
    Base class for handling IConsole events.

    The class has IConsoleCallback (<=3.2) compatible callback methods which
    the user can override as needed.

    Note! This class must not inherit from object or we'll get type errors in VBoxPython.
    """
    def __init__(self, dArgs, sName = None):
        self.oSession   = dArgs['oSession'];
        self.oConsole   = dArgs['oConsole'];
        if sName is None:
            sName       = self.oSession.sName;
        EventHandlerBase.__init__(self, dArgs, self.oSession.fpApiVer, sName);


    # pylint: disable=missing-docstring,too-many-arguments,unused-argument
    def onMousePointerShapeChange(self, fVisible, fAlpha, xHot, yHot, cx, cy, abShape):
        reporter.log2('onMousePointerShapeChange/%s' % (self.sName));
    def onMouseCapabilityChange(self, fSupportsAbsolute, *aArgs): # Extra argument was added in 3.2.
        reporter.log2('onMouseCapabilityChange/%s' % (self.sName));
    def onKeyboardLedsChange(self, fNumLock, fCapsLock, fScrollLock):
        reporter.log2('onKeyboardLedsChange/%s' % (self.sName));
    def onStateChange(self, eState):
        reporter.log2('onStateChange/%s' % (self.sName));
    def onAdditionsStateChange(self):
        reporter.log2('onAdditionsStateChange/%s' % (self.sName));
    def onNetworkAdapterChange(self, oNic):
        reporter.log2('onNetworkAdapterChange/%s' % (self.sName));
    def onSerialPortChange(self, oPort):
        reporter.log2('onSerialPortChange/%s' % (self.sName));
    def onParallelPortChange(self, oPort):
        reporter.log2('onParallelPortChange/%s' % (self.sName));
    def onStorageControllerChange(self):
        reporter.log2('onStorageControllerChange/%s' % (self.sName));
    def onMediumChange(self, attachment):
        reporter.log2('onMediumChange/%s' % (self.sName));
    def onCPUChange(self, iCpu, fAdd):
        reporter.log2('onCPUChange/%s' % (self.sName));
    def onVRDPServerChange(self):
        reporter.log2('onVRDPServerChange/%s' % (self.sName));
    def onRemoteDisplayInfoChange(self):
        reporter.log2('onRemoteDisplayInfoChange/%s' % (self.sName));
    def onUSBControllerChange(self):
        reporter.log2('onUSBControllerChange/%s' % (self.sName));
    def onUSBDeviceStateChange(self, oDevice, fAttached, oError):
        reporter.log2('onUSBDeviceStateChange/%s' % (self.sName));
    def onSharedFolderChange(self, fGlobal):
        reporter.log2('onSharedFolderChange/%s' % (self.sName));
    def onRuntimeError(self, fFatal, sErrId, sMessage):
        reporter.log2('onRuntimeError/%s' % (self.sName));
    def onCanShowWindow(self):
        reporter.log2('onCanShowWindow/%s' % (self.sName));
        return True
    def onShowWindow(self):
        reporter.log2('onShowWindow/%s' % (self.sName));
        return None;
    # pylint: enable=missing-docstring,too-many-arguments,unused-argument

    def handleEvent(self, oEvt):
        """
        Compatibility wrapper.
        """
        try:
            oEvtBase = self.oVBoxMgr.queryInterface(oEvt, 'IEvent');
            eType = oEvtBase.type;
        except:
            reporter.logXcpt();
            return None;
        if eType == vboxcon.VBoxEventType_OnRuntimeError:
            try:
                oEvtIt = self.oVBoxMgr.queryInterface(oEvtBase, 'IRuntimeErrorEvent');
                return self.onRuntimeError(oEvtIt.fatal, oEvtIt.id, oEvtIt.message)
            except:
                reporter.logXcpt();
        ## @todo implement the other events.
        try:
            if eType not in (vboxcon.VBoxEventType_OnMousePointerShapeChanged,
                             vboxcon.VBoxEventType_OnCursorPositionChanged):
                if eType in self.dEventNo2Name:
                    reporter.log2('%s(%s)/%s' % (self.dEventNo2Name[eType], str(eType), self.sName));
                else:
                    reporter.log2('%s/%s' % (str(eType), self.sName));
        except AttributeError: # Handle older VBox versions which don't have a specific event.
            pass;
        return None;


class VirtualBoxEventHandlerBase(EventHandlerBase):
    """
    Base class for handling IVirtualBox events.

    The class has IConsoleCallback (<=3.2) compatible callback methods which
    the user can override as needed.

    Note! This class must not inherit from object or we'll get type errors in VBoxPython.
    """
    def __init__(self, dArgs, sName = "emanon"):
        self.oVBoxMgr  = dArgs['oVBoxMgr'];
        self.oVBox     = dArgs['oVBox'];
        EventHandlerBase.__init__(self, dArgs, self.oVBox.fpApiVer, sName);

    # pylint: disable=missing-docstring,unused-argument
    def onMachineStateChange(self, sMachineId, eState):
        pass;
    def onMachineDataChange(self, sMachineId):
        pass;
    def onExtraDataCanChange(self, sMachineId, sKey, sValue):
        # The COM bridge does tuples differently. Not very funny if you ask me... ;-)
        if self.oVBoxMgr.type == 'MSCOM':
            return '', 0, True;
        return True, ''
    def onExtraDataChange(self, sMachineId, sKey, sValue):
        pass;
    def onMediumRegistered(self, sMediumId, eMediumType, fRegistered):
        pass;
    def onMachineRegistered(self, sMachineId, fRegistered):
        pass;
    def onSessionStateChange(self, sMachineId, eState):
        pass;
    def onSnapshotTaken(self, sMachineId, sSnapshotId):
        pass;
    def onSnapshotDiscarded(self, sMachineId, sSnapshotId):
        pass;
    def onSnapshotChange(self, sMachineId, sSnapshotId):
        pass;
    def onGuestPropertyChange(self, sMachineId, sName, sValue, sFlags, fWasDeleted):
        pass;
    # pylint: enable=missing-docstring,unused-argument

    def handleEvent(self, oEvt):
        """
        Compatibility wrapper.
        """
        try:
            oEvtBase = self.oVBoxMgr.queryInterface(oEvt, 'IEvent');
            eType = oEvtBase.type;
        except:
            reporter.logXcpt();
            return None;
        if eType == vboxcon.VBoxEventType_OnMachineStateChanged:
            try:
                oEvtIt = self.oVBoxMgr.queryInterface(oEvtBase, 'IMachineStateChangedEvent');
                return self.onMachineStateChange(oEvtIt.machineId, oEvtIt.state)
            except:
                reporter.logXcpt();
        elif eType == vboxcon.VBoxEventType_OnGuestPropertyChanged:
            try:
                oEvtIt = self.oVBoxMgr.queryInterface(oEvtBase, 'IGuestPropertyChangedEvent');
                if hasattr(oEvtIt, 'fWasDeleted'): # Since 7.0 we have a dedicated flag
                    fWasDeleted = oEvtIt.fWasDeleted;
                else:
                    fWasDeleted = False; # Don't indicate deletion here -- there can be empty guest properties.
                return self.onGuestPropertyChange(oEvtIt.machineId, oEvtIt.name, oEvtIt.value, oEvtIt.flags, fWasDeleted);
            except:
                reporter.logXcpt();
        ## @todo implement the other events.
        if eType in self.dEventNo2Name:
            reporter.log2('%s(%s)/%s' % (self.dEventNo2Name[eType], str(eType), self.sName));
        else:
            reporter.log2('%s/%s' % (str(eType), self.sName));
        return None;


class SessionConsoleEventHandler(ConsoleEventHandlerBase):
    """
    For catching machine state changes and waking up the task machinery at that point.
    """
    def __init__(self, dArgs):
        ConsoleEventHandlerBase.__init__(self, dArgs);

    def onMachineStateChange(self, sMachineId, eState):                         # pylint: disable=unused-argument
        """ Just interrupt the wait loop here so it can check again. """
        _ = sMachineId; _ = eState;
        self.oVBoxMgr.interruptWaitEvents();

    def onRuntimeError(self, fFatal, sErrId, sMessage):
        reporter.log('onRuntimeError/%s: fFatal=%d sErrId=%s sMessage=%s' % (self.sName, fFatal, sErrId, sMessage));
        oSession = self.oSession;
        if oSession is not None: # paranoia
            if sErrId == 'HostMemoryLow':
                oSession.signalHostMemoryLow();
                if sys.platform == 'win32':
                    from testdriver import winbase;
                    winbase.logMemoryStats();
            oSession.signalTask();
        self.oVBoxMgr.interruptWaitEvents();



class TestDriver(base.TestDriver):                                              # pylint: disable=too-many-instance-attributes
    """
    This is the VirtualBox test driver.
    """

    def __init__(self):
        base.TestDriver.__init__(self);
        self.fImportedVBoxApi   = False;
        self.fpApiVer           = 3.2;
        self.uRevision          = 0;
        self.uApiRevision       = 0;
        self.oBuild             = None;
        self.oVBoxMgr           = None;
        self.oVBox              = None;
        self.aoRemoteSessions   = [];
        self.aoVMs              = []; ## @todo not sure if this list will be of any use.
        self.oTestVmManager     = vboxtestvms.TestVmManager(self.sResourcePath);
        self.oTestVmSet         = vboxtestvms.TestVmSet();
        self.sSessionTypeDef    = 'headless';
        self.sSessionType       = self.sSessionTypeDef;
        self.fEnableVrdp        = True;
        self.uVrdpBasePortDef   = 6000;
        self.uVrdpBasePort      = self.uVrdpBasePortDef;
        self.sDefBridgedNic     = None;
        self.fUseDefaultSvc     = False;
        self.sLogSelfGroups     = '';
        self.sLogSelfFlags      = 'time';
        self.sLogSelfDest       = '';
        self.sLogSessionGroups  = '';
        self.sLogSessionFlags   = 'time';
        self.sLogSessionDest    = '';
        self.sLogSvcGroups      = '';
        self.sLogSvcFlags       = 'time';
        self.sLogSvcDest        = '';
        self.sSelfLogFile       = None;
        self.sSessionLogFile    = None;
        self.sVBoxSvcLogFile    = None;
        self.oVBoxSvcProcess    = None;
        self.sVBoxSvcPidFile    = None;
        self.fVBoxSvcInDebugger = False;
        self.fVBoxSvcWaitForDebugger = False;
        self.sVBoxValidationKit     = None;
        self.sVBoxValidationKitIso  = None;
        self.sVBoxBootSectors   = None;
        self.fAlwaysUploadLogs  = False;
        self.fAlwaysUploadScreenshots = False;
        self.fAlwaysUploadRecordings  = False; # Only upload recording files on failure by default.
        self.fEnableDebugger          = True;
        self.fVmNoTerminate           = False; # Whether to skip exit handling and tearing down the VMs.
        self.adRecordingFiles         = [];
        self.fRecordingEnabled        = False; # Don't record by default (yet).
        self.fRecordingAudio          = False; # Don't record audio by default.
        self.cSecsRecordingMax        = 0;     # No recording time limit in seconds.
        self.cMbRecordingMax          = 195;   # The test manager web server has a configured upload limit of 200 MiBs.
                                               ## @todo Can we query the configured value here
                                               # (via `from testmanager import config`)?

        # Drop LD_PRELOAD and enable memory leak detection in LSAN_OPTIONS from vboxinstall.py
        # before doing build detection. This is a little crude and inflexible...
        if 'LD_PRELOAD' in os.environ:
            del os.environ['LD_PRELOAD'];
            if 'LSAN_OPTIONS' in os.environ:
                asLSanOptions = os.environ['LSAN_OPTIONS'].split(':');
                try:    asLSanOptions.remove('detect_leaks=0');
                except: pass;
                if asLSanOptions: os.environ['LSAN_OPTIONS'] = ':'.join(asLSanOptions);
                else:         del os.environ['LSAN_OPTIONS'];

        # Quietly detect build and validation kit.
        self._detectBuild(False);
        self._detectValidationKit(False);

        # Make sure all debug logs goes to the scratch area unless
        # specified otherwise (more of this later on).
        if 'VBOX_LOG_DEST' not in os.environ:
            os.environ['VBOX_LOG_DEST'] = 'nodeny dir=%s' % (self.sScratchPath);


    def _detectBuild(self, fQuiet = False):
        """
        This is used internally to try figure a locally installed build when
        running tests manually.
        """
        if self.oBuild is not None:
            return True;

        # Try dev build first since that's where I'll be using it first...
        if True is True: # pylint: disable=comparison-with-itself,comparison-of-constants
            try:
                self.oBuild = Build(self, None);
                reporter.log('VBox %s build at %s (%s).'
                             % (self.oBuild.sType, self.oBuild.sInstallPath, self.oBuild.sDesignation,));
                return True;
            except base.GenError:
                pass;

        # Try default installation locations.
        if self.sHost == 'win':
            sProgFiles = os.environ.get('ProgramFiles', 'C:\\Program Files');
            asLocs = [
                os.path.join(sProgFiles, 'Oracle', 'VirtualBox'),
                os.path.join(sProgFiles, 'OracleVM', 'VirtualBox'),
                os.path.join(sProgFiles, 'Sun', 'VirtualBox'),
            ];
        elif self.sHost == 'solaris':
            asLocs = [ '/opt/VirtualBox-3.2', '/opt/VirtualBox-3.1', '/opt/VirtualBox-3.0', '/opt/VirtualBox' ];
        elif self.sHost == 'darwin':
            asLocs = [ '/Applications/VirtualBox.app/Contents/MacOS' ];
        elif self.sHost == 'linux':
            asLocs = [ '/opt/VirtualBox-3.2', '/opt/VirtualBox-3.1', '/opt/VirtualBox-3.0', '/opt/VirtualBox' ];
        else:
            asLocs = [ '/opt/VirtualBox' ];
        if 'VBOX_INSTALL_PATH' in os.environ:
            asLocs.insert(0, os.environ['VBOX_INSTALL_PATH']);

        for sLoc in asLocs:
            try:
                self.oBuild = Build(self, sLoc);
                reporter.log('VBox %s build at %s (%s).'
                             % (self.oBuild.sType, self.oBuild.sInstallPath, self.oBuild.sDesignation,));
                return True;
            except base.GenError:
                pass;

        if not fQuiet:
            reporter.error('failed to find VirtualBox installation');
        return False;

    def _detectValidationKit(self, fQuiet = False):
        """
        This is used internally by the constructor to try locate an unzipped
        VBox Validation Kit somewhere in the immediate proximity.
        """
        if self.sVBoxValidationKit is not None:
            return True;

        #
        # Normally it's found where we're running from, which is the same as
        # the script directly on the testboxes.
        #
        asCandidates = [self.sScriptPath, ];
        if g_ksValidationKitDir not in asCandidates:
            asCandidates.append(g_ksValidationKitDir);
        if os.getcwd() not in asCandidates:
            asCandidates.append(os.getcwd());
        if self.oBuild is not None  and  self.oBuild.sInstallPath not in asCandidates:
            asCandidates.append(self.oBuild.sInstallPath);

        #
        # When working out of the tree, we'll search the current directory
        # as well as parent dirs.
        #
        for sDir in list(asCandidates):
            for i in range(10):
                sDir = os.path.dirname(sDir);
                if sDir not in asCandidates:
                    asCandidates.append(sDir);

        #
        # Do the searching.
        #
        sCandidate = None;
        for i, _ in enumerate(asCandidates):
            sCandidate = asCandidates[i];
            if os.path.isfile(os.path.join(sCandidate, 'VBoxValidationKit.iso')):
                break;
            sCandidate = os.path.join(sCandidate, 'validationkit');
            if os.path.isfile(os.path.join(sCandidate, 'VBoxValidationKit.iso')):
                break;
            sCandidate = None;

        fRc = sCandidate is not None;
        if fRc is False:
            if not fQuiet:
                reporter.error('failed to find VBox Validation Kit installation (candidates: %s)' % (asCandidates,));
            sCandidate = os.path.join(self.sScriptPath, 'validationkit'); # Don't leave the values as None.

        #
        # Set the member values.
        #
        self.sVBoxValidationKit     = sCandidate;
        self.sVBoxValidationKitIso  = os.path.join(sCandidate, 'VBoxValidationKit.iso');
        self.sVBoxBootSectors   = os.path.join(sCandidate, 'bootsectors');
        return fRc;

    def _makeEnvironmentChanges(self):
        """
        Make the necessary VBox related environment changes.
        Children not importing the VBox API should call this.
        """
        # Make sure we've got our own VirtualBox config and VBoxSVC (on XPCOM at least).
        if not self.fUseDefaultSvc:
            os.environ['VBOX_USER_HOME']    = os.path.join(self.sScratchPath, 'VBoxUserHome');
            sUser = os.environ.get('USERNAME', os.environ.get('USER', os.environ.get('LOGNAME', 'unknown')));
            os.environ['VBOX_IPC_SOCKETID'] = sUser + '-VBoxTest';
        return True;

    @staticmethod
    def makeApiRevision(uMajor, uMinor, uBuild, uApiRevision):
        """ Calculates an API revision number. """
        return (long(uMajor) << 56) | (long(uMinor) << 48) | (long(uBuild) << 40) | uApiRevision;

    def importVBoxApi(self):
        """
        Import the 'vboxapi' module from the VirtualBox build we're using and
        instantiate the two basic objects.

        This will try detect an development or installed build if no build has
        been associated with the driver yet.
        """
        reporter.log2('importVBoxApi started\n')
        if self.fImportedVBoxApi:
            return True;

        self._makeEnvironmentChanges();

        # Do the detecting.
        self._detectBuild();
        if self.oBuild is None:
            return False;

        # Avoid crashing when loading the 32-bit module (or whatever it is that goes bang).
        if     self.oBuild.sArch == 'x86' \
           and self.sHost == 'darwin' \
           and platform.architecture()[0] == '64bit' \
           and self.oBuild.sKind == 'development' \
           and os.getenv('VERSIONER_PYTHON_PREFER_32_BIT') != 'yes':
            reporter.log("WARNING: 64-bit python on darwin, 32-bit VBox development build => crash");
            reporter.log("WARNING:   bash-3.2$ /usr/bin/python2.5 ./testdriver");
            reporter.log("WARNING: or");
            reporter.log("WARNING:   bash-3.2$ VERSIONER_PYTHON_PREFER_32_BIT=yes ./testdriver");
            return False;

        # Start VBoxSVC and load the vboxapi bits.
        if self._startVBoxSVC() is True:
            assert(self.oVBoxSvcProcess is not None);

            sSavedSysPath = sys.path;
            self._setupVBoxApi();
            sys.path = sSavedSysPath;

            # Adjust the default machine folder.
            if self.fImportedVBoxApi and not self.fUseDefaultSvc and self.fpApiVer >= 4.0:
                sNewFolder = os.path.join(self.sScratchPath, 'VBoxUserHome', 'Machines');
                try:
                    self.oVBox.systemProperties.defaultMachineFolder = sNewFolder;
                except:
                    self.fImportedVBoxApi = False;
                    self.oVBoxMgr = None;
                    self.oVBox    = None;
                    reporter.logXcpt("defaultMachineFolder exception (sNewFolder=%s)" % (sNewFolder,));

            # Kill VBoxSVC on failure.
            if self.oVBoxMgr is None:
                self._stopVBoxSVC();
        else:
            assert(self.oVBoxSvcProcess is None);
        reporter.log2('importVBoxApi finished\n')
        return self.fImportedVBoxApi;

    def _printEnv(self, dEnv = os.environ, fRaw = False): # pylint: disable=dangerous-default-value
        """
        Prints the given environment block to log2.

        Uses the default environment block if not specified explicitly.
        Removes any control characters found to not mess up the screen output.
        """
        for sKey, sVal in sorted(dEnv.items()):
            reporter.log2('%s=%s' % (sKey, sVal if fRaw else re.sub(r'[\x00-\x1f]', '', sVal)));

    def _startVBoxSVC(self): # pylint: disable=too-many-statements
        """ Starts VBoxSVC. """
        assert(self.oVBoxSvcProcess is None);

        # Setup vbox logging for VBoxSVC now and start it manually.  This way
        # we can control both logging and shutdown.
        self.sVBoxSvcLogFile = '%s/VBoxSVC-debug.log' % (self.sScratchPath,);
        try:    os.remove(self.sVBoxSvcLogFile);
        except: pass;
        os.environ['VBOX_LOG']       = self.sLogSvcGroups;
        os.environ['VBOX_LOG_FLAGS'] = '%s append' % (self.sLogSvcFlags,);  # Append becuse of VBoxXPCOMIPCD.
        if self.sLogSvcDest:
            os.environ['VBOX_LOG_DEST'] = 'nodeny ' + self.sLogSvcDest;
        else:
            os.environ['VBOX_LOG_DEST'] = 'nodeny file=%s' % (self.sVBoxSvcLogFile,);
        os.environ['VBOXSVC_RELEASE_LOG_FLAGS'] = 'time append';

        reporter.log2('VBoxSVC environment:');
        self._printEnv();

        # Always leave a pid file behind so we can kill it during cleanup-before.
        self.sVBoxSvcPidFile = '%s/VBoxSVC.pid' % (self.sScratchPath,);
        fWritePidFile = True;

        cMsFudge      = 1;
        sVBoxSVC      = '%s/VBoxSVC' % (self.oBuild.sInstallPath,); ## @todo .exe and stuff.
        if self.fVBoxSvcInDebugger:
            if self.sHost in ('darwin', 'freebsd', 'linux', 'solaris', ):
                # Start VBoxSVC in gdb in a new terminal.
                #sTerm = '/usr/bin/gnome-terminal'; - doesn't work, some fork+exec stuff confusing us.
                sTerm = '/usr/bin/xterm';
                if not os.path.isfile(sTerm): sTerm = '/usr/X11/bin/xterm';
                if not os.path.isfile(sTerm): sTerm = '/usr/X11R6/bin/xterm';
                if not os.path.isfile(sTerm): sTerm = '/usr/bin/xterm';
                if not os.path.isfile(sTerm): sTerm = 'xterm';
                sGdb = '/usr/bin/gdb';
                if not os.path.isfile(sGdb): sGdb = '/usr/local/bin/gdb';
                if not os.path.isfile(sGdb): sGdb = '/usr/sfw/bin/gdb';
                if not os.path.isfile(sGdb): sGdb = 'gdb';
                sGdbCmdLine = '%s --args %s --pidfile %s' % (sGdb, sVBoxSVC, self.sVBoxSvcPidFile);
                # Cool tweak to run performance analysis instead of gdb:
                #sGdb = '/usr/bin/valgrind';
                #sGdbCmdLine = '%s --tool=callgrind --collect-atstart=no -- %s --pidfile %s' \
                #    % (sGdb, sVBoxSVC, self.sVBoxSvcPidFile);
                reporter.log('term="%s" gdb="%s"' % (sTerm, sGdbCmdLine));
                os.environ['SHELL'] = self.sOrgShell; # Non-working shell may cause gdb and/or the term problems.
                ## @todo -e  is deprecated; use "-- <args>".
                self.oVBoxSvcProcess = base.Process.spawnp(sTerm, sTerm, '-e', sGdbCmdLine);
                os.environ['SHELL'] = self.sOurShell;
                if self.oVBoxSvcProcess is not None:
                    reporter.log('Press enter or return after starting VBoxSVC in the debugger...');
                    sys.stdin.read(1);
                fWritePidFile = False;

            elif self.sHost == 'win':
                sWinDbg = 'c:\\Program Files\\Debugging Tools for Windows\\windbg.exe';
                if not os.path.isfile(sWinDbg): sWinDbg = 'c:\\Program Files\\Debugging Tools for Windows (x64)\\windbg.exe';
                if not os.path.isfile(sWinDbg): sWinDbg = 'c:\\Programme\\Debugging Tools for Windows\\windbg.exe'; # Localization rulez!  pylint: disable=line-too-long
                if not os.path.isfile(sWinDbg): sWinDbg = 'c:\\Programme\\Debugging Tools for Windows (x64)\\windbg.exe';
                if not os.path.isfile(sWinDbg): sWinDbg = 'windbg'; # WinDbg must be in the path; better than nothing.
                # Assume that everything WinDbg needs is defined using the environment variables.
                # See WinDbg help for more information.
                reporter.log('windbg="%s"' % (sWinDbg));
                self.oVBoxSvcProcess = base.Process.spawn(sWinDbg, sWinDbg, sVBoxSVC + base.exeSuff());
                if self.oVBoxSvcProcess is not None:
                    reporter.log('Press enter or return after starting VBoxSVC in the debugger...');
                    sys.stdin.read(1);
                fWritePidFile = False;
                ## @todo add a pipe interface similar to xpcom if feasible, i.e. if
                # we can get actual handle values for pipes in python.

            else:
                reporter.error('Port me!');
        else: # Run without a debugger attached.
            if self.sHost in ('darwin', 'freebsd', 'linux', 'solaris', ):
                #
                # XPCOM - We can use a pipe to let VBoxSVC notify us when it's ready.
                #
                iPipeR, iPipeW = os.pipe();
                if hasattr(os, 'set_inheritable'):
                    os.set_inheritable(iPipeW, True);             # pylint: disable=no-member

                # For VBox < 7.1 this is required.
                os.environ['NSPR_INHERIT_FDS'] = 'vboxsvc:startup-pipe:5:0x%x' % (iPipeW,);
                reporter.log2("NSPR_INHERIT_FDS=%s" % (os.environ['NSPR_INHERIT_FDS']));

                # New way since VBox 7.1
                os.environ['VBOX_STARTUP_PIPE_FD'] = '%u' % (iPipeW,);
                reporter.log2("VBOX_STARTUP_PIPE_FD=%s" % (os.environ['VBOX_STARTUP_PIPE_FD']));

                self.oVBoxSvcProcess = base.Process.spawn(sVBoxSVC, sVBoxSVC, '--auto-shutdown'); # SIGUSR1 requirement.
                try: # Try make sure we get the SIGINT and not VBoxSVC.
                    os.setpgid(self.oVBoxSvcProcess.getPid(), 0); # pylint: disable=no-member
                    os.setpgid(0, 0);                             # pylint: disable=no-member
                except:
                    reporter.logXcpt();

                os.close(iPipeW);
                try:
                    sResponse = os.read(iPipeR, 32);
                except:
                    reporter.logXcpt();
                    sResponse = None;
                os.close(iPipeR);

                if hasattr(sResponse, 'decode'):
                    sResponse = sResponse.decode('utf-8', 'ignore');

                if sResponse is None  or  sResponse.strip() != 'READY':
                    reporter.error('VBoxSVC failed starting up... (sResponse=%s)' % (sResponse,));
                    if not self.oVBoxSvcProcess.wait(5000):
                        self.oVBoxSvcProcess.terminate();
                    self.oVBoxSvcProcess.wait(5000);
                    self.oVBoxSvcProcess = None;

            elif self.sHost == 'win':
                #
                # Windows - Just fudge it for now.
                #
                cMsFudge = 2000;
                self.oVBoxSvcProcess = base.Process.spawn(sVBoxSVC, sVBoxSVC);

            else:
                reporter.error('Port me!');

            #
            # Enable automatic crash reporting if we succeeded.
            #
            if self.oVBoxSvcProcess is not None:
                self.oVBoxSvcProcess.enableCrashReporting('crash/report/svc', 'crash/dump/svc');

            #
            # Wait for debugger to attach.
            #
            if self.oVBoxSvcProcess is not None and self.fVBoxSvcWaitForDebugger:
                reporter.log('Press any key after attaching to VBoxSVC (pid %s) with a debugger...'
                             % (self.oVBoxSvcProcess.getPid(),));
                sys.stdin.read(1);

        #
        # Fudge and pid file.
        #
        if self.oVBoxSvcProcess is not None and not self.oVBoxSvcProcess.wait(cMsFudge):
            if fWritePidFile:
                iPid = self.oVBoxSvcProcess.getPid();
                try:
                    oFile = utils.openNoInherit(self.sVBoxSvcPidFile, "w+");
                    oFile.write('%s' % (iPid,));
                    oFile.close();
                except:
                    reporter.logXcpt('sPidFile=%s' % (self.sVBoxSvcPidFile,));
                reporter.log('VBoxSVC PID=%u' % (iPid,));

            #
            # Finally add the task so we'll notice when it dies in a relatively timely manner.
            #
            self.addTask(self.oVBoxSvcProcess);
        else:
            self.oVBoxSvcProcess = None;
            try:    os.remove(self.sVBoxSvcPidFile);
            except: pass;

        return self.oVBoxSvcProcess is not None;


    def _killVBoxSVCByPidFile(self, sPidFile):
        """ Kill a VBoxSVC given the pid from it's pid file. """

        # Read the pid file.
        if not os.path.isfile(sPidFile):
            return False;
        try:
            oFile = utils.openNoInherit(sPidFile, "r");
            sPid = oFile.readline().strip();
            oFile.close();
        except:
            reporter.logXcpt('sPidfile=%s' % (sPidFile,));
            return False;

        # Convert the pid to an integer and validate the range a little bit.
        try:
            iPid = long(sPid);
        except:
            reporter.logXcpt('sPidfile=%s sPid="%s"' % (sPidFile, sPid));
            return False;
        if iPid <= 0:
            reporter.log('negative pid - sPidfile=%s sPid="%s" iPid=%d' % (sPidFile, sPid, iPid));
            return False;

        # Take care checking that it's VBoxSVC we're about to inhume.
        if base.processCheckPidAndName(iPid, "VBoxSVC") is not True:
            reporter.log('Ignoring stale VBoxSVC pid file (pid=%s)' % (iPid,));
            return False;

        # Loop thru our different ways of getting VBoxSVC to terminate.
        for aHow in [ [ base.sendUserSignal1,  5000, 'Dropping VBoxSVC a SIGUSR1 hint...'], \
                      [ base.processInterrupt, 5000, 'Dropping VBoxSVC a SIGINT hint...'], \
                      [ base.processTerminate, 7500, 'VBoxSVC is still around, killing it...'] ]:
            reporter.log(aHow[2]);
            if aHow[0](iPid) is True:
                msStart = base.timestampMilli();
                while base.timestampMilli() - msStart < 5000 \
                  and base.processExists(iPid):
                    time.sleep(0.2);

            fRc = not base.processExists(iPid);
            if fRc is True:
                break;
        if fRc:
            reporter.log('Successfully killed VBoxSVC (pid=%s)' % (iPid,));
        else:
            reporter.log('Failed to kill VBoxSVC (pid=%s)' % (iPid,));
        return fRc;

    def _stopVBoxSVC(self):
        """
        Stops VBoxSVC.  Try the polite way first.
        """

        if self.oVBoxSvcProcess:
            self.removeTask(self.oVBoxSvcProcess);
            self.oVBoxSvcProcess.enableCrashReporting(None, None); # Disables it.

        fRc = False;
        if   self.oVBoxSvcProcess is not None \
         and not self.fVBoxSvcInDebugger:
            # by process object.
            if self.oVBoxSvcProcess.isRunning():
                reporter.log('Dropping VBoxSVC a SIGUSR1 hint...');
                if  not self.oVBoxSvcProcess.sendUserSignal1() \
                 or not self.oVBoxSvcProcess.wait(5000):
                    reporter.log('Dropping VBoxSVC a SIGINT hint...');
                    if  not self.oVBoxSvcProcess.interrupt() \
                     or not self.oVBoxSvcProcess.wait(5000):
                        reporter.log('VBoxSVC is still around, killing it...');
                        self.oVBoxSvcProcess.terminate();
                        self.oVBoxSvcProcess.wait(7500);
            else:
                reporter.log('VBoxSVC is no longer running...');

            if not self.oVBoxSvcProcess.isRunning():
                iExit = self.oVBoxSvcProcess.getExitCode();
                if iExit != 0 or not self.oVBoxSvcProcess.isNormalExit():
                    reporter.error("VBoxSVC exited with status %d (%#x)" % (iExit, self.oVBoxSvcProcess.uExitCode));
                self.oVBoxSvcProcess = None;
        else:
            # by pid file.
            self._killVBoxSVCByPidFile('%s/VBoxSVC.pid' % (self.sScratchPath,));
        return fRc;

    def _setupVBoxApi(self):
        """
        Import and set up the vboxapi.
        The caller saves and restores sys.path.
        """

        # Setup vbox logging for self (the test driver).
        self.sSelfLogFile = '%s/VBoxTestDriver.log' % (self.sScratchPath,);
        try:    os.remove(self.sSelfLogFile);
        except: pass;
        os.environ['VBOX_LOG']       = self.sLogSelfGroups;
        os.environ['VBOX_LOG_FLAGS'] = '%s append' % (self.sLogSelfFlags, );
        if self.sLogSelfDest:
            os.environ['VBOX_LOG_DEST'] = 'nodeny ' + self.sLogSelfDest;
        else:
            os.environ['VBOX_LOG_DEST'] = 'nodeny file=%s' % (self.sSelfLogFile,);
        os.environ['VBOX_RELEASE_LOG_FLAGS'] = 'time append';

        reporter.log2('Self environment:');
        self._printEnv();

        # Hack the sys.path + environment so the vboxapi can be found.
        sys.path.insert(0, self.oBuild.sInstallPath);
        if self.oBuild.sSdkPath is not None:
            sys.path.insert(0, os.path.join(self.oBuild.sSdkPath, 'installer'))
            sys.path.insert(0, os.path.join(self.oBuild.sSdkPath, 'installer', 'python', 'vboxapi', 'src')) # For >= VBox 7.1
            sys.path.insert(1, os.path.join(self.oBuild.sSdkPath, 'install')); # stupid stupid windows installer (VBox < 7.1)!
            sys.path.insert(2, os.path.join(self.oBuild.sSdkPath, 'bindings', 'xpcom', 'python'))
        os.environ['VBOX_PROGRAM_PATH'] = self.oBuild.sInstallPath;
        reporter.log("sys.path: %s" % (sys.path));

        try:
            from vboxapi import VirtualBoxManager;  # pylint: disable=import-error
        except:
            reporter.logXcpt('Error importing vboxapi (Python %s)' % (sys.version,));
            return False;

        # Exception and error hacks.
        try:
            # pylint: disable=import-error
            if self.sHost == 'win':
                from pythoncom import com_error as NativeComExceptionClass  # pylint: disable=no-name-in-module
                import winerror                 as NativeComErrorClass
            else:
                from xpcom import Exception     as NativeComExceptionClass
                from xpcom import nsError       as NativeComErrorClass
            # pylint: enable=import-error
        except:
            reporter.logXcpt('Error importing (XP)COM related stuff for exception hacks and errors');
            return False;
        __deployExceptionHacks__(NativeComExceptionClass)
        ComError.copyErrors(NativeComErrorClass);

        # Create the manager.
        try:
            self.oVBoxMgr = VirtualBoxManager(None, None)
        except:
            self.oVBoxMgr = None;
            reporter.logXcpt('VirtualBoxManager exception');
            return False;

        # Figure the API version.
        try:
            oVBox = self.oVBoxMgr.getVirtualBox();

            try:
                sVer = oVBox.version;
            except:
                reporter.logXcpt('Failed to get VirtualBox version, assuming 4.0.0');
                sVer = "4.0.0";
            reporter.log("IVirtualBox.version=%s" % (sVer,));

            # Convert the string to three integer values and check ranges.
            asVerComponents = sVer.split('.');
            try:
                sLast = asVerComponents[2].split('_')[0].split('r')[0];
                aiVerComponents = (int(asVerComponents[0]), int(asVerComponents[1]), int(sLast));
            except:
                raise base.GenError('Malformed version "%s"' % (sVer,));
            if aiVerComponents[0] < 3 or aiVerComponents[0] > 19:
                raise base.GenError('Malformed version "%s" - 1st component is out of bounds 3..19: %u'
                                    % (sVer, aiVerComponents[0]));
            if aiVerComponents[1] < 0 or aiVerComponents[1] > 9:
                raise base.GenError('Malformed version "%s" - 2nd component is out of bounds 0..9: %u'
                                    % (sVer, aiVerComponents[1]));
            if aiVerComponents[2] < 0 or aiVerComponents[2] > 99:
                raise base.GenError('Malformed version "%s" - 3rd component is out of bounds 0..99: %u'
                                    % (sVer, aiVerComponents[2]));

            # Convert the three integers into a floating point value.  The API is stable within a
            # x.y release, so the third component only indicates whether it's a stable or
            # development build of the next release.
            self.fpApiVer = aiVerComponents[0] + 0.1 * aiVerComponents[1];
            if aiVerComponents[2] >= 71:
                if self.fpApiVer not in [6.1, 5.2, 4.3, 3.2,]:
                    self.fpApiVer += 0.1;
                else:
                    self.fpApiVer = int(self.fpApiVer) + 1.0;
            # fudge value to be always bigger than the nominal value (0.1 gets rounded down)
            if round(self.fpApiVer, 1) > self.fpApiVer:
                self.fpApiVer += sys.float_info.epsilon * self.fpApiVer / 2.0;

            try:
                self.uRevision = oVBox.revision;
            except:
                reporter.logXcpt('Failed to get VirtualBox revision, assuming 0');
                self.uRevision = 0;
            reporter.log("IVirtualBox.revision=%u" % (self.uRevision,));

            try:
                self.uApiRevision = oVBox.APIRevision;
            except:
                reporter.logXcpt('Failed to get VirtualBox APIRevision, faking it.');
                self.uApiRevision = self.makeApiRevision(aiVerComponents[0], aiVerComponents[1], aiVerComponents[2], 0);
            reporter.log("IVirtualBox.APIRevision=%#x" % (self.uApiRevision,));

            # Patch VBox manage to gloss over portability issues (error constants, etc).
            self._patchVBoxMgr();

            # Wrap oVBox.
            from testdriver.vboxwrappers import VirtualBoxWrapper;
            self.oVBox = VirtualBoxWrapper(oVBox, self.oVBoxMgr, self.fpApiVer, self);

            # Install the constant wrapping hack.
            vboxcon.goHackModuleClass.oVBoxMgr  = self.oVBoxMgr; # VBoxConstantWrappingHack.
            vboxcon.fpApiVer                    = self.fpApiVer;
            reporter.setComXcptFormatter(formatComOrXpComException);

            # Enable possibly not production ready code which should be tested.
            if self.fpApiVer >= 7.1 and utils.getHostArch() == 'arm64':
                self.oVBox.setExtraData('VBoxInternal2/EnableX86OnArm', '1');

        except:
            self.oVBoxMgr = None;
            self.oVBox    = None;
            reporter.logXcpt("getVirtualBox / API version exception");
            return False;

        # Done
        self.fImportedVBoxApi = True;
        reporter.log('Found version %s (%s)' % (self.fpApiVer, sVer));
        return True;

    def _patchVBoxMgr(self):
        """
        Glosses over missing self.oVBoxMgr methods on older VBox versions.
        """

        def _xcptGetResult(oSelf, oXcpt = None):
            """ See vboxapi. """
            _ = oSelf;
            if oXcpt is None: oXcpt = sys.exc_info()[1];
            if sys.platform == 'win32':
                import winerror;                                            # pylint: disable=import-error
                hrXcpt = oXcpt.hresult;
                if hrXcpt == winerror.DISP_E_EXCEPTION:
                    hrXcpt = oXcpt.excepinfo[5];
            else:
                hrXcpt = oXcpt.error;
            return hrXcpt;

        def _xcptIsDeadInterface(oSelf, oXcpt = None):
            """ See vboxapi. """
            return oSelf.xcptGetStatus(oXcpt) in [
                0x80004004, -2147467260, # NS_ERROR_ABORT
                0x800706be, -2147023170, # NS_ERROR_CALL_FAILED (RPC_S_CALL_FAILED)
                0x800706ba, -2147023174, # RPC_S_SERVER_UNAVAILABLE.
                0x800706be, -2147023170, # RPC_S_CALL_FAILED.
                0x800706bf, -2147023169, # RPC_S_CALL_FAILED_DNE.
                0x80010108, -2147417848, # RPC_E_DISCONNECTED.
                0x800706b5, -2147023179, # RPC_S_UNKNOWN_IF
            ];

        def _xcptIsOurXcptKind(oSelf, oXcpt = None):
            """ See vboxapi. """
            _ = oSelf;
            if oXcpt is None: oXcpt = sys.exc_info()[1];
            if sys.platform == 'win32':
                from pythoncom import com_error as NativeComExceptionClass  # pylint: disable=import-error,no-name-in-module
            else:
                from xpcom import Exception     as NativeComExceptionClass  # pylint: disable=import-error
            return isinstance(oXcpt, NativeComExceptionClass);

        def _xcptIsEqual(oSelf, oXcpt, hrStatus):
            """ See vboxapi. """
            hrXcpt = oSelf.xcptGetResult(oXcpt);
            return hrXcpt == hrStatus or hrXcpt == hrStatus - 0x100000000;  # pylint: disable=consider-using-in

        def _xcptToString(oSelf, oXcpt):
            """ See vboxapi. """
            _ = oSelf;
            if oXcpt is None: oXcpt = sys.exc_info()[1];
            return str(oXcpt);

        def _getEnumValueName(oSelf, sEnumTypeNm, oEnumValue, fTypePrefix = False):
            """ See vboxapi. """
            _ = oSelf; _ = fTypePrefix;
            return '%s::%s' % (sEnumTypeNm, oEnumValue);

        # Add utilities found in newer vboxapi revision.
        if not hasattr(self.oVBoxMgr, 'xcptIsDeadInterface'):
            import types;
            self.oVBoxMgr.xcptGetResult         = types.MethodType(_xcptGetResult,       self.oVBoxMgr);
            self.oVBoxMgr.xcptIsDeadInterface   = types.MethodType(_xcptIsDeadInterface, self.oVBoxMgr);
            self.oVBoxMgr.xcptIsOurXcptKind     = types.MethodType(_xcptIsOurXcptKind,   self.oVBoxMgr);
            self.oVBoxMgr.xcptIsEqual           = types.MethodType(_xcptIsEqual,         self.oVBoxMgr);
            self.oVBoxMgr.xcptToString          = types.MethodType(_xcptToString,        self.oVBoxMgr);
        if not hasattr(self.oVBoxMgr, 'getEnumValueName'):
            import types;
            self.oVBoxMgr.getEnumValueName      = types.MethodType(_getEnumValueName,    self.oVBoxMgr);


    def _teardownVBoxApi(self):  # pylint: disable=too-many-statements
        """
        Drop all VBox object references and shutdown com/xpcom.
        """
        if not self.fImportedVBoxApi:
            return True;
        import gc;

        # Drop all references we've have to COM objects.
        self.aoRemoteSessions = [];
        self.aoVMs            = [];
        self.oVBoxMgr         = None;
        self.oVBox            = None;
        vboxcon.goHackModuleClass.oVBoxMgr = None; # VBoxConstantWrappingHack.
        reporter.setComXcptFormatter(None);

        # Do garbage collection to try get rid of those objects.
        try:
            gc.collect();
        except:
            reporter.logXcpt();
        self.fImportedVBoxApi = False;

        # Check whether the python is still having any COM objects/interfaces around.
        cVBoxMgrs = 0;
        aoObjsLeftBehind = [];
        if self.sHost == 'win':
            import pythoncom;                                   # pylint: disable=import-error
            try:
                cIfs  = pythoncom._GetInterfaceCount();         # pylint: disable=no-member,protected-access
                cObjs = pythoncom._GetGatewayCount();           # pylint: disable=no-member,protected-access
                if cObjs == 0 and cIfs == 0:
                    reporter.log('_teardownVBoxApi: no interfaces or objects left behind.');
                else:
                    reporter.log('_teardownVBoxApi: Python COM still has %s objects and %s interfaces...' % ( cObjs, cIfs));

                from win32com.client import DispatchBaseClass;  # pylint: disable=import-error
                for oObj in gc.get_objects():
                    if isinstance(oObj, DispatchBaseClass):
                        reporter.log('_teardownVBoxApi:   %s' % (oObj,));
                        aoObjsLeftBehind.append(oObj);
                    elif utils.getObjectTypeName(oObj) == 'VirtualBoxManager':
                        reporter.log('_teardownVBoxApi:   %s' % (oObj,));
                        cVBoxMgrs += 1;
                        aoObjsLeftBehind.append(oObj);
                oObj = None;
            except:
                reporter.logXcpt();

            # If not being used, we can safely uninitialize COM.
            if cIfs == 0 and cObjs == 0 and cVBoxMgrs == 0 and not aoObjsLeftBehind:
                reporter.log('_teardownVBoxApi:   Calling CoUninitialize...');
                try:    pythoncom.CoUninitialize();             # pylint: disable=no-member
                except: reporter.logXcpt();
                else:
                    reporter.log('_teardownVBoxApi:   Returned from CoUninitialize.');
        else:
            try:
                # XPCOM doesn't crash and burn like COM if you shut it down with interfaces and objects around.
                # Also, it keeps a number of internal objects and interfaces around to do its job, so shutting
                # it down before we go looking for dangling interfaces is more or less required.
                from xpcom import _xpcom as _xpcom;             # pylint: disable=import-error,useless-import-alias
                hrc   = _xpcom.DeinitCOM();
                cIfs  = _xpcom._GetInterfaceCount();            # pylint: disable=protected-access
                cObjs = _xpcom._GetGatewayCount();              # pylint: disable=protected-access

                if cObjs == 0 and cIfs == 0:
                    reporter.log('_teardownVBoxApi: No XPCOM interfaces or objects active. (hrc=%#x)' % (hrc,));
                else:
                    reporter.log('_teardownVBoxApi: %s XPCOM objects and %s interfaces still around! (hrc=%#x)'
                                 % (cObjs, cIfs, hrc));
                    if hasattr(_xpcom, '_DumpInterfaces'):
                        try:    _xpcom._DumpInterfaces();       # pylint: disable=protected-access
                        except: reporter.logXcpt('_teardownVBoxApi: _DumpInterfaces failed');

                from xpcom.client import Component;             # pylint: disable=import-error
                for oObj in gc.get_objects():
                    if isinstance(oObj, Component):
                        reporter.log('_teardownVBoxApi:   %s' % (oObj,));
                        aoObjsLeftBehind.append(oObj);
                    if utils.getObjectTypeName(oObj) == 'VirtualBoxManager':
                        reporter.log('_teardownVBoxApi:   %s' % (oObj,));
                        cVBoxMgrs += 1;
                        aoObjsLeftBehind.append(oObj);
                oObj = None;
            except:
                reporter.logXcpt();

        # Try get the referrers to (XP)COM interfaces and objects that was left behind.
        for iObj in range(len(aoObjsLeftBehind)): # pylint: disable=consider-using-enumerate
            try:
                aoReferrers = gc.get_referrers(aoObjsLeftBehind[iObj]);
                reporter.log('_teardownVBoxApi:   Found %u referrers to %s:' % (len(aoReferrers), aoObjsLeftBehind[iObj],));
                for oReferrer in aoReferrers:
                    oMyFrame = sys._getframe(0);  # pylint: disable=protected-access
                    if oReferrer is oMyFrame:
                        reporter.log('_teardownVBoxApi:     - frame of this function');
                    elif oReferrer is aoObjsLeftBehind:
                        reporter.log('_teardownVBoxApi:     - aoObjsLeftBehind');
                    else:
                        fPrinted = False;
                        if isinstance(oReferrer, (dict, list, tuple)):
                            try:
                                aoSubReferreres = gc.get_referrers(oReferrer);
                                for oSubRef in aoSubReferreres:
                                    if    not isinstance(oSubRef, list) \
                                      and not isinstance(oSubRef, dict) \
                                      and oSubRef is not oMyFrame \
                                      and oSubRef is not aoSubReferreres:
                                        reporter.log('_teardownVBoxApi:     - %s :: %s:'
                                                     % (utils.getObjectTypeName(oSubRef), utils.getObjectTypeName(oReferrer)));
                                        fPrinted = True;
                                        break;
                                del aoSubReferreres;
                            except:
                                reporter.logXcpt('subref');
                        if not fPrinted:
                            reporter.log('_teardownVBoxApi:     - %s:' % (utils.getObjectTypeName(oReferrer),));
                        try:
                            import pprint;
                            for sLine in pprint.pformat(oReferrer, width = 130).split('\n'):
                                reporter.log('_teardownVBoxApi:       %s' % (sLine,));
                        except:
                            reporter.log('_teardownVBoxApi:       %s' % (oReferrer,));
            except:
                reporter.logXcpt();
        del aoObjsLeftBehind;

        # Force garbage collection again, just for good measure.
        try:
            gc.collect();
            time.sleep(0.5); # fudge factor
        except:
            reporter.logXcpt();
        return True;

    def _powerOffAllVms(self):
        """
        Tries to power off all running VMs.
        """
        for oSession in self.aoRemoteSessions:
            uPid = oSession.getPid();
            if uPid is not None:
                reporter.log('_powerOffAllVms: PID is %s for %s, trying to kill it.' % (uPid, oSession.sName,));
                base.processKill(uPid);
            else:
                reporter.log('_powerOffAllVms: No PID for %s' % (oSession.sName,));
            oSession.close();
        return None;



    #
    # Build type, OS and arch getters.
    #

    def getBuildType(self):
        """
        Get the build type.
        """
        if not self._detectBuild():
            return 'release';
        return self.oBuild.sType;

    def getBuildOs(self):
        """
        Get the build OS.
        """
        if not self._detectBuild():
            return self.sHost;
        return self.oBuild.sOs;

    def getBuildArch(self):
        """
        Get the build arch.
        """
        if not self._detectBuild():
            return self.sHostArch;
        return self.oBuild.sArch;

    def getGuestAdditionsIso(self):
        """
        Get the path to the guest addition iso.
        """
        if not self._detectBuild():
            return None;
        return self.oBuild.sGuestAdditionsIso;

    @staticmethod
    def isVersionEqualOrBigger(sVer1, sVer2):
        """
        Checks whether version 1 is equal or bigger than version 2.

        Returns True if version 1 is equal or bigger than version 2, False if not.
        """
        return utils.versionCompare(sVer1, sVer2) >= 0;

    def getGuestAdditionsVersion(self, oSession, fIgnoreErrors = False):
        """
        Returns the installed Guest Additions version.

        Returns version as a string (e.g. "7.0.6-beta2-whatever"), or None if not found / on error.
        """
        assert oSession is not None;
        try:
            return oSession.o.console.guest.additionsVersion;
        except:
            if not fIgnoreErrors:
                reporter.errorXcpt('Getting the Guest Additions version failed');
        return None;

    def getGuestAdditionsRevision(self, oSession, fIgnoreErrors = False):
        """
        Returns the installed Guest Additions (SVN) revision.

        Returns revision as an integer, or None if not found / on error.
        """
        assert oSession is not None;
        try:
            return oSession.o.console.guest.additionsRevision;
        except:
            if not fIgnoreErrors:
                reporter.errorXcpt('Getting the Guest Additions revision failed');
        return None;

    #
    # Override everything from the base class so the testdrivers don't have to
    # check whether we have overridden a method or not.
    #

    def showUsage(self):
        rc = base.TestDriver.showUsage(self);
        reporter.log('');
        reporter.log('Generic VirtualBox Options:');
        reporter.log('  --vbox-session-type <type>');
        reporter.log('      Sets the session type.  Typical values are: gui, headless, sdl');
        reporter.log('      Default: %s' % (self.sSessionTypeDef));
        reporter.log('  --vrdp, --no-vrdp');
        reporter.log('      Enables VRDP, ports starting at 6000');
        reporter.log('      Default: --vrdp');
        reporter.log('  --vrdp-base-port <port>');
        reporter.log('      Sets the base for VRDP port assignments.');
        reporter.log('      Default: %s' % (self.uVrdpBasePortDef));
        reporter.log('  --vbox-default-bridged-nic <interface>');
        reporter.log('      Sets the default interface for bridged networking.');
        reporter.log('      Default: autodetect');
        reporter.log('  --vbox-use-svc-defaults');
        reporter.log('      Use default locations and files for VBoxSVC.  This is useful');
        reporter.log('      for automatically configuring the test VMs for debugging.');
        reporter.log('  --vbox-log');
        reporter.log('      The VBox logger group settings for everyone.');
        reporter.log('  --vbox-log-flags');
        reporter.log('      The VBox logger flags settings for everyone.');
        reporter.log('  --vbox-log-dest');
        reporter.log('      The VBox logger destination settings for everyone.');
        reporter.log('  --vbox-self-log');
        reporter.log('      The VBox logger group settings for the testdriver.');
        reporter.log('  --vbox-self-log-flags');
        reporter.log('      The VBox logger flags settings for the testdriver.');
        reporter.log('  --vbox-self-log-dest');
        reporter.log('      The VBox logger destination settings for the testdriver.');
        reporter.log('  --vbox-session-log');
        reporter.log('      The VM session logger group settings.');
        reporter.log('  --vbox-session-log-flags');
        reporter.log('      The VM session logger flags.');
        reporter.log('  --vbox-session-log-dest');
        reporter.log('      The VM session logger destination settings.');
        reporter.log('  --vbox-svc-log');
        reporter.log('      The VBoxSVC logger group settings.');
        reporter.log('  --vbox-svc-log-flags');
        reporter.log('      The VBoxSVC logger flag settings.');
        reporter.log('  --vbox-svc-log-dest');
        reporter.log('      The VBoxSVC logger destination settings.');
        reporter.log('  --vbox-svc-debug');
        reporter.log('      Start VBoxSVC in a debugger.');
        reporter.log('  --vbox-svc-wait-debug');
        reporter.log('      Start VBoxSVC and wait for debugger to attach to it.');
        reporter.log('  --vbox-always-upload-logs');
        reporter.log('      Whether to always upload log files, or only do so on failure.');
        reporter.log('  --vbox-always-upload-screenshots');
        reporter.log('      Whether to always upload final screen shots, or only do so on failure.');
        reporter.log('  --vbox-always-upload-recordings, --no-vbox-always-upload-recordings');
        reporter.log('      Whether to always upload recordings, or only do so on failure.');
        reporter.log('      Default: --no-vbox-always-upload-recordings');
        reporter.log('  --vbox-debugger, --no-vbox-debugger');
        reporter.log('      Enables the VBox debugger, port at 5000');
        reporter.log('      Default: --vbox-debugger');
        reporter.log('  --vbox-recording, --no-vbox-recording');
        reporter.log('      Enables/disables recording.');
        reporter.log('      Default: --no-vbox-recording');
        reporter.log('  --vbox-recording-audio, --no-vbox-recording-audio');
        reporter.log('      Enables/disables audio recording.');
        reporter.log('      Default: --no-vbox-recording-audio');
        reporter.log('  --vbox-recording-max-time <seconds>');
        reporter.log('      Limits the maximum recording time in seconds.');
        reporter.log('      Default: Unlimited.');
        reporter.log('  --vbox-recording-max-file-size <MiB>');
        reporter.log('      Limits the maximum per-file size in MiB.');
        reporter.log('      Explicitly specify 0 for unlimited size.');
        reporter.log('      Default: 195 MB.');
        reporter.log('  --vbox-vm-no-terminate');
        reporter.log('      Does not terminate the test VM after running the test driver.');
        if self.oTestVmSet is not None:
            self.oTestVmSet.showUsage();
        return rc;

    def parseOption(self, asArgs, iArg): # pylint: disable=too-many-branches,too-many-statements
        if asArgs[iArg] == '--vbox-session-type':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--vbox-session-type" takes an argument');
            self.sSessionType = asArgs[iArg];
        elif asArgs[iArg] == '--vrdp':
            self.fEnableVrdp = True;
        elif asArgs[iArg] == '--no-vrdp':
            self.fEnableVrdp = False;
        elif asArgs[iArg] == '--vrdp-base-port':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--vrdp-base-port" takes an argument');
            try:    self.uVrdpBasePort = int(asArgs[iArg]);
            except: raise base.InvalidOption('The "--vrdp-base-port" value "%s" is not a valid integer' % (asArgs[iArg],));
            if self.uVrdpBasePort <= 0 or self.uVrdpBasePort >= 65530:
                raise base.InvalidOption('The "--vrdp-base-port" value "%s" is not in the valid range (1..65530)'
                                         % (asArgs[iArg],));
        elif asArgs[iArg] == '--vbox-default-bridged-nic':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--vbox-default-bridged-nic" takes an argument');
            self.sDefBridgedNic = asArgs[iArg];
        elif asArgs[iArg] == '--vbox-use-svc-defaults':
            self.fUseDefaultSvc = True;
        elif asArgs[iArg] == '--vbox-self-log':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--vbox-self-log" takes an argument');
            self.sLogSelfGroups = asArgs[iArg];
        elif asArgs[iArg] == '--vbox-self-log-flags':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--vbox-self-log-flags" takes an argument');
            self.sLogSelfFlags = asArgs[iArg];
        elif asArgs[iArg] == '--vbox-self-log-dest':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--vbox-self-log-dest" takes an argument');
            self.sLogSelfDest = asArgs[iArg];
        elif asArgs[iArg] == '--vbox-session-log':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--vbox-session-log" takes an argument');
            self.sLogSessionGroups = asArgs[iArg];
        elif asArgs[iArg] == '--vbox-session-log-flags':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--vbox-session-log-flags" takes an argument');
            self.sLogSessionFlags = asArgs[iArg];
        elif asArgs[iArg] == '--vbox-session-log-dest':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--vbox-session-log-dest" takes an argument');
            self.sLogSessionDest = asArgs[iArg];
        elif asArgs[iArg] == '--vbox-svc-log':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--vbox-svc-log" takes an argument');
            self.sLogSvcGroups = asArgs[iArg];
        elif asArgs[iArg] == '--vbox-svc-log-flags':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--vbox-svc-log-flags" takes an argument');
            self.sLogSvcFlags = asArgs[iArg];
        elif asArgs[iArg] == '--vbox-svc-log-dest':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--vbox-svc-log-dest" takes an argument');
            self.sLogSvcDest = asArgs[iArg];
        elif asArgs[iArg] == '--vbox-log':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--vbox-log" takes an argument');
            self.sLogSelfGroups    = asArgs[iArg];
            self.sLogSessionGroups = asArgs[iArg];
            self.sLogSvcGroups     = asArgs[iArg];
        elif asArgs[iArg] == '--vbox-log-flags':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--vbox-svc-flags" takes an argument');
            self.sLogSelfFlags     = asArgs[iArg];
            self.sLogSessionFlags  = asArgs[iArg];
            self.sLogSvcFlags      = asArgs[iArg];
        elif asArgs[iArg] == '--vbox-log-dest':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--vbox-log-dest" takes an argument');
            self.sLogSelfDest     = asArgs[iArg];
            self.sLogSessionDest  = asArgs[iArg];
            self.sLogSvcDest      = asArgs[iArg];
        elif asArgs[iArg] == '--vbox-svc-debug':
            self.fVBoxSvcInDebugger = True;
        elif asArgs[iArg] == '--vbox-svc-wait-debug':
            self.fVBoxSvcWaitForDebugger = True;
        elif asArgs[iArg] == '--vbox-always-upload-logs':
            self.fAlwaysUploadLogs = True;
        elif asArgs[iArg] == '--vbox-always-upload-screenshots':
            self.fAlwaysUploadScreenshots = True;
        elif asArgs[iArg] == '--no-vbox-always-upload-recordings':
            self.fAlwaysUploadRecordings = False;
        elif asArgs[iArg] == '--vbox-always-upload-recordings':
            self.fAlwaysUploadRecordings = True;
        elif asArgs[iArg] == '--vbox-debugger':
            self.fEnableDebugger = True;
        elif asArgs[iArg] == '--no-vbox-debugger':
            self.fEnableDebugger = False;
        elif asArgs[iArg] == '--vbox-recording':
            self.fRecordingEnabled = True;
        elif asArgs[iArg] == '--vbox-no-recording':
            self.fRecordingEnabled = False;
        elif asArgs[iArg] == '--no-vbox-recording-audio':
            self.fRecordingAudio = False;
        elif asArgs[iArg] == '--vbox-recording-audio':
            self.fRecordingAudio = True;
        elif asArgs[iArg] == '--vbox-recording-max-time':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--vbox-recording-max-time" takes an argument');
            self.cSecsRecordingMax = int(asArgs[iArg]);
        elif asArgs[iArg] == '--vbox-recording-max-file-size':
            iArg += 1;
            if iArg >= len(asArgs):
                raise base.InvalidOption('The "--vbox-recording-max-file-size" takes an argument');
            self.cMbRecordingMax = int(asArgs[iArg]);
        elif asArgs[iArg] == '--vbox-vm-no-terminate':
            self.fVmNoTerminate = True;
        else:
            # Relevant for selecting VMs to test?
            if self.oTestVmSet is not None:
                iRc = self.oTestVmSet.parseOption(asArgs, iArg);
                if iRc != iArg:
                    return iRc;

            # Hand it to the base class.
            return base.TestDriver.parseOption(self, asArgs, iArg);
        return iArg + 1;

    def completeOptions(self):
        return base.TestDriver.completeOptions(self);

    def getNetworkAdapterNameFromType(self, oNic):
        """
        Returns the network adapter name from a given adapter type.

        Returns an empty string if not found / invalid.
        """
        sAdpName = '';
        if    oNic.adapterType in (vboxcon.NetworkAdapterType_Am79C970A, \
                                   vboxcon.NetworkAdapterType_Am79C973, \
                                   vboxcon.NetworkAdapterType_Am79C960):
            sAdpName = 'pcnet';
        elif    oNic.adapterType in (vboxcon.NetworkAdapterType_I82540EM, \
                                     vboxcon.NetworkAdapterType_I82543GC, \
                                     vboxcon.NetworkAdapterType_I82545EM):
            sAdpName = 'e1000';
        elif oNic.adapterType == vboxcon.NetworkAdapterType_Virtio:
            sAdpName = 'virtio-net';
        return sAdpName;

    def getResourceSet(self):
        asRsrcs = [];
        if self.oTestVmSet is not None:
            asRsrcs.extend(self.oTestVmSet.getResourceSet());
        asRsrcs.extend(base.TestDriver.getResourceSet(self));
        return asRsrcs;

    def actionExtract(self):
        return base.TestDriver.actionExtract(self);

    def actionVerify(self):
        return base.TestDriver.actionVerify(self);

    def actionConfig(self):
        return base.TestDriver.actionConfig(self);

    def actionExecute(self):
        return base.TestDriver.actionExecute(self);

    def actionCleanupBefore(self):
        """
        Kill any VBoxSVC left behind by a previous test run.
        """
        self._killVBoxSVCByPidFile('%s/VBoxSVC.pid' % (self.sScratchPath,));
        return base.TestDriver.actionCleanupBefore(self);

    def actionCleanupAfter(self):
        """
        Clean up the VBox bits and then call the base driver.

        If your test driver overrides this, it should normally call us at the
        end of the job.
        """
        cErrorsEntry = reporter.getErrorCount();

        # Kill any left over VM processes.
        self._powerOffAllVms();

        # Drop all VBox object references and shutdown xpcom then
        # terminating VBoxSVC,  with extreme prejudice if need be.
        self._teardownVBoxApi();
        self._stopVBoxSVC();

        # Add the VBoxSVC and testdriver debug+release log files.
        if self.fAlwaysUploadLogs or reporter.getErrorCount() > 0:
            if self.sVBoxSvcLogFile is not None  and  os.path.isfile(self.sVBoxSvcLogFile):
                reporter.addLogFile(self.sVBoxSvcLogFile, 'log/debug/svc', 'Debug log file for VBoxSVC');
                self.sVBoxSvcLogFile = None;

            if self.sSelfLogFile is not None  and  os.path.isfile(self.sSelfLogFile):
                reporter.addLogFile(self.sSelfLogFile, 'log/debug/client', 'Debug log file for the test driver');
                self.sSelfLogFile = None;

            if self.sSessionLogFile is not None  and  os.path.isfile(self.sSessionLogFile):
                reporter.addLogFile(self.sSessionLogFile, 'log/debug/session', 'Debug log file for the VM session');
                self.sSessionLogFile = None;

            sVBoxSvcRelLog = os.path.join(self.sScratchPath, 'VBoxUserHome', 'VBoxSVC.log');
            if os.path.isfile(sVBoxSvcRelLog):
                reporter.addLogFile(sVBoxSvcRelLog, 'log/release/svc', 'Release log file for VBoxSVC');
            for sSuff in [ '.1', '.2', '.3', '.4', '.5', '.6', '.7', '.8' ]:
                if os.path.isfile(sVBoxSvcRelLog + sSuff):
                    reporter.addLogFile(sVBoxSvcRelLog + sSuff, 'log/release/svc', 'Release log file for VBoxSVC');

        # Finally, call the base driver to wipe the scratch space.
        fRc = base.TestDriver.actionCleanupAfter(self);

        # Flag failure if the error count increased.
        if reporter.getErrorCount() > cErrorsEntry:
            fRc = False;
        return fRc;


    def actionAbort(self):
        """
        Terminate VBoxSVC if we've got a pid file.
        """
        #
        # Take default action first, then kill VBoxSVC.  The other way around
        # is problematic since the testscript would continue running and possibly
        # trigger a new VBoxSVC to start.
        #
        fRc1 = base.TestDriver.actionAbort(self);
        fRc2 = self._killVBoxSVCByPidFile('%s/VBoxSVC.pid' % (self.sScratchPath,));
        return fRc1 is True and fRc2 is True;

    def onExit(self, iRc):
        """
        Stop VBoxSVC if we've started it.
        """
        if  not self.fVmNoTerminate \
        and self.oVBoxSvcProcess is not None:
            reporter.log('*** Shutting down the VBox API... (iRc=%s)' % (iRc,));
            self._powerOffAllVms();
            self._teardownVBoxApi();
            self._stopVBoxSVC();
            reporter.log('*** VBox API shutdown done.');
        return base.TestDriver.onExit(self, iRc);


    #
    # Task wait method override.
    #

    def notifyAboutReadyTask(self, oTask):
        """
        Overriding base.TestDriver.notifyAboutReadyTask.
        """
        try:
            self.oVBoxMgr.interruptWaitEvents();
            reporter.log2('vbox.notifyAboutReadyTask: called interruptWaitEvents');
        except:
            reporter.logXcpt('vbox.notifyAboutReadyTask');
        return base.TestDriver.notifyAboutReadyTask(self, oTask);

    def waitForTasksSleepWorker(self, cMsTimeout):
        """
        Overriding base.TestDriver.waitForTasksSleepWorker.
        """
        try:
            rc = self.oVBoxMgr.waitForEvents(int(cMsTimeout));
            _ = rc; #reporter.log2('vbox.waitForTasksSleepWorker(%u): true (waitForEvents -> %s)' % (cMsTimeout, rc));
            reporter.doPollWork('vbox.TestDriver.waitForTasksSleepWorker');
            return True;
        except KeyboardInterrupt:
            raise;
        except:
            reporter.logXcpt('vbox.waitForTasksSleepWorker');
            return False;

    #
    # Utility methods.
    #

    def processEvents(self, cMsTimeout = 0):
        """
        Processes events, returning after the first batch has been processed
        or the time limit has been reached.

        Only Ctrl-C exception, no return.
        """
        try:
            self.oVBoxMgr.waitForEvents(cMsTimeout);
        except KeyboardInterrupt:
            raise;
        except:
            pass;
        return None;

    def processPendingEvents(self):
        """ processEvents(0) - no waiting. """
        return self.processEvents(0);

    def sleep(self, cSecs):
        """
        Sleep for a specified amount of time, processing XPCOM events all the while.
        """
        cMsTimeout = long(cSecs * 1000);
        msStart    = base.timestampMilli();
        self.processEvents(0);
        while True:
            cMsElapsed = base.timestampMilli() - msStart;
            if cMsElapsed > cMsTimeout:
                break;
            #reporter.log2('cMsTimeout=%s - cMsElapsed=%d => %s' % (cMsTimeout, cMsElapsed, cMsTimeout - cMsElapsed));
            self.processEvents(cMsTimeout - cMsElapsed);
        return None;

    def _logVmInfoUnsafe(self, oVM):                                            # pylint: disable=too-many-statements,too-many-branches
        """
        Internal worker for logVmInfo that is wrapped in try/except.
        """
        reporter.log("  Name:               %s" % (oVM.name,));
        reporter.log("  ID:                 %s" % (oVM.id,));
        oOsType = self.oVBox.getGuestOSType(oVM.OSTypeId);
        if self.fpApiVer >= 7.1:
            if oVM.platform.architecture == vboxcon.PlatformArchitecture_ARM:
                sArch = 'ARM';
            else:
                sArch = 'x86';
            reporter.log("  Architecture:       %s" % (sArch,));
        else: # x86 only.
            reporter.log("  Architecture:       x86");
        reporter.log("  OS Type:            %s - %s" % (oVM.OSTypeId, oOsType.description,));
        reporter.log("  Machine state:      %s" % (oVM.state,));
        reporter.log("  Session state:      %s" % (oVM.sessionState,));
        if self.fpApiVer >= 4.2:
            reporter.log("  Session PID:        %u (%#x)" % (oVM.sessionPID, oVM.sessionPID,));
        else:
            reporter.log("  Session PID:        %u (%#x)" % (oVM.sessionPid, oVM.sessionPid,));
        if self.fpApiVer >= 5.0:
            reporter.log("  Session Name:       %s" % (oVM.sessionName,));
        else:
            reporter.log("  Session Name:       %s" % (oVM.sessionType,));
        reporter.log("  CPUs:               %s" % (oVM.CPUCount,));
        reporter.log("  RAM:                %sMB" % (oVM.memorySize,));
        if self.fpApiVer >= 6.1 and hasattr(oVM, 'graphicsAdapter'):
            reporter.log("  VRAM:               %sMB" % (oVM.graphicsAdapter.VRAMSize,));
            reporter.log("  Monitors:           %s" % (oVM.graphicsAdapter.monitorCount,));
            reporter.log("  GraphicsController: %s"
                         % (self.oVBoxMgr.getEnumValueName('GraphicsControllerType',                                              # pylint: disable=not-callable
                                                           oVM.graphicsAdapter.graphicsControllerType),));
        else:
            reporter.log("  VRAM:               %sMB" % (oVM.VRAMSize,));
            reporter.log("  Monitors:           %s" % (oVM.monitorCount,));
            reporter.log("  GraphicsController: %s"
                         % (self.oVBoxMgr.getEnumValueName('GraphicsControllerType', oVM.graphicsControllerType),));              # pylint: disable=not-callable
        if self.fpApiVer >= 7.1:
            reporter.log("  Chipset:            %s" % (self.oVBoxMgr.getEnumValueName('ChipsetType', oVM.platform.chipsetType),));# pylint: disable=not-callable
            if hasattr(vboxcon, 'IommuType_None'):
                reporter.log("  IOMMU:              %s" % (self.oVBoxMgr.getEnumValueName('IommuType', oVM.platform.iommuType),));# pylint: disable=not-callable
            reporter.log("  Firmware:           %s"
                         % (self.oVBoxMgr.getEnumValueName('FirmwareType', oVM.firmwareSettings.firmwareType),));                 # pylint: disable=not-callable
            if oVM.platform.architecture == vboxcon.PlatformArchitecture_x86:
                reporter.log("  HwVirtEx:           %s"
                             % (oVM.platform.x86.getHWVirtExProperty(vboxcon.HWVirtExPropertyType_Enabled),));
                reporter.log("  VPID support:       %s"
                             % (oVM.platform.x86.getHWVirtExProperty(vboxcon.HWVirtExPropertyType_VPID),));
                reporter.log("  Nested paging:      %s"
                             % (oVM.platform.x86.getHWVirtExProperty(vboxcon.HWVirtExPropertyType_NestedPaging),));
        else:
            reporter.log("  Chipset:            %s" % (self.oVBoxMgr.getEnumValueName('ChipsetType', oVM.chipsetType),));         # pylint: disable=not-callable
            if self.fpApiVer >= 6.2 and hasattr(vboxcon, 'IommuType_None'):
                reporter.log("  IOMMU:              %s" % (self.oVBoxMgr.getEnumValueName('IommuType', oVM.iommuType),));         # pylint: disable=not-callable
            reporter.log("  Firmware:           %s" % (self.oVBoxMgr.getEnumValueName('FirmwareType', oVM.firmwareType),));       # pylint: disable=not-callable
            reporter.log("  HwVirtEx:           %s" % (oVM.getHWVirtExProperty(vboxcon.HWVirtExPropertyType_Enabled),));
            reporter.log("  VPID support:       %s" % (oVM.getHWVirtExProperty(vboxcon.HWVirtExPropertyType_VPID),));
            reporter.log("  Nested paging:      %s" % (oVM.getHWVirtExProperty(vboxcon.HWVirtExPropertyType_NestedPaging),));
        atCpuPropertyTypesX86 = [
            ( 'PAE',              'PAE:                '),
            ( 'LongMode',         'Long-mode:          '),
            ( 'HWVirt',           'Nested VT-x/AMD-V:  '),
            ( 'APIC',             'APIC:               '),
            ( 'X2APIC',           'X2APIC:             '),
            ( 'TripleFaultReset', 'TripleFaultReset:   '),
            ( 'IBPBOnVMExit',     'IBPBOnVMExit:       '),
            ( 'SpecCtrl',         'SpecCtrl:           '),
            ( 'SpecCtrlByHost',   'SpecCtrlByHost:     '),
            ## @todo r=andy Add missing properties.
        ];
        fnGetCpuPropertyX86 = None;
        if self.fpApiVer >= 7.1:
            sCpuPropertyTypeNamePrefix = 'CpuPropertyTypeX86_';
            if oVM.platform.architecture == vboxcon.PlatformArchitecture_x86:
                fnGetCpuPropertyX86 = oVM.platform.x86.getCPUProperty;
        else:
            sCpuPropertyTypeNamePrefix = 'CpuPropertyType_';
            fnGetCpuPropertyX86 = oVM.getCPUProperty;
        if fnGetCpuPropertyX86:
            for sEnumValue, sDesc in atCpuPropertyTypesX86:
                if hasattr(vboxcon, sCpuPropertyTypeNamePrefix + sEnumValue):
                    reporter.log("  %s%s" % (sDesc, fnGetCpuPropertyX86(getattr(vboxcon, sEnumValue)),));
        if self.fpApiVer >= 7.1:
            reporter.log("  ACPI:               %s" % (oVM.firmwareSettings.ACPIEnabled,));
            reporter.log("  IO-APIC:            %s" % (oVM.firmwareSettings.IOAPICEnabled,));
            if oVM.platform.architecture == vboxcon.PlatformArchitecture_x86:
                reporter.log("  HPET:               %s" % (oVM.platform.x86.HPETEnabled,));
        else:
            reporter.log("  ACPI:               %s" % (oVM.BIOSSettings.ACPIEnabled,));
            reporter.log("  IO-APIC:            %s" % (oVM.BIOSSettings.IOAPICEnabled,));
            if self.fpApiVer >= 3.2:
                if self.fpApiVer >= 4.2:
                    reporter.log("  HPET:               %s" % (oVM.HPETEnabled,));
                else:
                    reporter.log("  HPET:               %s" % (oVM.hpetEnabled,));
        if self.fpApiVer >= 6.1 and hasattr(oVM, 'graphicsAdapter'):
            if self.fpApiVer >= 7.1 and hasattr(oVM.graphicsAdapter, 'isFeatureEnabled'):
                fAccelerate3DEnabled = \
                    oVM.graphicsAdapter.isFeatureEnabled(vboxcon.GraphicsFeature_Acceleration3D);
                if self.fpApiVer < 7.2: # 2D video acceleration was removed with 7.2.
                    fAccelerate2DVideoEnabled = \
                        oVM.graphicsAdapter.isFeatureEnabled(vboxcon.GraphicsFeature_Acceleration2DVideo);
            else:
                fAccelerate3DEnabled      = oVM.graphicsAdapter.accelerate3DEnabled;
                fAccelerate2DVideoEnabled = oVM.graphicsAdapter.accelerate2DVideoEnabled;
        else:
            fAccelerate3DEnabled      = oVM.accelerate3DEnabled;
            fAccelerate2DVideoEnabled = oVM.accelerate2DVideoEnabled;
        reporter.log("  3D acceleration:    %s" % (fAccelerate3DEnabled,));
        if self.fpApiVer < 7.2:
            reporter.log("  2D acceleration:    %s" % (fAccelerate2DVideoEnabled,));
        reporter.log("  TeleporterEnabled:  %s" % (oVM.teleporterEnabled,));
        reporter.log("  TeleporterPort:     %s" % (oVM.teleporterPort,));
        reporter.log("  TeleporterAddress:  %s" % (oVM.teleporterAddress,));
        reporter.log("  TeleporterPassword: %s" % (oVM.teleporterPassword,));
        reporter.log("  Clipboard mode:     %s" % (oVM.clipboardMode,));
        if self.fpApiVer >= 5.0:
            reporter.log("  Drag and drop mode: %s" % (oVM.dnDMode,));
        elif self.fpApiVer >= 4.3:
            reporter.log("  Drag and drop mode: %s" % (oVM.dragAndDropMode,));
        if self.fpApiVer >= 4.0:
            reporter.log("  VRDP server:        %s" % (oVM.VRDEServer.enabled,));
            try:    sPorts = oVM.VRDEServer.getVRDEProperty("TCP/Ports");
            except: sPorts = "";
            reporter.log("  VRDP server ports:  %s" % (sPorts,));
            reporter.log("  VRDP auth:          %s (%s)" % (oVM.VRDEServer.authType, oVM.VRDEServer.authLibrary,));
        else:
            reporter.log("  VRDP server:        %s" % (oVM.VRDPServer.enabled,));
            reporter.log("  VRDP server ports:  %s" % (oVM.VRDPServer.ports,));
        reporter.log("  Last changed:       %s" % (oVM.lastStateChange,));

        aoControllers = self.oVBoxMgr.getArray(oVM, 'storageControllers')
        if aoControllers:
            reporter.log("  Controllers:");
        for oCtrl in aoControllers:
            reporter.log("    %s %s bus: %s type: %s" % (oCtrl.name, oCtrl.controllerType, oCtrl.bus, oCtrl.controllerType,));
        if self.fpApiVer >= 7.0:
            oAdapter = oVM.audioSettings.adapter;
        else:
            oAdapter = oVM.audioAdapter;
        reporter.log("    AudioController:  %s"
                     % (self.oVBoxMgr.getEnumValueName('AudioControllerType', oAdapter.audioController),));               # pylint: disable=not-callable
        reporter.log("    AudioEnabled:     %s" % (oAdapter.enabled,));
        if self.fpApiVer >= 7.0:
            reporter.log("    AudioEnabled In:  %s" % (oAdapter.enabledIn,));
            reporter.log("    AudioEnabled Out: %s" % (oAdapter.enabledOut,));
        reporter.log("    Host AudioDriver: %s"
                     % (self.oVBoxMgr.getEnumValueName('AudioDriverType', oAdapter.audioDriver),));                       # pylint: disable=not-callable

        self.processPendingEvents();
        aoAttachments = self.oVBoxMgr.getArray(oVM, 'mediumAttachments')
        if aoAttachments:
            reporter.log("  Attachments:");
        for oAtt in aoAttachments:
            sCtrl = "Controller: %s port: %s device: %s type: %s" % (oAtt.controller, oAtt.port, oAtt.device, oAtt.type);
            oMedium = oAtt.medium
            if oAtt.type == vboxcon.DeviceType_HardDisk:
                reporter.log("    %s: HDD" % sCtrl);
                reporter.log("      Id:             %s" % (oMedium.id,));
                reporter.log("      Name:           %s" % (oMedium.name,));
                reporter.log("      Format:         %s" % (oMedium.format,));
                reporter.log("      Location:       %s" % (oMedium.location,));

            if oAtt.type == vboxcon.DeviceType_DVD:
                reporter.log("    %s: DVD" % sCtrl);
                if oMedium:
                    reporter.log("      Id:             %s" % (oMedium.id,));
                    reporter.log("      Name:           %s" % (oMedium.name,));
                    if oMedium.hostDrive:
                        reporter.log("      Host DVD        %s" % (oMedium.location,));
                        if oAtt.passthrough:
                            reporter.log("      [passthrough mode]");
                    else:
                        reporter.log("      Virtual image:  %s" % (oMedium.location,));
                        reporter.log("      Size:           %s" % (oMedium.size,));
                else:
                    reporter.log("      empty");

            if oAtt.type == vboxcon.DeviceType_Floppy:
                reporter.log("    %s: Floppy" % sCtrl);
                if oMedium:
                    reporter.log("      Id:             %s" % (oMedium.id,));
                    reporter.log("      Name:           %s" % (oMedium.name,));
                    if oMedium.hostDrive:
                        reporter.log("      Host floppy:    %s" % (oMedium.location,));
                    else:
                        reporter.log("      Virtual image:  %s" % (oMedium.location,));
                        reporter.log("      Size:           %s" % (oMedium.size,));
                else:
                    reporter.log("      empty");
            self.processPendingEvents();

        reporter.log("  Network Adapter:");
        for iSlot in range(0, 32):
            try:    oNic = oVM.getNetworkAdapter(iSlot)
            except: break;
            if not oNic.enabled:
                reporter.log2("    slot #%d found but not enabled, skipping" % (iSlot,));
                continue;
            reporter.log("    slot #%d: type: %s (%s) MAC Address: %s lineSpeed: %s"
                         % (iSlot, self.oVBoxMgr.getEnumValueName('NetworkAdapterType', oNic.adapterType),                        # pylint: disable=not-callable
                            oNic.adapterType, oNic.MACAddress, oNic.lineSpeed) );

            if   oNic.attachmentType == vboxcon.NetworkAttachmentType_NAT:
                reporter.log("    attachmentType: NAT (%s)" % (oNic.attachmentType,));
                if self.fpApiVer >= 4.1:
                    reporter.log("    nat-network:    %s" % (oNic.NATNetwork,));
                if self.fpApiVer >= 7.0 and hasattr(oNic.NATEngine, 'localhostReachable'):
                    reporter.log("    localhostReachable: %s" % (oNic.NATEngine.localhostReachable,));

            elif oNic.attachmentType == vboxcon.NetworkAttachmentType_Bridged:
                reporter.log("    attachmentType: Bridged (%s)" % (oNic.attachmentType,));
                if self.fpApiVer >= 4.1:
                    reporter.log("    hostInterface:  %s" % (oNic.bridgedInterface,));
                else:
                    reporter.log("    hostInterface:  %s" % (oNic.hostInterface,));
            elif oNic.attachmentType == vboxcon.NetworkAttachmentType_Internal:
                reporter.log("    attachmentType: Internal (%s)" % (oNic.attachmentType,));
                reporter.log("    intnet-name:    %s" % (oNic.internalNetwork,));
            elif oNic.attachmentType == vboxcon.NetworkAttachmentType_HostOnly:
                reporter.log("    attachmentType: HostOnly (%s)" % (oNic.attachmentType,));
                if self.fpApiVer >= 4.1:
                    reporter.log("    hostInterface:  %s" % (oNic.hostOnlyInterface,));
                else:
                    reporter.log("    hostInterface:  %s" % (oNic.hostInterface,));
            else:
                if self.fpApiVer >= 7.0:
                    if oNic.attachmentType == vboxcon.NetworkAttachmentType_HostOnlyNetwork:
                        reporter.log("    attachmentType: HostOnlyNetwork (%s)" % (oNic.attachmentType,));
                        reporter.log("    hostonly-net: %s" % (oNic.hostOnlyNetwork,));
                elif self.fpApiVer >= 4.1:
                    if oNic.attachmentType == vboxcon.NetworkAttachmentType_Generic:
                        reporter.log("    attachmentType: Generic (%s)" % (oNic.attachmentType,));
                        reporter.log("    generic-driver: %s" % (oNic.GenericDriver,));
                    else:
                        reporter.log("    attachmentType: unknown-%s" % (oNic.attachmentType,));
                else:
                    reporter.log("    attachmentType: unknown-%s" % (oNic.attachmentType,));
            if oNic.traceEnabled:
                reporter.log("    traceFile:      %s" % (oNic.traceFile,));
            self.processPendingEvents();

        reporter.log("  Serial ports:");
        for iSlot in range(0, 8):
            try:    oPort = oVM.getSerialPort(iSlot);
            except: break;
            if oPort is not None and oPort.enabled:
                enmHostMode = oPort.hostMode;
                if self.fpApiVer >= 7.1:
                    sIOAddressOrBase = oPort.IOAddress;
                else:
                    sIOAddressOrBase = oPort.IOBase;
                reporter.log("    slot #%d: hostMode: %s (%s)  I/O address: %s  IRQ: %s  server: %s  path: %s" %
                             (iSlot,  self.oVBoxMgr.getEnumValueName('PortMode', enmHostMode),                                    # pylint: disable=not-callable
                              enmHostMode, sIOAddressOrBase, oPort.IRQ, oPort.server, oPort.path,) );
                self.processPendingEvents();

        return True;

    def logVmInfo(self, oVM):                                                   # pylint: disable=too-many-statements,too-many-branches
        """
        Logs VM configuration details.

        This is copy, past, search, replace and edit of infoCmd from vboxshell.py.
        """
        try:
            fRc = self._logVmInfoUnsafe(oVM);
        except:
            reporter.logXcpt();
            fRc = False;
        return fRc;

    def logVmInfoByName(self, sName):
        """
        logVmInfo + getVmByName.
        """
        return self.logVmInfo(self.getVmByName(sName));

    def tryFindGuestOsId(self, sIdOrDesc):
        """
        Takes a guest OS ID or Description and returns the ID.
        If nothing matching it is found, the input is returned unmodified.
        """

        if self.fpApiVer >= 4.0:
            if sIdOrDesc == 'Solaris (64 bit)':
                sIdOrDesc = 'Oracle Solaris 10 5/09 and earlier (64 bit)';

        try:
            aoGuestTypes = self.oVBoxMgr.getArray(self.oVBox, 'GuestOSTypes');
        except:
            reporter.logXcpt();
        else:
            for oGuestOS in aoGuestTypes:
                try:
                    sId   = oGuestOS.id;
                    sDesc = oGuestOS.description;
                except:
                    reporter.logXcpt();
                else:
                    if sIdOrDesc in (sId, sDesc,):
                        sIdOrDesc = sId;
                        break;
        self.processPendingEvents();
        return sIdOrDesc

    def resourceFindVmHd(self, sVmName, sFlavor):
        """
        Search the test resources for the most recent VM HD.

        Returns path relative to the test resource root.
        """
        ## @todo implement a proper search algo here.
        return '4.2/' + sFlavor + '/' + sVmName + '/t-' + sVmName + '.vdi';


    #
    # VM Api wrappers that logs errors, hides exceptions and other details.
    #

    def createTestVMOnly(self, sName, sKind, sPlatformArchitecture = 'x86'):
        """
        Creates and registers a test VM without doing any kind of configuration.
        Uses x86 as a platform architecture by default (only >= 7.1).

        Returns VM object (IMachine) on success, None on failure.
        """
        if not self.importVBoxApi():
            return None;

        # Default to x86 if not explicitly specified.
        if self.fpApiVer >= 7.1:
            if not sPlatformArchitecture \
            or     sPlatformArchitecture == 'x86':
                enmPlatformArchitecture = vboxcon.PlatformArchitecture_x86;
            elif sPlatformArchitecture == 'ARM':
                enmPlatformArchitecture = vboxcon.PlatformArchitecture_ARM;
            else:
                reporter.error('Unkown platform architecture "%s"' % (sPlatformArchitecture,));
                return None;
        elif sPlatformArchitecture != 'x86':  # < 7.1 only has x86 support.
            reporter.errorXcpt('This host version of VirtualBox only supports x86 as platform architecture');
            return None;

        # create + register the VM
        try:
            if self.fpApiVer >= 7.1: # Introduces platform support (x86 + ARM).
                oVM = self.oVBox.createMachine("", sName, enmPlatformArchitecture, [], self.tryFindGuestOsId(sKind), \
                                               "", "", "", "");
            elif self.fpApiVer >= 7.0: # Introduces VM encryption (three new parameters, empty for now).
                oVM = self.oVBox.createMachine("", sName, [], self.tryFindGuestOsId(sKind), "", "", "", "");
            elif self.fpApiVer >= 4.2: # Introduces grouping (third parameter, empty for now).
                oVM = self.oVBox.createMachine("", sName, [], self.tryFindGuestOsId(sKind), "");
            elif self.fpApiVer >= 4.0:
                oVM = self.oVBox.createMachine("", sName, self.tryFindGuestOsId(sKind), "", False);
            elif self.fpApiVer >= 3.2:
                oVM = self.oVBox.createMachine(sName, self.tryFindGuestOsId(sKind), "", "", False);
            else:
                oVM = self.oVBox.createMachine(sName, self.tryFindGuestOsId(sKind), "", "");
            try:
                oVM.saveSettings();
                try:
                    self.oVBox.registerMachine(oVM);
                    return oVM;
                except:
                    reporter.logXcpt();
                    raise;
            except:
                reporter.logXcpt();
                if self.fpApiVer >= 4.0:
                    try:
                        if self.fpApiVer >= 4.3:
                            oProgress = oVM.deleteConfig([]);
                        else:
                            oProgress = oVM.delete(None);
                        self.waitOnProgress(oProgress);
                    except:
                        reporter.logXcpt();
                else:
                    try:    oVM.deleteSettings();
                    except: reporter.logXcpt();
                raise;
        except:
            reporter.errorXcpt('failed to create vm "%s"' % (sName));
        return None;

    # pylint: disable=too-many-arguments,too-many-locals,too-many-statements,too-many-branches
    def createTestVM(self,
                     sName,
                     iGroup,
                     sHd = None,
                     cMbRam = None,
                     cCpus = 1,
                     fVirtEx = None,
                     fNestedPaging = None,
                     sDvdImage = None,
                     sKind = "Other",
                     fIoApic = None,
                     fNstHwVirt = None,
                     fPae = None,
                     fFastBootLogo = True,
                     eNic0Type = None,
                     eNic0AttachType = None,
                     sNic0NetName = 'default',
                     sNic0MacAddr = 'grouped',
                     sFloppy = None,
                     fNatForwardingForTxs = None,
                     sHddControllerType = 'IDE Controller',
                     fVmmDevTestingPart = None,
                     fVmmDevTestingMmio = False,
                     sFirmwareType = 'bios',
                     sChipsetType = 'piix3',
                     sIommuType = 'none',
                     sDvdControllerType = 'IDE Controller',
                     sCom1RawFile = None,
                     fSecureBoot = False,
                     sUefiMokPathPrefix = None,
                     sGraphicsControllerType = None,
                     sPlatformArchitecture = 'x86'):
        """
        Creates a test VM with a immutable HD from the test resources.
        """
        # create + register the VM
        oVM = self.createTestVMOnly(sName, sKind, sPlatformArchitecture);
        if not oVM:
            return None;

        # Configure the VM.
        fRc = True;
        oSession = self.openSession(oVM);
        if oSession is not None:
            fRc = oSession.setupPreferredConfig();

            if fRc and cMbRam is not None :
                fRc = oSession.setRamSize(cMbRam);
            if fRc and cCpus is not None:
                fRc = oSession.setCpuCount(cCpus);
            if fRc and sDvdImage is not None:
                fRc = oSession.attachDvd(sDvdImage, sDvdControllerType);
            if fRc and sHd is not None:
                fRc = oSession.attachHd(sHd, sHddControllerType);
            if fRc and sFloppy is not None:
                fRc = oSession.attachFloppy(sFloppy);
            if fRc and eNic0Type is not None:
                fRc = oSession.setNicType(eNic0Type, 0);
            if fRc and (eNic0AttachType is not None  or  (sNic0NetName is not None and sNic0NetName != 'default')):
                fRc = oSession.setNicAttachment(eNic0AttachType, sNic0NetName, 0);
            if fRc and sNic0MacAddr is not None:
                if sNic0MacAddr == 'grouped':
                    sNic0MacAddr = '%02X' % (iGroup);
                fRc = oSession.setNicMacAddress(sNic0MacAddr, 0);
            # Needed to reach the host (localhost) from the guest. See xTracker #9896.
            if fRc and self.fpApiVer >= 7.0:
                fRc = oSession.setNicLocalhostReachable(True, 0);
            if fRc and fNatForwardingForTxs is True:
                fRc = oSession.setupNatForwardingForTxs();
            if fRc and fFastBootLogo is not None:
                fRc = oSession.setupBootLogo(fFastBootLogo);
            if fRc and self.fEnableVrdp:
                fRc = oSession.setupVrdp(True, self.uVrdpBasePort + iGroup);
            if fRc and fVmmDevTestingPart is not None:
                fRc = oSession.enableVmmDevTestingPart(fVmmDevTestingPart, fVmmDevTestingMmio);
            if fRc and sFirmwareType == 'bios':
                fRc = oSession.setFirmwareType(vboxcon.FirmwareType_BIOS);
            elif fRc and sFirmwareType == 'efi':
                fRc = oSession.setFirmwareType(vboxcon.FirmwareType_EFI);
                if fRc and self.fpApiVer >= 7.0 and fSecureBoot:
                    fRc = oSession.enableSecureBoot(fSecureBoot, sUefiMokPathPrefix);
            if fRc and self.fEnableDebugger:
                fRc = oSession.setExtraData('VBoxInternal/DBGC/Enabled', '1');
            if fRc and self.fRecordingEnabled:
                try:
                    if self.fpApiVer >= 6.1: # Only for VBox 6.1 and up now.
                        reporter.log('Recording enabled');
                        if self.cSecsRecordingMax > 0:
                            reporter.log('Recording time limit is set to %d seconds' % (self.cSecsRecordingMax));
                        if self.cMbRecordingMax > 0:
                            reporter.log('Recording file limit is set to %d MB' % (self.cMbRecordingMax));
                        oRecSettings = oSession.o.machine.recordingSettings;
                        oRecSettings.enabled     = True; # For VBox < 7.1 this also starts recording.
                        if self.fpApiVer >= 7.1:
                            # For VBox >= 7.1 we have to explicitly start the recording.
                            oRecSettings.start();
                        aoScreens = self.oVBoxMgr.getArray(oRecSettings, 'screens');
                        for oScreen in aoScreens:
                            try:
                                oScreen.enabled  = True;
                                sRecFile = os.path.join(self.sScratchPath, "recording-%s.webm" % (sName));
                                oScreen.filename = sRecFile;
                                sRecFile = oScreen.filename; # Get back the file from Main, in case it was modified somehow.
                                dRecFile = { 'id' : oScreen.id, 'file' : sRecFile };
                                self.adRecordingFiles.append(dRecFile);
                                if self.fpApiVer >= 7.0:
                                    aFeatures = [ vboxcon.RecordingFeature_Video, ];
                                    if self.fRecordingAudio:
                                        aFeatures.append(vboxcon.RecordingFeature_Audio);
                                    try:
                                        oScreen.setFeatures(aFeatures);
                                    except: ## @todo Figure out why this is needed on Windows.
                                        oScreen.features = aFeatures;
                                else: # <= VBox 6.1 the feature were kept as a ULONG.
                                    uFeatures = vboxcon.RecordingFeature_Video;
                                    if self.fRecordingAudio:
                                        uFeatures = uFeatures | vboxcon.RecordingFeature_Audio;
                                    oScreen.features = uFeatures;
                                reporter.log2('Recording screen %d to "%s"' % (dRecFile['id'], dRecFile['file'],));
                                oScreen.maxTime     = self.cSecsRecordingMax;
                                oScreen.maxFileSize = self.cMbRecordingMax;
                            except:
                                reporter.errorXcpt('failed to configure recording for "%s" (screen %d)' % (sName, oScreen.id));
                    else:
                        # Not fatal.
                        reporter.log('Recording only available for VBox >= 6.1, sorry!')
                except:
                    reporter.errorXcpt('failed to configure recording for "%s"' % (sName));
            if fRc:
                if sChipsetType == 'piix3':
                    fRc = oSession.setChipsetType(vboxcon.ChipsetType_PIIX3);
                elif sChipsetType == 'ich9':
                    fRc = oSession.setChipsetType(vboxcon.ChipsetType_ICH9);
                elif    self.fpApiVer >= 7.1 \
                    and sChipsetType == 'armv8virtual': # ARM only is for VBox >= 7.1.
                    fRc = oSession.setChipsetType(vboxcon.ChipsetType_ARMv8Virtual);
                else: # This is fatal.
                    reporter.log('Unknown chipset type "%s" specified' % (sChipsetType,));
            if fRc and sCom1RawFile:
                fRc = oSession.setupSerialToRawFile(0, sCom1RawFile);
            if fRc and self.fpApiVer >= 6.2 and hasattr(vboxcon, 'IommuType_AMD') and sIommuType == 'amd':
                fRc = oSession.setIommuType(vboxcon.IommuType_AMD);
            elif fRc and self.fpApiVer >= 6.2 and hasattr(vboxcon, 'IommuType_Intel') and sIommuType == 'intel':
                fRc = oSession.setIommuType(vboxcon.IommuType_Intel);
            if fRc and sGraphicsControllerType is not None:
                if sGraphicsControllerType == 'VBoxSVGA':
                    fRc = oSession.setVideoControllerType(vboxcon.GraphicsControllerType_VBoxSVGA);
                elif sGraphicsControllerType == 'VMSVGA':
                    fRc = oSession.setVideoControllerType(vboxcon.GraphicsControllerType_VMSVGA);
                elif sGraphicsControllerType == 'VBoxVGA':
                    fRc = oSession.setVideoControllerType(vboxcon.GraphicsControllerType_VBoxVGA);
                elif sGraphicsControllerType == 'QemuRamFb':
                    fRc = oSession.setVideoControllerType(vboxcon.GraphicsControllerType_QemuRamFB);

            #
            # x86-specifics.
            #
            if sPlatformArchitecture == 'x86':
                if fRc and fVirtEx is not None:
                    fRc = oSession.enableVirtExX86(fVirtEx);
                if fRc and fNestedPaging is not None:
                    fRc = oSession.enableNestedPagingX86(fNestedPaging);
                if fRc and fIoApic is not None:
                    fRc = oSession.enableIoApic(fIoApic);
                if fRc and fNstHwVirt is not None:
                    fRc = oSession.enableNestedHwVirtX86(fNstHwVirt);
                if fRc and fPae is not None:
                    fRc = oSession.enablePaeX86(fPae);

            #
            # ARM-specifics.
            #
            elif sPlatformArchitecture == 'ARM':
                pass; ## @todo BUGBUG Add stuff for ARM here.

            if fRc: fRc = oSession.saveSettings();
            if not fRc:   oSession.discardSettings(True);
            oSession.close();
        if not fRc:
            if self.fpApiVer >= 4.0:
                try:    oVM.unregister(vboxcon.CleanupMode_Full);
                except: reporter.logXcpt();
                try:
                    if self.fpApiVer >= 4.3:
                        oProgress = oVM.deleteConfig([]);
                    else:
                        oProgress = oVM.delete([]);
                    self.waitOnProgress(oProgress);
                except:
                    reporter.logXcpt();
            else:
                try:    self.oVBox.unregisterMachine(oVM.id);
                except: reporter.logXcpt();
                try:    oVM.deleteSettings();
                except: reporter.logXcpt();
            return None;

        # success.
        reporter.log('created "%s" with name "%s"' % (oVM.id, sName));
        self.aoVMs.append(oVM);
        self.logVmInfo(oVM); # testing...
        return oVM;
    # pylint: enable=too-many-arguments,too-many-locals,too-many-statements

    def createTestVmWithDefaults(self,                                      # pylint: disable=too-many-arguments
                                 sName,
                                 iGroup,
                                 sKind,
                                 sPlatformArchitecture = 'x86',
                                 sDvdImage = None,
                                 fFastBootLogo = True,
                                 eNic0AttachType = None,
                                 sNic0NetName = 'default',
                                 sNic0MacAddr = 'grouped',
                                 fVmmDevTestingPart = None,
                                 fVmmDevTestingMmio = False,
                                 sCom1RawFile = None):
        """
        Creates a test VM with all defaults and no HDs.

        Defaults to the x86 platform architecture if not explicitly specified otherwise.
        """
        # create + register the VM
        oVM = self.createTestVMOnly(sName, sKind, sPlatformArchitecture);
        if oVM is not None:
            # Configure the VM with defaults according to sKind.
            fRc = True;
            oSession = self.openSession(oVM);
            if oSession is not None:
                if self.fpApiVer >= 6.0:
                    try:
                        oSession.o.machine.applyDefaults('');
                    except:
                        reporter.errorXcpt('failed to apply defaults to vm "%s"' % (sName,));
                        fRc = False;
                else:
                    reporter.error("Implement applyDefaults for vbox version %s" % (self.fpApiVer,));
                    #fRc = oSession.setupPreferredConfig();
                    fRc = False;

                # Apply the specified configuration:
                if fRc and sDvdImage is not None:
                    #fRc = oSession.insertDvd(sDvdImage); # attachDvd
                    reporter.error('Implement: oSession.insertDvd(%s)' % (sDvdImage,));
                    fRc = False;

                if fRc and fFastBootLogo is not None:
                    fRc = oSession.setupBootLogo(fFastBootLogo);

                if fRc and (eNic0AttachType is not None  or  (sNic0NetName is not None and sNic0NetName != 'default')):
                    fRc = oSession.setNicAttachment(eNic0AttachType, sNic0NetName, 0);
                if fRc and sNic0MacAddr is not None:
                    if sNic0MacAddr == 'grouped':
                        sNic0MacAddr = '%02X' % (iGroup,);
                    fRc = oSession.setNicMacAddress(sNic0MacAddr, 0);
                # Needed to reach the host (localhost) from the guest. See xTracker #9896.
                if fRc and self.fpApiVer >= 7.0:
                    fRc = oSession.setNicLocalhostReachable(True, 0);

                if fRc and self.fEnableVrdp:
                    fRc = oSession.setupVrdp(True, self.uVrdpBasePort + iGroup);

                if fRc and fVmmDevTestingPart is not None:
                    fRc = oSession.enableVmmDevTestingPart(fVmmDevTestingPart, fVmmDevTestingMmio);

                if fRc and sCom1RawFile:
                    fRc = oSession.setupSerialToRawFile(0, sCom1RawFile);

                # Save the settings if we were successfull, otherwise discard them.
                if fRc:
                    fRc = oSession.saveSettings();
                if not fRc:
                    oSession.discardSettings(True);
                oSession.close();

            if fRc is True:
                # If we've been successful, add the VM to the list and return it.
                # success.
                reporter.log('created "%s" with name "%s"' % (oVM.id, sName, ));
                self.aoVMs.append(oVM);
                self.logVmInfo(oVM); # testing...
                return oVM;

            # Failed. Unregister the machine and delete it.
            if self.fpApiVer >= 4.0:
                try:    oVM.unregister(vboxcon.CleanupMode_Full);
                except: reporter.logXcpt();
                try:
                    if self.fpApiVer >= 4.3:
                        oProgress = oVM.deleteConfig([]);
                    else:
                        oProgress = oVM.delete([]);
                    self.waitOnProgress(oProgress);
                except:
                    reporter.logXcpt();
            else:
                try:    self.oVBox.unregisterMachine(oVM.id);
                except: reporter.logXcpt();
                try:    oVM.deleteSettings();
                except: reporter.logXcpt();
        return None;

    def addTestMachine(self, sNameOrId, fQuiet = False):
        """
        Adds an already existing (that is, configured) test VM to the
        test VM list.

        Returns the VM object on success, None if failed.
        """
        # find + add the VM to the list.
        oVM = None;
        try:
            if self.fpApiVer >= 4.0:
                oVM = self.oVBox.findMachine(sNameOrId);
            else:
                reporter.error('fpApiVer=%s - did you remember to initialize the API' % (self.fpApiVer,));
        except:
            reporter.errorXcpt('could not find vm "%s"' % (sNameOrId,));

        if oVM:
            self.aoVMs.append(oVM);
            if not fQuiet:
                reporter.log('Added "%s" with name "%s"' % (oVM.id, sNameOrId));
                self.logVmInfo(oVM);
        return oVM;

    def forgetTestMachine(self, oVM, fQuiet = False):
        """
        Forget about an already known test VM in the test VM list.

        Returns True on success, False if failed.
        """
        try:
            sUuid = oVM.id;
            sName = oVM.name;
        except:
            reporter.errorXcpt('failed to get the UUID for VM "%s"' % (oVM,));
            return False;
        try:
            self.aoVMs.remove(oVM);
            if not fQuiet:
                reporter.log('Removed "%s" with name "%s"' % (sUuid, sName));
        except:
            reporter.errorXcpt('could not find vm "%s"' % (sName,));
            return False;
        return True;

    def openSession(self, oVM):
        """
        Opens a session for the VM.  Returns the a Session wrapper object that
        will automatically close the session when the wrapper goes out of scope.

        On failure None is returned and an error is logged.
        """
        try:
            sUuid = oVM.id;
        except:
            reporter.errorXcpt('failed to get the UUID for VM "%s"' % (oVM,));
            return None;

        # This loop is a kludge to deal with us racing the closing of the
        # direct session of a previous VM run. See waitOnDirectSessionClose.
        for i in range(10):
            try:
                if self.fpApiVer <= 3.2:
                    oSession = self.oVBoxMgr.openMachineSession(sUuid);
                else:
                    oSession = self.oVBoxMgr.openMachineSession(oVM);
                break;
            except:
                if i == 9:
                    reporter.errorXcpt('failed to open session for "%s" ("%s")' % (sUuid, oVM));
                    return None;
                if i > 0:
                    reporter.logXcpt('warning: failed to open session for "%s" ("%s") - retrying in %u secs' % (sUuid, oVM, i));
            self.waitOnDirectSessionClose(oVM, 5000 + i * 1000);
        from testdriver.vboxwrappers import SessionWrapper;
        return SessionWrapper(oSession, oVM, self.oVBox, self.oVBoxMgr, self, False);

    #
    # Guest locations.
    #

    @staticmethod
    def getGuestTempDir(oTestVm):
        """
        Helper for finding a temporary directory in the test VM.

        Note! It may be necessary to create it!
        """
        if oTestVm.isWindows():
            return "C:\\Temp";
        if oTestVm.isOS2():
            return "C:\\Temp";
        return '/var/tmp';

    @staticmethod
    def getGuestSystemDir(oTestVm, sPathPrefix = ''):
        """
        Helper for finding a system directory in the test VM that we can play around with.
        sPathPrefix can be used to specify other directories, such as /usr/local/bin/ or /usr/bin, for instance.

        On Windows this is always the System32 directory, so this function can be used as
        basis for locating other files in or under that directory.
        """
        if oTestVm.isWindows():
            return oTestVm.pathJoin(TestDriver.getGuestWinDir(oTestVm), 'System32');
        if oTestVm.isOS2():
            return 'C:\\OS2\\DLL';

        # OL / RHEL symlinks "/bin"/ to "/usr/bin". To avoid (unexpectedly) following symlinks, use "/usr/bin" then instead.
        if  not sPathPrefix \
        and oTestVm.sKind in ('Oracle_64', 'Oracle'): ## @todo Does this apply for "RedHat" as well?
            return "/usr/bin";

        return sPathPrefix + "/bin";

    @staticmethod
    def getGuestSystemAdminDir(oTestVm, sPathPrefix = ''):
        """
        Helper for finding a system admin directory ("sbin") in the test VM that we can play around with.
        sPathPrefix can be used to specify other directories, such as /usr/local/sbin/ or /usr/sbin, for instance.

        On Windows this is always the System32 directory, so this function can be used as
        basis for locating other files in or under that directory.
        On UNIX-y systems this always is the "sh" shell to guarantee a common shell syntax.
        """
        if oTestVm.isWindows():
            return oTestVm.pathJoin(TestDriver.getGuestWinDir(oTestVm), 'System32');
        if oTestVm.isOS2():
            return 'C:\\OS2\\DLL'; ## @todo r=andy Not sure here.

        # OL / RHEL symlinks "/sbin"/ to "/usr/sbin". To avoid (unexpectedly) following symlinks, use "/usr/sbin" then instead.
        if  not sPathPrefix \
        and oTestVm.sKind in ('Oracle_64', 'Oracle'): ## @todo Does this apply for "RedHat" as well?
            return "/usr/sbin";

        return sPathPrefix + "/sbin";

    @staticmethod
    def getGuestWinDir(oTestVm):
        """
        Helper for finding the Windows directory in the test VM that we can play around with.
        ASSUMES that we always install Windows on drive C.

        Returns the Windows directory, or an empty string when executed on a non-Windows guest (asserts).
        """
        sWinDir = '';
        if oTestVm.isWindows():
            if oTestVm.sKind in ['WindowsNT4', 'WindowsNT3x', 'Windows2000',]:
                sWinDir = 'C:\\WinNT\\';
            else:
                sWinDir = 'C:\\Windows\\';
        assert sWinDir != '', 'Retrieving Windows directory for non-Windows OS';
        return sWinDir;

    @staticmethod
    def getGuestSystemShell(oTestVm):
        """
        Helper for finding the default system shell in the test VM.
        """
        if oTestVm.isWindows():
            return TestDriver.getGuestSystemDir(oTestVm) + '\\cmd.exe';
        if oTestVm.isOS2():
            return TestDriver.getGuestSystemDir(oTestVm) + '\\..\\CMD.EXE';
        return "/bin/sh";

    @staticmethod
    def getGuestSystemFileForReading(oTestVm):
        """
        Helper for finding a file in the test VM that we can read.
        """
        if oTestVm.isWindows():
            return TestDriver.getGuestSystemDir(oTestVm) + '\\ntdll.dll';
        if oTestVm.isOS2():
            return TestDriver.getGuestSystemDir(oTestVm) + '\\DOSCALL1.DLL';
        return "/bin/sh";

    def getVmByName(self, sName):
        """
        Get a test VM by name.  Returns None if not found, logged.
        """
        # Look it up in our 'cache'.
        for oVM in self.aoVMs:
            try:
                #reporter.log2('cur: %s / %s (oVM=%s)' % (oVM.name, oVM.id, oVM));
                if oVM.name == sName:
                    return oVM;
            except:
                reporter.errorXcpt('failed to get the name from the VM "%s"' % (oVM));

        # Look it up the standard way.
        return self.addTestMachine(sName, fQuiet = True);

    def getVmByUuid(self, sUuid):
        """
        Get a test VM by uuid.  Returns None if not found, logged.
        """
        # Look it up in our 'cache'.
        for oVM in self.aoVMs:
            try:
                if oVM.id == sUuid:
                    return oVM;
            except:
                reporter.errorXcpt('failed to get the UUID from the VM "%s"' % (oVM));

        # Look it up the standard way.
        return self.addTestMachine(sUuid, fQuiet = True);

    def waitOnProgress(self, oProgress, cMsTimeout = 1000000, fErrorOnTimeout = True, cMsInterval = 1000):
        """
        Waits for a progress object to complete.  Returns the status code.
        """
        # Wait for progress no longer than cMsTimeout time period.
        tsStart = datetime.datetime.now()
        while True:
            self.processPendingEvents();
            try:
                if oProgress.completed:
                    break;
            except:
                return -1;
            self.processPendingEvents();

            tsNow = datetime.datetime.now()
            tsDelta = tsNow - tsStart
            if ((tsDelta.microseconds + tsDelta.seconds * 1000000) // 1000) > cMsTimeout:
                if fErrorOnTimeout:
                    reporter.errorTimeout('Timeout while waiting for progress.')
                return -1

            reporter.doPollWork('vbox.TestDriver.waitOnProgress');
            try:    oProgress.waitForCompletion(cMsInterval);
            except: return -2;

        try:    rc = oProgress.resultCode;
        except: rc = -2;
        self.processPendingEvents();
        return rc;

    def waitOnDirectSessionClose(self, oVM, cMsTimeout):
        """
        Waits for the VM process to close it's current direct session.

        Returns None.
        """
        # Get the original values so we're not subject to
        try:
            eCurState =             oVM.sessionState;
            if self.fpApiVer >= 5.0:
                sCurName  = sOrgName  = oVM.sessionName;
            else:
                sCurName  = sOrgName  = oVM.sessionType;
            if self.fpApiVer >= 4.2:
                iCurPid   = iOrgPid   = oVM.sessionPID;
            else:
                iCurPid   = iOrgPid   = oVM.sessionPid;
        except Exception as oXcpt:
            if ComError.notEqual(oXcpt, ComError.E_ACCESSDENIED):
                reporter.logXcpt();
            self.processPendingEvents();
            return None;
        self.processPendingEvents();

        msStart = base.timestampMilli();
        while iCurPid  == iOrgPid \
          and sCurName == sOrgName \
          and sCurName != '' \
          and base.timestampMilli() - msStart < cMsTimeout \
          and eCurState in (vboxcon.SessionState_Unlocking, vboxcon.SessionState_Spawning, vboxcon.SessionState_Locked,):
            self.processEvents(1000);
            try:
                eCurState = oVM.sessionState;
                sCurName  = oVM.sessionName if self.fpApiVer >= 5.0 else oVM.sessionType;
                iCurPid   = oVM.sessionPID if self.fpApiVer >= 4.2 else oVM.sessionPid;
            except Exception as oXcpt:
                if ComError.notEqual(oXcpt, ComError.E_ACCESSDENIED):
                    reporter.logXcpt();
                break;
            self.processPendingEvents();
        self.processPendingEvents();
        return None;

    def uploadStartupLogFile(self, oVM, sVmName):
        """
        Uploads the VBoxStartup.log when present.
        """
        fRc = True;
        try:
            sLogFile = os.path.join(oVM.logFolder, 'VBoxHardening.log');
        except:
            reporter.logXcpt();
            fRc = False;
        else:
            if os.path.isfile(sLogFile):
                reporter.addLogFile(sLogFile, 'log/release/vm', '%s hardening log' % (sVmName, ),
                                    sAltName = '%s-%s' % (sVmName, os.path.basename(sLogFile),));
        return fRc;

    def annotateAndUploadProcessReport(self, sProcessReport, sFilename, sKind, sDesc):
        """
        Annotates the given VM process report and uploads it if successfull.
        """
        fRc = False;
        if self.oBuild is not None and self.oBuild.sInstallPath is not None:
            oResolver = btresolver.BacktraceResolver(self.sScratchPath, self.oBuild.sInstallPath,
                                                     self.getBuildOs(), self.getBuildArch(),
                                                     fnLog = reporter.log);
            fRcTmp = oResolver.prepareEnv();
            if fRcTmp:
                reporter.log('Successfully prepared environment');
                sReportDbgSym = oResolver.annotateReport(sProcessReport);
                if sReportDbgSym and len(sReportDbgSym) > 8:
                    reporter.addLogString(sReportDbgSym, sFilename, sKind, sDesc);
                    fRc = True;
                else:
                    reporter.log('Annotating report failed');
                oResolver.cleanupEnv();
        return fRc;

    def startVmEx(self, oVM, fWait = True, sType = None, sName = None, asEnv = None): # pylint: disable=too-many-locals,too-many-statements
        """
        Start the VM, returning the VM session and progress object on success.
        The session is also added to the task list and to the aoRemoteSessions set.

        asEnv is a list of string on the putenv() form.

        On failure (None, None) is returned and an error is logged.
        """
        # Massage and check the input.
        if sType is None:
            sType = self.sSessionType;
        if sName is None:
            try:    sName = oVM.name;
            except: sName = 'bad-vm-handle';
        reporter.log('startVmEx: sName=%s fWait=%s sType=%s' % (sName, fWait, sType));
        if oVM is None:
            return (None, None);

        ## @todo Do this elsewhere.
        # Hack alert. Disables all annoying GUI popups.
        if sType == 'gui' and not self.aoRemoteSessions:
            try:
                self.oVBox.setExtraData('GUI/Input/AutoCapture', 'false');
                if self.fpApiVer >= 3.2:
                    self.oVBox.setExtraData('GUI/LicenseAgreed', '8');
                else:
                    self.oVBox.setExtraData('GUI/LicenseAgreed', '7');
                self.oVBox.setExtraData('GUI/RegistrationData',  'triesLeft=0');
                self.oVBox.setExtraData('GUI/SUNOnlineData',     'triesLeft=0');
                self.oVBox.setExtraData('GUI/SuppressMessages',  'confirmVMReset,remindAboutMouseIntegrationOn,'
                                        'remindAboutMouseIntegrationOff,remindAboutPausedVMInput,confirmInputCapture,'
                                        'confirmGoingFullscreen,remindAboutInaccessibleMedia,remindAboutWrongColorDepth,'
                                        'confirmRemoveMedium,allPopupPanes,allMessageBoxes,all');
                self.oVBox.setExtraData('GUI/UpdateDate',        'never');
                self.oVBox.setExtraData('GUI/PreventBetaWarning', self.oVBox.version);
            except:
                reporter.logXcpt();

        # The UUID for the name.
        try:
            sUuid = oVM.id;
        except:
            reporter.errorXcpt('failed to get the UUID for VM "%s"' % (oVM));
            return (None, None);
        self.processPendingEvents();

        # Construct the environment.
        self.sSessionLogFile = '%s/VM-%s.log' % (self.sScratchPath, sUuid);
        try:    os.remove(self.sSessionLogFile);
        except: pass;
        if self.sLogSessionDest:
            sLogDest = self.sLogSessionDest;
        else:
            sLogDest = 'file=%s' % (self.sSessionLogFile,);
        asEnvFinal = [
            'VBOX_LOG=%s' % (self.sLogSessionGroups,),
            'VBOX_LOG_FLAGS=%s' % (self.sLogSessionFlags,),
            'VBOX_LOG_DEST=nodeny %s' % (sLogDest,),
            'VBOX_RELEASE_LOG_FLAGS=append time',
        ];
        if sType == 'gui':
            asEnvFinal.append('VBOX_GUI_DBG_ENABLED=1');
        if asEnv is not None and asEnv:
            asEnvFinal += asEnv;

        reporter.log2('Session environment:\n%s' % (asEnvFinal,));

        # Shortcuts for local testing.
        oProgress = oWrapped = None;
        oTestVM = self.oTestVmSet.findTestVmByName(sName) if self.oTestVmSet is not None else None;
        try:
            if    oTestVM is not None \
              and oTestVM.fSnapshotRestoreCurrent is True:
                if oVM.state is vboxcon.MachineState_Running:
                    reporter.log2('Machine "%s" already running.' % (sName,));
                    oProgress = None;
                    oWrapped  = self.openSession(oVM);
                else:
                    reporter.log2('Checking if snapshot for machine "%s" exists.' % (sName,));
                    oSessionWrapperRestore = self.openSession(oVM);
                    if oSessionWrapperRestore is not None:
                        oSnapshotCur = oVM.currentSnapshot;
                        if oSnapshotCur is not None:
                            reporter.log2('Restoring snapshot for machine "%s".' % (sName,));
                            oSessionWrapperRestore.restoreSnapshot(oSnapshotCur);
                            reporter.log2('Current snapshot for machine "%s" restored.' % (sName,));
                        else:
                            reporter.log('warning: no current snapshot for machine "%s" found.' % (sName,));
                    oSessionWrapperRestore.close();
        except:
            reporter.errorXcpt();
            return (None, None);

        oSession = None; # Must be initialized, otherwise the log statement at the end of the function can fail.

        # Open a remote session, wait for this operation to complete.
        # (The loop is a kludge to deal with us racing the closing of the
        # direct session of a previous VM run. See waitOnDirectSessionClose.)
        if oWrapped is None:
            for i in range(10):
                try:
                    if   self.fpApiVer < 4.3 \
                      or (self.fpApiVer == 4.3 and not hasattr(self.oVBoxMgr, 'getSessionObject')):
                        oSession = self.oVBoxMgr.mgr.getSessionObject(self.oVBox);  # pylint: disable=no-member
                    elif self.fpApiVer < 5.2 \
                      or (self.fpApiVer == 5.2 and hasattr(self.oVBoxMgr, 'vbox')):
                        oSession = self.oVBoxMgr.getSessionObject(self.oVBox);      # pylint: disable=no-member
                    else:
                        oSession = self.oVBoxMgr.getSessionObject();           # pylint: disable=no-member,no-value-for-parameter
                    if self.fpApiVer < 3.3:
                        oProgress = self.oVBox.openRemoteSession(oSession, sUuid, sType, '\n'.join(asEnvFinal));
                    else:
                        if self.uApiRevision >= self.makeApiRevision(6, 1, 0, 1):
                            oProgress = oVM.launchVMProcess(oSession, sType, asEnvFinal);
                        else:
                            oProgress = oVM.launchVMProcess(oSession, sType, '\n'.join(asEnvFinal));
                    break;
                except:
                    if i == 9:
                        reporter.errorXcpt('failed to start VM "%s" ("%s"), aborting.' % (sUuid, sName));
                        return (None, None);
                    oSession = None;
                    if i >= 0:
                        reporter.logXcpt('warning: failed to start VM "%s" ("%s") - retrying in %u secs.' % (sUuid, oVM, i));     # pylint: disable=line-too-long
                self.waitOnDirectSessionClose(oVM, 5000 + i * 1000);
        if fWait and oProgress is not None:
            rc = self.waitOnProgress(oProgress);
            if rc < 0:
                self.waitOnDirectSessionClose(oVM, 5000);

                # VM failed to power up, still collect VBox.log, need to wrap the session object
                # in order to use the helper for adding the log files to the report.
                from testdriver.vboxwrappers import SessionWrapper;
                oTmp = SessionWrapper(oSession, oVM, self.oVBox, self.oVBoxMgr, self, True, sName, self.sSessionLogFile);
                oTmp.addLogsToReport();

                # Try to collect a stack trace of the process for further investigation of any startup hangs.
                uPid = oTmp.getPid();
                if uPid is not None:
                    sHostProcessInfoHung = utils.processGetInfo(uPid, fSudo = True);
                    if sHostProcessInfoHung is not None:
                        reporter.log('Trying to annotate the hung VM startup process report, please stand by...');
                        fRcTmp = self.annotateAndUploadProcessReport(sHostProcessInfoHung, 'vmprocess-startup-hung.log',
                                                                     'process/report/vm', 'Annotated hung VM process state during startup');     # pylint: disable=line-too-long
                        # Upload the raw log for manual annotation in case resolving failed.
                        if not fRcTmp:
                            reporter.log('Failed to annotate hung VM process report, uploading raw report');
                            reporter.addLogString(sHostProcessInfoHung, 'vmprocess-startup-hung.log', 'process/report/vm',
                                                  'Hung VM process state during startup');

                try:
                    if oSession is not None:
                        oSession.close();
                except: pass;
                reportError(oProgress, 'failed to open session for "%s"' % (sName));
                self.uploadStartupLogFile(oVM, sName);
                return (None, None);
            reporter.log2('waitOnProgress -> %s' % (rc,));

        # Wrap up the session object and push on to the list before returning it.
        if oWrapped is None:
            from testdriver.vboxwrappers import SessionWrapper;
            oWrapped = SessionWrapper(oSession, oVM, self.oVBox, self.oVBoxMgr, self, True, sName, self.sSessionLogFile);

        oWrapped.registerEventHandlerForTask();
        self.aoRemoteSessions.append(oWrapped);
        if oWrapped is not self.aoRemoteSessions[len(self.aoRemoteSessions) - 1]:
            reporter.error('not by reference: oWrapped=%s aoRemoteSessions[%s]=%s'
                           % (oWrapped, len(self.aoRemoteSessions) - 1,
                              self.aoRemoteSessions[len(self.aoRemoteSessions) - 1]));
        self.addTask(oWrapped);

        reporter.log2('startVmEx: oSession=%s, oSessionWrapper=%s, oProgress=%s' % (oSession, oWrapped, oProgress));

        from testdriver.vboxwrappers import ProgressWrapper;
        return (oWrapped, ProgressWrapper(oProgress, self.oVBoxMgr, self,
                                          'starting %s' % (sName,)) if oProgress else None);

    def startVm(self, oVM, sType=None, sName = None, asEnv = None):
        """ Simplified version of startVmEx.  """
        oSession, _ = self.startVmEx(oVM, True, sType, sName, asEnv = asEnv);
        return oSession;

    def startVmByNameEx(self, sName, fWait=True, sType=None, asEnv = None):
        """
        Start the VM, returning the VM session and progress object on success.
        The session is also added to the task list and to the aoRemoteSessions set.

        On failure (None, None) is returned and an error is logged.
        """
        oVM = self.getVmByName(sName);
        if oVM is None:
            return (None, None);
        return self.startVmEx(oVM, fWait, sType, sName, asEnv = asEnv);

    def startVmByName(self, sName, sType=None, asEnv = None):
        """
        Start the VM, returning the VM session on success.  The session is
        also added to the task list and to the aoRemoteSessions set.

        On failure None is returned and an error is logged.
        """
        oSession, _ = self.startVmByNameEx(sName, True, sType, asEnv = asEnv);
        return oSession;

    def terminateVmBySession(self, oSession, oProgress = None, fTakeScreenshot = None): # pylint: disable=too-many-statements
        """
        Terminates the VM specified by oSession and adds the release logs to
        the test report.

        This will try achieve this by using powerOff, but will resort to
        tougher methods if that fails.

        The session will always be removed from the task list.
        The session will be closed unless we fail to kill the process.
        The session will be removed from the remote session list if closed.

        The progress object (a wrapper!) is for teleportation and similar VM
        operations, it will be attempted canceled before powering off the VM.
        Failures are logged but ignored.
        The progress object will always be removed from the task list.

        Returns True if powerOff and session close both succeed.
        Returns False if on failure (logged), including when we successfully
        kill the VM process.
        """

        reporter.log2('terminateVmBySession: oSession=%s (pid=%s) oProgress=%s' % (oSession.sName, oSession.getPid(), oProgress));

        if self.fVmNoTerminate:
            reporter.log('terminateVmBySession: Skipping, as --vbox-vm-no-terminate was specified');
            # Make sure that we still process the events the VM needs.
            self.sleep(24 * 60 * 60 * 1000);

        # Call getPid first to make sure the PID is cached in the wrapper.
        oSession.getPid();

        #
        # If the host is out of memory, just skip all the info collection as it
        # requires memory too and seems to wedge.
        #
        sHostProcessInfo     = None;
        sHostProcessInfoHung = None;
        sLastScreenshotPath  = None;
        sOsKernelLog         = None;
        sVgaText             = None;
        asMiscInfos          = [];

        if not oSession.fHostMemoryLow:
            # Try to fetch the VM process info before meddling with its state.
            if self.fAlwaysUploadLogs or reporter.testErrorCount() > 0:
                sHostProcessInfo = utils.processGetInfo(oSession.getPid(), fSudo = True);

            #
            # Pause the VM if we're going to take any screenshots or dig into the
            # guest.  Failures are quitely ignored.
            #
            if self.fAlwaysUploadLogs or reporter.testErrorCount() > 0:
                try:
                    if oSession.oVM.state in [ vboxcon.MachineState_Running,
                                               vboxcon.MachineState_LiveSnapshotting,
                                               vboxcon.MachineState_Teleporting ]:
                        oSession.o.console.pause();
                except:
                    reporter.logXcpt();

            #
            # Take Screenshot and upload it (see below) to Test Manager if appropriate/requested.
            #
            if fTakeScreenshot is True  or  self.fAlwaysUploadScreenshots  or  reporter.testErrorCount() > 0:
                sLastScreenshotPath = os.path.join(self.sScratchPath, "LastScreenshot-%s.png" % oSession.sName);
                fRc = oSession.takeScreenshot(sLastScreenshotPath);
                if fRc is not True:
                    sLastScreenshotPath = None;

            # Query the OS kernel log from the debugger if appropriate/requested.
            if self.fAlwaysUploadLogs or reporter.testErrorCount() > 0:
                sOsKernelLog = oSession.queryOsKernelLog();

            # Do "info vgatext all" separately.
            if self.fAlwaysUploadLogs or reporter.testErrorCount() > 0:
                sVgaText = oSession.queryDbgInfoVgaText();

            # Various infos (do after kernel because of symbols).
            if self.fAlwaysUploadLogs or reporter.testErrorCount() > 0:
                # Dump the guest stack for all CPUs.
                cCpus = oSession.getCpuCount();
                if cCpus > 0:
                    for iCpu in xrange(0, cCpus):
                        sThis = oSession.queryDbgGuestStack(iCpu);
                        if sThis:
                            asMiscInfos += [
                                '================ start guest stack VCPU %s ================\n' % (iCpu,),
                                sThis,
                                '================ end guest stack VCPU %s ==================\n' % (iCpu,),
                            ];

                for sInfo, sArg in [ ('mode', 'all'),
                                     ('fflags', ''),
                                     ('cpumguest', 'verbose all'),
                                     ('cpumguestinstr', 'symbol all'),
                                     ('exits', ''),
                                     ('pic', ''),
                                     ('apic', ''),
                                     ('apiclvt', ''),
                                     ('apictimer', ''),
                                     ('ioapic', ''),
                                     ('pit', ''),
                                     ('phys', ''),
                                     ('clocks', ''),
                                     ('timers', ''),
                                     ('gdt', ''),
                                     ('ldt', ''),
                                    ]:
                    if sInfo in ['apic',] and self.fpApiVer < 5.1: # asserts and burns
                        continue;
                    sThis = oSession.queryDbgInfo(sInfo, sArg);
                    if sThis:
                        if sThis[-1] != '\n':
                            sThis += '\n';
                        asMiscInfos += [
                            '================ start %s %s ================\n' % (sInfo, sArg),
                            sThis,
                            '================ end %s %s ==================\n' % (sInfo, sArg),
                        ];

        #
        # Terminate the VM
        #

        # Cancel the progress object if specified.
        if oProgress is not None:
            if not oProgress.isCompleted()  and  oProgress.isCancelable():
                reporter.log2('terminateVmBySession: canceling "%s"...' % (oProgress.sName));
                try:
                    oProgress.o.cancel();
                except:
                    reporter.logXcpt();
                else:
                    oProgress.wait();
            self.removeTask(oProgress);

        # Check if the VM has terminated by itself before powering it off.
        fClose = True;
        fRc    = True;
        if oSession.needsPoweringOff():
            reporter.log('terminateVmBySession: powering off "%s"...' % (oSession.sName,));
            fRc = oSession.powerOff(fFudgeOnFailure = False);
            if fRc is not True:
                # power off failed, try terminate it in a nice manner.
                fRc = False;
                uPid = oSession.getPid();
                if uPid is not None:
                    #
                    # Collect some information about the VM process first to have
                    # some state information for further investigation why powering off failed.
                    #
                    sHostProcessInfoHung = utils.processGetInfo(uPid, fSudo = True);

                    # Exterminate...
                    reporter.error('terminateVmBySession: Terminating PID %u (VM %s)' % (uPid, oSession.sName));
                    fClose = base.processTerminate(uPid);
                    if fClose is True:
                        self.waitOnDirectSessionClose(oSession.oVM, 5000);
                        fClose = oSession.waitForTask(1000);

                    if fClose is not True:
                        # Being nice failed...
                        reporter.error('terminateVmBySession: Termination failed, trying to kill PID %u (VM %s) instead' \
                                       % (uPid, oSession.sName));
                        fClose = base.processKill(uPid);
                        if fClose is True:
                            self.waitOnDirectSessionClose(oSession.oVM, 5000);
                            fClose = oSession.waitForTask(1000);
                        if fClose is not True:
                            reporter.error('terminateVmBySession: Failed to kill PID %u (VM %s)' % (uPid, oSession.sName));

        # The final steps.
        if fClose is True:
            reporter.log('terminateVmBySession: closing session "%s"...' % (oSession.sName,));
            oSession.close();
            self.waitOnDirectSessionClose(oSession.oVM, 10000);
            try:
                eState = oSession.oVM.state;
            except:
                reporter.logXcpt();
            else:
                if eState == vboxcon.MachineState_Aborted:
                    reporter.error('terminateVmBySession: The VM "%s" aborted!' % (oSession.sName,));
        self.removeTask(oSession);

        #
        # Add the release log, debug log and a screenshot of the VM to the test report.
        #
        if self.fAlwaysUploadLogs or reporter.testErrorCount() > 0:
            oSession.addLogsToReport();

        # Add a screenshot if it has been requested and taken successfully.
        if sLastScreenshotPath is not None:
            if reporter.testErrorCount() > 0:
                reporter.addLogFile(sLastScreenshotPath, 'screenshot/failure', 'Last VM screenshot');
            else:
                reporter.addLogFile(sLastScreenshotPath, 'screenshot/success', 'Last VM screenshot');

        # Add the guest OS log if it has been requested and taken successfully.
        if sOsKernelLog is not None:
            reporter.addLogString(sOsKernelLog, 'kernel.log', 'log/guest/kernel', 'Guest OS kernel log');

        # Add "info vgatext all" if we've got it.
        if sVgaText is not None:
            reporter.addLogString(sVgaText, 'vgatext.txt', 'info/vgatext', 'info vgatext all');

        # Add the "info xxxx" items if we've got any.
        if asMiscInfos:
            reporter.addLogString(u''.join(asMiscInfos), 'info.txt', 'info/collection', 'A bunch of info items.');

        # Add the host process info if we were able to retrieve it.
        if sHostProcessInfo is not None:
            reporter.log('Trying to annotate the VM process report, please stand by...');
            fRcTmp = self.annotateAndUploadProcessReport(sHostProcessInfo, 'vmprocess.log',
                                                         'process/report/vm', 'Annotated VM process state');
            # Upload the raw log for manual annotation in case resolving failed.
            if not fRcTmp:
                reporter.log('Failed to annotate VM process report, uploading raw report');
                reporter.addLogString(sHostProcessInfo, 'vmprocess.log', 'process/report/vm', 'VM process state');

        # Add the host process info for failed power off attempts if we were able to retrieve it.
        if sHostProcessInfoHung is not None:
            reporter.log('Trying to annotate the hung VM process report, please stand by...');
            fRcTmp = self.annotateAndUploadProcessReport(sHostProcessInfoHung, 'vmprocess-hung.log',
                                                         'process/report/vm', 'Annotated hung VM process state');
            # Upload the raw log for manual annotation in case resolving failed.
            if not fRcTmp:
                reporter.log('Failed to annotate hung VM process report, uploading raw report');
                fRcTmp = reporter.addLogString(sHostProcessInfoHung, 'vmprocess-hung.log', 'process/report/vm',
                                               'Hung VM process state');
                if not fRcTmp:
                    try: reporter.log('******* START vmprocess-hung.log *******\n%s\n******* END vmprocess-hung.log *******\n'
                                      % (sHostProcessInfoHung,));
                    except: pass; # paranoia

        # Upload the screen video recordings if appropriate.
        if self.fAlwaysUploadRecordings or reporter.testErrorCount() > 0:
            reporter.log2('Uploading %d screen recordings ...' % (len(self.adRecordingFiles),));
            for dRecFile in self.adRecordingFiles:
                reporter.log2('Uploading screen recording "%s" (screen %d)' % (dRecFile['file'], dRecFile['id']));
                reporter.addLogFile(dRecFile['file'],
                                    'screenrecording/failure' if reporter.testErrorCount() > 0 else 'screenrecording/success',
                                    'Recording of screen #%d' % (dRecFile['id'],));

        return fRc;


    #
    # Some information query functions (mix).
    #
    # Methods require the VBox API.  If the information is provided by both
    # the testboxscript as well as VBox API, we'll check if it matches.
    #

    def _hasHostCpuFeature(self, sEnvVar, sEnum, fpApiMinVer, fQuiet):
        """
        Common Worker for hasHostNestedPaging() and hasHostHwVirt().

        Returns True / False.
        Raises exception on environment / host mismatch.
        """
        fEnv = os.environ.get(sEnvVar, None);
        if fEnv is not None:
            fEnv = fEnv.lower() not in [ 'false', 'f', 'not', 'no', 'n', '0', ];

        fVBox = None;
        self.importVBoxApi();
        if self.fpApiVer >= fpApiMinVer and hasattr(vboxcon, sEnum):
            try:
                fVBox = self.oVBox.host.getProcessorFeature(getattr(vboxcon, sEnum));
            except:
                if not fQuiet:
                    reporter.logXcpt();

        if fVBox is not None:
            if fEnv is not None:
                if fEnv != fVBox and not fQuiet:
                    reporter.log('TestBox configuration overwritten: fVBox=%s (%s) vs. fEnv=%s (%s)'
                                 % (fVBox, sEnum, fEnv, sEnvVar));
                return fEnv;
            return fVBox;
        if fEnv is not None:
            return fEnv;
        return False;

    def hasHostHwVirt(self, fQuiet = False):
        """
        Checks if hardware assisted virtualization is supported by the host.

        Returns True / False.
        Raises exception on environment / host mismatch.
        """
        return self._hasHostCpuFeature('TESTBOX_HAS_HW_VIRT', 'ProcessorFeature_HWVirtEx', 3.1, fQuiet);

    def hasHostNestedPaging(self, fQuiet = False):
        """
        Checks if nested paging is supported by the host.

        Returns True / False.
        Raises exception on environment / host mismatch.
        """
        return self._hasHostCpuFeature('TESTBOX_HAS_NESTED_PAGING', 'ProcessorFeature_NestedPaging', 4.2, fQuiet) \
           and self.hasHostHwVirt(fQuiet);

    def hasHostNestedHwVirt(self, fQuiet = False):
        """
        Checks if nested hardware-assisted virtualization is supported by the host.

        Returns True / False.
        Raises exception on environment / host mismatch.
        """
        return self._hasHostCpuFeature('TESTBOX_HAS_NESTED_HWVIRT', 'ProcessorFeature_NestedHWVirt', 6.0, fQuiet) \
           and self.hasHostHwVirt(fQuiet);

    def hasHostLongMode(self, fQuiet = False):
        """
        Checks if the host supports 64-bit guests.

        Returns True / False.
        Raises exception on environment / host mismatch.
        """
        # Note that the testboxscript doesn't export this variable atm.
        return self._hasHostCpuFeature('TESTBOX_HAS_LONG_MODE', 'ProcessorFeature_LongMode', 3.1, fQuiet);

    def getHostCpuCount(self, fQuiet = False):
        """
        Returns the number of CPUs on the host.

        Returns True / False.
        Raises exception on environment / host mismatch.
        """
        cEnv = os.environ.get('TESTBOX_CPU_COUNT', None);
        if cEnv is not None:
            cEnv = int(cEnv);

        try:
            cVBox = self.oVBox.host.processorOnlineCount;
        except:
            if not fQuiet:
                reporter.logXcpt();
            cVBox = None;

        if cVBox is not None:
            if cEnv is not None:
                assert cVBox == cEnv, 'Misconfigured TestBox: VBox: %u CPUs, testboxscript: %u CPUs' % (cVBox, cEnv);
            return cVBox;
        if cEnv is not None:
            return cEnv;
        return 1;

    def _getHostCpuDesc(self, fQuiet = False):
        """
        Internal method used for getting the host CPU description from VBoxSVC.
        Returns description string, on failure an empty string is returned.
        """
        try:
            return self.oVBox.host.getProcessorDescription(0);
        except:
            if not fQuiet:
                reporter.logXcpt();
        return '';

    def isHostCpuAmd(self, fQuiet = False):
        """
        Checks if the host CPU vendor is AMD.

        Returns True / False.
        """
        sCpuDesc = self._getHostCpuDesc(fQuiet);
        return 'AMD' in sCpuDesc or sCpuDesc == 'AuthenticAMD';

    def isHostCpuIntel(self, fQuiet = False):
        """
        Checks if the host CPU vendor is Intel.

        Returns True / False.
        """
        sCpuDesc = self._getHostCpuDesc(fQuiet);
        return sCpuDesc.startswith("Intel") or sCpuDesc == 'GenuineIntel';

    def isHostCpuVia(self, fQuiet = False):
        """
        Checks if the host CPU vendor is VIA (or Centaur).

        Returns True / False.
        """
        sCpuDesc = self._getHostCpuDesc(fQuiet);
        return sCpuDesc.startswith("VIA") or sCpuDesc == 'CentaurHauls';

    def isHostCpuShanghai(self, fQuiet = False):
        """
        Checks if the host CPU vendor is Shanghai (or Zhaoxin).

        Returns True / False.
        """
        sCpuDesc = self._getHostCpuDesc(fQuiet);
        return sCpuDesc.startswith("ZHAOXIN") or sCpuDesc.strip(' ') == 'Shanghai';

    def isHostCpuP4(self, fQuiet = False):
        """
        Checks if the host CPU is a Pentium 4 / Pentium D.

        Returns True / False.
        """
        if not self.isHostCpuIntel(fQuiet):
            return False;

        if self.fpApiVer >= 7.1:
            (uFamilyModel, _, _, _) = self.oVBox.host.x86.getProcessorCPUIDLeaf(0, 0x1, 0);
        else:
            (uFamilyModel, _, _, _) = self.oVBox.host.getProcessorCPUIDLeaf(0, 0x1, 0);
        return ((uFamilyModel >> 8) & 0xf) == 0xf;

    def hasRawModeSupport(self, fQuiet = False):
        """
        Checks if raw-mode is supported by VirtualBox that the testbox is
        configured for it.

        Returns True / False.
        Raises no exceptions.

        Note! Differs from the rest in that we don't require the
              TESTBOX_WITH_RAW_MODE value to match the API.  It is
              sometimes helpful to disable raw-mode on individual
              test boxes. (This probably goes for
        """
        # The environment variable can be used to disable raw-mode.
        fEnv = os.environ.get('TESTBOX_WITH_RAW_MODE', None);
        if fEnv is not None:
            fEnv = fEnv.lower() not in [ 'false', 'f', 'not', 'no', 'n', '0', ];
            if fEnv is False:
                return False;

        # Starting with 5.0 GA / RC2 the API can tell us whether VBox was built
        # with raw-mode support or not.
        self.importVBoxApi();
        if self.fpApiVer >= 5.0:
            try:
                if self.fpApiVer >= 7.1:
                    # @todo r=aeichner Not entirely correct as we can support multiple platforms on a single host
                    #                  each having an individual raw-mode status. Not relevant right now because
                    #                  there is no raw-mode at all currently.
                    oPlatformProperties = self.oVBox.getPlatformProperties(self.oVBox.host.architecture);
                    fVBox = oPlatformProperties.rawModeSupported;
                else:
                    fVBox = self.oVBox.systemProperties.rawModeSupported;
            except:
                if not fQuiet:
                    reporter.logXcpt();
                fVBox = True;
            if fVBox is False:
                return False;

        return True;

    #
    # Testdriver execution methods.
    #

    def handleTask(self, oTask, sMethod):
        """
        Callback method for handling unknown tasks in the various run loops.

        The testdriver should override this if it already tasks running when
        calling startVmAndConnectToTxsViaTcp, txsRunTest or similar methods.
        Call super to handle unknown tasks.

        Returns True if handled, False if not.
        """
        reporter.error('%s: unknown task %s' % (sMethod, oTask));
        return False;

    def txsDoTask(self, oSession, oTxsSession, fnAsync, aArgs):
        """
        Generic TXS task wrapper which waits both on the TXS and the session tasks.

        Returns False on error, logged.
        Returns task result on success.
        """
        # All async methods ends with the following two args.
        cMsTimeout    = aArgs[-2];
        fIgnoreErrors = aArgs[-1];

        fRemoveVm  = self.addTask(oSession);
        fRemoveTxs = self.addTask(oTxsSession);

        reporter.log2('txsDoTask(%s): Running' % (str(fnAsync)));
        rc = fnAsync(*aArgs); # pylint: disable=star-args
        if rc is True:
            rc = False;
            oTask = self.waitForTasks(cMsTimeout + 1);
            if oTask is oTxsSession:
                if oTxsSession.isSuccess():
                    rc = oTxsSession.getResult();
                elif fIgnoreErrors is True:
                    reporter.log(  'txsDoTask(%s): task failed (%s)' % (str(fnAsync), oTxsSession.getLastReply()[1],));
                else:
                    reporter.error('txsDoTask(%s): task failed (%s)' % (str(fnAsync), oTxsSession.getLastReply()[1],));
            else:
                oTxsSession.cancelTask();
                if oTask is None:
                    if fIgnoreErrors is True:
                        reporter.log(  'txsDoTask(%s): The task timed out.' % (str(fnAsync)));
                    else:
                        reporter.errorTimeout('txsDoTask(%s): The task timed out.' % (str(fnAsync)));
                elif oTask is oSession:
                    reporter.error('txsDoTask(%s): The VM terminated unexpectedly' % (str(fnAsync)));
                else:
                    if fIgnoreErrors is True:
                        reporter.log(  'txsDoTask(%s): An unknown task %s was returned' % (str(fnAsync), oTask,));
                    else:
                        reporter.error('txsDoTask(%s): An unknown task %s was returned' % (str(fnAsync), oTask,));
        else:
            reporter.error('txsDoTask(%s) returned %s' % (str(fnAsync), rc,));

        if fRemoveTxs:
            self.removeTask(oTxsSession);
        if fRemoveVm:
            self.removeTask(oSession);
        return rc;

    # pylint: disable=missing-docstring

    def txsDisconnect(self, oSession, oTxsSession, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncDisconnect,
                              (self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsVer(self, oSession, oTxsSession, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncVer,
                              (self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsUuid(self, oSession, oTxsSession, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncUuid,
                              (self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsMkDir(self, oSession, oTxsSession, sRemoteDir, fMode = 0o700, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncMkDir,
                              (sRemoteDir, fMode, self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsMkDirPath(self, oSession, oTxsSession, sRemoteDir, fMode = 0o700, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncMkDirPath,
                              (sRemoteDir, fMode, self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsMkSymlink(self, oSession, oTxsSession, sLinkTarget, sLink, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncMkSymlink,
                              (sLinkTarget, sLink, self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsRmDir(self, oSession, oTxsSession, sRemoteDir, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncRmDir,
                              (sRemoteDir, self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsRmFile(self, oSession, oTxsSession, sRemoteFile, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncRmFile,
                              (sRemoteFile, self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsRmSymlink(self, oSession, oTxsSession, sRemoteSymlink, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncRmSymlink,
                              (sRemoteSymlink, self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsRmTree(self, oSession, oTxsSession, sRemoteTree, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncRmTree,
                              (sRemoteTree, self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsIsDir(self, oSession, oTxsSession, sRemoteDir, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncIsDir,
                              (sRemoteDir, self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsIsFile(self, oSession, oTxsSession, sRemoteFile, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncIsFile,
                              (sRemoteFile, self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsIsSymlink(self, oSession, oTxsSession, sRemoteSymlink, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncIsSymlink,
                              (sRemoteSymlink, self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsCopyFile(self, oSession, oTxsSession, sSrcFile, sDstFile, fMode = 0, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncCopyFile, \
                              (sSrcFile, sDstFile, fMode, self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsUploadFile(self, oSession, oTxsSession, sLocalFile, sRemoteFile, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncUploadFile, \
                              (sLocalFile, sRemoteFile, self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsUploadString(self, oSession, oTxsSession, sContent, sRemoteFile, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncUploadString, \
                              (sContent, sRemoteFile, self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsDownloadFile(self, oSession, oTxsSession, sRemoteFile, sLocalFile, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncDownloadFile, \
                              (sRemoteFile, sLocalFile, self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsDownloadFiles(self, oSession, oTxsSession, aasFiles, fAddToLog = True, fIgnoreErrors = False):
        """
        Convenience function to get files from the guest, storing them in the
        scratch and adding them to the test result set (optional, but default).

        The aasFiles parameter contains an array of with guest-path + host-path
        pairs, optionally a file 'kind', description and an alternative upload
        filename can also be specified.

        Host paths are relative to the scratch directory or they must be given
        in absolute form.  The guest path should be using guest path style.

        Returns True on success.
        Returns False on failure (unless fIgnoreErrors is set), logged.
        """
        for asEntry in aasFiles:
            # Unpack:
            sGstFile     = asEntry[0];
            sHstFile     = asEntry[1];
            sKind        = asEntry[2] if len(asEntry) > 2 and asEntry[2] else 'misc/other';
            sDescription = asEntry[3] if len(asEntry) > 3 and asEntry[3] else '';
            sAltName     = asEntry[4] if len(asEntry) > 4 and asEntry[4] else None;
            assert len(asEntry) <= 5 and sGstFile and sHstFile;
            if not os.path.isabs(sHstFile):
                sHstFile = os.path.join(self.sScratchPath, sHstFile);

            reporter.log2('Downloading file "%s" to "%s" ...' % (sGstFile, sHstFile,));

            try:    os.unlink(sHstFile); ## @todo txsDownloadFile doesn't truncate the output file.
            except: pass;

            fRc = self.txsDownloadFile(oSession, oTxsSession, sGstFile, sHstFile, 30 * 1000, fIgnoreErrors);
            if fRc:
                if fAddToLog:
                    reporter.addLogFile(sHstFile, sKind, sDescription, sAltName);
            else:
                if fIgnoreErrors is not True:
                    return reporter.error('error downloading file "%s" to "%s"' % (sGstFile, sHstFile));
                reporter.log('warning: file "%s" was not downloaded, ignoring.' % (sGstFile,));
        return True;

    def txsDownloadString(self, oSession, oTxsSession, sRemoteFile, sEncoding = 'utf-8', fIgnoreEncodingErrors = True,
                          cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncDownloadString,
                              (sRemoteFile, sEncoding, fIgnoreEncodingErrors, self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsPackFile(self, oSession, oTxsSession, sRemoteFile, sRemoteSource, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncPackFile, \
                              (sRemoteFile, sRemoteSource, self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsUnpackFile(self, oSession, oTxsSession, sRemoteFile, sRemoteDir, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncUnpackFile, \
                              (sRemoteFile, sRemoteDir, self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    def txsExpandString(self, oSession, oTxsSession, sString, cMsTimeout = 30000, fIgnoreErrors = False):
        return self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncExpandString, \
                              (sString, self.adjustTimeoutMs(cMsTimeout), fIgnoreErrors));

    # pylint: enable=missing-docstring

    def txsCdWait(self,
                  oSession,             # type: vboxwrappers.SessionWrapper
                  oTxsSession,          # type: txsclient.Session
                  cMsTimeout = 30000,   # type: int
                  sFile = None          # type: String
                  ):                    # -> bool
        """
        Mostly an internal helper for txsRebootAndReconnectViaTcp and
        startVmAndConnectToTxsViaTcp that waits for the CDROM drive to become
        ready.  It does this by polling for a file it knows to exist on the CD.

        Returns True on success.

        Returns False on failure, logged.
        """

        if sFile is None:
            sFile = 'valkit.txt';

        reporter.log('txsCdWait: Waiting for file "%s" to become available ...' % (sFile,));

        fRemoveVm   = self.addTask(oSession);
        fRemoveTxs  = self.addTask(oTxsSession);
        cMsTimeout  = self.adjustTimeoutMs(cMsTimeout);
        msStart     = base.timestampMilli();
        cMsTimeout2 = cMsTimeout;
        fRc         = oTxsSession.asyncIsFile('${CDROM}/%s' % (sFile,), cMsTimeout2);
        if fRc is True:
            while True:
                # wait for it to complete.
                oTask = self.waitForTasks(cMsTimeout2 + 1);
                if oTask is not oTxsSession:
                    oTxsSession.cancelTask();
                    if oTask is None:
                        reporter.errorTimeout('txsCdWait: The task timed out (after %s ms).'
                                              % (base.timestampMilli() - msStart,));
                    elif oTask is oSession:
                        reporter.error('txsCdWait: The VM terminated unexpectedly');
                    else:
                        reporter.error('txsCdWait: An unknown task %s was returned' % (oTask,));
                    fRc = False;
                    break;
                if oTxsSession.isSuccess():
                    break;

                # Check for timeout.
                cMsElapsed = base.timestampMilli() - msStart;
                if cMsElapsed >= cMsTimeout:
                    reporter.error('txsCdWait: timed out');
                    fRc = False;
                    break;
                # delay.
                self.sleep(1);

                # resubmit the task.
                cMsTimeout2 = msStart + cMsTimeout - base.timestampMilli();
                cMsTimeout2 = max(cMsTimeout2, 500);
                fRc = oTxsSession.asyncIsFile('${CDROM}/%s' % (sFile,), cMsTimeout2);
                if fRc is not True:
                    reporter.error('txsCdWait: asyncIsFile failed');
                    break;
        else:
            reporter.error('txsCdWait: asyncIsFile failed');

        if not fRc:
            # Do some diagnosis to find out why this failed.
            ## @todo Identify guest OS type and only run one of the following commands.
            fIsNotWindows = True;
            reporter.log('txsCdWait: Listing root contents of ${CDROM}:');
            if fIsNotWindows:
                reporter.log('txsCdWait: Tiggering udevadm ...');
                oTxsSession.syncExec("/sbin/udevadm", ("/sbin/udevadm", "trigger", "--verbose"), fIgnoreErrors = True);
                time.sleep(15);
                oTxsSession.syncExec("/bin/ls", ("/bin/ls", "-al", "${CDROM}"), fIgnoreErrors = True);
                reporter.log('txsCdWait: Listing media directory:');
                oTxsSession.syncExec('/bin/ls', ('/bin/ls', '-l', '-a', '-R', '/media'), fIgnoreErrors = True);
                reporter.log('txsCdWait: Listing mount points / drives:');
                oTxsSession.syncExec('/bin/mount', ('/bin/mount',), fIgnoreErrors = True);
                oTxsSession.syncExec('/bin/cat', ('/bin/cat', '/etc/fstab'), fIgnoreErrors = True);
                oTxsSession.syncExec('/bin/dmesg', ('/bin/dmesg',), fIgnoreErrors = True);
                oTxsSession.syncExec('/usr/bin/lshw', ('/usr/bin/lshw', '-c', 'disk'), fIgnoreErrors = True);
                oTxsSession.syncExec('/bin/journalctl',
                                     ('/bin/journalctl', '-x', '-b'), fIgnoreErrors = True);
                oTxsSession.syncExec('/bin/journalctl',
                                     ('/bin/journalctl', '-x', '-b', '/usr/lib/udisks2/udisksd'), fIgnoreErrors = True);
                oTxsSession.syncExec('/usr/bin/udisksctl',
                                     ('/usr/bin/udisksctl', 'info', '-b', '/dev/sr0'), fIgnoreErrors = True);
                oTxsSession.syncExec('/bin/systemctl',
                                     ('/bin/systemctl', 'status', 'udisks2'), fIgnoreErrors = True);
                oTxsSession.syncExec('/bin/ps',
                                     ('/bin/ps', '-a', '-u', '-x'), fIgnoreErrors = True);
                reporter.log('txsCdWait: Mounting manually ...');
                for _ in range(3):
                    oTxsSession.syncExec('/bin/mount', ('/bin/mount', '/dev/sr0', '${CDROM}'), fIgnoreErrors = True);
                    time.sleep(5);
                reporter.log('txsCdWait: Re-Listing media directory:');
                oTxsSession.syncExec('/bin/ls', ('/bin/ls', '-l', '-a', '-R', '/media'), fIgnoreErrors = True);
            else:
                # ASSUMES that we always install Windows on drive C right now.
                sWinDir = "C:\\Windows\\System32\\";
                # Should work since WinXP Pro.
                oTxsSession.syncExec(sWinDir + "wbem\\WMIC.exe",
                                     ("WMIC.exe", "logicaldisk", "get",
                                      "deviceid, volumename, description"),
                                     fIgnoreErrors = True);
                oTxsSession.syncExec(sWinDir + " cmd.exe",
                                     ('cmd.exe', '/C', 'dir', '${CDROM}'),
                                     fIgnoreErrors = True);

        if fRemoveTxs:
            self.removeTask(oTxsSession);
        if fRemoveVm:
            self.removeTask(oSession);
        return fRc;

    def txsDoConnectViaTcp(self, oSession, cMsTimeout, fNatForwardingForTxs = False):
        """
        Mostly an internal worker for connecting to TXS via TCP used by the
        *ViaTcp methods.

        Returns a tuplet with True/False and TxsSession/None depending on the
        result. Errors are logged.
        """

        reporter.log2('txsDoConnectViaTcp: oSession=%s, cMsTimeout=%s, fNatForwardingForTxs=%s'
                      % (oSession, cMsTimeout, fNatForwardingForTxs));

        cMsTimeout = self.adjustTimeoutMs(cMsTimeout);
        oTxsConnect = oSession.txsConnectViaTcp(cMsTimeout, fNatForwardingForTxs = fNatForwardingForTxs);
        if oTxsConnect is not None:
            self.addTask(oTxsConnect);
            fRemoveVm = self.addTask(oSession);
            oTask     = self.waitForTasks(cMsTimeout + 1);
            reporter.log2('txsDoConnectViaTcp: waitForTasks returned %s' % (oTask,));
            self.removeTask(oTxsConnect);
            if oTask is oTxsConnect:
                oTxsSession = oTxsConnect.getResult();
                if oTxsSession is not None:
                    reporter.log('txsDoConnectViaTcp: Connected to TXS on %s.' % (oTxsSession.oTransport.sHostname,));
                    return (True, oTxsSession);

                reporter.error('txsDoConnectViaTcp: failed to connect to TXS.');
            else:
                oTxsConnect.cancelTask();
                if oTask is None:
                    reporter.errorTimeout('txsDoConnectViaTcp: connect stage 1 timed out');
                elif oTask is oSession:
                    oSession.reportPrematureTermination('txsDoConnectViaTcp: ');
                else:
                    reporter.error('txsDoConnectViaTcp: unknown/wrong task %s' % (oTask,));
            if fRemoveVm:
                self.removeTask(oSession);
        else:
            reporter.error('txsDoConnectViaTcp: txsConnectViaTcp failed');
        return (False, None);

    def startVmAndConnectToTxsViaTcp(self, sVmName, fCdWait = False, cMsTimeout = 15*60000, \
                                     cMsCdWait = 30000, sFileCdWait = None, \
                                     fNatForwardingForTxs = False):
        """
        Starts the specified VM and tries to connect to its TXS via TCP.
        The VM will be powered off if TXS doesn't respond before the specified
        time has elapsed.

        Returns a the VM and TXS sessions (a two tuple) on success.  The VM
        session is in the task list, the TXS session is not.
        Returns (None, None) on failure, fully logged.
        """

        # Zap the guest IP to make sure we're not getting a stale entry
        # (unless we're restoring the VM of course).
        oTestVM = self.oTestVmSet.findTestVmByName(sVmName) if self.oTestVmSet is not None else None;
        if oTestVM is None \
          or oTestVM.fSnapshotRestoreCurrent is False:
            try:
                oSession1 = self.openSession(self.getVmByName(sVmName));
                oSession1.delGuestPropertyValue('/VirtualBox/GuestInfo/Net/0/V4/IP');
                oSession1.saveSettings(True);
                del oSession1;
            except:
                reporter.logXcpt();

        # Start the VM.
        reporter.log('startVmAndConnectToTxsViaTcp: Starting(/preparing) "%s" (timeout %s s)...' % (sVmName, cMsTimeout / 1000));
        reporter.flushall();
        oSession = self.startVmByName(sVmName);
        if oSession is not None:
            # Connect to TXS.
            reporter.log2('startVmAndConnectToTxsViaTcp: Started(/prepared) "%s", connecting to TXS ...' % (sVmName,));
            (fRc, oTxsSession) = self.txsDoConnectViaTcp(oSession, cMsTimeout, fNatForwardingForTxs);
            if fRc is True:
                if fCdWait:
                    # Wait for CD?
                    reporter.log2('startVmAndConnectToTxsViaTcp: Waiting for file "%s" to become available ...' % (sFileCdWait,));
                    fRc = self.txsCdWait(oSession, oTxsSession, cMsCdWait, sFileCdWait);
                    if fRc is not True:
                        reporter.error('startVmAndConnectToTxsViaTcp: txsCdWait failed');

                sVer = self.txsVer(oSession, oTxsSession, cMsTimeout, fIgnoreErrors = True);
                if sVer is not False:
                    reporter.log('startVmAndConnectToTxsViaTcp: TestExecService version %s' % (sVer,));
                else:
                    reporter.log('startVmAndConnectToTxsViaTcp: Unable to retrieve TestExecService version');

                if fRc is True:
                    # Success!
                    return (oSession, oTxsSession);
            else:
                reporter.error('startVmAndConnectToTxsViaTcp: txsDoConnectViaTcp failed');
            # If something went wrong while waiting for TXS to be started - take VM screenshot before terminate it
            self.terminateVmBySession(oSession);
        return (None, None);

    def txsRebootAndReconnectViaTcp(self, oSession, oTxsSession, fCdWait = False, cMsTimeout = 15*60000, \
                                    cMsCdWait = 30000, sFileCdWait = None, fNatForwardingForTxs = False):
        """
        Executes the TXS reboot command

        Returns A tuple of True and the new TXS session on success.

        Returns A tuple of False and either the old TXS session or None on failure.
        """
        reporter.log2('txsRebootAndReconnect: cMsTimeout=%u' % (cMsTimeout,));

        #
        # This stuff is a bit complicated because of rebooting being kind of
        # disruptive to the TXS and such...  The protocol is that TXS will:
        #   - ACK the reboot command.
        #   - Shutdown the transport layer, implicitly disconnecting us.
        #   - Execute the reboot operation.
        #   - On failure, it will be re-init the transport layer and be
        #     available pretty much immediately. UUID unchanged.
        #   - On success, it will be respawed after the reboot (hopefully),
        #     with a different UUID.
        #
        fRc = False;
        iStart = base.timestampMilli();

        # Get UUID.
        cMsTimeout2 = min(60000, cMsTimeout);
        sUuidBefore = self.txsUuid(oSession, oTxsSession, self.adjustTimeoutMs(cMsTimeout2, 60000));
        if sUuidBefore is not False:
            # Reboot.
            cMsElapsed  = base.timestampMilli() - iStart;
            cMsTimeout2 = cMsTimeout - cMsElapsed;
            fRc = self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncReboot,
                                 (self.adjustTimeoutMs(cMsTimeout2, 60000), False));
            if fRc is True:
                # Reconnect.
                if fNatForwardingForTxs is True:
                    self.sleep(22); # NAT fudge - Two fixes are wanted: 1. TXS connect retries.  2. Main API reboot/reset hint.
                cMsElapsed = base.timestampMilli() - iStart;
                (fRc, oTxsSession) = self.txsDoConnectViaTcp(oSession, cMsTimeout - cMsElapsed, fNatForwardingForTxs);
                if fRc is True:
                    # Check the UUID.
                    cMsElapsed  = base.timestampMilli() - iStart;
                    cMsTimeout2 = min(60000, cMsTimeout - cMsElapsed);
                    sUuidAfter  = self.txsDoTask(oSession, oTxsSession, oTxsSession.asyncUuid,
                                                 (self.adjustTimeoutMs(cMsTimeout2, 60000), False));
                    if sUuidBefore is not False:
                        if sUuidAfter != sUuidBefore:
                            reporter.log('The guest rebooted (UUID %s -> %s)' % (sUuidBefore, sUuidAfter))

                            # Do CD wait if specified.
                            if fCdWait:
                                fRc = self.txsCdWait(oSession, oTxsSession, cMsCdWait, sFileCdWait);
                                if fRc is not True:
                                    reporter.error('txsRebootAndReconnectViaTcp: txsCdWait failed');

                            sVer = self.txsVer(oSession, oTxsSession, cMsTimeout, fIgnoreErrors = True);
                            if sVer is not False:
                                reporter.log('txsRebootAndReconnectViaTcp: TestExecService version %s' % (sVer,));
                            else:
                                reporter.log('txsRebootAndReconnectViaTcp: Unable to retrieve TestExecService version');
                        else:
                            reporter.error('txsRebootAndReconnectViaTcp: failed to get UUID (after)');
                    else:
                        reporter.error('txsRebootAndReconnectViaTcp: did not reboot (UUID %s)' % (sUuidBefore,));
                else:
                    reporter.error('txsRebootAndReconnectViaTcp: txsDoConnectViaTcp failed');
            else:
                reporter.error('txsRebootAndReconnectViaTcp: reboot failed');
        else:
            reporter.error('txsRebootAndReconnectViaTcp: failed to get UUID (before)');
        return (fRc, oTxsSession);

    # pylint: disable=too-many-locals,too-many-arguments

    def txsRunTest(self, oTxsSession, sTestName, cMsTimeout, sExecName, asArgs = (), asAddEnv = (), sAsUser = "",
                   fCheckSessionStatus = False):
        """
        Executes the specified test task, waiting till it completes or times out.

        The VM session (if any) must be in the task list.

        Returns True if we executed the task and nothing abnormal happend.
        Query the process status from the TXS session.

        Returns False if some unexpected task was signalled or we failed to
        submit the job.

        If fCheckSessionStatus is set to True, the overall session status will be
        taken into account and logged as an error on failure.
        """
        reporter.testStart(sTestName);
        reporter.log2('txsRunTest: cMsTimeout=%u sExecName=%s asArgs=%s' % (cMsTimeout, sExecName, asArgs));

        # Submit the job.
        fRc = False;
        if oTxsSession.asyncExec(sExecName, asArgs, asAddEnv, sAsUser, cMsTimeout = self.adjustTimeoutMs(cMsTimeout)):
            self.addTask(oTxsSession);

            # Wait for the job to complete.
            while True:
                oTask = self.waitForTasks(cMsTimeout + 1);
                if oTask is None:
                    if fCheckSessionStatus:
                        reporter.error('txsRunTest: waitForTasks for test "%s" timed out' % (sTestName,));
                    else:
                        reporter.log('txsRunTest: waitForTasks for test "%s" timed out' % (sTestName,));
                    break;
                if oTask is oTxsSession:
                    if      fCheckSessionStatus \
                    and not oTxsSession.isSuccess():
                        reporter.error('txsRunTest: Test "%s" failed' % (sTestName,));
                    else:
                        fRc = True;
                        reporter.log('txsRunTest: isSuccess=%s getResult=%s' \
                                     % (oTxsSession.isSuccess(), oTxsSession.getResult()));
                    break;
                if not self.handleTask(oTask, 'txsRunTest'):
                    break;

            self.removeTask(oTxsSession);
            if not oTxsSession.pollTask():
                oTxsSession.cancelTask();
        else:
            reporter.error('txsRunTest: asyncExec failed');

        reporter.testDone();
        return fRc;

    def txsRunTestRedirectStd(self, oTxsSession, sTestName, cMsTimeout, sExecName, asArgs = (), asAddEnv = (), sAsUser = "",
                              oStdIn = '/dev/null', oStdOut = '/dev/null', oStdErr = '/dev/null', oTestPipe = '/dev/null',
                              fIgnoreErrors = False):
        """
        Executes the specified test task, waiting till it completes or times out,
        redirecting stdin, stdout and stderr to the given objects.

        The VM session (if any) must be in the task list.

        Returns True if we executed the task and nothing abnormal happend.
        Query the process status from the TXS session.

        Returns False if some unexpected task was signalled or we failed to
        submit the job.
        """
        reporter.testStart(sTestName);
        reporter.log2('txsRunTestRedirectStd: cMsTimeout=%u sExecName=%s asArgs=%s' % (cMsTimeout, sExecName, asArgs));

        # Submit the job.
        fRc = False;
        if oTxsSession.asyncExecEx(sExecName, asArgs, asAddEnv, oStdIn, oStdOut, oStdErr,
                                   oTestPipe, sAsUser, cMsTimeout = self.adjustTimeoutMs(cMsTimeout),
                                   fIgnoreErrors = fIgnoreErrors):
            self.addTask(oTxsSession);

            # Wait for the job to complete.
            while True:
                oTask = self.waitForTasks(cMsTimeout + 1);
                if oTask is None:
                    if fIgnoreErrors:
                        reporter.log('txsRunTestRedirectStd: waitForTasks timed out');
                    else:
                        reporter.error('txsRunTestRedirectStd: waitForTasks timed out');
                    break;
                if oTask is oTxsSession:
                    fRc = True;
                    if  not oTxsSession.isSuccess() \
                    and not fIgnoreErrors:
                        reporter.error('txsRunTestRedirectStd: failed; result is "%s"' % (oTxsSession.getResult()));
                    else:
                        reporter.log('txsRunTestRedirectStd: isSuccess=%s getResult=%s'
                                     % (oTxsSession.isSuccess(), oTxsSession.getResult()));
                    break;

                if not self.handleTask(oTask, 'txsRunTestRedirectStd'):
                    break;

            self.removeTask(oTxsSession);
            if not oTxsSession.pollTask():
                oTxsSession.cancelTask();
        else:
            reporter.error('txsRunTestRedirectStd: asyncExec failed');

        reporter.testDone();
        return fRc;

    def txsRunTestStdIn(self, oTxsSession, sTestName, cMsTimeout, sExecName, asArgs = (), asAddEnv = (), sAsUser = "",
                        sStdIn = None, fIgnoreErrors = False):
        """
        Executes the specified test task, waiting till it completes or times out.
        Redirecting simple string input into stdin, redirecting text stdout / stderr output to verbose logging.

        The VM session (if any) must be in the task list.

        Returns True if we executed the task and nothing abnormal happend.
        Query the process status from the TXS session.

        Returns False if some unexpected task was signalled or we failed to
        submit the job.
        """
        assert sStdIn is not None;
        class StdInWrapper(object): # pylint: disable=too-few-public-methods
            """
            Wraps sStdIn in a file like class.
            """
            def __init__(self, sStdIn):
                self.sContent = sStdIn;
                self.off      = 0;

            def read(self, cbMax):
                """
                Returns next stdin input (up to cbMax), or an empty string if all input has been supplied already.
                """
                cbLeft = len(self.sContent) - self.off;
                if cbLeft == 0:
                    return "";
                if cbLeft <= cbMax:
                    sRet = self.sContent[self.off:(self.off + cbLeft)];
                else:
                    sRet = self.sContent[self.off:(self.off + cbMax)];
                self.off = self.off + len(sRet);
                reporter.log2('Reading from stdin: "%s"' % (sRet,));
                return sRet;

        return self.txsRunTestRedirectStd(oTxsSession, sTestName, cMsTimeout, sExecName, asArgs, asAddEnv, sAsUser,
                                          oStdIn  = StdInWrapper(sStdIn),
                                          oStdErr = reporter.FileWrapper('stderr'), oStdOut = reporter.FileWrapper('stdout'),
                                          fIgnoreErrors = fIgnoreErrors);

    def txsRunTest2(self, oTxsSession1, oTxsSession2, sTestName, cMsTimeout,
            sExecName1, asArgs1,
            sExecName2, asArgs2,
            asAddEnv1 = (), sAsUser1 = '', fWithTestPipe1 = True,
            asAddEnv2 = (), sAsUser2 = '', fWithTestPipe2 = True):
        """
        Executes the specified test tasks, waiting till they complete or
        times out.  The 1st task is started after the 2nd one.

        The VM session (if any) must be in the task list.

        Returns True if we executed the task and nothing abnormal happend.
        Query the process status from the TXS sessions.

        Returns False if some unexpected task was signalled or we failed to
        submit the job.
        """
        reporter.testStart(sTestName);

        # Submit the jobs.
        fRc = False;
        if oTxsSession1.asyncExec(sExecName1, asArgs1, asAddEnv1, sAsUser1, fWithTestPipe1, '1-',
                                  self.adjustTimeoutMs(cMsTimeout)):
            self.addTask(oTxsSession1);

            self.sleep(2); # fudge! grr

            if oTxsSession2.asyncExec(sExecName2, asArgs2, asAddEnv2, sAsUser2, fWithTestPipe2, '2-',
                                      self.adjustTimeoutMs(cMsTimeout)):
                self.addTask(oTxsSession2);

                # Wait for the jobs to complete.
                cPendingJobs = 2;
                while True:
                    oTask = self.waitForTasks(cMsTimeout + 1);
                    if oTask is None:
                        reporter.log('txsRunTest2: waitForTasks timed out');
                        break;

                    if oTask is oTxsSession1 or oTask is oTxsSession2:
                        if oTask is oTxsSession1:   iTask = 1;
                        else:                       iTask = 2;
                        reporter.log('txsRunTest2: #%u - isSuccess=%s getResult=%s' \
                                     % (iTask,  oTask.isSuccess(), oTask.getResult()));
                        self.removeTask(oTask);
                        cPendingJobs -= 1;
                        if cPendingJobs <= 0:
                            fRc = True;
                            break;

                    elif not self.handleTask(oTask, 'txsRunTest'):
                        break;

                self.removeTask(oTxsSession2);
                if not oTxsSession2.pollTask():
                    oTxsSession2.cancelTask();
            else:
                reporter.error('txsRunTest2: asyncExec #2 failed');

            self.removeTask(oTxsSession1);
            if not oTxsSession1.pollTask():
                oTxsSession1.cancelTask();
        else:
            reporter.error('txsRunTest2: asyncExec #1 failed');

        reporter.testDone();
        return fRc;

    # pylint: enable=too-many-locals,too-many-arguments


    #
    # Working with test results via serial port.
    #

    class TxsMonitorComFile(base.TdTaskBase):
        """
        Class that monitors a COM output file.
        """

        def __init__(self, sComRawFile, asStopWords = None):
            base.TdTaskBase.__init__(self, utils.getCallerName());
            self.sComRawFile    = sComRawFile;
            self.oStopRegExp    = re.compile('\\b(' + '|'.join(asStopWords if asStopWords else ('PASSED', 'FAILED',)) + ')\\b');
            self.sResult        = None; ##< The result.
            self.cchDisplayed   = 0;    ##< Offset into the file string of what we've already fed to the logger.

        def toString(self):
            return '<%s sComRawFile=%s oStopRegExp=%s sResult=%s cchDisplayed=%s>' \
                  % (base.TdTaskBase.toString(self), self.sComRawFile, self.oStopRegExp, self.sResult, self.cchDisplayed,);

        def pollTask(self, fLocked = False):
            """
            Overrides TdTaskBase.pollTask() for the purpose of polling the file.
            """
            if not fLocked:
                self.lockTask();

            sFile = utils.noxcptReadFile(self.sComRawFile, '', 'rU');
            if len(sFile) > self.cchDisplayed:
                sNew = sFile[self.cchDisplayed:];
                oMatch = self.oStopRegExp.search(sNew);
                if oMatch:
                    # Done! Get result, flush all the output and signal the task.
                    self.sResult = oMatch.group(1);
                    for sLine in sNew.split('\n'):
                        reporter.log('COM OUTPUT: %s' % (sLine,));
                    self.cchDisplayed = len(sFile);
                    self.signalTaskLocked();
                else:
                    # Output whole lines only.
                    offNewline = sFile.find('\n', self.cchDisplayed);
                    while offNewline >= 0:
                        reporter.log('COM OUTPUT: %s' % (sFile[self.cchDisplayed:offNewline]))
                        self.cchDisplayed = offNewline + 1;
                        offNewline = sFile.find('\n', self.cchDisplayed);

            fRet = self.fSignalled;
            if not fLocked:
                self.unlockTask();
            return fRet;

        # Our stuff.
        def getResult(self):
            """
            Returns the connected TXS session object on success.
            Returns None on failure or if the task has not yet completed.
            """
            self.oCv.acquire();
            sResult = self.sResult;
            self.oCv.release();
            return sResult;

        def cancelTask(self):
            """ Cancels the task. """
            self.signalTask();
            return True;


    def monitorComRawFile(self, oSession, sComRawFile, cMsTimeout = 15*60000, asStopWords = None):
        """
        Monitors the COM output file for stop words (PASSED and FAILED by default).

        Returns the stop word.
        Returns None on VM error and timeout.
        """

        reporter.log2('monitorComRawFile: oSession=%s, cMsTimeout=%s, sComRawFile=%s' % (oSession, cMsTimeout, sComRawFile));

        oMonitorTask = self.TxsMonitorComFile(sComRawFile, asStopWords);
        self.addTask(oMonitorTask);

        cMsTimeout = self.adjustTimeoutMs(cMsTimeout);
        oTask = self.waitForTasks(cMsTimeout + 1);
        reporter.log2('monitorComRawFile: waitForTasks returned %s' % (oTask,));

        if oTask is not oMonitorTask:
            oMonitorTask.cancelTask();
        self.removeTask(oMonitorTask);

        oMonitorTask.pollTask();
        return oMonitorTask.getResult();


    def runVmAndMonitorComRawFile(self, sVmName, sComRawFile, cMsTimeout = 15*60000, asStopWords = None):
        """
        Runs the specified VM and monitors the given COM output file for stop
        words (PASSED and FAILED by default).

        The caller is assumed to have configured the VM to use the given
        file. The method will take no action to verify this.

        Returns the stop word.
        Returns None on VM error and timeout.
        """

        # Start the VM.
        reporter.log('runVmAndMonitorComRawFile: Starting(/preparing) "%s" (timeout %s s)...' % (sVmName, cMsTimeout / 1000));
        reporter.flushall();
        oSession = self.startVmByName(sVmName);
        if oSession is not None:
            # Let it run and then terminate it.
            sRet = self.monitorComRawFile(oSession, sComRawFile, cMsTimeout, asStopWords);
            self.terminateVmBySession(oSession);
        else:
            sRet = None;
        return sRet;

    #
    # Other stuff
    #

    def waitForGAs(self,
                   oSession, # type: vboxwrappers.SessionWrapper
                   cMsTimeout = 120000, aenmWaitForRunLevels = None, aenmWaitForActive = None, aenmWaitForInactive = None):
        """
        Waits for the guest additions to enter a certain state.

        aenmWaitForRunLevels - List of run level values to wait for (success if one matches).
        aenmWaitForActive    - List facilities (type values) that must be active.
        aenmWaitForInactive  - List facilities (type values) that must be inactive.

        Defaults to wait for AdditionsRunLevelType_Userland if nothing else is given.

        Returns True on success, False w/ error logging on timeout or failure.
        """
        reporter.log2('waitForGAs: oSession=%s, cMsTimeout=%s' % (oSession, cMsTimeout,));

        #
        # Get IGuest:
        #
        try:
            oIGuest = oSession.o.console.guest;
        except:
            return reporter.errorXcpt();

        #
        # Create a wait task:
        #
        from testdriver.vboxwrappers import AdditionsStatusTask;
        try:
            oGaStatusTask = AdditionsStatusTask(oSession             = oSession,
                                                oIGuest              = oIGuest,
                                                cMsTimeout           = cMsTimeout,
                                                aenmWaitForRunLevels = aenmWaitForRunLevels,
                                                aenmWaitForActive    = aenmWaitForActive,
                                                aenmWaitForInactive  = aenmWaitForInactive);
        except:
            return reporter.errorXcpt();

        #
        # Add the task and make sure the VM session is also present.
        #
        self.addTask(oGaStatusTask);
        fRemoveSession = self.addTask(oSession);
        oTask          = self.waitForTasks(cMsTimeout + 1);
        reporter.log2('waitForGAs: returned %s (oGaStatusTask=%s, oSession=%s)' % (oTask, oGaStatusTask, oSession,));
        self.removeTask(oGaStatusTask);
        if fRemoveSession:
            self.removeTask(oSession);

        #
        # Digest the result.
        #
        if oTask is oGaStatusTask:
            fSucceeded = oGaStatusTask.getResult();
            if fSucceeded is True:
                reporter.log('waitForGAs: Succeeded.');
            else:
                reporter.error('waitForGAs: Failed.');
        else:
            oGaStatusTask.cancelTask();
            if oTask is None:
                reporter.error('waitForGAs: Timed out.');
            elif oTask is oSession:
                oSession.reportPrematureTermination('waitForGAs: ');
            else:
                reporter.error('waitForGAs: unknown/wrong task %s' % (oTask,));
            fSucceeded = False;
        return fSucceeded;

    @staticmethod
    def controllerTypeToName(eControllerType):
        """
        Translate a controller type to a standard controller name.
        """
        if eControllerType in (vboxcon.StorageControllerType_PIIX3, vboxcon.StorageControllerType_PIIX4,):
            sName = "IDE Controller";
        elif eControllerType == vboxcon.StorageControllerType_IntelAhci:
            sName = "SATA Controller";
        elif eControllerType == vboxcon.StorageControllerType_LsiLogicSas:
            sName = "SAS Controller";
        elif eControllerType in (vboxcon.StorageControllerType_LsiLogic, vboxcon.StorageControllerType_BusLogic,):
            sName = "SCSI Controller";
        elif eControllerType == vboxcon.StorageControllerType_NVMe:
            sName = "NVMe Controller";
        elif eControllerType == vboxcon.StorageControllerType_VirtioSCSI:
            sName = "VirtIO SCSI Controller";
        else:
            sName = "Storage Controller";
        return sName;
