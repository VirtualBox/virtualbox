# -*- coding: utf-8 -*-
# $Id: utils.py 168903 2025-05-16 12:31:15Z knut.osmundsen@oracle.com $
# pylint: disable=too-many-lines

"""
Common Utility Functions.
"""

from __future__ import print_function;

__copyright__ = \
"""
Copyright (C) 2012-2024 Oracle and/or its affiliates.

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
__version__ = "$Revision: 168903 $"


# Standard Python imports.
import datetime;
import errno;
import os;
import platform;
import re;
import stat;
import subprocess;
import sys;
import time;
import traceback;
import unittest;

if sys.platform == 'win32':
    import ctypes;
    import msvcrt;              # pylint: disable=import-error
    import win32api;            # pylint: disable=import-error
    import win32con;            # pylint: disable=import-error
    import win32console;        # pylint: disable=import-error
    import win32file;           # pylint: disable=import-error
    import win32process;        # pylint: disable=import-error
    import winerror;            # pylint: disable=import-error
    import pywintypes;          # pylint: disable=import-error
else:
    import signal;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    unicode = str;  # pylint: disable=redefined-builtin,invalid-name
    xrange = range; # pylint: disable=redefined-builtin,invalid-name
    long = int;     # pylint: disable=redefined-builtin,invalid-name


#
# Python 2/3 glue.
#

if sys.version_info[0] >= 3:
    def iteritems(dDict):
        """
        Wrapper around dict.items() / dict.iteritems().
        """
        return iter(dDict.items());
else:
    def iteritems(dDict):
        """
        Wrapper around dict.items() / dict.iteritems().
        """
        return dDict.iteritems();


#
# Strings.
#

def toUnicode(sString, encoding = None, errors = 'strict'):
    """
    A little like the python 2 unicode() function.
    """
    if sys.version_info[0] >= 3:
        if isinstance(sString, bytes):
            return str(sString, encoding if encoding else 'utf-8', errors);
    else:
        if not isinstance(sString, unicode):
            return unicode(sString, encoding if encoding else 'utf-8', errors);
    return sString;



#
# Output.
#

def printOut(sString):
    """
    Outputs a string to standard output, dealing with python 2.x encoding stupidity.
    """
    sStreamEncoding = sys.stdout.encoding;
    if sStreamEncoding is None:         # Files, pipes and such on 2.x.  (pylint is confused here)
        sStreamEncoding = 'US-ASCII';   # pylint: disable=redefined-variable-type
    if sStreamEncoding == 'UTF-8' or not isinstance(sString, unicode):
        print(sString);
    else:
        print(sString.encode(sStreamEncoding, 'backslashreplace').decode(sStreamEncoding));

def printErr(sString):
    """
    Outputs a string to standard error, dealing with python 2.x encoding stupidity.
    """
    sStreamEncoding = sys.stderr.encoding;
    if sStreamEncoding is None:         # Files, pipes and such on 2.x. (pylint is confused here)
        sStreamEncoding = 'US-ASCII';   # pylint: disable=redefined-variable-type
    if sStreamEncoding == 'UTF-8' or not isinstance(sString, unicode):
        print(sString, file = sys.stderr);
    else:
        print(sString.encode(sStreamEncoding, 'backslashreplace').decode(sStreamEncoding), file = sys.stderr);


#
# Host OS and CPU.
#

def getHostOs():
    """
    Gets the host OS name (short).

    See the KBUILD_OSES variable in kBuild/header.kmk for possible return values.
    """
    sPlatform = platform.system();
    if sPlatform in ('Linux', 'Darwin', 'Solaris', 'FreeBSD', 'NetBSD', 'OpenBSD'):
        sPlatform = sPlatform.lower();
    elif sPlatform == 'Windows':
        sPlatform = 'win';
    elif sPlatform == 'SunOS':
        sPlatform = 'solaris';
    else:
        raise Exception('Unsupported platform "%s"' % (sPlatform,));
    return sPlatform;

g_sHostArch = None;

def getHostArch():
    """
    Gets the host CPU architecture.

    See the KBUILD_ARCHES variable in kBuild/header.kmk for possible return values.
    """
    global g_sHostArch;
    if g_sHostArch is None:
        sArch = platform.machine();
        if sArch in ('i386', 'i486', 'i586', 'i686', 'i786', 'i886', 'x86'):
            sArch = 'x86';
        elif sArch in ('AMD64', 'amd64', 'x86_64'):
            sArch = 'amd64';
        elif sArch == 'i86pc': # SunOS
            if platform.architecture()[0] == '64bit':
                sArch = 'amd64';
            else:
                try:
                    sArch = str(processOutputChecked(['/usr/bin/isainfo', '-n',]));
                except:
                    pass;
                sArch = sArch.strip();
                if sArch != 'amd64':
                    sArch = 'x86';
        elif sArch in ('arm64', 'ARM64', 'aarch64'):
            sArch = 'arm64';
        else:
            raise Exception('Unsupported architecture/machine "%s"' % (sArch,));
        g_sHostArch = sArch;
    return g_sHostArch;


def getHostOsDotArch():
    """
    Gets the 'os.arch' for the host.
    """
    return '%s.%s' % (getHostOs(), getHostArch());


def isValidOs(sOs):
    """
    Validates the OS name.
    """
    if sOs in ('darwin', 'dos', 'dragonfly', 'freebsd', 'haiku', 'l4', 'linux', 'netbsd', 'nt', 'openbsd', \
               'os2', 'solaris', 'win', 'os-agnostic'):
        return True;
    return False;


def isValidArch(sArch):
    """
    Validates the CPU architecture name.
    """
    if sArch in ('x86', 'amd64', 'sparc32', 'sparc64', 's390', 's390x', 'ppc32', 'ppc64', \
               'mips32', 'mips64', 'ia64', 'hppa32', 'hppa64', 'arm', 'arm64', 'alpha'):
        return True;
    return False;

def isValidOsDotArch(sOsDotArch):
    """
    Validates the 'os.arch' string.
    """

    asParts = sOsDotArch.split('.');
    if asParts.length() != 2:
        return False;
    return isValidOs(asParts[0]) \
       and isValidArch(asParts[1]);

def getHostOsVersion():
    """
    Returns the host OS version.  This is platform.release with additional
    distro indicator on linux.
    """
    sVersion = platform.release();
    sOs = getHostOs();
    if sOs == 'linux':
        sDist = '';
        try:
            # try /etc/lsb-release first to distinguish between Debian and Ubuntu
            with open('/etc/lsb-release') as oFile: # pylint: disable=unspecified-encoding
                for sLine in oFile:
                    oMatch = re.search(r'(?:DISTRIB_DESCRIPTION\s*=)\s*"*(.*)"', sLine);
                    if oMatch is not None:
                        sDist = oMatch.group(1).strip();
        except:
            pass;
        if sDist:
            sVersion += ' / ' + sDist;
        else:
            asFiles = \
            [
                [ '/etc/debian_version', 'Debian v'],
                [ '/etc/gentoo-release', '' ],
                [ '/etc/oracle-release', '' ],
                [ '/etc/redhat-release', '' ],
                [ '/etc/SUSE-brand', '' ],
            ];
            for sFile, sPrefix in asFiles:
                if os.path.isfile(sFile):
                    try:
                        with open(sFile) as oFile: # pylint: disable=unspecified-encoding
                            sLine = ''.join(oFile.readlines());
                    except:
                        continue;
                    sLine = sLine.strip().replace("VERSION =", "") # suse tweek
                    if sLine:
                        sVersion += ' / ' + sPrefix + sLine;
                    break;

    elif sOs == 'solaris':
        sVersion = platform.version();
        if os.path.isfile('/etc/release'):
            try:
                with open('/etc/release') as oFile: # pylint: disable=unspecified-encoding
                    sLast = oFile.readlines()[-1];
                sLast = sLast.strip();
                if sLast:
                    sVersion += ' (' + sLast + ')';
            except:
                pass;

    elif sOs == 'darwin':
        def getMacVersionName(sVersion):
            """
            Figures out the Mac OS X/macOS code name from the numeric version.
            """
            aOsVersion = sVersion.split('.')    # example: ('10','15','7')
            codenames = {"4": "Tiger",
                         "5": "Leopard",
                         "6": "Snow Leopard",
                         "7": "Lion",
                         "8": "Mountain Lion",
                         "9": "Mavericks",
                         "10": "Yosemite",
                         "11": "El Capitan",
                         "12": "Sierra",
                         "13": "High Sierra",
                         "14": "Mojave",
                         "15": "Catalina",
                         "16": "Wrong version",
                         }
            codenames_afterCatalina = {"11": "Big Sur",
                                       "12": "Monterey",
                                       "13": "Ventura",
                                       "14": "Sonoma",
                                       "15": "Sequoia",
                                       "16": "Unknown 16",
                                       "17": "Unknown 17"}

            if aOsVersion[0] == '10':
                sResult = codenames[aOsVersion[1]]
            else:
                sResult = codenames_afterCatalina[aOsVersion[0]]
            return sResult

        sOsxVersion = platform.mac_ver()[0]
        sVersion += ' / OS X ' + sOsxVersion + ' (' + getMacVersionName(sOsxVersion) + ')'

    elif sOs == 'win':
        class OSVersionInfoEx(ctypes.Structure): # pylint: disable=used-before-assignment
            """ OSVERSIONEX """
            kaFields = [
                    ('dwOSVersionInfoSize', ctypes.c_ulong),
                    ('dwMajorVersion',      ctypes.c_ulong),
                    ('dwMinorVersion',      ctypes.c_ulong),
                    ('dwBuildNumber',       ctypes.c_ulong),
                    ('dwPlatformId',        ctypes.c_ulong),
                    ('szCSDVersion',        ctypes.c_wchar*128),
                    ('wServicePackMajor',   ctypes.c_ushort),
                    ('wServicePackMinor',   ctypes.c_ushort),
                    ('wSuiteMask',          ctypes.c_ushort),
                    ('wProductType',        ctypes.c_byte),
                    ('wReserved',           ctypes.c_byte)]
            _fields_ = kaFields # pylint: disable=invalid-name

            def __init__(self):
                super(OSVersionInfoEx, self).__init__()
                self.dwOSVersionInfoSize = ctypes.sizeof(self)

        oOsVersion = OSVersionInfoEx()
        rc = ctypes.windll.Ntdll.RtlGetVersion(ctypes.byref(oOsVersion))
        if rc == 0:
            # Python platform.release() is not reliable for newer server releases
            if oOsVersion.wProductType != 1:
                if oOsVersion.dwMajorVersion == 10 and oOsVersion.dwMinorVersion == 0 \
                                                   and oOsVersion.dwBuildNumber == 17763:
                    sVersion = '2019Server'
                elif oOsVersion.dwMajorVersion == 10 and oOsVersion.dwMinorVersion == 0:
                    sVersion = '2016Server'; #todo: should probably add dwBuildNumber for it as well..
                elif oOsVersion.dwMajorVersion == 6 and oOsVersion.dwMinorVersion == 3:
                    sVersion = '2012ServerR2';
                elif oOsVersion.dwMajorVersion == 6 and oOsVersion.dwMinorVersion == 2:
                    sVersion = '2012Server';
                elif oOsVersion.dwMajorVersion == 6 and oOsVersion.dwMinorVersion == 1:
                    sVersion = '2008ServerR2';
                elif oOsVersion.dwMajorVersion == 6 and oOsVersion.dwMinorVersion == 0:
                    sVersion = '2008Server';
                elif oOsVersion.dwMajorVersion == 5 and oOsVersion.dwMinorVersion == 2:
                    sVersion = '2003Server';
            sVersion += ' build ' + str(oOsVersion.dwBuildNumber)
            if oOsVersion.wServicePackMajor:
                sVersion += ' SP' + str(oOsVersion.wServicePackMajor)
                if oOsVersion.wServicePackMinor:
                    sVersion += '.' + str(oOsVersion.wServicePackMinor)

    return sVersion;

def getPresentCpuCount():
    """
    Gets the number of CPUs present in the system.

    This differs from multiprocessor.cpu_count() and os.cpu_count() on windows in
    that we return the active count rather than the maximum count.  If we don't,
    we will end up thinking testboxmem1 has 512 CPU threads, which it doesn't and
    never will have.

    @todo This is probably not exactly what we get on non-windows...
    """

    if getHostOs() == 'win':
        fnGetActiveProcessorCount = getattr(ctypes.windll.kernel32, 'GetActiveProcessorCount', None);
        if fnGetActiveProcessorCount:
            cCpus = fnGetActiveProcessorCount(ctypes.c_ushort(0xffff));
            if cCpus > 0:
                return cCpus;

    import multiprocessing
    return multiprocessing.cpu_count();


#
# File system.
#

def openNoInherit(sFile, sMode = 'r'):
    """
    Wrapper around open() that tries it's best to make sure the file isn't
    inherited by child processes.

    This is a best effort thing at the moment as it doesn't synchronizes with
    child process spawning in any way.  Thus it can be subject to races in
    multithreaded programs.
    """

    # Python 3.4 and later automatically creates non-inherit handles. See PEP-0446.
    uPythonVer = (sys.version_info[0] << 16) | (sys.version_info[1] & 0xffff);
    if uPythonVer >= ((3 << 16) | 4):
        oFile = open(sFile, sMode);                             # pylint: disable=consider-using-with,unspecified-encoding
    else:
        try:
            from fcntl import FD_CLOEXEC, F_GETFD, F_SETFD, fcntl; # pylint: disable=import-error
        except:
            # On windows, we can use the 'N' flag introduced in Visual C++ 7.0 or 7.1 with python 2.x.
            if getHostOs() == 'win':
                if uPythonVer < (3 << 16):
                    offComma = sMode.find(',');
                    if offComma < 0:
                        return open(sFile, sMode + 'N');        # pylint: disable=consider-using-with,unspecified-encoding
                    return open(sFile,                          # pylint: disable=consider-using-with,unspecified-encoding,bad-open-mode
                                sMode[:offComma] + 'N' + sMode[offComma:]);

            # Just in case.
            return open(sFile, sMode);                          # pylint: disable=consider-using-with,unspecified-encoding

        oFile = open(sFile, sMode);                             # pylint: disable=consider-using-with,unspecified-encoding
        #try:
        fcntl(oFile, F_SETFD, fcntl(oFile, F_GETFD) | FD_CLOEXEC);
        #except:
        #    pass;
    return oFile;

def openNoDenyDeleteNoInherit(sFile, sMode = 'r'):
    """
    Wrapper around open() that tries it's best to make sure the file isn't
    inherited by child processes.

    This is a best effort thing at the moment as it doesn't synchronizes with
    child process spawning in any way.  Thus it can be subject to races in
    multithreaded programs.
    """

    if getHostOs() == 'win':
        # Need to use CreateFile directly to open the file so we can feed it FILE_SHARE_DELETE.
        # pylint: disable=no-member,c-extension-no-member
        fAccess = 0;
        fDisposition = win32file.OPEN_EXISTING; # pylint: disable=used-before-assignment
        if 'r' in sMode or '+' in sMode:
            fAccess |= win32file.GENERIC_READ;
        if 'a' in sMode:
            fAccess |= win32file.GENERIC_WRITE;
            fDisposition = win32file.OPEN_ALWAYS;
        elif 'w' in sMode:
            fAccess = win32file.GENERIC_WRITE;
            if '+' in sMode:
                fDisposition = win32file.OPEN_ALWAYS;
                fAccess |= win32file.GENERIC_READ;
            else:
                fDisposition = win32file.CREATE_ALWAYS;
        if not fAccess:
            fAccess |= win32file.GENERIC_READ;
        fSharing = (win32file.FILE_SHARE_READ | win32file.FILE_SHARE_WRITE
                    | win32file.FILE_SHARE_DELETE);
        hFile = win32file.CreateFile(sFile, fAccess, fSharing, None, fDisposition, 0, None);
        if 'a' in sMode:
            win32file.SetFilePointer(hFile, 0, win32file.FILE_END);

        # Turn the NT handle into a CRT file descriptor.
        hDetachedFile = hFile.Detach();
        if fAccess == win32file.GENERIC_READ:
            fOpen = os.O_RDONLY;
        elif fAccess == win32file.GENERIC_WRITE:
            fOpen = os.O_WRONLY;
        else:
            fOpen = os.O_RDWR;
        # pulint: enable=no-member,c-extension-no-member
        if 'a' in sMode:
            fOpen |= os.O_APPEND;
        if 'b' in sMode or 't' in sMode:
            fOpen |= os.O_TEXT;                                         # pylint: disable=no-member
        fdFile = msvcrt.open_osfhandle(hDetachedFile, fOpen);           # pylint: disable=used-before-assignment

        # Tell python to use this handle.
        oFile = os.fdopen(fdFile, sMode);
    else:
        oFile = open(sFile, sMode);                                     # pylint: disable=consider-using-with,unspecified-encoding

        # Python 3.4 and later automatically creates non-inherit handles. See PEP-0446.
        uPythonVer = (sys.version_info[0] << 16) | (sys.version_info[1] & 0xffff);
        if uPythonVer < ((3 << 16) | 4):
            try:
                from fcntl import FD_CLOEXEC, F_GETFD, F_SETFD, fcntl;  # pylint: disable=import-error
            except:
                pass;
            else:
                fcntl(oFile, F_SETFD, fcntl(oFile, F_GETFD) | FD_CLOEXEC);
    return oFile;

def noxcptReadLink(sPath, sXcptRet, sEncoding = 'utf-8'):
    """
    No exceptions os.readlink wrapper.
    """
    try:
        sRet = os.readlink(sPath); # pylint: disable=no-member
    except:
        return sXcptRet;
    if hasattr(sRet, 'decode'):
        sRet = sRet.decode(sEncoding, 'ignore');
    return sRet;

def readFile(sFile, sMode = 'rb'):
    """
    Reads the entire file.
    """
    with open(sFile, sMode) as oFile: # pylint: disable=unspecified-encoding
        sRet = oFile.read();
    return sRet;

def noxcptReadFile(sFile, sXcptRet, sMode = 'rb', sEncoding = 'utf-8'):
    """
    No exceptions common.readFile wrapper.
    """
    try:
        sRet = readFile(sFile, sMode);
    except:
        sRet = sXcptRet;
    if sEncoding is not None and hasattr(sRet, 'decode'):
        sRet = sRet.decode(sEncoding, 'ignore');
    return sRet;

def noxcptRmDir(sDir, oXcptRet = False):
    """
    No exceptions os.rmdir wrapper.
    """
    oRet = True;
    try:
        os.rmdir(sDir);
    except:
        oRet = oXcptRet;
    return oRet;

def noxcptDeleteFile(sFile, oXcptRet = False):
    """
    No exceptions os.remove wrapper.
    """
    oRet = True;
    try:
        os.remove(sFile);
    except:
        oRet = oXcptRet;
    return oRet;


def dirEnumerateTree(sDir, fnCallback, fIgnoreExceptions = True):
    # type: (string, (string, stat) -> bool) -> bool
    """
    Recursively walks a directory tree, calling fnCallback for each.

    fnCallback takes a full path and stat object (can be None).  It
    returns a boolean value, False stops walking and returns immediately.

    Returns True or False depending on fnCallback.
    Returns None fIgnoreExceptions is True and an exception was raised by listdir.
    """
    def __worker(sCurDir):
        """ Worker for """
        try:
            asNames = os.listdir(sCurDir);
        except:
            if not fIgnoreExceptions:
                raise;
            return None;
        rc = True;
        for sName in asNames:
            if sName not in [ '.', '..' ]:
                sFullName = os.path.join(sCurDir, sName);
                try:    oStat = os.lstat(sFullName);
                except: oStat = None;
                if fnCallback(sFullName, oStat) is False:
                    return False;
                if oStat is not None and stat.S_ISDIR(oStat.st_mode):
                    rc =  __worker(sFullName);
                    if rc is False:
                        break;
        return rc;

    # Ensure unicode path here so listdir also returns unicode on windows.
    ## @todo figure out unicode stuff on non-windows.
    if sys.platform == 'win32':
        sDir = unicode(sDir);
    return __worker(sDir);



def formatFileMode(uMode):
    # type: (int) -> string
    """
    Format a st_mode value 'ls -la' fasion.
    Returns string.
    """
    if   stat.S_ISDIR(uMode):   sMode = 'd';
    elif stat.S_ISREG(uMode):   sMode = '-';
    elif stat.S_ISLNK(uMode):   sMode = 'l';
    elif stat.S_ISFIFO(uMode):  sMode = 'p';
    elif stat.S_ISCHR(uMode):   sMode = 'c';
    elif stat.S_ISBLK(uMode):   sMode = 'b';
    elif stat.S_ISSOCK(uMode):  sMode = 's';
    else:                       sMode = '?';
    ## @todo sticky bits.
    sMode += 'r' if uMode & stat.S_IRUSR else '-';
    sMode += 'w' if uMode & stat.S_IWUSR else '-';
    sMode += 'x' if uMode & stat.S_IXUSR else '-';
    sMode += 'r' if uMode & stat.S_IRGRP else '-';
    sMode += 'w' if uMode & stat.S_IWGRP else '-';
    sMode += 'x' if uMode & stat.S_IXGRP else '-';
    sMode += 'r' if uMode & stat.S_IROTH else '-';
    sMode += 'w' if uMode & stat.S_IWOTH else '-';
    sMode += 'x' if uMode & stat.S_IXOTH else '-';
    sMode += ' ';
    return sMode;


def formatFileStat(oStat):
    # type: (stat) -> string
    """
    Format a stat result 'ls -la' fasion (numeric IDs).
    Returns string.
    """
    return '%s %3s %4s %4s %10s %s' \
          % (formatFileMode(oStat.st_mode), oStat.st_nlink, oStat.st_uid, oStat.st_gid, oStat.st_size,
             time.strftime('%Y-%m-%d %H:%M', time.localtime(oStat.st_mtime)), );

## Good buffer for file operations.
g_cbGoodBufferSize = 256*1024;

## The original shutil.copyfileobj.
g_fnOriginalShCopyFileObj = None;

def __myshutilcopyfileobj(fsrc, fdst, length = g_cbGoodBufferSize):
    """ shutil.copyfileobj with different length default value (16384 is slow with python 2.7 on windows). """
    return g_fnOriginalShCopyFileObj(fsrc, fdst, length);

def __installShUtilHacks(shutil):
    """ Installs the shutil buffer size hacks. """
    global g_fnOriginalShCopyFileObj;
    if g_fnOriginalShCopyFileObj is None:
        g_fnOriginalShCopyFileObj = shutil.copyfileobj;
        shutil.copyfileobj = __myshutilcopyfileobj;
    return True;


def copyFileSimple(sFileSrc, sFileDst):
    """
    Wrapper around shutil.copyfile that simply copies the data of a regular file.
    Raises exception on failure.
    Return True for show.
    """
    import shutil;
    __installShUtilHacks(shutil);
    return shutil.copyfile(sFileSrc, sFileDst);


def getDiskUsage(sPath):
    """
    Get free space of a partition that corresponds to specified sPath in MB.

    Returns partition free space value in MB.
    """
    if platform.system() == 'Windows':
        oCTypeFreeSpace = ctypes.c_ulonglong(0);
        ctypes.windll.kernel32.GetDiskFreeSpaceExW(ctypes.c_wchar_p(sPath), None, None,
                                                   ctypes.pointer(oCTypeFreeSpace));
        cbFreeSpace = oCTypeFreeSpace.value;
    else:
        oStats = os.statvfs(sPath); # pylint: disable=no-member
        cbFreeSpace = long(oStats.f_frsize) * oStats.f_bfree;

    # Convert to MB
    cMbFreeSpace = long(cbFreeSpace) / (1024 * 1024);

    return cMbFreeSpace;



#
# SubProcess.
#

def _processFixPythonInterpreter(aPositionalArgs, dKeywordArgs):
    """
    If the "executable" is a python script, insert the python interpreter at
    the head of the argument list so that it will work on systems which doesn't
    support hash-bang scripts.
    """

    asArgs = dKeywordArgs.get('args');
    if asArgs is None:
        asArgs = aPositionalArgs[0];

    if asArgs[0].endswith('.py'):
        if sys.executable:
            asArgs.insert(0, sys.executable);
        else:
            asArgs.insert(0, 'python');

        # paranoia...
        if dKeywordArgs.get('args') is not None:
            dKeywordArgs['args'] = asArgs;
        else:
            aPositionalArgs = (asArgs,) + aPositionalArgs[1:];
    return None;

def processPopenSafe(*aPositionalArgs, **dKeywordArgs):
    """
    Wrapper for subprocess.Popen that's Ctrl-C safe on windows.
    """
    if getHostOs() == 'win':
        if dKeywordArgs.get('creationflags', 0) == 0:
            dKeywordArgs['creationflags'] = subprocess.CREATE_NEW_PROCESS_GROUP;
    return subprocess.Popen(*aPositionalArgs, **dKeywordArgs);  # pylint: disable=consider-using-with

def processStart(*aPositionalArgs, **dKeywordArgs):
    """
    Wrapper around subprocess.Popen to deal with its absence in older
    python versions.
    Returns process object on success which can be worked on.
    """
    _processFixPythonInterpreter(aPositionalArgs, dKeywordArgs);
    return processPopenSafe(*aPositionalArgs, **dKeywordArgs);

def processCall(*aPositionalArgs, **dKeywordArgs):
    """
    Wrapper around subprocess.call to deal with its absence in older
    python versions.
    Returns process exit code (see subprocess.poll).
    """
    assert dKeywordArgs.get('stdout') is None;
    assert dKeywordArgs.get('stderr') is None;
    oProcess = processStart(*aPositionalArgs, **dKeywordArgs);
    return oProcess.wait();

def processOutputChecked(*aPositionalArgs, **dKeywordArgs):
    """
    Wrapper around subprocess.check_output to deal with its absense in older
    python versions.

    Extra keywords for specifying now output is to be decoded:
        sEncoding='utf-8
        fIgnoreEncoding=True/False
    """
    sEncoding = dKeywordArgs.get('sEncoding');
    if sEncoding is not None:   del dKeywordArgs['sEncoding'];
    else:                       sEncoding = 'utf-8';

    fIgnoreEncoding = dKeywordArgs.get('fIgnoreEncoding');
    if fIgnoreEncoding is not None:   del dKeywordArgs['fIgnoreEncoding'];
    else:                             fIgnoreEncoding = True;

    _processFixPythonInterpreter(aPositionalArgs, dKeywordArgs);
    oProcess = processPopenSafe(stdout=subprocess.PIPE, *aPositionalArgs, **dKeywordArgs);

    sOutput, _ = oProcess.communicate();
    iExitCode  = oProcess.poll();

    if iExitCode != 0:
        asArgs = dKeywordArgs.get('args');
        if asArgs is None:
            asArgs = aPositionalArgs[0];
        print(sOutput);
        raise subprocess.CalledProcessError(iExitCode, asArgs);

    if hasattr(sOutput, 'decode'):
        sOutput = sOutput.decode(sEncoding, 'ignore' if fIgnoreEncoding else 'strict');
    return sOutput;

def processOutputUnchecked(*aPositionalArgs, **dKeywordArgs):
    """
    Similar to processOutputChecked, but returns status code and both stdout
    and stderr results.

    Extra keywords for specifying now output is to be decoded:
        sEncoding='utf-8
        fIgnoreEncoding=True/False
    """
    sEncoding = dKeywordArgs.get('sEncoding');
    if sEncoding is not None:   del dKeywordArgs['sEncoding'];
    else:                       sEncoding = 'utf-8';

    fIgnoreEncoding = dKeywordArgs.get('fIgnoreEncoding');
    if fIgnoreEncoding is not None:   del dKeywordArgs['fIgnoreEncoding'];
    else:                             fIgnoreEncoding = True;

    _processFixPythonInterpreter(aPositionalArgs, dKeywordArgs);
    oProcess = processPopenSafe(stdout = subprocess.PIPE, stderr = subprocess.PIPE, *aPositionalArgs, **dKeywordArgs);

    sOutput, sError = oProcess.communicate();
    iExitCode       = oProcess.poll();

    if hasattr(sOutput, 'decode'):
        sOutput = sOutput.decode(sEncoding, 'ignore' if fIgnoreEncoding else 'strict');
    if hasattr(sError, 'decode'):
        sError = sError.decode(sEncoding, 'ignore' if fIgnoreEncoding else 'strict');
    return (iExitCode, sOutput, sError);

g_fOldSudo = None;
def _sudoFixArguments(aPositionalArgs, dKeywordArgs, fInitialEnv = True):
    """
    Adds 'sudo' (or similar) to the args parameter, whereever it is.
    """

    # Are we root?
    fIsRoot = True;
    try:
        fIsRoot = os.getuid() == 0; # pylint: disable=no-member
    except:
        pass;

    # If not, prepend sudo (non-interactive, simulate initial login).
    if fIsRoot is not True:
        asArgs = dKeywordArgs.get('args');
        if asArgs is None:
            asArgs = aPositionalArgs[0];

        # Detect old sudo.
        global g_fOldSudo;
        if g_fOldSudo is None:
            try:
                sVersion = str(processOutputChecked(['sudo', '-V']));
            except:
                sVersion = '1.7.0';
            sVersion = sVersion.strip().split('\n', 1)[0];
            sVersion = sVersion.replace('Sudo version', '').strip();
            g_fOldSudo = len(sVersion) >= 4 \
                     and sVersion[0] == '1' \
                     and sVersion[1] == '.' \
                     and sVersion[2] <= '6' \
                     and sVersion[3] == '.';

        asArgs.insert(0, 'sudo');
        if not g_fOldSudo:
            asArgs.insert(1, '-n');
        if fInitialEnv and not g_fOldSudo:
            asArgs.insert(1, '-i');

        # paranoia...
        if dKeywordArgs.get('args') is not None:
            dKeywordArgs['args'] = asArgs;
        else:
            aPositionalArgs = (asArgs,) + aPositionalArgs[1:];
    return None;


def sudoProcessStart(*aPositionalArgs, **dKeywordArgs):
    """
    sudo (or similar) + subprocess.Popen,
    returning the process object on success.
    """
    _processFixPythonInterpreter(aPositionalArgs, dKeywordArgs);
    _sudoFixArguments(aPositionalArgs, dKeywordArgs);
    return processStart(*aPositionalArgs, **dKeywordArgs);

def sudoProcessCall(*aPositionalArgs, **dKeywordArgs):
    """
    sudo (or similar) + subprocess.call
    """
    _processFixPythonInterpreter(aPositionalArgs, dKeywordArgs);
    _sudoFixArguments(aPositionalArgs, dKeywordArgs);
    return processCall(*aPositionalArgs, **dKeywordArgs);

def sudoProcessOutputChecked(*aPositionalArgs, **dKeywordArgs):
    """
    sudo (or similar) + subprocess.check_output.
    """
    _processFixPythonInterpreter(aPositionalArgs, dKeywordArgs);
    _sudoFixArguments(aPositionalArgs, dKeywordArgs);
    return processOutputChecked(*aPositionalArgs, **dKeywordArgs);

def sudoProcessOutputCheckedNoI(*aPositionalArgs, **dKeywordArgs):
    """
    sudo (or similar) + subprocess.check_output, except '-i' isn't used.
    """
    _processFixPythonInterpreter(aPositionalArgs, dKeywordArgs);
    _sudoFixArguments(aPositionalArgs, dKeywordArgs, False);
    return processOutputChecked(*aPositionalArgs, **dKeywordArgs);

def sudoProcessPopen(*aPositionalArgs, **dKeywordArgs):
    """
    sudo (or similar) + processPopenSafe.
    """
    _processFixPythonInterpreter(aPositionalArgs, dKeywordArgs);
    _sudoFixArguments(aPositionalArgs, dKeywordArgs);
    return processPopenSafe(*aPositionalArgs, **dKeywordArgs);


def whichProgram(sName, sPath = None):
    """
    Works similar to the 'which' utility on unix.

    Returns path to the given program if found.
    Returns None if not found.
    """
    sHost = getHostOs();
    sSep  = ';' if sHost in [ 'win', 'os2' ] else ':';

    if sPath is None:
        if sHost == 'win':
            sPath = os.environ.get('Path', None);
        else:
            sPath = os.environ.get('PATH', None);
        if sPath is None:
            return None;

    for sDir in sPath.split(sSep):
        if sDir.strip() != '':
            sTest = os.path.abspath(os.path.join(sDir, sName));
        else:
            sTest = os.path.abspath(sName);
        if os.path.exists(sTest):
            return sTest;

    return None;

#
# Generic process stuff.
#

def processInterrupt(uPid):
    """
    Sends a SIGINT or equivalent to interrupt the specified process.
    Returns True on success, False on failure.

    On Windows hosts this may not work unless the process happens to be a
    process group leader.
    """
    if sys.platform == 'win32':
        try:
            win32console.GenerateConsoleCtrlEvent(win32con.CTRL_BREAK_EVENT, # pylint: disable=no-member,c-extension-no-member
                                                  uPid);
            fRc = True;
        except:
            fRc = False;
    else:
        try:
            os.kill(uPid, signal.SIGINT);
            fRc = True;
        except:
            fRc = False;
    return fRc;

def sendUserSignal1(uPid):
    """
    Sends a SIGUSR1 or equivalent to nudge the process into shutting down
    (VBoxSVC) or something.
    Returns True on success, False on failure or if not supported (win).

    On Windows hosts this may not work unless the process happens to be a
    process group leader.
    """
    if sys.platform == 'win32':
        fRc = False;
    else:
        try:
            os.kill(uPid, signal.SIGUSR1); # pylint: disable=no-member
            fRc = True;
        except:
            fRc = False;
    return fRc;

def processTerminate(uPid):
    """
    Terminates the process in a nice manner (SIGTERM or equivalent).
    Returns True on success, False on failure.
    """
    fRc = False;
    if sys.platform == 'win32':
        try:
            hProcess = win32api.OpenProcess(win32con.PROCESS_TERMINATE,     # pylint: disable=no-member,c-extension-no-member
                                            False, uPid);
        except:
            pass;
        else:
            try:
                win32process.TerminateProcess(hProcess,                     # pylint: disable=no-member,c-extension-no-member
                                              0x40010004); # DBG_TERMINATE_PROCESS
                fRc = True;
            except:
                pass;
            hProcess.Close(); #win32api.CloseHandle(hProcess)
    else:
        try:
            os.kill(uPid, signal.SIGTERM);
            fRc = True;
        except:
            pass;
    return fRc;

def processKill(uPid):
    """
    Terminates the process with extreme prejudice (SIGKILL).
    Returns True on success, False on failure.
    """
    if sys.platform == 'win32':
        fRc = processTerminate(uPid);
    else:
        try:
            os.kill(uPid, signal.SIGKILL); # pylint: disable=no-member
            fRc = True;
        except:
            fRc = False;
    return fRc;

def processKillWithNameCheck(uPid, sName):
    """
    Like processKill(), but checks if the process name matches before killing
    it.  This is intended for killing using potentially stale pid values.

    Returns True on success, False on failure.
    """

    if processCheckPidAndName(uPid, sName) is not True:
        return False;
    return processKill(uPid);


def processExists(uPid):
    """
    Checks if the specified process exits.
    This will only work if we can signal/open the process.

    Returns True if it positively exists, False otherwise.
    """
    sHostOs = getHostOs();
    if sHostOs == 'win':
        fRc = False;
        # We try open the process for waiting since this is generally only forbidden in a very few cases.
        try:
            hProcess = win32api.OpenProcess(win32con.SYNCHRONIZE,           # pylint: disable=no-member,c-extension-no-member
                                            False, uPid);
        except pywintypes.error as oXcpt:                                   # pylint: disable=no-member,used-before-assignment
            if oXcpt.winerror == winerror.ERROR_ACCESS_DENIED:              # pylint: disable=used-before-assignment
                fRc = True;
        except:
            pass;
        else:
            hProcess.Close();
            fRc = True;
    else:
        fRc = False;
        try:
            os.kill(uPid, 0);
            fRc = True;
        except OSError as oXcpt:
            if oXcpt.errno == errno.EPERM:
                fRc = True;
        except:
            pass;
    return fRc;

def processCheckPidAndName(uPid, sName):
    """
    Checks if a process PID and NAME matches.
    """
    fRc = processExists(uPid);
    if fRc is not True:
        return False;

    if sys.platform == 'win32':
        try:
            from win32com.client import GetObject; # pylint: disable=import-error
            oWmi = GetObject('winmgmts:');
            aoProcesses = oWmi.InstancesOf('Win32_Process');
            for oProcess in aoProcesses:
                if long(oProcess.Properties_("ProcessId").Value) == uPid:
                    sCurName = oProcess.Properties_("Name").Value;
                    #reporter.log2('uPid=%s sName=%s sCurName=%s' % (uPid, sName, sCurName));
                    sName    = sName.lower();
                    sCurName = sCurName.lower();
                    if os.path.basename(sName) == sName:
                        sCurName = os.path.basename(sCurName);

                    if   sCurName == sName \
                      or sCurName + '.exe' == sName \
                      or sCurName == sName  + '.exe':
                        fRc = True;
                    break;
        except:
            #reporter.logXcpt('uPid=%s sName=%s' % (uPid, sName));
            pass;
    else:
        if sys.platform in ('linux2', 'linux', 'linux3', 'linux4', 'linux5', 'linux6'):
            asPsCmd = ['/bin/ps',     '-p', '%u' % (uPid,), '-o', 'fname='];
        elif sys.platform in ('sunos5',):
            asPsCmd = ['/usr/bin/ps', '-p', '%u' % (uPid,), '-o', 'fname='];
        elif sys.platform in ('darwin',):
            asPsCmd = ['/bin/ps',     '-p', '%u' % (uPid,), '-o', 'ucomm='];
        else:
            asPsCmd = None;

        if asPsCmd is not None:
            try:
                oPs = subprocess.Popen(asPsCmd, stdout=subprocess.PIPE);    # pylint: disable=consider-using-with
                sCurName = oPs.communicate()[0];
                iExitCode = oPs.wait();
            except:
                #reporter.logXcpt();
                return False;

            # ps fails with non-zero exit code if the pid wasn't found.
            if iExitCode != 0:
                return False;
            if sCurName is None:
                return False;
            sCurName = sCurName.strip();
            if not sCurName:
                return False;

            if os.path.basename(sName) == sName:
                sCurName = os.path.basename(sCurName);
            elif os.path.basename(sCurName) == sCurName:
                sName = os.path.basename(sName);

            if sCurName != sName:
                return False;

            fRc = True;
    return fRc;

def processGetInfo(uPid, fSudo = False):
    """
    Tries to acquire state information of the given process.

    Returns a string with the information on success or None on failure or
    if the host is not supported.

    Note that the format of the information is host system dependent and will
    likely differ much between different hosts.
    """
    fRc = processExists(uPid);
    if fRc is not True:
        return None;

    sHostOs = getHostOs();
    if sHostOs in [ 'linux',]:
        sGdb = '/usr/bin/gdb';
        if not os.path.isfile(sGdb): sGdb = '/usr/local/bin/gdb';
        if not os.path.isfile(sGdb): sGdb = 'gdb';
        aasCmd = [
            [ sGdb, '-batch',
              '-ex', 'set pagination off',
              '-ex', 'thread apply all bt',
              '-ex', 'info proc mapping',
              '-ex', 'info sharedlibrary',
              '-p', '%u' % (uPid,), ],
        ];
    elif sHostOs == 'darwin':
        # LLDB doesn't work in batch mode when attaching to a process, at least
        # with macOS Sierra (10.12). GDB might not be installed. Use the sample
        # tool instead with a 1 second duration and 1000ms sampling interval to
        # get one stack trace.  For the process mappings use vmmap.
        aasCmd = [
            [ '/usr/bin/sample', '-mayDie', '%u' % (uPid,), '1', '1000', ],
            [ '/usr/bin/vmmap', '%u' % (uPid,), ],
        ];
    elif sHostOs == 'solaris':
        aasCmd = [
            [ '/usr/bin/pstack', '%u' % (uPid,), ],
            [ '/usr/bin/pmap', '%u' % (uPid,), ],
        ];
    else:
        aasCmd = [];

    sInfo = '';
    for asCmd in aasCmd:
        try:
            if fSudo:
                sThisInfo = sudoProcessOutputChecked(asCmd);
            else:
                sThisInfo = processOutputChecked(asCmd);
            if sThisInfo is not None:
                sInfo += sThisInfo;
        except:
            pass;
    if not sInfo:
        sInfo = None;

    return sInfo;


class ProcessInfo(object):
    """Process info."""
    def __init__(self, iPid):
        self.iPid       = iPid;
        self.iParentPid = None;
        self.sImage     = None;
        self.sName      = None;
        self.asArgs     = None;
        self.sCwd       = None;
        self.iGid       = None;
        self.iUid       = None;
        self.iProcGroup = None;
        self.iSessionId = None;

    def loadAll(self):
        """Load all the info."""
        sOs = getHostOs();
        if sOs == 'linux':
            sProc = '/proc/%s/' % (self.iPid,);
            if self.sImage   is None: self.sImage = noxcptReadLink(sProc + 'exe', None);
            if self.sImage   is None:
                self.sImage = noxcptReadFile(sProc + 'comm', None);
                if self.sImage: self.sImage = self.sImage.strip();
            if self.sCwd     is None: self.sCwd   = noxcptReadLink(sProc + 'cwd', None);
            if self.asArgs   is None: self.asArgs = noxcptReadFile(sProc + 'cmdline', '').split('\x00');
        #elif sOs == 'solaris': - doesn't work for root processes, suid proces, and other stuff.
        #    sProc = '/proc/%s/' % (self.iPid,);
        #    if self.sImage   is None: self.sImage = noxcptReadLink(sProc + 'path/a.out', None);
        #    if self.sCwd     is None: self.sCwd   = noxcptReadLink(sProc + 'path/cwd', None);
        else:
            pass;
        if self.sName is None and self.sImage is not None:
            self.sName = self.sImage;

    def windowsGrabProcessInfo(self, oProcess):
        """Windows specific loadAll."""
        try:    self.sName      = oProcess.Properties_("Name").Value;
        except: pass;
        try:    self.sImage     = oProcess.Properties_("ExecutablePath").Value;
        except: pass;
        try:    self.asArgs     = [oProcess.Properties_("CommandLine").Value]; ## @todo split it.
        except: pass;
        try:    self.iParentPid = oProcess.Properties_("ParentProcessId").Value;
        except: pass;
        try:    self.iSessionId = oProcess.Properties_("SessionId").Value;
        except: pass;
        if self.sName is None and self.sImage is not None:
            self.sName = self.sImage;

    def getBaseImageName(self):
        """
        Gets the base image name if available, use the process name if not available.
        Returns image/process base name or None.
        """
        sRet = self.sImage if self.sName is None else self.sName;
        if sRet is None:
            self.loadAll();
            sRet = self.sImage if self.sName is None else self.sName;
            if sRet is None:
                if not self.asArgs:
                    return None;
                sRet = self.asArgs[0];
                if not sRet:
                    return None;
        return os.path.basename(sRet);

    def getBaseImageNameNoExeSuff(self):
        """
        Same as getBaseImageName, except any '.exe' or similar suffix is stripped.
        """
        sRet = self.getBaseImageName();
        if sRet is not None and len(sRet) > 4 and sRet[-4] == '.':
            if (sRet[-4:]).lower() in [ '.exe', '.com', '.msc', '.vbs', '.cmd', '.bat' ]:
                sRet = sRet[:-4];
        return sRet;


def processListAll():
    """
    Return a list of ProcessInfo objects for all the processes in the system
    that the current user can see.
    """
    asProcesses = [];

    sOs = getHostOs();
    if sOs == 'win':
        from win32com.client import GetObject; # pylint: disable=import-error
        oWmi = GetObject('winmgmts:');
        aoProcesses = oWmi.InstancesOf('Win32_Process');
        for oProcess in aoProcesses:
            try:
                iPid = int(oProcess.Properties_("ProcessId").Value);
            except:
                continue;
            oMyInfo = ProcessInfo(iPid);
            oMyInfo.windowsGrabProcessInfo(oProcess);
            asProcesses.append(oMyInfo);
        return asProcesses;

    if sOs in [ 'linux', ]:  # Not solaris, ps gets more info than /proc/.
        try:
            asDirs = os.listdir('/proc');
        except:
            asDirs = [];
        for sDir in asDirs:
            if sDir.isdigit():
                asProcesses.append(ProcessInfo(int(sDir),));
        return asProcesses;

    #
    # The other OSes parses the output from the 'ps' utility.
    #
    asPsCmd = [
        '/bin/ps',          # 0
        '-A',               # 1
        '-o', 'pid=',       # 2,3
        '-o', 'ppid=',      # 4,5
        '-o', 'pgid=',      # 6,7
        '-o', 'sid=',       # 8,9
        '-o', 'uid=',       # 10,11
        '-o', 'gid=',       # 12,13
        '-o', 'comm='       # 14,15
    ];

    if sOs == 'darwin':
        assert asPsCmd[9] == 'sid=';
        asPsCmd[9] = 'sess=';
    elif sOs == 'solaris':
        asPsCmd[0] = '/usr/bin/ps';

    try:
        sRaw = processOutputChecked(asPsCmd);
    except:
        return asProcesses;

    for sLine in sRaw.split('\n'):
        sLine = sLine.lstrip();
        if len(sLine) < 7 or not sLine[0].isdigit():
            continue;

        iField   = 0;
        off      = 0;
        aoFields = [None, None, None, None, None, None, None];
        while iField < 7:
            # Eat whitespace.
            while off < len(sLine) and (sLine[off] == ' ' or sLine[off] == '\t'):
                off += 1;

            # Final field / EOL.
            if iField == 6:
                aoFields[6] = sLine[off:];
                break;
            if off >= len(sLine):
                break;

            # Generic field parsing.
            offStart = off;
            off += 1;
            while off < len(sLine) and sLine[off] != ' ' and sLine[off] != '\t':
                off += 1;
            try:
                if iField != 3:
                    aoFields[iField] = int(sLine[offStart:off]);
                else:
                    aoFields[iField] = long(sLine[offStart:off], 16); # sess is a hex address.
            except:
                pass;
            iField += 1;

        if aoFields[0] is not None:
            oMyInfo = ProcessInfo(aoFields[0]);
            oMyInfo.iParentPid = aoFields[1];
            oMyInfo.iProcGroup = aoFields[2];
            oMyInfo.iSessionId = aoFields[3];
            oMyInfo.iUid       = aoFields[4];
            oMyInfo.iGid       = aoFields[5];
            oMyInfo.sName      = aoFields[6];
            asProcesses.append(oMyInfo);

    return asProcesses;


def processCollectCrashInfo(uPid, fnLog, fnCrashFile):
    """
    Looks for information regarding the demise of the given process.
    """
    sOs = getHostOs();
    if sOs == 'darwin':
        #
        # On darwin we look for crash and diagnostic reports.
        #
        asLogDirs = [
            u'/Library/Logs/DiagnosticReports/',
            u'/Library/Logs/CrashReporter/',
            u'~/Library/Logs/DiagnosticReports/',
            u'~/Library/Logs/CrashReporter/',
        ];
        for sDir in asLogDirs:
            sDir = os.path.expanduser(sDir);
            if not os.path.isdir(sDir):
                continue;
            try:
                asDirEntries = os.listdir(sDir);
            except:
                continue;
            for sEntry in asDirEntries:
                # Only interested in .crash files.
                _, sSuff = os.path.splitext(sEntry);
                if sSuff != '.crash':
                    continue;

                # The pid can be found at the end of the first line.
                sFull = os.path.join(sDir, sEntry);
                try:
                    with open(sFull, 'r') as oFile: # pylint: disable=unspecified-encoding
                        sFirstLine = oFile.readline();
                except:
                    continue;
                if len(sFirstLine) <= 4 or sFirstLine[-2] != ']':
                    continue;
                offPid = len(sFirstLine) - 3;
                while offPid > 1 and sFirstLine[offPid - 1].isdigit():
                    offPid -= 1;
                try:    uReportPid = int(sFirstLine[offPid:-2]);
                except: continue;

                # Does the pid we found match?
                if uReportPid == uPid:
                    fnLog('Found crash report for %u: %s' % (uPid, sFull,));
                    fnCrashFile(sFull, False);
    elif sOs == 'win':
        #
        # Getting WER reports would be great, however we have trouble match the
        # PID to those as they seems not to mention it in the brief reports.
        # Instead we'll just look for crash dumps in C:\CrashDumps (our custom
        # location - see the windows readme for the testbox script) and what
        # the MSDN article lists for now.
        #
        # It's been observed on Windows server 2012 that the dump files takes
        # the form: <processimage>.<decimal-pid>.dmp
        #
        asDmpDirs = [
            u'%SystemDrive%/CrashDumps/',                   # Testboxes.
            u'%LOCALAPPDATA%/CrashDumps/',                  # MSDN example.
            u'%WINDIR%/ServiceProfiles/LocalServices/',     # Local and network service.
            u'%WINDIR%/ServiceProfiles/NetworkSerices/',
            u'%WINDIR%/ServiceProfiles/',
            u'%WINDIR%/System32/Config/SystemProfile/',     # System services.
        ];
        sMatchSuffix = '.%u.dmp' % (uPid,);

        for sDir in asDmpDirs:
            sDir = os.path.expandvars(sDir);
            if not os.path.isdir(sDir):
                continue;
            try:
                asDirEntries = os.listdir(sDir);
            except:
                continue;
            for sEntry in asDirEntries:
                if sEntry.endswith(sMatchSuffix):
                    sFull = os.path.join(sDir, sEntry);
                    fnLog('Found crash dump for %u: %s' % (uPid, sFull,));
                    fnCrashFile(sFull, True);
    elif sOs == 'solaris':
        asDmpDirs = [];
        try:
            sScratchPath = os.environ.get('TESTBOX_PATH_SCRATCH', None);
            asDmpDirs.extend([ sScratchPath ]);
        except:
            pass;
        # Some other useful locations as fallback.
        asDmpDirs.extend([
            u'/var/cores/'
        ]);
        #
        # Solaris by default creates a core file in the directory of the crashing process with the name 'core'.
        #
        # As we need to distinguish the core files correlating to their PIDs and have a persistent storage location,
        # the host needs to be tweaked via:
        #
        # ```coreadm -g /var/cores/core.%f.%p```
        #
        sMatchRegEx = r'core\..*\.%u' % (uPid);
        for sDir in asDmpDirs:
            sDir = os.path.expandvars(sDir);
            if not os.path.isdir(sDir):
                continue;
            try:
                asDirEntries = os.listdir(sDir);
            except:
                continue;
            for sEntry in asDirEntries:
                fnLog('Entry: %s' % (os.path.join(sDir, sEntry)));
                if re.search(sMatchRegEx, sEntry):
                    sFull = os.path.join(sDir, sEntry);
                    fnLog('Found crash dump for %u: %s' % (uPid, sFull,));
                    fnCrashFile(sFull, True);
    else:
        pass; ## TODO
    return None;


#
# Time.
#

#
# The following test case shows how time.time() only have ~ms resolution
# on Windows (tested W10) and why it therefore makes sense to try use
# performance counters.
#
# Note! We cannot use time.clock() as the timestamp must be portable across
#       processes.  See timeout testcase problem on win hosts (no logs).
#       Also, time.clock() was axed in python 3.8 (https://bugs.python.org/issue31803).
#
#import sys;
#import time;
#from common import utils;
#
#atSeries = [];
#for i in xrange(1,160):
#    if i == 159: time.sleep(10);
#    atSeries.append((utils.timestampNano(), long(time.clock() * 1000000000), long(time.time() * 1000000000)));
#
#tPrev = atSeries[0]
#for tCur in atSeries:
#    print 't1=%+22u, %u' % (tCur[0], tCur[0] - tPrev[0]);
#    print 't2=%+22u, %u' % (tCur[1], tCur[1] - tPrev[1]);
#    print 't3=%+22u, %u' % (tCur[2], tCur[2] - tPrev[2]);
#    print '';
#    tPrev = tCur
#
#print 't1=%u' % (atSeries[-1][0] - atSeries[0][0]);
#print 't2=%u' % (atSeries[-1][1] - atSeries[0][1]);
#print 't3=%u' % (atSeries[-1][2] - atSeries[0][2]);

g_fWinUseWinPerfCounter           = sys.platform == 'win32';
g_fpWinPerfCounterFreq            = None;
g_oFuncwinQueryPerformanceCounter = None;

def _winInitPerfCounter():
    """ Initializes the use of performance counters. """
    global g_fWinUseWinPerfCounter, g_fpWinPerfCounterFreq, g_oFuncwinQueryPerformanceCounter

    uFrequency = ctypes.c_ulonglong(0);
    if ctypes.windll.kernel32.QueryPerformanceFrequency(ctypes.byref(uFrequency)):
        if uFrequency.value >= 1000:
            #print 'uFrequency = %s' % (uFrequency,);
            #print 'type(uFrequency) = %s' % (type(uFrequency),);
            g_fpWinPerfCounterFreq = float(uFrequency.value);

            # Check that querying the counter works too.
            global g_oFuncwinQueryPerformanceCounter
            g_oFuncwinQueryPerformanceCounter = ctypes.windll.kernel32.QueryPerformanceCounter;
            uCurValue = ctypes.c_ulonglong(0);
            if g_oFuncwinQueryPerformanceCounter(ctypes.byref(uCurValue)):
                if uCurValue.value > 0:
                    return True;
    g_fWinUseWinPerfCounter = False;
    return False;

def _winFloatTime():
    """ Gets floating point time on windows. """
    if g_fpWinPerfCounterFreq is not None or _winInitPerfCounter():
        uCurValue = ctypes.c_ulonglong(0);
        if g_oFuncwinQueryPerformanceCounter(ctypes.byref(uCurValue)):
            return float(uCurValue.value) / g_fpWinPerfCounterFreq;
    return time.time();

def timestampNano():
    """
    Gets a nanosecond timestamp.
    """
    if g_fWinUseWinPerfCounter is True:
        return long(_winFloatTime() * 1000000000);
    return long(time.time() * 1000000000);

def timestampMilli():
    """
    Gets a millisecond timestamp.
    """
    if g_fWinUseWinPerfCounter is True:
        return long(_winFloatTime() * 1000);
    return long(time.time() * 1000);

def timestampSecond():
    """
    Gets a second timestamp.
    """
    if g_fWinUseWinPerfCounter is True:
        return long(_winFloatTime());
    return long(time.time());

def secondsSinceUnixEpoch():
    """
    Returns unix time, floating point second count since 1970-01-01T00:00:00Z
    """
    ## ASSUMES This returns unix epoch time on all systems we care about...
    return time.time();

def getTimePrefix():
    """
    Returns a timestamp prefix, typically used for logging. UTC.
    """
    try:
        oNow = datetime.datetime.utcnow();
        sTs = '%02u:%02u:%02u.%06u' % (oNow.hour, oNow.minute, oNow.second, oNow.microsecond);
    except:
        sTs = 'getTimePrefix-exception';
    return sTs;

def getTimePrefixAndIsoTimestamp():
    """
    Returns current UTC as log prefix and iso timestamp.
    """
    try:
        oNow = datetime.datetime.utcnow();
        sTsPrf = '%02u:%02u:%02u.%06u' % (oNow.hour, oNow.minute, oNow.second, oNow.microsecond);
        sTsIso = formatIsoTimestamp(oNow);
    except:
        sTsPrf = sTsIso = 'getTimePrefix-exception';
    return (sTsPrf, sTsIso);

class UtcTzInfo(datetime.tzinfo):
    """UTC TZ Info Class"""
    def utcoffset(self, _):
        return datetime.timedelta(0);
    def tzname(self, _):
        return "UTC";
    def dst(self, _):
        return datetime.timedelta(0);

class GenTzInfo(datetime.tzinfo):
    """Generic TZ Info Class"""
    def __init__(self, offInMin):
        datetime.tzinfo.__init__(self);
        self.offInMin = offInMin;
    def utcoffset(self, _):
        return datetime.timedelta(minutes = self.offInMin);
    def tzname(self, _):
        if self.offInMin >= 0:
            return "+%02d%02d" % (self.offInMin // 60, self.offInMin % 60);
        return "-%02d%02d" % (-self.offInMin // 60, -self.offInMin % 60);
    def dst(self, _):
        return datetime.timedelta(0);

def formatIsoTimestamp(oNow):
    """Formats the datetime object as an ISO timestamp."""
    assert oNow.tzinfo is None or isinstance(oNow.tzinfo, UtcTzInfo);
    sTs = '%s.%09uZ' % (oNow.strftime('%Y-%m-%dT%H:%M:%S'), oNow.microsecond * 1000);
    return sTs;

def getIsoTimestamp():
    """Returns the current UTC timestamp as a string."""
    return formatIsoTimestamp(datetime.datetime.utcnow());

def formatShortIsoTimestamp(oNow):
    """Formats the datetime object as an ISO timestamp, but w/o microseconds."""
    assert oNow.tzinfo is None or isinstance(oNow.tzinfo, UtcTzInfo);
    return oNow.strftime('%Y-%m-%dT%H:%M:%SZ');

def getShortIsoTimestamp():
    """Returns the current UTC timestamp as a string, but w/o microseconds."""
    return formatShortIsoTimestamp(datetime.datetime.utcnow());

def convertDateTimeToZulu(oDateTime):
    """ Converts oDateTime to zulu time if it has timezone info. """
    if oDateTime.tzinfo is not None:
        oDateTime = oDateTime.astimezone(UtcTzInfo());
    else:
        oDateTime = oDateTime.replace(tzinfo = UtcTzInfo());
    return oDateTime;

def parseIsoTimestamp(sTs):
    """
    Parses a typical ISO timestamp, returing a datetime object, reasonably
    forgiving, but will throw weird indexing/conversion errors if the input
    is malformed.
    """
    # YYYY-MM-DD
    iYear  = int(sTs[0:4]);
    assert(sTs[4] == '-');
    iMonth = int(sTs[5:7]);
    assert(sTs[7] == '-');
    iDay   = int(sTs[8:10]);

    # Skip separator
    sTime = sTs[10:];
    while sTime[0] in 'Tt \t\n\r':
        sTime = sTime[1:];

    # HH:MM[:SS]
    iHour = int(sTime[0:2]);
    assert(sTime[2] == ':');
    iMin  = int(sTime[3:5]);
    if sTime[5] == ':':
        iSec = int(sTime[6:8]);

        # Fraction?
        offTime = 8;
        iMicroseconds = 0;
        if offTime < len(sTime) and sTime[offTime] in '.,':
            offTime += 1;
            cchFraction = 0;
            while offTime + cchFraction < len(sTime) and sTime[offTime + cchFraction] in '0123456789':
                cchFraction += 1;
            if cchFraction > 0:
                iMicroseconds = int(sTime[offTime : (offTime + cchFraction)]);
                offTime += cchFraction;
                while cchFraction < 6:
                    iMicroseconds *= 10;
                    cchFraction += 1;
                while cchFraction > 6:
                    iMicroseconds = iMicroseconds // 10;
                    cchFraction -= 1;

    else:
        iSec          = 0;
        iMicroseconds = 0;
        offTime       = 5;

    # Naive?
    if offTime >= len(sTime):
        return datetime.datetime(iYear, iMonth, iDay, iHour, iMin, iSec, iMicroseconds);

    # Zulu?
    if offTime >= len(sTime) or sTime[offTime] in 'Zz':
        return datetime.datetime(iYear, iMonth, iDay, iHour, iMin, iSec, iMicroseconds, tzinfo = UtcTzInfo());

    # Some kind of offset afterwards, and strptime is useless. sigh.
    if sTime[offTime] in '+-':
        chSign = sTime[offTime];
        offTime += 1;
        cMinTz = int(sTime[offTime : (offTime + 2)]) * 60;
        offTime += 2;
        if offTime  < len(sTime) and sTime[offTime] in ':':
            offTime += 1;
        if offTime + 2 <= len(sTime):
            cMinTz += int(sTime[offTime : (offTime + 2)]);
            offTime += 2;
        assert offTime == len(sTime);
        if chSign == '-':
            cMinTz = -cMinTz;
        return datetime.datetime(iYear, iMonth, iDay, iHour, iMin, iSec, iMicroseconds, tzinfo = GenTzInfo(cMinTz));
    assert False, sTs;
    return datetime.datetime(iYear, iMonth, iDay, iHour, iMin, iSec, iMicroseconds);

def normalizeIsoTimestampToZulu(sTs):
    """
    Takes a iso timestamp string and normalizes it (basically parseIsoTimestamp
    + convertDateTimeToZulu + formatIsoTimestamp).
    Returns ISO tiemstamp string.
    """
    return formatIsoTimestamp(convertDateTimeToZulu(parseIsoTimestamp(sTs)));

def getLocalHourOfWeek():
    """ Local hour of week (0 based). """
    oNow = datetime.datetime.now();
    return (oNow.isoweekday() - 1) * 24 + oNow.hour;


def formatIntervalSeconds(cSeconds):
    """ Format a seconds interval into a nice 01h 00m 22s string  """
    # Two simple special cases.
    if cSeconds < 60:
        return '%ss' % (cSeconds,);
    if cSeconds < 3600:
        cMins = cSeconds // 60;
        cSecs = cSeconds % 60;
        if cSecs == 0:
            return '%sm' % (cMins,);
        return '%sm %ss' % (cMins, cSecs,);

    # Generic and a bit slower.
    cDays     = cSeconds // 86400;
    cSeconds %= 86400;
    cHours    = cSeconds // 3600;
    cSeconds %= 3600;
    cMins     = cSeconds // 60;
    cSecs     = cSeconds % 60;
    sRet = '';
    if cDays > 0:
        sRet = '%sd ' % (cDays,);
    if cHours > 0:
        sRet += '%sh ' % (cHours,);
    if cMins > 0:
        sRet += '%sm ' % (cMins,);
    if cSecs > 0:
        sRet += '%ss ' % (cSecs,);
    assert sRet; assert sRet[-1] == ' ';
    return sRet[:-1];

def formatIntervalSeconds2(oSeconds):
    """
    Flexible input version of formatIntervalSeconds for use in WUI forms where
    data is usually already string form.
    """
    if isinstance(oSeconds, (int, long)):
        return formatIntervalSeconds(oSeconds);
    if not isString(oSeconds):
        try:
            lSeconds = long(oSeconds);
        except:
            pass;
        else:
            if lSeconds >= 0:
                return formatIntervalSeconds2(lSeconds);
    return oSeconds;

def parseIntervalSeconds(sString):
    """
    Reverse of formatIntervalSeconds.

    Returns (cSeconds, sError), where sError is None on success.
    """

    # We might given non-strings, just return them without any fuss.
    if not isString(sString):
        if isinstance(sString, (int, long)) or sString is None:
            return (sString, None);
        ## @todo time/date objects?
        return (int(sString), None);

    # Strip it and make sure it's not empty.
    sString = sString.strip();
    if not sString:
        return (0, 'Empty interval string.');

    #
    # Split up the input into a list of 'valueN, unitN, ...'.
    #
    # Don't want to spend too much time trying to make re.split do exactly what
    # I need here, so please forgive the extra pass I'm making here.
    #
    asRawParts = re.split(r'\s*([0-9]+)\s*([^0-9,;]*)[\s,;]*', sString);
    asParts    = [];
    for sPart in asRawParts:
        sPart = sPart.strip();
        if sPart:
            asParts.append(sPart);
    if not asParts:
        return (0, 'Empty interval string or something?');

    #
    # Process them one or two at the time.
    #
    cSeconds   = 0;
    asErrors   = [];
    i          = 0;
    while i < len(asParts):
        sNumber = asParts[i];
        i += 1;
        if sNumber.isdigit():
            iNumber = int(sNumber);

            sUnit = 's';
            if i < len(asParts) and not asParts[i].isdigit():
                sUnit = asParts[i];
                i += 1;

            sUnitLower = sUnit.lower();
            if sUnitLower in [ 's', 'se', 'sec', 'second', 'seconds' ]:
                pass;
            elif sUnitLower in [ 'm', 'mi', 'min', 'minute', 'minutes' ]:
                iNumber *= 60;
            elif sUnitLower in [ 'h', 'ho', 'hou', 'hour', 'hours' ]:
                iNumber *= 3600;
            elif sUnitLower in [ 'd', 'da', 'day', 'days' ]:
                iNumber *= 86400;
            elif sUnitLower in [ 'w', 'week', 'weeks' ]:
                iNumber *= 7 * 86400;
            else:
                asErrors.append('Unknown unit "%s".' % (sUnit,));
            cSeconds += iNumber;
        else:
            asErrors.append('Bad number "%s".' % (sNumber,));
    return (cSeconds, None if not asErrors else ' '.join(asErrors));

def formatIntervalHours(cHours):
    """ Format a hours interval into a nice 1w 2d 1h string. """
    # Simple special cases.
    if cHours < 24:
        return '%sh' % (cHours,);

    # Generic and a bit slower.
    cWeeks    = cHours / (7 * 24);
    cHours   %= 7 * 24;
    cDays     = cHours / 24;
    cHours   %= 24;
    sRet = '';
    if cWeeks > 0:
        sRet = '%sw ' % (cWeeks,);
    if cDays > 0:
        sRet = '%sd ' % (cDays,);
    if cHours > 0:
        sRet += '%sh ' % (cHours,);
    assert sRet; assert sRet[-1] == ' ';
    return sRet[:-1];

def parseIntervalHours(sString):
    """
    Reverse of formatIntervalHours.

    Returns (cHours, sError), where sError is None on success.
    """

    # We might given non-strings, just return them without any fuss.
    if not isString(sString):
        if isinstance(sString, (int, long)) or sString is None:
            return (sString, None);
        ## @todo time/date objects?
        return (int(sString), None);

    # Strip it and make sure it's not empty.
    sString = sString.strip();
    if not sString:
        return (0, 'Empty interval string.');

    #
    # Split up the input into a list of 'valueN, unitN, ...'.
    #
    # Don't want to spend too much time trying to make re.split do exactly what
    # I need here, so please forgive the extra pass I'm making here.
    #
    asRawParts = re.split(r'\s*([0-9]+)\s*([^0-9,;]*)[\s,;]*', sString);
    asParts    = [];
    for sPart in asRawParts:
        sPart = sPart.strip();
        if sPart:
            asParts.append(sPart);
    if not asParts:
        return (0, 'Empty interval string or something?');

    #
    # Process them one or two at the time.
    #
    cHours     = 0;
    asErrors   = [];
    i          = 0;
    while i < len(asParts):
        sNumber = asParts[i];
        i += 1;
        if sNumber.isdigit():
            iNumber = int(sNumber);

            sUnit = 'h';
            if i < len(asParts) and not asParts[i].isdigit():
                sUnit = asParts[i];
                i += 1;

            sUnitLower = sUnit.lower();
            if sUnitLower in [ 'h', 'ho', 'hou', 'hour', 'hours' ]:
                pass;
            elif sUnitLower in [ 'd', 'da', 'day', 'days' ]:
                iNumber *= 24;
            elif sUnitLower in [ 'w', 'week', 'weeks' ]:
                iNumber *= 7 * 24;
            else:
                asErrors.append('Unknown unit "%s".' % (sUnit,));
            cHours += iNumber;
        else:
            asErrors.append('Bad number "%s".' % (sNumber,));
    return (cHours, None if not asErrors else ' '.join(asErrors));


#
# Introspection.
#

def getCallerName(oFrame=None, iFrame=2):
    """
    Returns the name of the caller's caller.
    """
    if oFrame is None:
        try:
            raise Exception();
        except:
            oFrame = sys.exc_info()[2].tb_frame.f_back;
        while iFrame > 1:
            if oFrame is not None:
                oFrame = oFrame.f_back;
            iFrame = iFrame - 1;
    if oFrame is not None:
        sName = '%s:%u' % (oFrame.f_code.co_name, oFrame.f_lineno);
        return sName;
    return "unknown";


def getXcptInfo(cFrames = 1):
    """
    Gets text detailing the exception. (Good for logging.)
    Returns list of info strings.
    """

    #
    # Try get exception info.
    #
    try:
        oType, oValue, oTraceback = sys.exc_info();
    except:
        oType = oValue = oTraceback = None;
    if oType is not None:

        #
        # Try format the info
        #
        asRet = [];
        try:
            try:
                asRet = asRet + traceback.format_exception_only(oType, oValue);
                asTraceBack = traceback.format_tb(oTraceback);
                if cFrames is not None and cFrames <= 1:
                    asRet.append(asTraceBack[-1]);
                else:
                    asRet.append('Traceback:')
                    for iFrame in range(min(cFrames, len(asTraceBack))):
                        asRet.append(asTraceBack[-iFrame - 1]);
                    asRet.append('Stack:')
                    asRet = asRet + traceback.format_stack(oTraceback.tb_frame.f_back, cFrames);
            except:
                asRet.append('internal-error: Hit exception #2! %s' % (traceback.format_exc(),));

            if not asRet:
                asRet.append('No exception info...');
        except:
            asRet.append('internal-error: Hit exception! %s' % (traceback.format_exc(),));
    else:
        asRet = ['Couldn\'t find exception traceback.'];
    return asRet;


def getObjectTypeName(oObject):
    """
    Get the type name of the given object.
    """
    if oObject is None:
        return 'None';

    # Get the type object.
    try:
        oType = type(oObject);
    except:
        return 'type-throws-exception';

    # Python 2.x only: Handle old-style object wrappers.
    if sys.version_info[0] < 3:
        try:
            from types import InstanceType;     # pylint: disable=no-name-in-module
            if oType == InstanceType:
                oType = oObject.__class__;
        except:
            pass;

    # Get the name.
    try:
        return oType.__name__;
    except:
        return '__type__-throws-exception';


def chmodPlusX(sFile):
    """
    Makes the specified file or directory executable.
    Returns success indicator, no exceptions.

    Note! Symbolic links are followed and the target will be changed.
    """
    try:
        oStat = os.stat(sFile);
    except:
        return False;
    try:
        os.chmod(sFile, oStat.st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH);
    except:
        return False;
    return True;


#
# TestSuite stuff.
#

def isRunningFromCheckout(cScriptDepth = 1):
    """
    Checks if we're running from the SVN checkout or not.
    """

    try:
        sFile = __file__;
        cScriptDepth = 1;
    except:
        sFile = sys.argv[0];

    sDir = os.path.abspath(sFile);
    while cScriptDepth >= 0:
        sDir = os.path.dirname(sDir);
        if    os.path.exists(os.path.join(sDir, 'Makefile.kmk')) \
           or os.path.exists(os.path.join(sDir, 'Makefile.kup')):
            return True;
        cScriptDepth -= 1;

    return False;


#
# Bourne shell argument fun.
#


def argsSplit(sCmdLine):
    """
    Given a bourne shell command line invocation, split it up into arguments
    assuming IFS is space.
    Returns None on syntax error.
    """
    ## @todo bourne shell argument parsing!
    return sCmdLine.split(' ');

def argsGetFirst(sCmdLine):
    """
    Given a bourne shell command line invocation, get return the first argument
    assuming IFS is space.
    Returns None on invalid syntax, otherwise the parsed and unescaped argv[0] string.
    """
    asArgs = argsSplit(sCmdLine);
    if not asArgs:
        return None;

    return asArgs[0];

#
# String helpers.
#

def stricmp(sFirst, sSecond):
    """
    Compares to strings in an case insensitive fashion.

    Python doesn't seem to have any way of doing the correctly, so this is just
    an approximation using lower.
    """
    if sFirst == sSecond:
        return 0;
    sLower1 = sFirst.lower();
    sLower2 = sSecond.lower();
    if sLower1 == sLower2:
        return 0;
    if sLower1 < sLower2:
        return -1;
    return 1;


## RT_C_IS_PUNCT
#return (unsigned)ch - 0x21U < 15U /* 21..2f */
#    || (unsigned)ch - 0x3aU <  7U /* 3a..40 */
#    || (unsigned)ch - 0x5bU <  6U /* 5a..60 */
#    || (unsigned)ch - 0x7bU <  4U /* 7b..7e */;
g_dVerCompPunct = {
    #   21..2f:
    '!',
    '"',
    '#',
    '$',
    '%',
    '&',
    '\'',
    '(',
    ')',
    '*',
    '+',
    ',',
    '-',
    '.',
    '/',
    #   3a..40:
    ':',
    ';',
    '<',
    '=',
    '>',
    '?',
    '@',
    #   5a..60:
    '[',
    '\\',
    ']',
    '^',
    '_',
    '`',
    #   7b..7e
    '{',
    '|',
    '}',
    '~',
};

def versionCompare(sVer1, sVer2):
    """
    Compares to version strings in a fashion similar to RTStrVersionCompare.

    Note! The current code is a python translation of RTStrVersionCompare.
    """

    def rtStrVersionParseBlock(sStr):
        """
        return (fNumeric, iVal, sBlock, sVer)
        """

        #
        # Check for end-of-string.
        #
        if not sStr:
            return (False, 0, '', '');

        #
        # Try convert the block to a number the simple way.
        #
        iValue   = 0;
        fNumeric = sStr[0].isdigit();
        if fNumeric:
            cchBlock = 1;
            while cchBlock < len(sStr) and sStr[cchBlock].isdigit():
                cchBlock += 1;
            sBlock = sStr[:cchBlock];
            try:
                iValue = int(sBlock);
            except:
                #print('dbg: int exception: %s' % (sBlock,))
                fNumeric = False
            ## @todo
            #if (RT_FAILURE(rc) || rc == VWRN_NUMBER_TOO_BIG)
            #{
            #    AssertRC(rc);
            #    fNumeric = False;
            #    *pi32Value = 0;
            #}
        else:
            #
            # Find the end of the current string.  Make a special case for SVN
            # revision numbers that immediately follows a release tag string.
            #
            cchBlock = 1;
            while (    cchBlock < len(sStr)
                   and not sStr[cchBlock].isdigit()
                   and sStr[cchBlock] not in g_dVerCompPunct ):
                cchBlock += 1;

            if (    1 < cchBlock < len(sStr)
                and sStr[cchBlock - 1] == 'r'
                and sStr[cchBlock].isdigit()):
                cchBlock -= 1;

            #
            # Translate standard pre release terms to negative values.
            #
            dTerms = {
                "RC":      -100000,
                "PRE":     -200000,
                "GAMMA":   -300000,
                "BETA":    -400000,
                "ALPHA":   -500000,
            };

            sBlock = sStr[:cchBlock];
            iValue = dTerms.get(sBlock.upper(), 0);
            if iValue != 0:
                #
                # Does the prelease term have a trailing number?
                # Add it assuming BETA == BETA1.
                #
                if cchBlock < len(sStr) and sStr[cchBlock].isdigit():
                    offNum = cchBlock;
                    cchBlock += 1;
                    while cchBlock < len(sStr) and sStr[cchBlock].isdigit():
                        cchBlock += 1;

                    try:
                        iTmp = int(sStr[offNum:cchBlock]);
                        if iTmp != 0:
                            iValue += iTmp - 1;
                    except:
                        cchBlock = offNum;
                        #print('dbg: int exception: %s' % (sStr[offNum:cchBlock],))
                    sBlock = sStr[:cchBlock];
                fNumeric = True;

        #
        # Skip trailing punctuation.
        #
        if (    cchBlock < len(sStr)
            and sStr[cchBlock] in g_dVerCompPunct):
            return (fNumeric, iValue, sBlock, sStr[cchBlock + 1:]);
        return (fNumeric, iValue, sBlock, sStr[cchBlock:]);


    #
    # Do a parallel parse of the strings.
    #
    #print('dbg: ver1=%s ver2=%s' % (sVer1, sVer2,));
    while sVer1 or sVer2:
        (fNumeric1, iVal1, sBlock1, sVer1) = rtStrVersionParseBlock(sVer1);
        #print('dbg: 1: %s,%s,%s,%s' % (fNumeric1, iVal1, sBlock1, sVer1));
        (fNumeric2, iVal2, sBlock2, sVer2) = rtStrVersionParseBlock(sVer2);
        #print('dbg: 2: %s,%s,%s,%s' % (fNumeric2, iVal2, sBlock2, sVer2));

        if fNumeric1 and fNumeric2:
            if iVal1 != iVal2:
                return -1 if iVal1 < iVal2 else 1;
        elif (    fNumeric1 != fNumeric2
              and (   (fNumeric1 and iVal1 == 0 and not sBlock2)
                   or (fNumeric2 and iVal2 == 0 and not sBlock1) ) ):
            pass; #/*else: 1.0 == 1.0.0.0.0. */;
        elif (    fNumeric1 != fNumeric2
              and (iVal1 if fNumeric1 else iVal2) < 0):
            #/* Pre-release indicators are smaller than all other strings. */
            return -1 if fNumeric1 else 1;
        else:
            sBlock1  = sBlock1.lower();
            sBlock2  = sBlock2.lower();
            if sBlock1 < sBlock2:
                return -1;
            if sBlock1 > sBlock2:
                return 1;
    return 0;


def formatNumber(lNum, sThousandSep = ' '):
    """
    Formats a decimal number with pretty separators.
    """
    sNum = str(lNum);
    sRet = sNum[-3:];
    off  = len(sNum) - 3;
    while off > 0:
        off -= 3;
        sRet = sNum[(off if off >= 0 else 0):(off + 3)] + sThousandSep + sRet;
    return sRet;


def formatNumberNbsp(lNum):
    """
    Formats a decimal number with pretty separators.
    """
    sRet = formatNumber(lNum);
    return unicode(sRet).replace(' ', u'\u00a0');


def isString(oString):
    """
    Checks if the object is a string object, hiding difference between python 2 and 3.

    Returns True if it's a string of some kind.
    Returns False if not.
    """
    if sys.version_info[0] >= 3:
        return isinstance(oString, str);
    return isinstance(oString, basestring);     # pylint: disable=undefined-variable


def hasNonAsciiCharacters(sText):
    """
    Returns True is specified string has non-ASCII characters, False if ASCII only.
    """
    if isString(sText):
        for ch in sText:
            if ord(ch) >= 128:
                return True;
    else:
        # Probably byte array or some such thing.
        for ch in sText:
            if ch >= 128 or ch < 0:
                return True;
    return False;


#
# Unpacking.
#

def unpackZipFile(sArchive, sDstDir, fnLog, fnError = None, fnFilter = None):
    # type: (string, string, (string) -> None, (string) -> None, (string) -> bool) -> list[string]
    """
    Worker for unpackFile that deals with ZIP files, same function signature.
    """
    import zipfile
    if fnError is None:
        fnError = fnLog;

    fnLog('Unzipping "%s" to "%s"...' % (sArchive, sDstDir));

    # Open it.
    try: oZipFile = zipfile.ZipFile(sArchive, 'r');             # pylint: disable=consider-using-with
    except Exception as oXcpt:
        fnError('Error opening "%s" for unpacking into "%s": %s' % (sArchive, sDstDir, oXcpt,));
        return None;

    # Extract all members.
    asMembers = [];
    try:
        for sMember in oZipFile.namelist():
            if fnFilter is None  or  fnFilter(sMember) is not False:
                if sMember.endswith('/'):
                    os.makedirs(os.path.join(sDstDir, sMember.replace('/', os.path.sep)), 0x1fd); # octal: 0775 (python 3/2)
                else:
                    oZipFile.extract(sMember, sDstDir);
                asMembers.append(os.path.join(sDstDir, sMember.replace('/', os.path.sep)));
    except Exception as oXcpt:
        fnError('Error unpacking "%s" into "%s": %s' % (sArchive, sDstDir, oXcpt));
        asMembers = None;

    # close it.
    try: oZipFile.close();
    except Exception as oXcpt:
        fnError('Error closing "%s" after unpacking into "%s": %s' % (sArchive, sDstDir, oXcpt));
        asMembers = None;

    return asMembers;


## Set if we've replaced tarfile.copyfileobj with __mytarfilecopyfileobj already.
g_fTarCopyFileObjOverriddend = False;

def __mytarfilecopyfileobj(src, dst, length = None, exception = OSError, bufsize = None):
    """ tarfile.copyfileobj with different buffer size (16384 is slow on windows). """
    _ = bufsize;
    if length is None:
        __myshutilcopyfileobj(src, dst, g_cbGoodBufferSize);
    elif length > 0:
        cFull, cbRemainder = divmod(length, g_cbGoodBufferSize);
        for _ in xrange(cFull):
            abBuffer = src.read(g_cbGoodBufferSize);
            dst.write(abBuffer);
            if len(abBuffer) != g_cbGoodBufferSize:
                raise exception('unexpected end of source file');
        if cbRemainder > 0:
            abBuffer = src.read(cbRemainder);
            dst.write(abBuffer);
            if len(abBuffer) != cbRemainder:
                raise exception('unexpected end of source file');


def unpackTarFile(sArchive, sDstDir, fnLog, fnError = None, fnFilter = None):
    # type: (string, string, (string) -> None, (string) -> None, (string) -> bool) -> list[string]
    """
    Worker for unpackFile that deals with tarballs, same function signature.
    """
    import shutil;
    import tarfile;
    if fnError is None:
        fnError = fnLog;

    fnLog('Untarring "%s" to "%s"...' % (sArchive, sDstDir));

    #
    # Default buffer sizes of 16384 bytes is causing too many syscalls on Windows.
    # 60%+ speedup for python 2.7 and 50%+ speedup for python 3.5, both on windows with PDBs.
    # 20%+ speedup for python 2.7 and 15%+ speedup for python 3.5, both on windows skipping PDBs.
    #
    if True is True: # pylint: disable=comparison-with-itself,comparison-of-constants
        __installShUtilHacks(shutil);
        global g_fTarCopyFileObjOverriddend;
        if g_fTarCopyFileObjOverriddend is False:
            g_fTarCopyFileObjOverriddend = True;
            #if sys.hexversion < 0x03060000:
            tarfile.copyfileobj = __mytarfilecopyfileobj;

    #
    # Open it.
    #
    # Note! We not using 'r:*' because we cannot allow seeking compressed files!
    #       That's how we got a 13 min unpack time for VBoxAll on windows (hardlinked pdb).
    #
    try:
        if sys.hexversion >= 0x03060000:
            oTarFile = tarfile.open(sArchive, 'r|*',                                # pylint: disable=consider-using-with
                                    bufsize = g_cbGoodBufferSize, copybufsize = g_cbGoodBufferSize);
        else:
            oTarFile = tarfile.open(sArchive, 'r|*', bufsize = g_cbGoodBufferSize); # pylint: disable=consider-using-with
    except Exception as oXcpt:
        fnError('Error opening "%s" for unpacking into "%s": %s' % (sArchive, sDstDir, oXcpt,));
        return None;

    # Extract all members.
    asMembers = [];
    try:
        for oTarInfo in oTarFile:
            try:
                if fnFilter is None  or  fnFilter(oTarInfo.name) is not False:
                    if oTarInfo.islnk():
                        # Links are trouble, especially on Windows.  We must avoid the falling that will end up seeking
                        # in the compressed tar stream.  So, fall back on shutil.copy2 instead.
                        sLinkFile     = os.path.join(sDstDir, oTarInfo.name.rstrip('/').replace('/', os.path.sep));
                        sLinkTarget   = os.path.join(sDstDir, oTarInfo.linkname.rstrip('/').replace('/', os.path.sep));
                        sParentDir    = os.path.dirname(sLinkFile);
                        try:    os.unlink(sLinkFile);
                        except: pass;
                        if sParentDir and not os.path.exists(sParentDir):
                            os.makedirs(sParentDir);
                        try:    os.link(sLinkTarget, sLinkFile);
                        except: shutil.copy2(sLinkTarget, sLinkFile);
                    else:
                        if oTarInfo.isdir():
                            # Just make sure the user (we) got full access to dirs.  Don't bother getting it 100% right.
                            oTarInfo.mode |= 0x1c0; # (octal: 0700)
                        if hasattr(tarfile, 'tar_filter'):
                            oTarFile.extract(oTarInfo, sDstDir, filter = 'tar');
                        else:
                            oTarFile.extract(oTarInfo, sDstDir);
                    asMembers.append(os.path.join(sDstDir, oTarInfo.name.replace('/', os.path.sep)));
            except Exception as oXcpt:
                fnError('Error unpacking "%s" member "%s" into "%s": %s' % (sArchive, oTarInfo.name, sDstDir, oXcpt));
                for sAttr in [ 'name', 'linkname', 'type', 'mode', 'size', 'mtime', 'uid', 'uname', 'gid', 'gname' ]:
                    fnError('Info: %8s=%s' % (sAttr, getattr(oTarInfo, sAttr),));
                for sFn in [ 'isdir', 'isfile', 'islnk', 'issym' ]:
                    fnError('Info: %8s=%s' % (sFn, getattr(oTarInfo, sFn)(),));
                asMembers = None;
                break;
    except Exception as oXcpt:
        fnError('Error unpacking "%s" into "%s": %s' % (sArchive, sDstDir, oXcpt));
        asMembers = None;

    #
    # Finally, close it.
    #
    try: oTarFile.close();
    except Exception as oXcpt:
        fnError('Error closing "%s" after unpacking into "%s": %s' % (sArchive, sDstDir, oXcpt));
        asMembers = None;

    return asMembers;


def unpackFile(sArchive, sDstDir, fnLog, fnError = None, fnFilter = None):
    # type: (string, string, (string) -> None, (string) -> None, (string) -> bool) -> list[string]
    """
    Unpacks the given file if it has a know archive extension, otherwise do
    nothing.

    fnLog & fnError both take a string parameter.

    fnFilter takes a member name (string) and returns True if it's included
    and False if excluded.

    Returns list of the extracted files (full path) on success.
    Returns empty list if not a supported archive format.
    Returns None on failure.  Raises no exceptions.
    """
    sBaseNameLower = os.path.basename(sArchive).lower();

    #
    # Zip file?
    #
    if sBaseNameLower.endswith('.zip'):
        return unpackZipFile(sArchive, sDstDir, fnLog, fnError, fnFilter);

    #
    # Tarball?
    #
    if   sBaseNameLower.endswith('.tar') \
      or sBaseNameLower.endswith('.tar.gz') \
      or sBaseNameLower.endswith('.tgz') \
      or sBaseNameLower.endswith('.tar.bz2'):
        return unpackTarFile(sArchive, sDstDir, fnLog, fnError, fnFilter);

    #
    # Cannot classify it from the name, so just return that to the caller.
    #
    fnLog('Not unpacking "%s".' % (sArchive,));
    return [];


#
# Misc.
#
def areBytesEqual(oLeft, oRight):
    """
    Compares two byte arrays, strings or whatnot.

    returns true / false accordingly.
    """

    # If both are None, consider them equal (bogus?):
    if oLeft is None and oRight is None:
        return True;

    # If just one is None, they can't match:
    if oLeft is None or oRight is None:
        return False;

    # If both have the same type, use the compare operator of the class:
    if type(oLeft) is type(oRight):
        #print('same type: %s' % (oLeft == oRight,));
        return oLeft == oRight;

    # On the offchance that they're both strings, but of different types.
    if isString(oLeft) and isString(oRight):
        #print('string compare: %s' % (oLeft == oRight,));
        return oLeft == oRight;

    #
    # See if byte/buffer stuff that can be compared directory. If not convert
    # strings to bytes.
    #
    # Note! For 2.x, we must convert both sides to the buffer type or the
    #       comparison may fail despite it working okay in test cases.
    #
    if sys.version_info[0] >= 3:
        if isinstance(oLeft, (bytearray, memoryview, bytes)) and isinstance(oRight, (bytearray, memoryview, bytes)):  # pylint: disable=undefined-variable
            return oLeft == oRight;

        if isString(oLeft):
            try:    oLeft = bytes(oLeft, 'utf-8');
            except: pass;
        if isString(oRight):
            try:    oRight = bytes(oRight, 'utf-8');
            except: pass;
    else:
        if isinstance(oLeft, (bytearray, buffer)) and isinstance(oRight, (bytearray, buffer)): # pylint: disable=undefined-variable
            if isinstance(oLeft, bytearray):
                oLeft  = buffer(oLeft);                     # pylint: disable=redefined-variable-type,undefined-variable
            else:
                oRight = buffer(oRight);                    # pylint: disable=redefined-variable-type,undefined-variable
            #print('buf/byte #1 compare: %s (%s vs %s)' % (oLeft == oRight, type(oLeft), type(oRight),));
            return oLeft == oRight;

        if isString(oLeft):
            try:    oLeft = bytearray(oLeft, 'utf-8');      # pylint: disable=redefined-variable-type
            except: pass;
        if isString(oRight):
            try:    oRight = bytearray(oRight, 'utf-8');    # pylint: disable=redefined-variable-type
            except: pass;

    # Check if we now have the same type for both:
    if type(oLeft) is type(oRight):
        #print('same type now: %s' % (oLeft == oRight,));
        return oLeft == oRight;

    # Check if we now have buffer/memoryview vs bytes/bytesarray again.
    if sys.version_info[0] >= 3:
        if isinstance(oLeft, (bytearray, memoryview, bytes)) and isinstance(oRight, (bytearray, memoryview, bytes)):  # pylint: disable=undefined-variable
            return oLeft == oRight;
    else:
        if isinstance(oLeft, (bytearray, buffer)) and isinstance(oRight, (bytearray, buffer)): # pylint: disable=undefined-variable
            if isinstance(oLeft, bytearray):
                oLeft  = buffer(oLeft);                     # pylint: disable=redefined-variable-type,undefined-variable
            else:
                oRight = buffer(oRight);                    # pylint: disable=redefined-variable-type,undefined-variable
            #print('buf/byte #2 compare: %s (%s vs %s)' % (oLeft == oRight, type(oLeft), type(oRight),));
            return oLeft == oRight;

    # Do item by item comparison:
    if len(oLeft) != len(oRight):
        #print('different length: %s vs %s' % (len(oLeft), len(oRight)));
        return False;
    i = len(oLeft);
    while i > 0:
        i = i - 1;

        iElmLeft = oLeft[i];
        if not isinstance(iElmLeft, int) and not isinstance(iElmLeft, long):
            iElmLeft = ord(iElmLeft);

        iElmRight = oRight[i];
        if not isinstance(iElmRight, int) and not isinstance(iElmRight, long):
            iElmRight = ord(iElmRight);

        if iElmLeft != iElmRight:
            #print('element %d differs: %x %x' % (i, iElmLeft, iElmRight,));
            return False;
    return True;


def calcCrc32OfFile(sFile):
    """
    Simple helper for calculating the CRC32 of a file.

    Throws stuff if the file cannot be opened or read successfully.
    """
    import zlib;

    uCrc32 = 0;
    with open(sFile, 'rb') as oFile: # pylint: disable=unspecified-encoding
        while True:
            oBuf = oFile.read(1024 * 1024);
            if not oBuf:
                break
            uCrc32 = zlib.crc32(oBuf, uCrc32);

    return uCrc32 % 2**32;


#
# Unit testing.
#

# pylint: disable=missing-docstring
# pylint: disable=undefined-variable
class BuildCategoryDataTestCase(unittest.TestCase):
    def testIntervalSeconds(self):
        self.assertEqual(parseIntervalSeconds(formatIntervalSeconds(3600)), (3600, None));
        self.assertEqual(parseIntervalSeconds(formatIntervalSeconds(1209438593)), (1209438593, None));
        self.assertEqual(parseIntervalSeconds('123'), (123, None));
        self.assertEqual(parseIntervalSeconds(123), (123, None));
        self.assertEqual(parseIntervalSeconds(99999999999), (99999999999, None));
        self.assertEqual(parseIntervalSeconds(''), (0, 'Empty interval string.'));
        self.assertEqual(parseIntervalSeconds('1X2'), (3, 'Unknown unit "X".'));
        self.assertEqual(parseIntervalSeconds('1 Y3'), (4, 'Unknown unit "Y".'));
        self.assertEqual(parseIntervalSeconds('1 Z 4'), (5, 'Unknown unit "Z".'));
        self.assertEqual(parseIntervalSeconds('1 hour 2m 5second'), (3725, None));
        self.assertEqual(parseIntervalSeconds('1 hour,2m ; 5second'), (3725, None));

    def testZuluNormalization(self):
        self.assertEqual(normalizeIsoTimestampToZulu('2011-01-02T03:34:25.000000000Z'), '2011-01-02T03:34:25.000000000Z');
        self.assertEqual(normalizeIsoTimestampToZulu('2011-01-02T03:04:25-0030'), '2011-01-02T03:34:25.000000000Z');
        self.assertEqual(normalizeIsoTimestampToZulu('2011-01-02T03:04:25+0030'), '2011-01-02T02:34:25.000000000Z');
        self.assertEqual(normalizeIsoTimestampToZulu('2020-03-20T20:47:39,832312863+01:00'), '2020-03-20T19:47:39.832312000Z');
        self.assertEqual(normalizeIsoTimestampToZulu('2020-03-20T20:47:39,832312863-02:00'), '2020-03-20T22:47:39.832312000Z');

    def testHasNonAsciiChars(self):
        self.assertEqual(hasNonAsciiCharacters(''), False);
        self.assertEqual(hasNonAsciiCharacters('asdfgebASDFKJ@#$)(!@#UNASDFKHB*&$%&)@#(!)@(#!(#$&*#$&%*Y@#$IQWN---00;'), False);
        self.assertEqual(hasNonAsciiCharacters('\x80 '), True);
        self.assertEqual(hasNonAsciiCharacters('\x79 '), False);
        self.assertEqual(hasNonAsciiCharacters(u'12039889y!@#$%^&*()0-0asjdkfhoiuyweasdfASDFnvV'), False);
        self.assertEqual(hasNonAsciiCharacters(u'\u0079'), False);
        self.assertEqual(hasNonAsciiCharacters(u'\u0080'), True);
        self.assertEqual(hasNonAsciiCharacters(u'\u0081 \u0100'), True);
        self.assertEqual(hasNonAsciiCharacters(b'\x20\x20\x20'), False);
        self.assertEqual(hasNonAsciiCharacters(b'\x20\x81\x20'), True);

    def testAreBytesEqual(self):
        self.assertEqual(areBytesEqual(None, None), True);
        self.assertEqual(areBytesEqual(None, ''), False);
        self.assertEqual(areBytesEqual('', ''), True);
        self.assertEqual(areBytesEqual('1', '1'), True);
        self.assertEqual(areBytesEqual('12345', '1234'), False);
        self.assertEqual(areBytesEqual('1234', '1234'), True);
        self.assertEqual(areBytesEqual('1234', b'1234'), True);
        self.assertEqual(areBytesEqual(b'1234', b'1234'), True);
        self.assertEqual(areBytesEqual(b'1234', '1234'), True);
        self.assertEqual(areBytesEqual(b'1234', bytearray([0x31,0x32,0x33,0x34])), True);
        self.assertEqual(areBytesEqual('1234', bytearray([0x31,0x32,0x33,0x34])), True);
        self.assertEqual(areBytesEqual(u'1234', bytearray([0x31,0x32,0x33,0x34])), True);
        self.assertEqual(areBytesEqual(bytearray([0x31,0x32,0x33,0x34]), bytearray([0x31,0x32,0x33,0x34])), True);
        self.assertEqual(areBytesEqual(bytearray([0x31,0x32,0x33,0x34]), '1224'), False);
        self.assertEqual(areBytesEqual(bytearray([0x31,0x32,0x33,0x34]), bytearray([0x31,0x32,0x32,0x34])), False);
        if sys.version_info[0] >= 3:
            pass;
        else:
            self.assertEqual(areBytesEqual(buffer(bytearray([0x30,0x31,0x32,0x33,0x34]), 1),
                                                       bytearray([0x31,0x32,0x33,0x34])), True);
            self.assertEqual(areBytesEqual(buffer(bytearray([0x30,0x31,0x32,0x33,0x34]), 1),
                                                       bytearray([0x99,0x32,0x32,0x34])), False);
            self.assertEqual(areBytesEqual(buffer(bytearray([0x30,0x31,0x32,0x33,0x34]), 1),
                                                buffer(bytearray([0x31,0x32,0x33,0x34,0x34]), 0, 4)), True);
            self.assertEqual(areBytesEqual(buffer(bytearray([0x30,0x31,0x32,0x33,0x34]), 1),
                                                buffer(bytearray([0x99,0x32,0x33,0x34,0x34]), 0, 4)), False);
            self.assertEqual(areBytesEqual(buffer(bytearray([0x30,0x31,0x32,0x33,0x34]), 1), b'1234'), True);
            self.assertEqual(areBytesEqual(buffer(bytearray([0x30,0x31,0x32,0x33,0x34]), 1),  '1234'), True);
            self.assertEqual(areBytesEqual(buffer(bytearray([0x30,0x31,0x32,0x33,0x34]), 1), u'1234'), True);

    kaVerCompTests = (
        # (pszVer1, pszVer2, iResult) - copied from tstRTStrVersion.cpp
        ( "",           "",                 0  ),
        ( "asdf",       "",                 1  ),
        ( "asdf234",    "1.4.5",            1  ),
        ( "12.foo006",  "12.6",             1  ),
        ( "1",          "1",                0  ),
        ( "1",          "100",              -1 ),
        ( "100",        "1",                1  ),
        ( "3",          "4",                -1 ),
        ( "1",          "0.1",              1  ),
        ( "1",          "0.0.0.0.10000",    1  ),
        ( "0100",       "100",              0  ),
        ( "1.0.0",      "1",                0  ),
        ( "1.0.0",      "100.0.0",          -1 ),
        ( "1",          "1.0.3.0",          -1 ),
        ( "1.4.5",      "1.2.3",            1  ),
        ( "1.2.3",      "1.4.5",            -1 ),
        ( "1.2.3",      "4.5.6",            -1 ),
        ( "1.0.4",      "1.0.3",            1  ),
        ( "0.1",        "0.0.1",            1  ),
        ( "0.0.1",      "0.1.1",            -1 ),
        ( "3.1.0",      "3.0.14",           1  ),
        ( "2.0.12",     "3.0.14",           -1 ),
        ( "3.1",        "3.0.22",           1  ),
        ( "3.0.14",     "3.1.0",            -1 ),
        ( "45.63",      "04.560.30",        1  ),
        ( "45.006",     "45.6",             0  ),
        ( "23.206",     "23.06",            1  ),
        ( "23.2",       "23.060",           -1 ),

        ( "VirtualBox-2.0.8-Beta2",     "VirtualBox-2.0.8_Beta3-r12345",    -1 ),
        ( "VirtualBox-2.2.4-Beta2",     "VirtualBox-2.2.2",                  1 ),
        ( "VirtualBox-2.2.4-Beta3",     "VirtualBox-2.2.2-Beta4",            1 ),
        ( "VirtualBox-3.1.8-Alpha1",    "VirtualBox-3.1.8-Alpha1-r61454",   -1 ),
        ( "VirtualBox-3.1.0",           "VirtualBox-3.1.2_Beta1",           -1 ),
        ( "3.1.0_BETA-r12345",          "3.1.2",                            -1 ),
        ( "3.1.0_BETA1r12345",          "3.1.0",                            -1 ),
        ( "3.1.0_BETAr12345",           "3.1.0",                            -1 ),
        ( "3.1.0_BETA-r12345",          "3.1.0",                            -1 ),
        ( "3.1.0_BETA-r12345",          "3.1.0",                            -1 ),
        ( "3.1.0_BETA-r12345",          "3.1.0.0",                          -1 ),
        ( "3.1.0_BETA",                 "3.1.0.0",                          -1 ),
        ( "3.1.0_BETA1",                "3.1.0",                            -1 ),
        ( "3.1.0_BETA-r12345",          "3.1.0r12345",                      -1 ),
        ( "3.1.0_BETA1-r12345",         "3.1.0_BETA-r12345",                 0 ),
        ( "3.1.0_BETA1-r12345",         "3.1.0_BETA1-r12345",                0 ),
        ( "3.1.0_BETA2-r12345",         "3.1.0_BETA1-r12345",                1 ),
        ( "3.1.0_BETA2-r12345",         "3.1.0_BETA999-r12345",             -1 ),
        ( "3.1.0_BETA2",                "3.1.0_ABC",                        -1 ), # ABC isn't indicating a prerelease, BETA does.
        ( "3.1.0_BETA",                 "3.1.0_ATEB",                       -1 ),
        ( "4.0.0_ALPHAr68482",          "4.0.0_ALPHAr68483",                -1 ),
        ( "4.0.0_ALPHA1r68482",         "4.0.0_ALPHAr68482",                 0 ),
        ( "4.0.0_ALPHA-r68482",         "4.0.0_ALPHAr68482",                 0 ),
        ( "4.0.0_ALPHAr68483",          "4.0.0_BETAr68783",                 -1 ),
        ( "4.0.0_ALPHAr68483",          "4.0.0_BETA1r68783",                -1 ),
        ( "4.0.0_ALPHAr68483",          "4.0.0_BETA2r68783",                -1 ),
        ( "4.0.0_ALPHAr68483",          "4.0.0_BETA2r68784",                -1 ),
        ( "4.0.6",                      "4.0.6_Ubuntu",                     -1 ), # Without stripped guest OS string (Ubuntu).
        ( "4.0.6_Windows",              "4.0.6",                             1 ), # Without stripped guest OS string (Windows).
        ( "4.1.6r74567",                "4.1.6r74567",                       0 ),
        ( "4.1.7r74567",                "4.1.6r74567",                       1 ),
        ( "4.1.5r74567",                "4.1.6r74567",                      -1 ),
        ( "4.1.6r74567-ENTERPRISE",     "4.1.6r74567",                       1 ), # The tagged version is "newer".
    );

    def testVersionCompare(self):
        for sVer1, sVer2, iExpectedResult in self.kaVerCompTests:
            self.assertEqual( versionCompare(sVer1, sVer2), iExpectedResult, 'sVer1=%s sVer2=%s' % (sVer1, sVer2,));
            self.assertEqual(-versionCompare(sVer2, sVer1), iExpectedResult, 'sVer2=%s sVer1=%s' % (sVer2, sVer1,)); # pylint: disable=arguments-out-of-order


if __name__ == '__main__':
    unittest.main();
    # not reached.
