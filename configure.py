#!/bin/sh
# -*- coding: utf-8 -*-
# pylint: disable=bare-except
# pylint: disable=consider-using-f-string
# pylint: disable=global-statement
# pylint: disable=line-too-long
# pylint: disable=too-many-lines
# pylint: disable=unnecessary-semicolon
# pylint: disable=import-error
# pylint: disable=import-outside-toplevel
# pylint: disable=invalid-name
# pylint: disable=multiple-statements
# pylint: disable=line-too-long
# $Id: configure.py 114902 2026-08-07 21:57:38Z klaus.espenlaub@oracle.com $
#
# The following checks for the right (i.e. most recent) Python binary available
# and re-starts the script using that binary (like a shell wrapper).
#
# Using a shebang like "#!/bin/env python" on newer Fedora/Debian distros is banned [1]
# and also won't work on other newer distros (Ubuntu >= 23.10), as those only ship
# python3 without a python->python3 symlink anymore.
#
# [1] https://lists.fedoraproject.org/archives/list/devel@lists.fedoraproject.org/message/2PD5RNJRKPN2DVTNGJSBHR5RUSVZSDZI/
''':'
for python_bin in python3 python
do
    type "$python_bin" > /dev/null 2>&1 && exec "$python_bin" "$0" "$@"
done
echo >&2 "ERROR: Python not found! Please install this first in order to run this program."
exit 1
':'''

#
# Configuration script for building VirtualBox.
#
# Requires >= Python 3.6.
#

__copyright__ = \
"""
Copyright (C) 2025-2026 Oracle and/or its affiliates.

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

SPDX-License-Identifier: GPL-3.0-only
"""

#
# The script is designed to check for the presence of various libraries and tools required for building VirtualBox.
# It uses a modular approach with classes like `LibraryCheck`, `ToolCheck` and 'FeatureCheck' to verify the availability
# of dependencies. Those classes are derived from a common CheckBase class.
#
# Each class instance represents a specific library, tool or feature and contains methods to check for its presence,
# validates its version, and provides appropriate feedback.
#
# - LibraryCheck: A class which checks for a certain (system or third-party library) required for building VirtualBox.
# - ToolCheck   : A third party tool (binary, script, ...) required for building VirtualBox or one of its dependencies.
# - FeatureCheck: A feature of VirtualBox which needs further checking in order to get built.
#                 Only use this as a last resort if our regular (in-tree) Makefiles can't (or won't) handle this!
#
# To add a new library check:
# 1. Create an instance of `LibraryCheck` with the required parameters (name, header files, library files, etc.).
# 2. Append this instance to the `g_aoLibs` list.
#
# To add a new tool check:
# 1. Create an instance of `ToolCheck` with the required parameters (name, command names, etc.).
# 2. Append this instance to the `g_aoTools` list.
#
# To add a new feature check:
# 1. Create an instance of `FeatureCheck` with the required parameters (name, etc.).
# 2. Append this instance to the `g_aoFeatures` list.
#
# The script handles different build targets and architectures, and it generates output files like 'AutoConfig.kmk'
# and 'env.sh' / 'env.bat' based on the checks performed.
#
# External Python modules or other dependencies are not allowed!
#

__revision__ = ''.join(c for c in "$Revision: 114902 $" if c.isdigit())

import argparse
import collections;
import ctypes
import datetime
import fnmatch
import glob
import importlib;
import io
import os
import platform
import re
import shlex
import shutil
import signal
import subprocess
import sysconfig # Since Python 3.2.
import sys
import tempfile

# Check for minimum Python version first.
g_uMinPythonVerTuple = (3, 6);
if sys.version_info < g_uMinPythonVerTuple:
    sys.exit(f"Python {g_uMinPythonVerTuple[0]}.{g_uMinPythonVerTuple[1]} or newer is required, found {sys.version_info.major}.{sys.version_info.minor}")

# Handle to log file (if any).
g_fhLog       = None;
g_sScriptPath = os.path.abspath(os.path.dirname(__file__));
g_sScriptName = os.path.basename(__file__);
g_sOutPath    = os.path.join(g_sScriptPath, 'out');

class Log(io.TextIOBase):
    """
    Duplicates output to multiple file-like objects (used for logging and stdout).
    """
    def __init__(self, *files):
        self.asFiles = files;
    def write(self, data):
        """
        Write data to all files.
        """
        for f in self.asFiles:
            f.write(data);
    def flush(self):
        """
        Flushes all files.
        """
        for f in self.asFiles:
            if not f.closed:
                f.flush();

class BuildArch:
    """
    Supported build architectures enumeration.
    These are a subset of the kBuild architectures, (except ‚'any' and 'unknown').
    """
    ANY = "any";
    X86 = "x86";
    AMD64 = "amd64";
    ARM64 = "arm64";
    UNKNOWN = "unknown";

# Map to translate the Python architecture to kBuild architecture.
g_mapPythonArch2BuildArch = {
    "i386": BuildArch.X86,
    "i686": BuildArch.X86,
    "x86_64": BuildArch.AMD64,
    "amd64": BuildArch.AMD64,
    "aarch64": BuildArch.ARM64,
    "arm64": BuildArch.ARM64
};
# Supported build architectures.
g_aeBuildArchs = [ BuildArch.X86, BuildArch.AMD64, BuildArch.ARM64 ];

# Defines the host architecture (pythonic name).
g_sHostArch = platform.machine().lower();
# Solaris detection kludge (skip SPARC for simplicity, ASSUMES Intel).
if 'i86pc' in g_sHostArch:
    g_sHostArch = "x86_64" if "64" in platform.architecture()[0] else "i686";
# Maps host arch to build arch.
g_enmHostArch = g_mapPythonArch2BuildArch.get(g_sHostArch, BuildArch.UNKNOWN);
# Maps Python (interpreter) arch to build arch. Matches g_enmHostArch.
g_enmPythonArch = g_enmHostArch;

class BuildTarget:
    """
    Supported build targets enumeration.
    These represent the kBuild targets (KBUILD_TARGET + KBUILD_HOST).
    """
    ANY = "os-agonstic";
    LINUX = "linux";
    DOS = "dos";
    WINDOWS = "win";
    DARWIN = "darwin";
    SOLARIS = "solaris";
    FREEBSD = "freebsd";
    NETBSD = "netbsd";
    OPENBSD = "openbsd";
    OS2 = "os2";
    UNKNOWN = "unknown";
# Supported build targets.
g_aeBuildTargets = [ BuildTarget.LINUX, BuildTarget.WINDOWS, BuildTarget.DARWIN, BuildTarget.SOLARIS ];

g_fDebug = False;             # Enables debug mode. For development.
g_fContOnErr = False;         # Continue on fatal errors.
g_sFileLog = 'configure.log'; # Log file path.
g_cVerbosity = 0;             # Verbosity level (0=none, 1=min, 5=max).
g_cErrors = 0;                # Number of error messages.
g_asErrors = [];              # List of error messages.
g_cWarnings = 0;              # Number of warning messages.
g_asWarnings = [];            # List of warning messages.

# Defines the host target.
# Note! In kBuild term this is the 'host OS' (KBUILD_HOST).  (In GCC cross-build
#       terms, this is the build OS.)
g_sHostOS = platform.system();
# Maps Python system string to kBuild OS names.
g_enmHostOS = {
    "Linux":    BuildTarget.LINUX,
    "Windows":  BuildTarget.WINDOWS,
    "Darwin":   BuildTarget.DARWIN,
    "Solaris":  BuildTarget.SOLARIS,
    "SunOS":    BuildTarget.SOLARIS,
    "FreeBSD":  BuildTarget.FREEBSD,
    "NetBSD":   BuildTarget.NETBSD,
    "OpenBSD":  BuildTarget.OPENBSD,
    "":         BuildTarget.UNKNOWN
}.get(g_sHostOS, BuildTarget.UNKNOWN);

# The command line arguments. Populated in main().
g_oArgs = None;

class BuildType:
    """
    Supported build types enumeration.
    This resembles the kBuild targets.
    """
    DEBUG = "debug";
    RELEASE = "release";
    PROFILE = "profile";
    ASAN = "asan";
# Supported build types.
g_aeBuildTypes = [ BuildType.DEBUG, BuildType.RELEASE, BuildType.PROFILE, BuildType.ASAN ];

# Maps Visual Studio build architecture to (kBuild dir name, Windows SDK dir name).
g_mapWinVSArch2Dir = {
    BuildArch.X86  : ('x86', 'Hostx86'),
    BuildArch.AMD64: ('x64', 'Hostx64'),
    BuildArch.ARM64: ('arm64', 'Hostarm64')
};

# Maps Visual Studio build architecture to (kBuild dir name, Windows SDK dir name).
g_mapWinSDK10Arch2Dir = {
    BuildArch.X86  : ('x86', 'Hostx86'),
    BuildArch.AMD64: ('x64', 'Hostx64'),
    BuildArch.ARM64: ('arm64', 'Hostx64')
};

class PkgMgr:
    """
    Enumeration for package managers.
    """
    PKGCFG = "pkg-config";
    BREW   = "brew";
    VCPKG  = "vcpkg";

class PkgMgrVarPKGCFG:
    """
    Enumeration for pkg-config variables.
    """
    BINDIR     = "--variable=bindir";
    CFLAGS     = "--cflags";
    INCDIR     = "--cflags-only-I";
    EXECPREFIX = "--variable=exec_prefix";
    LIBS       = "--libs";
    LIBDIR     = "--variable=libdir";
    LIBEXEC    = "--variable=libexecdir";
    PREFIX     = "--variable=prefix";

class PkgMgrVarBREW:
    """
    Enumeration for brew variables.
    """
    PREFIX     = "--prefix";

class PkgMgrVarVCPKG:
    """
    Enumeration for vcpkg variables.
    """
    PREFIX     = "--x-install-root";

class PkgMgrVar:
    """"
    Class which holds the implemented variables for package managers.

    Note: Not all package managers implement all variables.
    """
    BINDIR     = { PkgMgr.PKGCFG: PkgMgrVarPKGCFG.BINDIR };
    CFLAGS     = { PkgMgr.PKGCFG: PkgMgrVarPKGCFG.CFLAGS };
    INCDIR     = { PkgMgr.PKGCFG: PkgMgrVarPKGCFG.INCDIR };
    EXECPREFIX = { PkgMgr.PKGCFG: PkgMgrVarPKGCFG.EXECPREFIX };
    LIBS       = { PkgMgr.PKGCFG: PkgMgrVarPKGCFG.LIBS };
    LIBDIR     = { PkgMgr.PKGCFG: PkgMgrVarPKGCFG.LIBDIR };
    LIBEXEC    = { PkgMgr.PKGCFG: PkgMgrVarPKGCFG.LIBEXEC };
    PREFIX     = { PkgMgr.PKGCFG: PkgMgrVarPKGCFG.PREFIX,
                   PkgMgr.BREW  : PkgMgrVarBREW.PREFIX,
                   PkgMgr.VCPKG : PkgMgrVarVCPKG.PREFIX };

# Dictionary of path lists to prepend to something.
# See command line arguments '--prepend-<something>-path'.
# Note: The keys must match <something>, e.g. 'programfiles' (for parsing).
g_asPathsPrepend = { 'programfiles' : [], 'ewdk'  : [], 'tools'  : [] };
# Dictionary of path lists to append to something.
# See command line arguments '--append-<something>-path'.
# Note: The keys must match <something>, e.g. 'programfiles' (for parsing).
g_asPathsAppend = { 'programfiles' : [], 'ewdk'  : [], 'tools'  : [] };


def printWarn(sMessage, fLogOnly = False, fDontCount = False):
    """
    Prints warning message to stdout.
    """
    _ = fLogOnly;
    print(f"[WARN]: {sMessage}", file=sys.stdout);
    if not fDontCount:
        globals()['g_cWarnings'] += 1;
        globals()['g_asWarnings'].extend([ sMessage ]);

def printError(sMessage, fLogOnly = False, fDontCount = False):
    """
    Prints an error message to stderr.
    """
    _ = fLogOnly;
    print(f"[ERROR]: {sMessage}", file=sys.stdout);
    if not fDontCount:
        globals()['g_cErrors'] += 1;
        globals()['g_asErrors'].extend([ sMessage ]);

def printVerbose(uVerbosity, sMessage, fLogOnly = False):
    """
    Prints a verbose message if the global verbosity level is high enough.
    """
    _ = fLogOnly;
    if g_cVerbosity >= uVerbosity:
        print(f"=== {sMessage}");

def printLog(sMessage, sPrefix = '---'):
    """
    Prints a log message to the log.
    """
    if g_fhLog:
        g_fhLog.write(f'{sPrefix} {sMessage}\n');

def printLogHeader():
    """
    Prints the log header.
    """
    printLog(f'Log created: {datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")}');
    printLog(f'Revision: {__revision__}');
    printLog(f'Generated by: {g_sScriptName} '  + ' '.join(sys.argv[1:]));
    printLog(f'Working directory: {os.getcwd()}');
    printLog(f'Python version: {sys.version}');
    printLog(f'Platform: {platform.platform()} ({platform.machine()})');
    printLog('');

def pathExists(sPath, fNoLog = False):
    """
    Checks if a path exists.

    Returns success as boolean.
    """
    fRc = sPath and os.path.exists(sPath);
    if not fNoLog:
        printLog('Checking if path exists: ' + (sPath if sPath else '<None>') + (' [YES]' if fRc else ' [NO]'));
    return fRc;

def isDir(sDir, fNoLog = False):
    """
    Checks if a path is a directory.

    Returns success as boolean.
    """
    fRc = sDir and os.path.isdir(sDir);
    if not fNoLog:
        printLog('Checking if directory exists: ' + (sDir if sDir else '<None>') + (' [YES]' if fRc else ' [NO]'));
    return fRc;

def isFile(sFile, fNoLog = False):
    """
    Checks if a path is a file.

    Returns success as boolean.
    """
    fRc = sFile and os.path.isfile(sFile);
    if not fNoLog:
        printLog('Checking if file exists: ' + (sFile if sFile else '<None>') + (' [YES]' if fRc else ' [NO]'));
    return fRc;

def getExeSuff(enmBuildTarget = g_enmHostOS):
    """
    Returns the (dot) executable suffix for a given build target.
    Defaults to the host target.
    """
    if enmBuildTarget != BuildTarget.WINDOWS:
        return '';
    return ".exe";

# Map of library suffixes. Index 0 marks the ending for static libs, index 1 for dynamic ones.
g_mapLibSuffix = {
    BuildTarget.WINDOWS: [ ".lib", ".dll"   ],
    BuildTarget.LINUX:   [ ".a",   ".so"    ],
    BuildTarget.SOLARIS: [ ".a",   ".so"    ],
    BuildTarget.DARWIN:  [ ".a",   ".dylib" ]
}

def getFileLibSuff(sFilename, enmBuildTarget = g_enmHostOS, sDefaultSuff = None):
    """
    Returns the suffix of the given library file name. Must match the given target (host target by default).

    If no suffix found and sDefaultSuff is empty, the dynamic suffix for the given target will be returned.
    """
    asExt = os.path.splitext(sFilename);
    if asExt and len(asExt) >= 1:
        sSuff = asExt[1];
        if not sSuff:
            sSuff = sDefaultSuff if sDefaultSuff else g_mapLibSuffix[enmBuildTarget][1];
        assert sSuff in g_mapLibSuffix[enmBuildTarget];
        return sSuff;
    return '';

def getLibSuff(fStatic = True, enmBuildTarget = g_enmHostOS):
    """
    Returns the (dot) library suffix for a given build target.

    By default static libraries suffixes will be returned.
    Defaults to the host target.
    """
    return g_mapLibSuffix[enmBuildTarget][0 if fStatic else 1];


def hasLibSuff(sFilename, fStatic = True):
    """
    Return True if a given file name has a (static) library suffix,
    or False if not.
    """
    assert sFilename;
    return sFilename.endswith(getLibSuff(fStatic));

def withLibSuff(sFile, fStatic = True):
    """
    Returns sFile with a (static) library suffix.

    With sFile already has a (static) library suffix, the original
    value will be returned.
    """
    assert sFile;
    if hasLibSuff(sFile, fStatic):
        return sFile;
    return sFile + getLibSuff(fStatic);

def stripLibSuff(sLib):
    """
    Strips common static/dynamic library suffixes (UNIX, macOS, Windows) from a filename.

    Returns None if no or empty library is specified.
    """
    if not sLib:
        return None;
    # Handle .so.X[.Y...] versioned shared libraries.
    sLib = re.sub(r'\.so(\.\d+)*$', '', sLib);
    # Handle .dylib (macOS), .dll/.lib (Windows), .a (static).
    sLib = re.sub(r'\.(dylib|dll|lib|a)$', '', sLib, flags = re.IGNORECASE);
    return sLib;

def stripPrefix(s, p):
    """
    Strips a prefix from a string if present. Exists in Python 3.9 as str.removeprefix(p).

    Returns string with prefix removed.
    """
    if s.startswith(p):
        return s[len(p):];
    else:
        return s;

def stripPrefixFromWhitespaceSeparatedString(s, p):
    """
    Strips a prefix from all the parts of a string separated by whitespace.

    Returns string with prefixes removed, now separated by single space.
    """
    return ' '.join([stripPrefix(str(i), p) for i in s.split()]);

def checkWhich(sCmdName, sToolDesc = None, sCustomPath = None, asVersionSwitches = None, fMultiline = False):
    """
    Helper to check for a command in PATH or custom path.

    Returns a tuple of (command path, version [string or array]) or (None, None) if not found.
    The version string can be None if not found.

    If fMultiline is specified, the version returned either represents a string or an array.
    """

    if not sCmdName:
        return None, None;

    sExeSuff = getExeSuff();
    if not sCmdName.endswith(sExeSuff):
        sCmdName += sExeSuff;

    printVerbose(1, f"Checking which '{sCmdName}' ...");

    sCmdPath = None;
    if sCustomPath:
        sCmdPath = os.path.join(sCustomPath, sCmdName);
        if isFile(sCmdPath) and os.access(sCmdPath, os.X_OK):
            printVerbose(1, f"Found '{sCmdName}' at custom path: {sCustomPath}");
        else:
            printVerbose(1, f"'{sCmdName}' not found at custom path: {sCustomPath}");
            return None, None;
    else:
        sCmdPath = shutil.which(sCmdName);
        if sCmdPath:
            printVerbose(1, f"Found '{sCmdName}' at: {sCmdPath}");

    # Try to get version.
    if sCmdPath:
        if not asVersionSwitches:
            asVersionSwitches = [ '--version', '-V', '/?', '/h', '/help', '-version', 'version' ];
        try:
            for sSwitch in asVersionSwitches:
                fWinCreationFlags = 0;
                if g_enmHostOS == BuildTarget.WINDOWS: # Watcom wlink hacks:
                    fWinCreationFlags = getattr(subprocess, 'DETACHED_PROCESS', 0) | getattr(subprocess, 'CREATE_NO_WINDOW', 0);
                oProc = subprocess.run([ sCmdPath, sSwitch ],
                                       stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       check=False, timeout=10, creationflags=fWinCreationFlags);
                if oProc.returncode == 0:
                    oVerRet = None; # String or array (depending on fMultiline).
                    asVer   = None;
                    try:
                        asVer = oProc.stdout.decode('utf-8', 'replace').strip().splitlines();
                    except UnicodeDecodeError:
                        pass;
                    if not asVer: # Some programs (java, for instance) output their version info in stderr.
                        try:
                            asVer = oProc.stderr.decode('utf-8', 'replace').strip().splitlines();
                        except UnicodeDecodeError:
                            pass;
                    if asVer:
                        oVerRet = asVer[0] if not fMultiline else asVer;
                        printVerbose(1, f"Detected version for '{sCmdName}' is: {oVerRet}");
                    else:
                        printVerbose(1, f"No version for '{sCmdName}' returned");
                    return sCmdPath, oVerRet;
            return sCmdPath, None;
        except subprocess.SubprocessError as ex:
            printError(f"Error while checking version of {sToolDesc if sToolDesc else sCmdName}: {str(ex)}");
        return None, None;

    printVerbose(1, f"'{sCmdName}' not found in PATH.");
    return None, None;

def getLinkerArgs(asLibPaths, asLibFiles, enmBuildTarget):
    """
    Returns the linker arguments for the library as a list.

    Returns an empty list for no arguments.
    """
    if not asLibFiles:
        return [];

    asLinkerArg = [];

    if enmBuildTarget == BuildTarget.WINDOWS:
        asLinkerArg.extend([ '/link' ]);

    for sLibCur in asLibFiles:
        if not sLibCur:
            continue;
        if enmBuildTarget == BuildTarget.WINDOWS:
            asLinkerArg.extend([ withLibSuff(sLibCur) ]);
        else:
            sLibPath = os.path.dirname(sLibCur);
            # Absolute path not covered by the library paths?
            if sLibPath and sLibPath not in asLibPaths:
                asLinkerArg += [ f'{sLibCur}' ];
            else:
                sLibName = os.path.basename(sLibCur);
                # Remove 'lib' prefix if present for -l on UNIX-y OSes (libfoo -> -lfoo):
                if sLibName.startswith('lib'):
                    sLibName = sLibName[3:];
                    sLibName = stripLibSuff(sLibName);
                asLinkerArg += [ f'-l{sLibName}' ];
    return asLinkerArg;

def hasCPPHeader(asHeader):
    """
    Rough guess which headers require C++.
    Header selection is based on the library test programs of this file.

    Returns True if it requires C++, False if C only.
    """
    if len(asHeader) == 0:
        return False; # ASSUME C on empty headers.
    asCPPHdr = [ 'c++', 'iostream', 'Qt', 'QtGlobal', 'qcoreapplication.h', 'stdsoap2.h' ];
    for sCurHdr in asHeader:
        if sCurHdr.endswith(('.hpp', '.hxx', '.hh')):
            return True;
        sMatch = [h for h in asCPPHdr if h in sCurHdr];
        if sMatch:
            return True;
    return False;

def getWinError(uCode):
    """
    Returns an error string for a given Windows error code.
    """
    FORMAT_MESSAGE_FROM_SYSTEM    = 0x00001000;
    FORMAT_MESSAGE_IGNORE_INSERTS = 0x00000200;

    wszBuf = ctypes.create_unicode_buffer(2048);
    dwBuf = ctypes.windll.kernel32.FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                                  None, uCode, 0, # Default language.
                                                  wszBuf, len(wszBuf), None);
    if dwBuf:
        return wszBuf.value.strip();
    return f'{uCode:#x}'; # Return the plain error (as hex).

def getSignalName(uSig):
    """
    Returns the name of a POSIX signal.
    """
    try:
        return signal.Signals(uSig).name;
    except ValueError:
        pass;
    return f"SIG{uSig}";

def getSignalDesc(uSig):
    """
    Returns the description of a POSIX signal.
    """
    strsignal = getattr(signal, "strsignal", None);
    if callable(strsignal):
        try:
            sDesc = strsignal(uSig);
            if sDesc:
                return sDesc;
        except ValueError:
            pass;
    return "";

def getPosixError(uCode):
    """
    Returns an error string for a given POSIX error code.
    """
    uSig = None;
    if 0 <= uCode <= 255:
        if uCode > 128: # Signal (128 + signal number)
            uSig = uCode - 128;
        else:
            return f"Exit status {uCode}";
    elif uCode < 0: # Signal
        uSig = abs(uCode);
    sName = getSignalName(uSig);
    sDesc = getSignalDesc(uSig);
    if sDesc:
        return f"Killed by signal {sName} ({sDesc})";
    return f"Killed by signal {sName}";

def compileAndRun(sName, asIncPaths, asLibPaths, asIncFiles, asLibFiles, \
                  sCode, fRun = True, \
                  enmBuildTarget = g_enmHostOS, enmBuildArch = g_enmHostArch,
                  oEnv = None, asCompilerArgs = None, asLinkerArgs = None, asDefines = None, fLog = True, fErrorsAsWarnings = False):
    """
    Compiles and runs (executes) a test program.

    Returns a tuple (Success, StdOut, StdErr).
    """
    _ = enmBuildArch;
    fRet = False;
    sStdOut = sStdErr = None;

    printVerbose(1, f'Compiling and executing "{sName}" ...');

    if enmBuildTarget == BuildTarget.WINDOWS:
        fCPP      = True;
        sCompiler = g_oArgs.config_cpp_compiler;
    else:
        fCPP      = hasCPPHeader(asIncFiles);
        sCompiler = g_oArgs.config_cpp_compiler if fCPP else g_oArgs.config_c_compiler;
    if not sCompiler:
        printError(f'No compiler found for test program "{sName}"');
        return False, None, None;

    if g_fDebug:
        sTempDir = tempfile.gettempdir();
    else:
        sTempDir = tempfile.mkdtemp();

    asFilesToDelete = []; # For cleanup

    sFileSource = os.path.join(sTempDir, "testlib.cpp" if fCPP else "testlib.c");
    asFilesToDelete.extend( [sFileSource] );
    sFileImage  = os.path.join(sTempDir, "testlib" if enmBuildTarget != BuildTarget.WINDOWS else "testlib.exe");
    asFilesToDelete.extend( [sFileImage] );

    with open(sFileSource, "w", encoding = 'utf-8') as fh:
        fh.write(sCode);
    fh.close();

    asCmd = [ sCompiler ];
    oProcEnv = EnvManager(oEnv.env if oEnv else g_oEnv.env);
    if g_fDebug:
        if enmBuildTarget == BuildTarget.WINDOWS:
            asCmd.extend( [ '/showIncludes' ]);
    if enmBuildTarget == BuildTarget.WINDOWS:
        if fCPP: # Stuff required by qt6. Probably doesn't hurt for other stuff either.
            asCmd.extend([ '/Zc:__cplusplus' ]);
            asCmd.extend([ '/std:c++17' ]);
            asCmd.extend([ '/permissive-' ]);
        if asIncPaths:
            for sIncPath in asIncPaths:
                oProcEnv.prependPath('INCLUDE', sIncPath);
        if asLibPaths:
            for sLibPath in asLibPaths:
                oProcEnv.prependPath('LIB', sLibPath);
        if asDefines:
            for sDefine in asDefines:
                asCmd.extend( [ '/D' + sDefine ] );
        asCmd.extend( [ sFileSource ] );
        asCmd.extend( [ '/Fe:' + sFileImage ] );
    else: # Non-Windows
        if asIncPaths:
            for sIncPath in asIncPaths:
                asCmd.extend( [ f'-I{sIncPath}' ] );
        if asLibPaths:
            for sLibPath in asLibPaths:
                asCmd.extend( [ f'-L{sLibPath}' ] );
        if asDefines:
            for sDefine in asDefines:
                asCmd.extend( [ f'-D{sDefine}' ] );
        asCmd.extend( [ sFileSource ] );
        asCmd.extend( [ '-fPIC' ] );
        asCmd.extend( [ '-o', sFileImage ] );
        if asCompilerArgs:
            for sDef in asCompilerArgs:
                asCmd.extend( [ sDef ] );

    asCmd.extend(getLinkerArgs(asLibPaths, asLibFiles, enmBuildTarget));
    if asLinkerArgs:
        asCmd.extend(asLinkerArgs);

    if g_fDebug:
        printLog( 'Process environment:');
        oProcEnv.printLog('    ', [ 'PATH', 'INCLUDE', 'LIB' ]);
        printLog( 'Process command line:');
        printLog(f'    {asCmd}');

    try:
        # Add the compiler's path to PATH.
        oProcEnv.prependPath('PATH', os.path.dirname(sCompiler));
        # Try compiling the test source file.
        oProc = subprocess.run(asCmd, env = oProcEnv.env, stdout = subprocess.PIPE, stderr = subprocess.STDOUT, check = False, timeout = 15);
        if oProc.returncode != 0:
            sStdOut = oProc.stdout.decode("utf-8", errors="ignore");
            if fLog:
                fnLog = printWarn if fErrorsAsWarnings else printError;
                fnLog(f'Compilation of test program for {sName} failed');
                fnLog(f'    { " ".join(asCmd) }', fDontCount = True);
                fnLog(sStdOut, fDontCount = True);
        else:
            printLog(f'Compilation of test program for {sName} successful');
            if not fRun:
                fRet  = True;
            else:
                # Try executing the compiled binary and capture stdout + stderr.
                try:
                    printVerbose(2, f"Executing '{sFileImage}' ...");
                    oProc = subprocess.run([ sFileImage ], env = oProcEnv.env, stdout = subprocess.PIPE, stderr = subprocess.STDOUT, check = False, timeout = 10);
                    if oProc.returncode == 0:
                        printLog(f'Running test program for {sName} successful (exit code 0)');
                        sStdOut = oProc.stdout.decode('utf-8', 'replace').strip();
                        fRet = True;
                    else:
                        sStdErr = oProc.stderr.decode("utf-8", errors="ignore") if oProc.stderr else None;
                        if fLog:
                            fnLog = printError;
                            if enmBuildTarget not in [ BuildTarget.WINDOWS ]:
                                # Some build boxes don't like running X stuff and simply SIGSEGV, so just skip those errors for now.
                                if oProc.returncode == -11 \
                                or oProc.returncode == 139: # 128 + Signal number
                                    printVerbose(1, f'Treating return code {oProc.returncode} as warning instead of error');
                                    printVerbose(1,  'Running on headless build box (no X11, ...)?');
                                    fnLog = printWarn; # Just warn, don't fail.
                                    fRet  = True;      # Ditto.
                            fnLog(f"Execution of test binary for {sName} failed with return code {oProc.returncode}:");
                            if enmBuildTarget == BuildTarget.WINDOWS:
                                fnLog(f"Windows Error { getWinError(oProc.returncode) }", fDontCount = True);
                            else:
                                fnLog(getPosixError(oProc.returncode), fDontCount = True);
                            if sStdErr:
                                fnLog(sStdErr, fDontCount = True);
                except subprocess.SubprocessError as ex:
                    if fLog:
                        printError(f"Execution of test binary for {sName} failed: {str(ex)}");
                        printError(f'    {sFileImage}', fDontCount = True);
    except PermissionError as e:
        printError(f'Compiler not found: {str(e)}');
    except FileNotFoundError as e:
        printError( 'Compiler not found:', fDontCount = True);
        printError(f'    { " ".join(asCmd) }', fDontCount = True);
        printError(str(e));
    except subprocess.SubprocessError as e:
        printError( 'Invoking compiler failed:', fDontCount = True);
        printError(f'    { " ".join(asCmd) }', fDontCount = True);
        printError(str(e));

    # Clean up.
    try:
        if not g_fDebug:
            for sFileToDel in asFilesToDelete:
                try:
                    os.remove(sFileToDel);
                except PermissionError:
                    pass;
            os.rmdir(sTempDir);
    except OSError as ex:
        if fLog:
            printVerbose(1, f"Failed to remove temporary files in '{sTempDir}': {str(ex)}");

    return fRet, sStdOut, sStdErr;

def getPackageLibs(sPackageName):
    """
    Returns a tuple (success, list) of libraries of a given package.
    """
    try:
        #
        # Linux, Solaris and macOS
        #
        if g_enmHostOS in [ BuildTarget.LINUX, BuildTarget.SOLARIS, BuildTarget.DARWIN ]:
            # Use pkg-config on Linux and macOS.
            asCmd = [ 'pkg-config', '--libs', sPackageName ];
            oProc = subprocess.run(asCmd, check = True, stdout = subprocess.PIPE, stderr = subprocess.PIPE, universal_newlines = True);
            if oProc \
            and oProc.returncode == 0:
                asArg = shlex.split(oProc.stdout.strip());
                asLibDir = [];
                asLibName = [];
                asLibs = [];
                # Gather library dirs and names.
                for sCurArg in asArg:
                    if sCurArg.startswith("-L"):
                        asLibDir.append(sCurArg[2:]);
                    elif sCurArg.startswith("-l"):
                        asLibName.append(sCurArg[2:]);
                # For each lib, search in the lib_dirs for its corresponding file.
                for sCurLibName in asLibName:
                    fFound = False;
                    asLibPattern = [ f'lib{sCurLibName}.dylib',
                                     f'lib{sCurLibName}.so',
                                     f'lib{sCurLibName}.a' ];
                    for sCurLibDir in asLibDir:
                        for sCurPattern in asLibPattern:
                            sCandidate = os.path.join(sCurLibDir, sCurPattern);
                            asMatches = glob.glob(sCandidate);
                            if asMatches:
                                asLibs.append(os.path.abspath(asMatches[0]));
                                fFound = True;
                                break;
                        if fFound:
                            break;
                return True, asLibs;
        #
        # Windows
        #
        elif g_enmHostOS == BuildTarget.WINDOWS:
            sVcPkgRoot = g_oEnv['VCPKG_ROOT'];
            if sVcPkgRoot:
                triplet = 'arm64-windows';
                lib_dirs = [
                    os.path.join(sVcPkgRoot, 'installed', triplet, 'lib'),
                    os.path.join(sVcPkgRoot, 'installed', triplet, 'debug', 'lib')
                ]
                libs = []
                for lib_dir in lib_dirs:
                    if isDir(lib_dir):
                        for file in os.listdir(lib_dir):
                            if sPackageName.lower() in file.lower() and file.endswith(('.lib', '.a', '.so', '.dll', '.dylib')):
                                libs.append(os.path.join(lib_dir, file));
                print(f"Found libraries for package '{sPackageName}' in vcpkg: {libs}");
        else:
            raise RuntimeError('Unsupported OS');
    except subprocess.CalledProcessError:
        printVerbose(1, f'Package "{sPackageName}" invalid or not found');
    return False, None;

def getPackageVar(sPackageName, enmPkgMgrVar : PkgMgrVar):
    """
    Returns the package variable for a given package.

    Returns a tuple (Success status, Output [string, list]).
    """
    try:
        if not enmPkgMgrVar:
            return True, '';
        asCmd = None;
        if  g_enmHostOS in [ BuildTarget.LINUX, BuildTarget.SOLARIS, BuildTarget.DARWIN ] \
        and PkgMgr.PKGCFG in enmPkgMgrVar:
            # Use pkg-config on Linux and Solaris.
            # On Darwin we ask pkg-config (needs to be installed through brew) first,
            # then try brew down below.
            asCmd = [ PkgMgr.PKGCFG,
                      enmPkgMgrVar[PkgMgr.PKGCFG],
                      sPackageName ];
        elif g_enmHostOS == BuildTarget.WINDOWS:
            # Detect VCPKG.
            # See: https://learn.microsoft.com/en-us/vcpkg/ + https://vcpkg.io
            sCmd, _ = checkWhich('vcpkg');
            if sCmd:
                sVcPkgRoot = g_oEnv.get('config_vcpkg_root', os.environ['VCPKG_ROOT'] if 'VCPKG_ROOT' in os.environ else None);
                if sVcPkgRoot:
                    printVerbose(1, f"vcpkg found at '{sVcPkgRoot}'");
                    asCmd = [ sCmd ];
                    ## @todo Implement this.
                else:
                    printError('vcpkg found, but VCPKG_ROOT is not defined');
        else:
            raise RuntimeError('Unsupported OS');

        if asCmd:
            oProc = subprocess.run(asCmd, check = False, stdout = subprocess.PIPE, stderr = subprocess.PIPE, universal_newlines = True);
            if  oProc.returncode == 0 \
            and oProc.stdout:
                sRet = oProc.stdout.strip();
                # Output parsing.
                if enmPkgMgrVar == PkgMgrVar.INCDIR:
                    sRet = [f[2:] for f in sRet.split()];
                return True, sRet;

        # If pkg-config fails on Darwin, try asking brew instead.
        if  g_enmHostOS == BuildTarget.DARWIN \
        and PkgMgr.BREW in enmPkgMgrVar:
            sCmd, _ = checkWhich(PkgMgr.BREW);
            if sCmd:
                asCmd = [ sCmd,
                          enmPkgMgrVar[PkgMgr.BREW],
                          sPackageName ];
                oProc = subprocess.run(asCmd, check = False, stdout = subprocess.PIPE, stderr = subprocess.PIPE, universal_newlines = True);
                if  oProc.returncode == 0 \
                and oProc.stdout:
                    sRet = oProc.stdout.strip();
                    return True, sRet;

    except subprocess.CalledProcessError as ex:
        printVerbose(1, f'Package "{sPackageName}" invalid or not found: {ex}');
    return False, None;

def getPackagePath(sPackageName):
    """
    Returns the package path for a given package.
    """
    return getPackageVar(sPackageName, PkgMgrVar.PREFIX);

class CheckBase:
    """
    Base class for checks.
    """
    def __init__(self, sName, enmBuildTarget = g_enmHostOS, enmBuildArch = g_enmHostArch, aeTargets = None, aeArchs = None, aeTargetsExcluded = None):
        """
        Constructor.
        """
        self.sName = sName;
        # Build target (i.e. BuildTarget.LINUX) to use.
        # Defaults to global build target if not explicitly specified.
        # Note! In kBuild term this is the 'target OS' (KBUILD_TARGET).
        self.enmBuildTarget = enmBuildTarget;
        # Build architecture (i.e. BuildArch.amd64) to use.
        # Defaults to global build architecture if not explicitly specified.
        # Note! In kBuild term this is the 'target architecture' (KBUILD_TARGET_ARCH).
        self.enmBuildArch = enmBuildArch;
        # Defines the list of targets which require this component.
        self.aeTargets = [ BuildTarget.ANY ] if aeTargets is None else aeTargets;
        # Defines the list of architectures which require this component.
        self.aeArchs = [ BuildArch.ANY ] if aeArchs is None else aeArchs;
        # Defines the list of excluded targets NOT requiring this component.
        self.aeTargetsExcluded = aeTargetsExcluded if aeTargetsExcluded else [];

    def print(self, sMessage):
        """
        Prints info about the check.
        """
        print(f'{self.sName}: {sMessage}');

    def printLog(self, sMessage):
        """
        Prints info about the check.
        """
        printLog(f'{self.sName}: {sMessage}');

    def printError(self, sMessage, fDontCount = False):
        """
        Prints error about the check.
        """
        printError(f'{self.sName}: {sMessage}', fDontCount = fDontCount);

    def printWarn(self, sMessage, fDontCount = False):
        """
        Prints warning about the check.
        """
        printWarn(f'{self.sName}: {sMessage}', fDontCount = fDontCount);

    def printVerbose(self, uVerbosity, sMessage):
        """
        Prints verbose info about the check.
        """
        printVerbose(uVerbosity, f'{self.sName}: {sMessage}');

    def getVersionFromString(self, sStr, fAsString = False):
        """
        Returns the version (as a tuple or string) from a given string.

        Examples:
            release_1.2.3.txt           -> 1.2.3
            foo-2.3.4                   -> 2.3.4
            v_3.0                       -> 3.0
            myproject-4.10.7-beta       -> 4.10.7
            module1.0.1                 -> 1.0.1
            patch-1.2                   -> 1.2
            something_else              -> (no match)
            5.6.7-hotfix                -> 5.6.7
            stable/v1.2.3.4             -> 1.2.3.4
            xyz-0.0.1-alpha             -> 0.0.1

        Returns None if no version string found.
        """
        tupleCur = None;
        sVerCur  = None;
        rePattern = re.compile(
            r'(?:[vV][_/]?)?'         # Optional 'v' or 'V' with optional separator.
            r'(\d+(?:\.\d+)+)'        # Main version group.
            r'(?:[-_]?).*'            # Optional separators for suffixes (e.g. '-beta', '_whatever-foo')
        );
        oMatch = rePattern.search(sStr);
        if oMatch:
            sVerCur = oMatch.group(1);
            tupleCur = tuple(map(int, sVerCur.split('.')));
        if g_fDebug:
            printVerbose(1, f'getVersionFromString: {sStr} -> {sVerCur}, {tupleCur}');
        if tupleCur and fAsString:
            return '.'.join(str(x) for x in tupleCur);
        return tupleCur;

    def getHighestVersionDir(self, sPath):
        """
        Finds the directory with the highest version number in the given path.

        Returns a tuple of (version, full path) or (None, None) if not found.
        """
        tupleHighest = None;
        sPathHighest = None;
        sVerHighest = None;
        try:
            for sCurEntry in os.listdir(sPath):
                tupleCur = self.getVersionFromString(sCurEntry);
                if tupleCur:
                    if (tupleHighest is None) or (tupleCur > tupleHighest):
                        tupleHighest = tupleCur;
                        sPathHighest = os.path.join(sPath, sCurEntry);
                        sVerHighest = sCurEntry;
            if sPathHighest:
                return (sVerHighest, sPathHighest);
        except FileNotFoundError:
            pass;
        return (None, None);

    ddDirFileCache = {} #< file set cache for for findFiles() indexed by sBaseDir.

    def findFiles(self, sBaseDir, asFilePaths, fAbsolute = False, fStripFilenames = False):
        """
        Finds files in a base directory.

        Returns a tuple containing of
            - a dictionary of all passed-in files describing the found path (if any)
              and other attributes.
            - a boolean: True if all specific files were found, else False.
        """
        assert sBaseDir;
        assert asFilePaths;

        dictFound = {}
        sBaseDir = os.path.abspath(sBaseDir);
        printVerbose(2, f"Finding files in '{sBaseDir}': {asFilePaths}");
        # Check the cache for the directory tree.
        dictAllFilesInfo = self.ddDirFileCache.get(sBaseDir);
        if dictAllFilesInfo is None:
            # Walk directory and build a set of all files' relative paths.
            dictAllFilesInfo = {};
            for sDirRoot, _, asFiles in os.walk(sBaseDir, followlinks = True):
                for sCurFile in asFiles:
                    sFilePathRel = os.path.relpath(os.path.join(sDirRoot, sCurFile), sBaseDir)
                    sFilePathAbs = os.path.join(sBaseDir, sFilePathRel);
                    dictAllFilesInfo[sFilePathRel] = sFilePathAbs;
            self.ddDirFileCache[sBaseDir] = dictAllFilesInfo; # add it to the cache

        for sCurFile in asFilePaths:
            sFilePathNorm = os.path.normpath(sCurFile);
            asMatches = [key for key in dictAllFilesInfo if key.endswith(sFilePathNorm)];
            sFilePathAbs = None;
            sFilePathRel = None;
            if asMatches:
                sFilePathRel = asMatches[0];
                sFilePathAbs = os.path.join(sBaseDir, sFilePathRel);
            if sFilePathAbs and os.path.exists(sFilePathAbs):
                fIsFile = os.path.isfile(sFilePathAbs);
                fIsSymlink = os.path.islink(sFilePathAbs);
                if fAbsolute:
                    sFilePath = sFilePathAbs;
                else:
                    # We need the absolute path if we need to strip the filename.
                    sFilePath = sFilePathAbs if fStripFilenames else sFilePathNorm;
                if fStripFilenames:
                    if  sFilePath.endswith(sFilePathNorm):
                        sFilePath = os.path.normpath(sFilePath[:-len(sFilePathNorm)]);
                    else:
                        sFilePath = None;
                dictFound[sCurFile] = {
                    "found_path": sFilePath,
                    "is_file": fIsFile if sFilePath else None,
                    "is_symlink": fIsSymlink if sFilePath else None
                };
                if g_fDebug:
                    printVerbose(1, f"Found file '{sFilePathNorm}' in '{sFilePath}'");
            else:
                dictFound[sCurFile] = {
                    "found_path": None,
                    "is_file": None,
                    "is_symlink": None
                }
        fAllFound = all(
            v["found_path"] is not None and v["is_file"] is True
            for v in dictFound.values()
        )
        return dictFound, fAllFound;

    def findFilesGetPaths(self, find_files_result):
        """
        Returns a simple list of found file paths from a given findFiles() result.
        """
        dictRes = find_files_result[0] if isinstance(find_files_result, tuple) else find_files_result;
        return [k for k, v in dictRes.items() if v.get("found_path") is not None];

    def findFilesGetUniquePaths(self, find_files_result):
        """
        Returns a simple list of unique found file paths from a given findFiles() result.
        """
        dictRes = find_files_result[0] if isinstance(find_files_result, tuple) else find_files_result;
        asPaths = [];
        setSeen = set();
        for curEntry in dictRes.values():
            sPath = curEntry.get("found_path");
            if sPath is not None and sPath not in setSeen:
                setSeen.add(sPath);
                asPaths.append(sPath);
        return asPaths;

    def isInTarget(self):
        """
        Returns whether the library or tool is handled by the current build target or not.
        """
        fInTarget = (   self.enmBuildTarget in self.aeTargets \
                     or BuildTarget.ANY  in self.aeTargets) \
                and self.enmBuildTarget not in self.aeTargetsExcluded;
        fInTarget = fInTarget and (   self.enmBuildArch in self.aeArchs \
                                   or BuildArch.ANY in self.aeArchs);
        return fInTarget;

    def name2Attr(self):
        """
        Returns a variable-friendly name of the tool / library
        so that it can be used w/ getattr and friends.
        """
        assert(self.sName);
        return self.sName.replace("-", "_");

class LibraryCheck(CheckBase):
    """
    Describes and checks for a library / package.
    """
    def __init__(self, sName, asIncFiles, asLibFiles,
                 enmBuildTarget = g_enmHostOS, enmBuildArch = g_enmHostArch, aeTargets = None, aeArchs = None,
                 sCode = None, fRun = True,
                 asIncPaths = None, asLibPaths = None,
                 fnCallback = None, aeTargetsExcluded = None, fUseInTree = False, sSdkName = None,
                 dictArgsToSetIfFailed = None):
        """
        Constructor.
        """
        super().__init__(sName, enmBuildTarget, enmBuildArch, aeTargets, aeArchs, aeTargetsExcluded);

        # List of library header (.h) files required to be found.
        self.asHdrFiles = asIncFiles or [];
        # List of library shared object / static library names for this library check.
        # The first entry (index 0) is the main library of the check.
        # The following indices are for auxillary libraries needed.
        # Without suffix a dynamic library will be ASSUMED.
        self.asLibFiles = asLibFiles or [];
        # Whether run (execute) the optional test or or just compile it.
        # Defaults to True (compile + execute).
        self.fRun  = fRun;
        # Optional C/C++ test code to compile and execute to proof that the library is installed correctly
        # and in a working shape.
        self.sCode = sCode;
        # Optional callback function to assist handling the library check.
        # Will be executed before anything else. The success value (True / False) will decide whether the
        # library checking process will continue or not.
        self.fnCallback = fnCallback;
        # A string for constructing kBuild / VBox SDK defines (e.g. "MYLIB" -> SDK_MYLIB_INCS / SDK_MYLIB_LIBS).
        # If None, no SDK_ defines will be set.
        self.sSdkName = sSdkName;
        # Defines arguments of g_oArgs to set (e.g. { "config_with_foo : '' }) if the library check failed.
        # The key contains the config argument (must be part of g_oArgs), the value the value to set.
        # A non-empty dictonary makes the library optional.
        self.dictArgsToSetIfFailed = dictArgsToSetIfFailed or {};
        # Whether the library is disabled or not.
        self.fDisabled = False;
        # Base (root) path of the library. None if not (yet) found or not specified.
        self.sRootPath = None;
        # Note: The first entry (index 0) always points to the library include path.
        #       The following indices are for auxillary header paths.
        self.asIncPaths = asIncPaths if asIncPaths else [];
        # Note: The first entry (index 0) always points to the library path.
        #       The following indices are for auxillary library paths.
        self.asLibPaths = asLibPaths if asLibPaths else [];
        # Additional compiler args.
        self.asCompilerArgs = [];
        # Additional linker args.
        self.asLinkerArgs = [];
        # Additional defines.
        self.asDefines = [];
        # Is a tri-state: None if not required (optional or not needed), False if required but not found, True if found.
        self.fHave = None;
        # Whether the library will be used from (in-tree) sources (if available) or not. Default is False.
        # Can be explicitly specified via '--build-<libname>'.
        self.fUseInTree = fUseInTree;
        # Flag if the library is part of our source tree (and thus is source-only).
        # Will be determined at runtime.
        self.fIsInTree = False;
        # Contains the (parsable) version string if detected.
        # Only valid if self.fHave is True.
        self.sVer = None;

    def getTestCode(self):
        """
        Return minimal program *with version print* for header check, per-library logic.
        """
        if not self.asHdrFiles:
            return '';
        if self.sCode:
            if hasCPPHeader(self.asHdrFiles):
                return '#include <iostream>\n' + self.sCode;
            else:
                return '#include <stdio.h>\n' + self.sCode;
        else:
            sIncludes = [f'#include <{h}>' for h in self.asHdrFiles];
            if hasCPPHeader(self.asHdrFiles):
                return '\n'.join(sIncludes) + '#include <iostream>\nint main() {{ std::cout << "<found>" << std::endl; return 0; }}\n';
        return '\n'.join(sIncludes) + '#include <stdio.h>\nint main(void) {{ printf("<found>"); return 0; }}\n';

    def compileAndRun(self, fErrorsAsWarnings):
        """
        Attempts to compile and run (execute) test code using the discovered paths and headers.

        Returns a tuple (Success, StdOut, StdErr).
        """

        sCode = self.getTestCode();
        if not sCode:
            return True, None, None; # No code? Skip.

        # Filter out double entries.
        self.asIncPaths = list(set(self.asIncPaths));
        self.asLibPaths = list(set(self.asLibPaths));

        fRc, sStdOut, sStdErr = compileAndRun(self.sName, \
                                              self.asIncPaths, self.asLibPaths, self.asHdrFiles, self.asLibFiles, \
                                              sCode, self.fRun,
                                              enmBuildTarget = self.enmBuildTarget, enmBuildArch = self.enmBuildArch,
                                              asCompilerArgs = self.asCompilerArgs, asLinkerArgs = self.asLinkerArgs, asDefines = self.asDefines,
                                              fErrorsAsWarnings = fErrorsAsWarnings);
        if fRc and sStdOut:
            self.sVer = sStdOut;
        return fRc, sStdOut, sStdErr;

    def getSdkLibs(self):
        """
        Returns the library references in the form expected by kBuild SDKs.
        """
        asSdkLibs = [];
        for sLib in self.asLibFiles:
            if self.enmBuildTarget == BuildTarget.WINDOWS:
                asSdkLibs.append(withLibSuff(sLib));
            elif os.path.dirname(sLib):
                asSdkLibs.append(sLib);
            else:
                sLibName = os.path.basename(sLib);
                if sLibName.startswith('lib'):
                    sLibName = sLibName[3:];
                asSdkLibs.append(sLibName);
        return asSdkLibs;

    def setArgs(self, args):
        """
        Applies argparse options for disabling and custom paths.
        """
        sAttr = self.name2Attr();
        fUseInTree = getattr(args, f'config_libs_build_{sAttr}', None);
        if fUseInTree:
            self.fUseInTree = fUseInTree; # Only set if explicitly specified on command line -- otherwise take the lib's default.
        self.fDisabled = getattr(args, f'config_libs_disable_{sAttr}', False);
        self.sRootPath = getattr(args, f'config_libs_path_{sAttr}', None);
        if self.sRootPath:
            self.fUseInTree = False; # An explicitly specified root path overrides using in-tree library.

        return True;

    def getRootPath(self):
        """
        Returns the library's root path.

        Will return a tuple (root path, in tree) of the library, or (None, False) if not found.
        This also will take care of custom root paths (if specified).
        """
        sRootPath = self.sRootPath; # A custom path has precedence.
        fInTree   = False;
        if sRootPath:
            self.printVerbose(1, f'Root path was set to custom path: {sRootPath}');
        if  not g_oArgs.config_ignore_in_tree_libs \
        and not sRootPath : # Search for in-tree libs.
            sPath  = os.path.join(g_sScriptPath, 'src', 'libs');
            self.printVerbose(1, f'Searching in-tree library: {sPath}');
            asPath = glob.glob(os.path.join(sPath, self.sName + '*'));
            for sCurDir in asPath:
                sPath = os.path.join(sPath, sCurDir);
                self.printVerbose(1, f'In-tree path found: {sPath}');
                if self.fUseInTree:
                    sRootPath = sPath;
                    fInTree   = True;
                else:
                    self.printVerbose(1, 'In-tree usage is disabled or not specified, ignoring');
        if not sRootPath:
            self.printVerbose(1, 'No root path found');
        else:
            self.printVerbose(1, f'Root path determined: {sRootPath}');
        return sRootPath, fInTree;

    def getToolPath(self):
        """
        Returns the path of the dev tools package.

        Dev tools packages are pre-compiled libraries / SDKs / Frameworks.

        Will return None if not found.
        """

        sRootPath = self.sRootPath; # A custom path has precedence.
        if not sRootPath:
            sToolsDir  = g_oEnv['PATH_DEVTOOLS'];
            if not sToolsDir:
                sToolsDir = os.path.join(g_sScriptPath, 'tools');
            sPath = os.path.join(sToolsDir, f"{ self.enmBuildTarget }.{ self.enmBuildArch }", self.sName);
            if pathExists(sPath):
                _, sPath = self.getHighestVersionDir(sPath);
                if pathExists(sPath):
                    sRootPath = sPath;

        if not sRootPath:
            self.printVerbose(1, 'No root path found');
        else:
            self.printVerbose(1, f'Root path determined: {sRootPath}');
        return sRootPath;

    def isPathInTree(self, sPath):
        """
        Returns True if a given path is part of our source, False if not.
        """
        return sPath.startswith(os.path.join(g_sScriptPath, 'src', 'libs'));

    def findLibFiles(self, sBaseDir, asFiles, fStatic = True, sSuffix = None, fAbsolute = False, fStripFilenames = False):
        """
        Finds a set of (static) library files in a given base directory.

        Returns either the directories of the files found, or the absolute path if fAbsolute is set to True.
        """
        sSuff         = getLibSuff(fStatic) if sSuffix is None else sSuffix;
        asLibFiles    = [ f + sSuff for f in asFiles ];
        asResults, _  = self.findFiles(sBaseDir, asLibFiles, fAbsolute, fStripFilenames);
        return self.findFilesGetPaths(asResults);

    def getIncSearchPaths(self):
        """
        Returns a list of existing search directories for includes.
        """
        self.printVerbose(1, 'Determining include search paths');

        asPaths = [];

        sPath, _ = self.getRootPath();
        if sPath:
            asPaths.extend([ sPath ]);

        #
        # Windows
        #
        if self.enmBuildTarget == BuildTarget.WINDOWS:
            #
            # Try VCPKG first.
            #
            if g_oEnv['VCPKG_ROOT']:
                asPaths.extend([ os.path.join(g_oEnv['VCPKG_ROOT'], 'packages', self.sName) ]);

            #
            # MSVC
            #
            if g_oEnv['INCLUDE']:
                asPaths.extend(re.split(r'[;]', g_oEnv['INCLUDE']));

            #
            # Desperate fallback.
            #
            asRootDrivers = [ d+":" for d in "CDEFGHIJKLMNOPQRSTUVWXYZ" if pathExists(d+":", fNoLog = True) ];
            for r in asRootDrivers:
                asPaths.extend([ os.path.join(r, p) for p in [
                    "\\msys64\\mingw64\\include", "\\msys64\\mingw32\\include", "\\include" ]]);
                asPaths.extend([ r"c:\\Program Files", r"c:\\Program Files (x86)" ]);

        #
        # macOS (Darwin)
        #
        elif self.enmBuildTarget == BuildTarget.DARWIN:
            asPaths.extend([ '/opt/homebrew/include',
                             os.path.join(g_oEnv['VBOX_PATH_MACOSX_SDK'], 'usr', 'include', 'c++', 'v1') ]);

        #
        # Linux
        #
        elif self.enmBuildTarget == BuildTarget.LINUX:
            # Sorted by most likely-ness.
            asPaths.extend([ "/usr/include", "/usr/local/include",
                             "/usr/include/" + self.sName, "/usr/local/include/" + self.sName,
                             "/opt/include", "/opt/local/include" ]);
        #
        # Walk the custom path to guess where the include files are.
        #
        if self.sRootPath:
            for sIncFile in self.asHdrFiles:
                for sRoot, _, asFiles in os.walk(self.sRootPath):
                    if sIncFile in asFiles:
                        asPaths = [ sRoot ] + asPaths;

        return [p for p in asPaths if isDir(p)];

    def getLibSearchPaths(self):
        """
        Returns a list of existing search directories for libraries.
        """
        self.printVerbose(1, 'Determining library search paths');

        asPaths = [];

        sPath, _ = self.getRootPath();
        if sPath:
            asPaths.extend([ sPath ]);
            asPaths.extend([ os.path.join(sPath, 'lib') ]);

        if self.asLibPaths:
            asPaths.extend(self.asLibPaths);

        #
        # Windows
        #
        if self.enmBuildTarget == BuildTarget.WINDOWS:
            #
            # Try VCPKG first.
            #
            if g_oArgs.config_win_vcpkg_root:
                asPaths.extend([ os.path.join(g_oArgs.config_win_vcpkg_root, 'packages', self.sName) ]);
            #
            # MSVC
            #
            if g_oEnv['LIB']:
                asPaths.extend(re.split(r'[;]', g_oEnv['LIB']));

            #
            # Desperate fallback.
            #
            asRootDrives = [d+":" for d in "CDEFGHIJKLMNOPQRSTUVWXYZ" if pathExists(d+":", fNoLog = True)];
            for r in asRootDrives:
                asPaths += [os.path.join(r, p) for p in [
                    '\\msys64\\mingw64\\lib', '\\msys64\\mingw32\\lib', '\\lib']];
                asPaths += [r'c:\\Program Files', r'c:\\Program Files (x86)'];
        #
        # Linux / MacOS / Solaris
        #
        else:  # Linux / MacOS / Solaris
            if self.enmBuildTarget == BuildTarget.LINUX \
            or self.enmBuildTarget == BuildTarget.SOLARIS:
                # Sorted by most likely-ness.
                asPaths.extend([ "/lib", "/lib64",
                                 "/usr/lib", "/usr/local/lib",
                                 "/usr/lib64", "/lib", "/lib64",
                                 "/opt/lib", "/opt/local/lib" ]);
            else: # Darwin
                asPaths.append("/opt/homebrew/lib");
        #
        # Walk the custom path to guess where the lib files are.
        #
        if self.sRootPath:
            for sLibFile in self.asLibFiles:
                for sRoot, _, asFiles in os.walk(self.sRootPath):
                    if sLibFile in asFiles:
                        if isFile(sLibFile):
                            asPaths = [ sRoot ] + asPaths;

        return [p for p in asPaths if pathExists(p)];

    def checkHdr(self):
        """
        Checks for headers in standard/custom include paths.

        Returns a tuple of (True, list of lib paths found) on success or (False, None) on failure.
        """
        self.printVerbose(1, 'Checking headers ...');
        if not self.asHdrFiles:
            return True, [];
        asHdrToSearch = [];
        if self.asHdrFiles:
            asHdrToSearch.extend(self.asHdrFiles);
        if hasCPPHeader(self.asHdrFiles):
            asHdrToSearch.extend([ 'iostream' ]); # Non-library headers must come last.

        setHdrFound = {}; # Key = Header file, Value = Path to header file.

        asSearchPath = self.asIncPaths + self.getIncSearchPaths(); # Own include paths have precedence.
        asSearchPath = list(collections.OrderedDict.fromkeys(asSearchPath)); # Eliminate duplicates to reduce pointless searching.
        self.printVerbose(2, f"Search paths: {asSearchPath}");
        for sCurSearchPath in asSearchPath:
            asResults, _ = self.findFiles(sCurSearchPath, asHdrToSearch, fAbsolute = True, fStripFilenames = True);
            for sResIncFile, dictRes in asResults.items():
                sIncPath = dictRes['found_path'];
                if  sIncPath \
                and sResIncFile not in setHdrFound: # Take the first match found.
                    setHdrFound[sResIncFile] = sIncPath;

        fRc = True;

        asIncPaths = [];
        self.printVerbose(1, 'Found header files:');
        for sHdr, sPath in setHdrFound.items():
            self.printVerbose(1, f'\t{os.path.join(sPath, sHdr)}');
            if sPath != '/usr/include':
                asIncPaths.extend([ sPath ]);

        for sHdr in asHdrToSearch:
            if sHdr not in setHdrFound:
                self.printVerbose(1, f'Header file not found: {sHdr}');

        if fRc:
            self.printVerbose(1, 'All header files found');
        return fRc, asIncPaths if fRc else None;

    def checkLib(self, fStatic = False):
        """
        Checks libraries in standard/custom lib paths and returns the results.

        Returns a tuple of (True, list of lib paths, list of absolute lib file paths) on success
        or (False, None, None) on failure.
        """
        self.printVerbose(1, 'Checking library paths ...');
        if not self.asLibFiles:
            self.printVerbose(1, 'No libraries defined, skipping');
            return True, [], [];
        if self.fUseInTree:
            self.printVerbose(1, 'Library needs to be used in-tree and thus is source only, skipping');
            return True, [], [];

        # On some OSes the compiler / linker should know where to find its stuff,
        # so just return the unmodified lists.
        if self.enmBuildTarget in [ BuildTarget.LINUX, BuildTarget.SOLARIS ]:
            self.printVerbose(1, 'Library paths should be automatically determined by compiler / linker, skipping');
            return True, self.asLibPaths, self.asLibFiles;

        asSearchPath = self.asLibPaths + self.getLibSearchPaths(); # Own lib paths have precedence.
        setLibFound  = {}; # Key = Lib file, Value = Path to lib file.
        asLibToSearch = self.asLibFiles;
        self.printVerbose(2, f"Search paths: {asSearchPath}");
        for sCurSearchPath in asSearchPath:
            for sCurLib in asLibToSearch:
                if hasLibSuff(sCurLib):
                    sPattern = os.path.join(sCurSearchPath, sCurLib);
                else:
                    sPattern = os.path.join(sCurSearchPath, f"{sCurLib}{getLibSuff(fStatic)}*");
                self.printVerbose(2, f"Checking '{sPattern}'");
                for sCurFile in glob.glob(sPattern):
                    if isFile(sCurFile) \
                    or os.path.islink(sCurFile):
                        if sCurLib not in setLibFound:
                            setLibFound[sCurLib] = sCurFile;
                        break;

        fRc = True;

        asLibPaths = [];
        asLibFiles = [];
        self.printVerbose(1, 'Found library files:');
        for sLib, sFile in setLibFound.items():
            self.printVerbose(1, f'\t{sFile}');
            sPath = os.path.dirname(sFile);
            if sPath not in asLibPaths:
                asLibPaths.extend([ sPath ]);
            asLibFiles.extend([ sFile ]);

        for sLib in asLibToSearch:
            if sLib not in setLibFound:
                self.printVerbose(1, f'Library file not found: {sLib}');

        if fRc:
            self.printVerbose(1, 'All libraries found');
            return True, asLibPaths, asLibFiles;

        return False, None, None;

    def performCheck(self):
        """
        Run library detection.

        Returns status:
            - None if not found and not required (optional or not needed)
            - False if required but not found
            - True if found
        """
        if self.fDisabled:
            self.fHave = None;
            return self.fHave;

        self.fHave = False;
        self.print('Performing library check ...');

        # Check if no custom path was specified and we have the lib in-tree.
        sPath, self.fIsInTree = self.getRootPath();
        if sPath:
            self.fHave     = True;
            self.sRootPath = sPath;
            self.sVer      = self.getVersionFromString(os.path.basename(sPath), fAsString = True);

        fRc = True;
        if self.fnCallback:
            fRc = self.fnCallback(self);
        if fRc:
            fRc, self.asIncPaths = self.checkHdr();
            if fRc:
                fRc, self.asLibPaths, self.asLibFiles = self.checkLib();
                if  fRc \
                and not self.fIsInTree:
                    # The compilation is allowed to fail w/o triggering an error if
                    #   - this library is in-tree, as we ASSUME that we only have working libraries in there
                    #   or
                    #   - there are defines to disable the feature.
                    fMayFail = self.fUseInTree or len(self.dictArgsToSetIfFailed) > 0;
                    # Only try to compile libraries which are not in-tree, as we only have sources in-tree, not binaries.
                    fRc, _, _ = self.compileAndRun(fErrorsAsWarnings = fMayFail);
                    if not fRc:
                        self.fHave = None if fMayFail else False;
                    else:
                        self.fHave = True;

                if self.fUseInTree and not self.fIsInTree:
                    self.printWarn('Library needs to be used from in-tree sources but was not detected there -- might lead to build errors');

        ## @todo r=klaus the hack with skipping this for QT6 is fixing the build (the include paths appear wrong) but needs a proper solution.
        if self.fHave and self.sSdkName and self.sSdkName != 'QT6' and not self.fIsInTree:
            g_oEnv.set(f'SDK_{self.sSdkName}_INCS', ' '.join(self.asIncPaths));
            g_oEnv.set(f'SDK_{self.sSdkName}_LIBS', ' '.join(self.getSdkLibs()));

        if not fRc:
            if self.dictArgsToSetIfFailed: # Implies being optional.
                self.printWarn('Library check failed and is optional');
                for oArg, oVal in self.dictArgsToSetIfFailed.items():
                    self.printWarn(f"    - {oArg} -> {oVal if oVal else '<Unset>'}", fDontCount = True);
                    setattr(g_oArgs, oArg, oVal);
                self.fHave = None; # Set to optional.
            else:
                self.printError('Library check failed, but is required (see errors above)');

        return self.fHave;

    def getStatusString(self):
        """
        Return string indicator: yes, no, DISABLED, or - (not checked / disabled / whatever).
        """
        if self.fDisabled:
            return "DISABLED";
        elif self.fHave:
            return "in-tree" if self.fUseInTree else "ok";
        elif self.fHave is None:
            return "?";
        else:
            return "failed";

    def compareStringVersions(self, sVer1, sVer2):
        """
        Compares two string versions and returns the result.

        Returns -1 if sVer1 <  sVer2.
        Returns  1 if sVer2 >  sVer1.
        Returns  0 if sVer1 == sVer2.
        """
        assert sVer1 and sVer2;
        asPartVer1 = [ int(p) for p in sVer1.split('.') ];
        asPartVer2 = [ int(p) for p in sVer2.split('.') ];
        # Optionally normalize lengths (pad shorter with zeros)
        cchLen = max(len(asPartVer1), len(asPartVer2));
        asPartVer1 += [0] * (cchLen - len(asPartVer1));
        asPartVer2 += [0] * (cchLen - len(asPartVer2));
        if asPartVer1 < asPartVer2:
            return -1; # v1 < v2
        elif asPartVer1 > asPartVer2:
            return 1;  # v1 > v2
        return 0;      # v1 == v2

    def checkCallback_libxslt(self):
        """
        Checks for tools required from libxslt.
        """

        # Note: This is a weird one, as the dev tools package
        #        only contains Windows binaries of mixed libraries and no sources. Don't ask me why.
        mapFiles = {
             'xmllint': [ 'VBOX_XMLLINT', 'VBOX_HAVE_XMLLINT' ],
             'xsltproc': [ 'VBOX_XSLTPROC', 'VBOX_HAVE_XSLTPROC' ]
        }

        for sCurFile, asDefs in mapFiles.items():
            sPath = self.sRootPath;
            sBin  = sCurFile + getExeSuff();
            if sPath:
                asFiles = self.findFiles(sPath, [ sBin ]);
                sPath = asFiles[0] if asFiles else None;
            else:
                sPath, _ = checkWhich(sBin);

            if sPath:
                g_oEnv.set(asDefs[0], sPath);
                continue;

            self.printWarn(f"Unable to find '{sCurFile}' binary");
            g_oEnv.set(asDefs[1], '');

        return True;

    def checkCallback_qt(self):
        """
        Tweaks needed for using Qt 6.x.
        """

        sPathBase = None;
        sPathInc = None;
        sPathLib = None;
        sPathBin = None;
        sPathLibExec = None;

        # Check if we have our own pre-compiled Qt in tools first.
        sPathBase = self.getToolPath();
        if sPathBase:
            self.asLibFiles = [ 'libQt6CoreVBox' ];
            g_oEnv.set('VBOX_WITH_ORACLE_QT', '1');

        else:

            #
            # Windows
            #
            # Qt 6.x requires a recent compiler (>= C++17).
            # For MSVC this means at least 14.1 (VS 2017).
            #
            if self.enmBuildTarget == BuildTarget.WINDOWS:
                sCompilerVer = g_oArgs.config_cpp_compiler_ver;
                if self.compareStringVersions(sCompilerVer, "14.1") < 1:
                    self.printError(f'MSVC compiler version too old ({sCompilerVer}), requires at least 15.7 (2017 Update 7)');
                    return False;
            #
            # Linux + Solaris
            #
            elif self.enmBuildTarget== BuildTarget.LINUX:
                self.asLibFiles = [ 'libQt6Core' ];

            #
            # Solaris
            #
            elif self.enmBuildTarget == BuildTarget.SOLARIS:
                self.asLibFiles = [ 'libQt6Core' ];

            #
            # macOS
            #
            elif self.enmBuildTarget == BuildTarget.DARWIN:
                # On macOS we have to ask brew for the Qt installation path.

                # Search for the library file.
                # Note: Ordered by precedence. Do not change!
                asPath = [ sPathBase,
                        getPackagePath('qt@6')[1],
                        '/System/Library',
                        '/Library' ];
                sPathFramework = None;
                for sPathBase in asPath:
                    if not sPathBase: # No custom path? Skip.
                        continue;
                    asLibFile = [ 'Frameworks/QtCore.framework/QtCore',
                                'clang_64/lib/QtCore.framework/QtCore',
                                'lib/QtCore.framework/QtCore' ];
                    for sLibFile in asLibFile:
                        sPath = os.path.join(sPathBase, sLibFile);
                        if isFile(sPath):
                            sPathFramework = os.path.dirname(sPath);
                            self.printVerbose(1, f"Using framework at '{sPathFramework}'");

                            break;
                    if sPathFramework:
                        break;

                if sPathFramework:
                    # We need to clear the library defined the the LibraryCheck definition
                    # -- macOS uses the framework concept instead.
                    self.asLibFiles = [];
                    self.asLibPaths.insert(0, sPathFramework);
                    # Include the framework headers.
                    self.asIncPaths.insert(0, f'{sPathBase}/lib/QtCore.framework/Headers');
                    # More stuff needed in order to get it linked.
                    self.asLinkerArgs.extend([ '-std=c++17', '-framework', 'QtCore', '-F', f'{sPathBase}/lib', '-g', '-O', '-Wall' ]);

        sPkgName = 'Qt6Core'; ## @todo Make the code generic once we have similar SDKs.
        if sPathBase:
            g_oEnv.set( 'VBOX_PATH_QT', sPathBase);
            g_oEnv.set(f'PATH_SDK_{self.sSdkName}', sPathBase);
            sPathBin     = os.path.join(sPathBase, 'bin');
            sPathInc     = os.path.join(sPathBase, 'include');
            sPathLib     = os.path.join(sPathBase, 'lib');
            sPathLibExec = os.path.join(sPathBase, 'libexec');

            if self.enmBuildTarget != BuildTarget.WINDOWS:
                # Tell g++ that we need C++17 -- otherwise Qt6 won't compile.
                # Required for older compilers (i.e. G++ 9.4).
                self.asCompilerArgs.extend([ '-std=c++17' ]);
                # Explicitly set the RPATH, so that our test program can find the dynamic libs.
                self.asCompilerArgs.extend([ f'-Wl,-rpath,{sPathBase}/lib' ]);

        else: # Ask the system.
            _, sPathBin = getPackageVar(sPkgName, PkgMgrVar.BINDIR);
            _, sPathInc = getPackageVar(sPkgName, PkgMgrVar.INCDIR);
            if isinstance(sPathInc, list):
                sPathInc = sPathInc[0]; # Only use the first include path.
            _, sPathLib = getPackageVar(sPkgName, PkgMgrVar.LIBDIR);

        if isDir(sPathBin):
            g_oEnv.set(f'PATH_SDK_{self.sSdkName}_BIN', sPathBin);
        if isDir(sPathInc):
            self.asIncPaths.insert(0, sPathInc);
            g_oEnv.set(f'PATH_SDK_{self.sSdkName}_INC', sPathInc);
        if isDir(sPathLib):
            self.asLibPaths.insert(0, sPathLib);
            g_oEnv.set(f'PATH_SDK_{self.sSdkName}_LIB', sPathLib);
        if isDir(sPathLibExec):
            g_oEnv.set(f'PATH_SDK_{self.sSdkName}_LIBEXEC', sPathLibExec);

        # Tool stuff.
        if isDir(sPathBin):
            g_oEnv.set(f'PATH_TOOL_{self.sSdkName}_BIN', sPathBin);
        if isDir(sPathLibExec):
            g_oEnv.set(f'PATH_TOOL_{self.sSdkName}_LIBEXEC', sPathLibExec);

        return True;

    def __repr__(self):
        return f"{self.getStatusString()}";

class ToolCheck(CheckBase):
    """
    Describes and checks for a build tool.
    """
    def __init__(self, sName, asCmd = None, fnCallback = None, aeTargets = None, aeArchs = None,
                 enmBuildTarget = g_enmHostOS, enmBuildArch = g_enmHostArch,
                 aeTargetsExcluded = None, dictArgsToSetIfFailed = None):
        """
        Constructor.
        """
        super().__init__(sName, enmBuildTarget, enmBuildArch, aeTargets, aeArchs, aeTargetsExcluded);

        # Optional callback function to assist handling the tool check.
        # Will be executed before anything else. The success value (True / False) will decide whether the
        # tool checking process will continue or not.
        self.fnCallback = fnCallback;
        # Defines set set (e.g. { "VBOX_WITH_MYFEATURE : '' }) if the library check failed.
        # The key contains the define, the value the value to set.
        # A non-empty dictonary makes the library optional.
        self.dictArgsToSetIfFailed = dictArgsToSetIfFailed or {};
        # Whether the tool is disabled or not.
        self.fDisabled = False;
        # Absolute root path of the tool found.
        self.sRootPath = None;
        # Represents the overall outcome of the tool check.
        # Is a tri-state: None if not required (optional or not needed), False if required but not found, True if found.
        self.fHave = None;
        # List of command names (binaries) to check for.
        # A tool can have multiple binaries, and all binaries are considered as being required.
        self.asCmd = asCmd;
        # Path to the found command.
        # Only valid if self.fHave is True.
        self.sCmdPath = None;
        # Contains the (parsable) version string if detected.
        # Only valid if self.fHave is True.
        self.sVer = None;

    def setArgs(self, oArgs):
        """
        Apply argparse options for disabling the tool.
        """
        sAttr = self.name2Attr();
        self.fDisabled = getattr(oArgs, f"config_tools_disable_{sAttr}", False);
        self.sRootPath = getattr(oArgs, f"config_tools_path_{sAttr}", None);

        # Sanity checks.
        if self.fDisabled and self.sRootPath:
            self.printError(f"Disabling and setting a root path for tool '{self.sName}' really makes no sense?!");
            return False;

        return True;

    def getRootPath(self):
        """
        Returns the tuple (root path, in tree) of the tool.

        Will return (None, False) if not found.
        """
        sRootPath = self.sRootPath; # A custom path has precedence.
        if  sRootPath \
        and not isDir(sRootPath):   # Use the parent directory if the given path isn't a directory.
            sRootPath = os.path.dirname(sRootPath);

        fInTree = False;
        if not sRootPath: # Search for in-tree tools.
            sPath  = os.path.join(g_sScriptPath, g_oEnv['PATH_DEVTOOLS'] if g_oEnv['PATH_DEVTOOLS'] else 'tools');
            asToolsSubDir = [
                 "common",
                f"{self.enmBuildTarget}.{self.enmBuildArch}"
            ];
            asPath = [];
            for sSubDir in asToolsSubDir:
                asPath.extend( glob.glob(os.path.join(sPath, sSubDir, self.sName + '*')) );
            for sCurDir in asPath:
                _, sRootPath = self.getHighestVersionDir(sCurDir);
                if sRootPath:
                    self.printVerbose(1, f'In-tree path found: {sRootPath}');
                    fInTree = True;
                    break;

        if sRootPath:
            if not isDir(sRootPath):
                self.printError(f"Root path '{sRootPath}' but does not exist or is not a directory");
            else:
                self.printVerbose(1, f'Root path set to: {sRootPath}');
        else:
            self.printVerbose(1, 'No root path found');

        return sRootPath, fInTree;

    def performCheck(self):
        """
        Performs the actual check of the tool.

        Returns status:
            - None if not found and not required (optional or not needed)
            - False if required but not found
            - True if found
        """
        if self.fDisabled:
            self.fHave = None;
            return self.fHave;

        self.fHave = False;
        self.print('Performing tool check ...');

        self.sRootPath, _ = self.getRootPath();

        if self.fnCallback:
            self.fHave = self.fnCallback(self);
        else:
            # Do a simple 'which' to figure out where we might find the binaries.
            for sCmdCur in self.asCmd:
                self.sCmdPath, self.sVer = checkWhich(sCmdCur, self.sName, self.sRootPath);
                self.fHave = self.sCmdPath; # Note: Version is optional.
            # Still not found? Try harder by finding the binaries.
            if  not self.fHave \
            and     self.sRootPath:
                asCmd, _ = self.findFiles(self.sRootPath, self.asCmd);
                for sCmdCur in asCmd:
                    self.sCmdPath, self.sVer = checkWhich(sCmdCur, self.sName, os.path.dirname(sCmdCur));
                    self.fHave = True if self.sCmdPath else False; # Note: Version is optional.

        if not self.fHave:
            if self.dictArgsToSetIfFailed: # Implies being optional.
                self.printWarn('Tool check failed and is optional, disabling dependent features');
                for oArg, oVal in self.dictArgsToSetIfFailed.items():
                    self.printWarn(f"    - {oArg} -> {oVal if oVal else '<Unset>'}", fDontCount = True);
                    setattr(g_oArgs, oArg, oVal);
                self.fHave = None; # Set to optional.
            else:
                self.printError('Tool not found, but is required (see errors above)');

        return self.fHave;

    def getStatusString(self):
        """
        Returns a string for the tool's status.
        """
        if self.fDisabled:
            return 'DISABLED';
        if self.fHave:
            return f'ok ({os.path.basename(self.sCmdPath)})' if self.sCmdPath else 'ok';
        if self.fHave is None:
            return '?';
        return "failed";

    def __repr__(self):
        return f"{self.getStatusString()}"

    def getWinProgramFiles(self):
        """
        Returns a list of existing Windows "Program Files" directories.

        @todo Cache this?
        """
        asPaths = [];
        for sEnv in [ 'ProgramFiles', r'C:\Program File', \
                      'ProgramFiles(x86)', r'C:\Program Files (x86)',
                      'ProgramFiles(Arm)', r'C:\Program Files (Arm)' ]:
            sPath = os.environ.get(sEnv);
            if sPath and pathExists(sPath):
                asPaths.extend([ sPath ]);

        if 'programfiles' in g_asPathsPrepend:
            asPaths = g_asPathsPrepend['programfiles'] + asPaths;
        if 'programfiles' in g_asPathsAppend:
            asPaths.extend(g_asPathsAppend['programfiles']);

        return asPaths;

    def checkCallback_GSOAP(self):
        """
        Checks for the GSOAP compiler. Needed for the webservices.
        """

        sPath = self.sRootPath; # Acts as the 'found' beacon.
        if not sPath:
            sPath = os.environ.get('VBOX_PATH_GSOAP');

        if not sPath:
            _, sPath = getPackagePath('gsoapssl++');

        if not sPath: # Try in dev tools.
            asDevPaths = sorted(glob.glob(f"{g_oEnv['PATH_DEVTOOLS']}/common/gsoap/v*"));
            for sDevPath in asDevPaths:
                if pathExists(sDevPath):
                    sPath = sDevPath;

        # Detect binaries.
        sPathBin = None;
        if sPath:
            asPathBin = [ os.path.join(sPath, 'bin'), os.path.join(sPath, 'bin', self.enmBuildTarget + '.' + self.enmBuildArch) ];
            asFile    = [ 'soapcpp2', 'wsdl2h' ];
            for sCurPath in asPathBin:
                if pathExists(sCurPath):
                    for sFile in asFile:
                        self.sCmdPath, self.sVer = checkWhich(sFile, sCustomPath = sCurPath);
                        if self.sCmdPath:
                            sPathBin = sCurPath;
                            break;
                if self.sCmdPath:
                    break;

        g_oEnv.set('VBOX_WITH_GSOAP', '1' if sPath else '');
        g_oEnv.set('VBOX_GSOAP_INSTALLED', '1' if sPath else '');
        g_oEnv.set('VBOX_PATH_GSOAP', sPath);
        g_oEnv.set('VBOX_PATH_GSOAP_BIN', sPathBin if sPathBin else None);

        return True; # Optional, just skip.

    def checkCallback_GSOAPSources(self):
        """
        Checks for the GSOAP sources.

        This is needed for linking to functions which are needed when building
        the webservices.
        """
        sPath        = self.sRootPath if self.sRootPath else g_oEnv['VBOX_PATH_GSOAP'];
        sPathImport  = None;
        sPathSource  = None;
        sPathInclude = None;
        _, sLibs = getPackageVar('gsoapssl++', PkgMgrVar.LIBS);
        if sPath:
            # Imports
            self.printVerbose(1, f"GSOAP base path is '{sPath}'");
            asImportPath = [ os.path.join(sPath, 'share', 'gsoap', 'import'),
                             os.path.join(sPath, 'import') ];
            for sCurPath in asImportPath:
                if pathExists(sCurPath):
                    self.printVerbose(1, f"GSOAP import directory found at '{sCurPath}'");
                    sPathImport = sCurPath;
                    break;

            # Sources
            asSourcePath = [ os.path.join(sPath, 'share', 'gsoap'),
                             os.path.join(sPath) ];
            for sCurPath in asSourcePath:
                sCurPath = os.path.join(sCurPath, 'stdsoap2.cpp');
                if isFile(sCurPath):
                    self.printVerbose(1, f"GSOAP sources found at '{sCurPath}'");
                    sPathSource = sCurPath;
                    break;

            # Includes
            asSourcePath = [ os.path.join(sPath, 'share', 'gsoap'),
                             sPath,
                             os.path.join(sPath, 'include') ];
            for sCurPath in asSourcePath:
                sCurHdrPath = os.path.join(sCurPath, 'stdsoap2.h');
                if isFile(sCurHdrPath):
                    self.printVerbose(1, f"GSOAP includes found at '{sCurHdrPath}'");
                    sPathInclude = sCurPath;
                    if sCurPath == '/usr/include':
                        sPathInclude = None;
                    break;

        if sPathSource:
            self.sCmdPath = sPathSource; # To show the source path in the summary table.
        elif sLibs:
            self.sCmdPath = sLibs; # To show the library path in the summary table.
            sPathSource = ''; # Force setting the environment variable below to empty.
        else:
            if not g_oEnv['VBOX_WITH_WEBSERVICES'] \
            or     g_oEnv['VBOX_WITH_WEBSERVICES'] == '1':
                self.printWarn('GSOAP source package not found for building webservices, disabling');
                g_oEnv.set('VBOX_WITH_WEBSERVICES', '');
                return True; # Optional, just skip.

        g_oEnv.set('VBOX_GSOAP_CXX_SOURCES', sPathSource);
        g_oEnv.set('VBOX_GSOAP_CXX_INCS', sPathInclude);
        if sLibs:
            g_oEnv.set('VBOX_GSOAP_CXX_LIBS', stripPrefixFromWhitespaceSeparatedString(sLibs, '-l'));
        g_oEnv.set('VBOX_PATH_GSOAP_IMPORT', sPathImport);
        return True;

    def checkCallback_Java(self):
        """
        Checks for Java.
        """

        # Detect Java home directory.
        sJavaHome = self.sRootPath;
        if not sJavaHome:
            sJavaHome = os.environ.get('JAVA_HOME');
        if not sJavaHome:
            if self.enmBuildTarget == BuildTarget.DARWIN:
                _, sJavaHome = getPackagePath('openjdk');
                if sJavaHome:
                    sJavaHome = sJavaHome + '@8'; # One last try (we prefer Java <= 8, see below).
            else:
                try:
                    sStdErr = subprocess.check_output(['java', '-XshowSettings:properties', '-version'], stderr=subprocess.STDOUT);
                    for sLine in sStdErr.decode().splitlines():
                        if 'java.home =' in sLine:
                            sJavaHome = sLine.split('=', 1)[1].strip();
                            break;
                except:
                    pass;

        uVerMaj = None; # Acts as success beacon.

        if isDir(sJavaHome):
            # Strip 'jre' component if found.
            sHead, sTail = os.path.split(os.path.normpath(sJavaHome));
            if sTail == 'jre':
                sJavaHome = sHead;

            mapCmds = { 'java':  [ r'(?:java|openjdk)(?:\s+version)?\s+["]?(\d+)\.(\d+)\.(\d+)["]?' ],
                        'javac': [ r'javac (\d+)\.(\d+)\.(\d+)_?.*' ] };

            for sCmd, (asRegEx) in mapCmds.items():
                sPath = os.path.join(sJavaHome, 'bin') if sJavaHome else None;
                _, sVer = checkWhich(sCmd, sCustomPath = sPath);
                if sVer:
                    for sRegEx in asRegEx:
                        try:
                            reMatch = re.search(sRegEx, sVer);
                            if reMatch:
                                uVerMaj = int(reMatch.group(1));
                                # Parsing the version is a bit more tricky, as the output changed between versions.
                                # See also: https://openjdk.org/jeps/223
                                #
                                # Examples:
                                #   java version "1.8.0_361"             ->  v8
                                #   openjdk version "11.0.20" 2023-07-18 -> v11
                                #   java version "17.0.2" 2022-01-18 LTS -> v17
                                #
                                # So for Java 8 and below, major version is 1 and minor is 8 or less.
                                if uVerMaj == 1:
                                    uVerMaj = int(reMatch.group(2));
                                # We prefer Java <= 8, as only there the 'wsimport' binary is available.
                                # See also: https://openjdk.org/jeps/320
                                if uVerMaj > 8:
                                    # Check if 'wsimport' installed via other packages onto the system.
                                    _, sVer = checkWhich('wsimport');
                                    if not sVer:
                                        self.printWarn(f"Found Java {uVerMaj} installed ({sCmd}), which does not include the 'wsimport' binary anymore.");
                                        self.printWarn( "Please either install Java <= 8, or install 'wsimport' according to your distribution / OS.", fDontCount = True);
                                        uVerMaj = None; # Reset version to continue iteration.
                                else: # Java <= 8 found, bail out.
                                    self.printVerbose(1, f"Found: {sCmd}, {uVerMaj}");
                                    break;
                            else:
                                self.printVerbose(1, f"No match for: {sCmd}, {sVer}");
                        except:
                            pass;
                if uVerMaj:
                    break;

            if uVerMaj:
                self.printVerbose(1, f'Java {uVerMaj} installed');
                if uVerMaj:
                    self.sVer = str(uVerMaj);
                self.sCmdPath = sJavaHome;
                g_oEnv.set('VBOX_JAVA_HOME', sJavaHome);
        else:
            self.printWarn('Unable to detect Java home directory');

        return True if uVerMaj else False;

    def checkCallback_makeself(self):
        """
        Checks for makeself[.sh].
        """

        # Distributions such as Ubuntu ship makeself packages without a .sh suffix,
        # our build tools use the original naming though (makeself.sh). Prefer the build tools if found.
        asMakeselfNames = [ 'makeself.sh', 'makeself' ];
        if self.sRootPath:
            asMakeselfFound, _ = self.findFiles(self.sRootPath, asMakeselfNames, fAbsolute = True);
            if asMakeselfFound:
                asPath = self.findFilesGetUniquePaths(asMakeselfFound);
                if asPath:
                    self.sCmdPath, self.sVer = checkWhich(asPath[0]);
        else:
            for sName in asMakeselfNames:
                self.sCmdPath, self.sVer = checkWhich(sName);
                if self.sCmdPath:
                    break;

        if self.sCmdPath:
            g_oEnv.set('VBOX_MAKESELF', self.sCmdPath);
        return True if self.sCmdPath else False;

    def checkCallback_MacOSSDK(self):
        """
        Checks for the macOS SDK.
        """

        sPath = None;
        if self.sRootPath:
            sPath = self.sRootPath;
        else:
            asPath = [ '/Library/Developer/CommandLineTools/SDKs' ];
            oPattern = re.compile(r'MacOSX(\d+)\.(\d+)\.sdk');
            tupleVer = None;
            for sCurPath in asPath:
                if not sCurPath:
                    continue;
                try:
                    # Check for an SDK which is not too old.
                    for sCurDir in os.listdir(sCurPath):
                        if oPattern.match(sCurDir):
                            tupleCur = tuple(map(int, oPattern.match(sCurDir).groups()));
                            if tupleCur:
                                # Set as a baseline for the host; for macOS guests we at least need 10.4.4 (on Intel/x86).
                                # Choose the oldest SDK we find to provide maximum compatibility.
                                if not tupleVer:
                                    tupleVer = tupleCur;
                                elif tupleVer > tupleCur:
                                    tupleVer = tupleCur;
                                sPath = os.path.join(sCurPath, sCurDir);
                except FileNotFoundError as ex:
                    self.printError(f'{ex}');

            if tupleVer:
                if tupleVer < (10, 4):
                    self.printWarn(1, f"Found SDK {'.'.join(str(x) for x in tupleVer)} which might be too old for the macOS Guest Additions (< 10.4.4)");
                if tupleVer < (11, 0):
                    self.printWarn(1, f"Found SDK {'.'.join(str(x) for x in tupleVer)} which might be too old (< 11.0)");

        if  sPath \
        and isDir(sPath):
            self.sCmdPath = sPath;
            # Check the .plist file if this is a valid SDK directory.
            import plistlib; # Since Python 3.4.
            with open(os.path.join(self.sCmdPath, 'SDKSettings.plist'), 'rb') as fh:
                plistData = plistlib.load(fh);
            if plistData:
                self.sVer = plistData.get('Version');
            if self.sVer:
                g_oEnv.set('VBOX_PATH_MACOSX_SDK', self.sCmdPath);
                self.print(f"Using SDK {self.sVer} at '{self.sCmdPath}'");
                return True;

        self.printError('MacOS SDK not found or invalid directory specified');
        return False;

    def checkCallback_WinVisualCPP(self):
        """
        Checks for Visual C++ Build Tools 16 (2019), 15 (2017), 14 (2015), 12 (2013), 11 (2012) or 10 (2010).
        """

        sVCPPPath = self.sRootPath;
        sVCPPVer  = self.getVersionFromString(os.path.basename(self.sRootPath), fAsString = True) if self.sRootPath else None;

        if not sVCPPPath:
            # Since VS 2017 we can use vswhere.exe, so try using that first.
            for sProgramPath in self.getWinProgramFiles():
                sPath = os.path.join(sProgramPath, 'Microsoft Visual Studio', 'Installer', 'vswhere.exe');
                if isFile(sPath):
                    # Stupid vswhere can't handle multiple properties at once, so we have to deal with it
                    # by calling it multiple times. Joy.
                    asProps = [ 'installationVersion', 'installationPath', 'displayName' ];
                    for sCurProp in asProps:
                        asCmd = [ sPath,
                                '-sort', # Sort newest version first.
                                '-products', '*',
                                '-requires', 'Microsoft.VisualStudio*',
                                '-property', sCurProp,
                                '-format', 'json' ];
                        oProc = subprocess.run(asCmd, capture_output = True, check = False, universal_newlines = True);
                        if oProc.returncode == 0 and oProc.stdout.strip():
                            import json
                            asList = json.loads(oProc.stdout);
                            for curProd in asList:
                                if sCurProp == 'installationVersion':
                                    sVCPPVer = curProd.get('installationVersion', None);
                                if sCurProp == 'installationPath':
                                    sVCPPPath = curProd.get('installationPath', None);
                                if sCurProp == 'displayName':
                                    self.printVerbose(1, f"Found {curProd.get('displayName', '')} version {sVCPPVer} at '{sVCPPPath}'");

                    if not g_fDebug:
                        break;

                if sVCPPVer:
                    break;

            # For older versions we have to use the registry. Start with "newest" first.
            if not sVCPPVer:
                import winreg
                for uVer, sName in [(14, "2015"), (12, "2013"), (11, "2012"), (10, "2010")]:
                    try:
                        sVer = r'SOFTWARE\Microsoft\VisualStudio\{}.0\Setup\VC'.format(uVer);
                        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, sVer) as k:
                            sVCPPPath, _ = winreg.QueryValueEx(k, "ProductDir");
                            sVCPPVer     = sName;
                            break;
                    except FileNotFoundError:
                        pass;

        if sVCPPVer:
            self.print(f"Found Visual C++ version {sVCPPVer} at '{sVCPPPath}'");

            sVCPPBasePath = os.path.join(sVCPPPath, 'VC', 'Tools', 'MSVC'); # Used by Visual Studio installer.
            if not pathExists(sVCPPBasePath):
                # Used for internal tools.
                sVCPPBasePath = os.path.join(sVCPPPath, 'Tools', 'MSVC');

            asVCPPVer = sorted(glob.glob(os.path.join(sVCPPBasePath, '*')), reverse = True);
            for sVer in asVCPPVer:
                sVCPPBasePath = os.path.join(sVCPPBasePath, sVer);

            # The order is important here for parsing lateron.
            # Key: Visual Studio Version -- Tuple: MSVC Toolset stem define (kBuild), Description.
            mapVsToolsScheme = {
                "14.0x": ( "VCC140", "Visual Studio 2015"),
                "14.1x": ( "VCC141", "Visual Studio 2017"),
                "14.2x": ( "VCC142", "Visual Studio 2019"),
                "14.3x": ( "VCC143", "Visual Studio 2022")
            };

            sVCPPVer    = '.'.join(sVCPPVer.split('.')[:2]); # Strip build #.
            sVCPPArchBinPath = None;

            asMatches = [];
            # Match all versions.
            for sVer, (sScheme, sDesc) in mapVsToolsScheme.items():
                asVer = sVer.split('-');
                for sVer in asVer:
                    sVer = sVer.replace('x', '*');
                    if fnmatch.fnmatch(sVCPPVer, sVer):
                        asMatches.append((sVer, sScheme, sDesc));

            if not asMatches:
                self.printWarn(f'Warning: Version {sVCPPVer} is unsupported, but it may work');

            if asMatches:
                # Process all versions matched.
                for _, sScheme, sDesc in asMatches:

                    # Warn if not the current version we're going to use by default.
                    if int(sScheme.replace("VCC", "")) < 140:
                        self.printWarn(f'Warning: Found unsupported {sDesc} ({sVCPPVer}), but it may work');

                    g_oEnv.set( 'VBOX_VCC_TOOL_STEM', sScheme);
                    g_oEnv.set(f'PATH_TOOL_{sScheme}', sVCPPBasePath);

                    fFound = False;
                    for sCurArch, curTuple in g_mapWinVSArch2Dir.items():
                        sDirArch, sDirHost = curTuple;
                        sCurArchBinPath = os.path.join(sVCPPBasePath, 'bin', sDirHost, sDirArch);
                        if pathExists(sCurArchBinPath):
                            g_oEnv.set(f'PATH_TOOL_{sScheme}{sCurArch.upper()}', f'$(PATH_TOOL_{sScheme})');
                            if self.enmBuildArch == sCurArch:
                                # Make sure that we have cl.exe in our path so that we can use it for tests compilation lateron.
                                g_oEnv.prependPath('PATH', sCurArchBinPath);
                                # Same goes for the libs.
                                g_oEnv.prependPath('LIB', os.path.join(sVCPPBasePath, 'lib', sCurArch));
                                sVCPPArchBinPath = sCurArchBinPath;
                                self.printVerbose(1, f"Using Visual C++ {sDesc} tools at '{sVCPPArchBinPath}' for architecture '{sCurArch}'");
                                fFound = True;
                                break;
                        # else: Not all architectures are installed / in package.

                if not fFound:
                    self.printError(f"No valid Visual C++ package at '{sVCPPBasePath}' found!");
                    sVCPPVer = None; # Reset version to indicate failure.
            else:
                self.printWarn(f'Warning: Found unsupported Visual C++ version {sVCPPVer}, but it may work');

            # Set up standard include/lib paths.
            g_oEnv.prependPath('INCLUDE', os.path.join(sVCPPBasePath, 'include', ));
            g_oEnv.prependPath('LIB', os.path.join(sVCPPBasePath, 'lib', g_mapWinVSArch2Dir.get(self.enmBuildArch)[0]));

            if sVCPPArchBinPath:
                assert sVCPPVer;
                self.sVer = sVCPPVer;
                self.sCmdPath = sVCPPArchBinPath;
                g_oArgs.config_c_compiler       = os.path.join(sVCPPArchBinPath, 'cl.exe');
                g_oArgs.config_c_compiler_ver   = sVCPPVer;
                g_oArgs.config_cpp_compiler     = os.path.join(sVCPPArchBinPath, 'cl.exe');
                g_oArgs.config_cpp_compiler_ver = sVCPPVer;

        return True if sVCPPVer else False;

    def checkCallback_Win10SDK(self):
        """
        Checks for Windows 10 SDK.
        """

        sSDKVer = '';
        sSDKPath = None;

        # Check our tools first if no custom path is set.
        sSDKPath = self.sRootPath if self.sRootPath else os.path.join(g_sScriptPath, 'tools', 'win.x86', 'sdk');
        if not sSDKPath:
            try:
                import winreg;
                with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                                    r'SOFTWARE\Microsoft\Windows Kits\Installed Roots') as oKey:

                    sSDKPath, _ = winreg.QueryValueEx(oKey, "KitsRoot10");
                    sPathInc    = os.path.join(sSDKPath, "Include");
                    if not isDir(sPathInc):
                        self.printVerbose(1, f"Windows 10 SDK Include path not found in '{sPathInc}'");
                    else:
                        asVersions = [];
                        for sCurPath in os.listdir(sPathInc):
                            sPathVer = os.path.join(sPathInc, sCurPath);
                            if isDir(sPathVer) and sCurPath[0].isdigit():
                                asVersions.append(sCurPath);

                        # Sort by version (splitting at '.' and converting to ints);
                        if asVersions:
                            asVersions.sort(key=lambda v: [int(sPart) for sPart in v.split('.')], reverse = True);
                            sSDKVer = asVersions[0];

            except FileNotFoundError as ex:
                self.printVerbose(1, f'Could not find Windows 10 SDK path in registry: {ex}');
            finally:
                pass;

        if sSDKPath:
            self.printVerbose(1, f"Found Windows 10 SDK at '{sSDKPath}'");
            sSDKVer, sIncPath= self.getHighestVersionDir(os.path.join(sSDKPath, 'Include'));
            if pathExists(sIncPath):
                sArchDir = g_mapWinSDK10Arch2Dir.get(self.enmBuildArch, ('x64', 'Hostx64'))[0];
                asFile = [ f'Include/{sSDKVer}/um/Windows.h',
                        f'Include/{sSDKVer}/ucrt/malloc.h',
                        f'Include/{sSDKVer}/ucrt/stdio.h',
                        f'Lib/{sSDKVer}/um/{sArchDir}/kernel32.lib',
                        f'Lib/{sSDKVer}/um/{sArchDir}/user32.lib',
                        f'Lib/{sSDKVer}/ucrt/{sArchDir}/libucrt.lib',
                        f'Lib/{sSDKVer}/ucrt/{sArchDir}/ucrt.lib',
                        f'Bin/{sSDKVer}/{sArchDir}/rc.exe',
                        f'Bin/{sSDKVer}/{sArchDir}/midl.exe' ];
                for sCurFile in asFile:
                    if not isFile(os.path.join(sSDKPath, sCurFile)):
                        self.printError(f"File '{sCurFile}' not found in '{sSDKPath}'");
                        return False;

                # Set up standard include/lib paths.
                g_oEnv.prependPath('INCLUDE', os.path.join(sSDKPath, 'Include', sSDKVer, 'ucrt'));
                g_oEnv.prependPath('INCLUDE', os.path.join(sSDKPath, 'Include', sSDKVer, 'shared'));
                g_oEnv.prependPath('LIB', os.path.join(sSDKPath, 'Lib', sSDKVer, 'ucrt', sArchDir));
                g_oEnv.prependPath('LIB', os.path.join(sSDKPath, 'Lib', sSDKVer, 'um', sArchDir));

                g_oEnv.set('PATH_SDK_WINSDK10', sSDKPath);
                g_oEnv.set('SDK_WINSDK10_VERSION', sSDKVer);
                self.sVer = sSDKVer;
                self.sCmdPath = sSDKPath;

        return True if sSDKVer else False;

    def checkCallback_WinDDK(self):
        """
        Checks for the Windows DDK/WDK.
        """

        if g_oEnv['VBOX_PATH_WIN_DDK_ROOT']:
            self.printVerbose(1, 'Path already set, skipping check');
            return True;

        # Check our tools first.
        sDDKVer, sDDKPath = self.getHighestVersionDir(os.path.join(g_sScriptPath, 'tools', 'win.x86', 'ddk'));
        if sDDKVer:
            sDDKVer = '.'.join(str(s) for _, s in enumerate(sDDKVer));
        else:
            asVer = [];
            asRegKey = [
                r"SOFTWARE\Microsoft\Windows Kits\WDK",
                r"SOFTWARE\Wow6432Node\Microsoft\WINDDK" # Legacy DDKs.
            ];

            import winreg;
            for sCurKey in asRegKey:
                try:
                    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, sCurKey) as key:
                        uIdx = 0;
                        while True:
                            try:
                                sSubKey = winreg.EnumKey(key, uIdx);
                                with winreg.OpenKey(key, sSubKey) as oSubKey:
                                    sPath, _ = winreg.QueryValueEx(oSubKey, "InstallationFolder");
                                    if pathExists(sPath):
                                        asVer.append((sSubKey, sPath));
                                        self.printVerbose(1, "Found directroy in registry '{sPath}'");
                                uIdx += 1;
                            except OSError:
                                break;
                except FileNotFoundError:
                    continue;

            asDir = [
                r"C:\Program Files (x86)\Windows Kits\10",
                r"C:\WINDDK"
            ];

            for sCurDir in asDir:
                if not sCurDir:
                    continue;
                if isDir(sCurDir):
                    for sPath in os.listdir(sCurDir):
                        sPathAbs = os.path.join(sCurDir, sPath);
                        if   isDir(sPathAbs) \
                        and (sPath.lower().startswith("wdk") or sPath[0].isdigit()):
                            asVer.append((sPathAbs, sPath));
                            self.printVerbose(1, "Found file system directroy '{sPathAbs}'");

            if self.sRootPath:
                asVer.append((self.sRootPath, os.path.dirname(self.sRootPath)));

            # Find 7600.16385.1 (Windows 7 Driver Development Kit, also for Server 2008 R2).
            if asVer:
                for sCurPath, _ in asVer:
                    asFiles = [
                        "inc/api/ntdef.h",
                        "lib/win7/i386/int64.lib",
                        "lib/wlh/i386/int64.lib",
                        "lib/wnet/i386/int64.lib",
                        "lib/wxp/i386/int64.lib",
                        "bin/x86/rc.exe"
                    ];
                    for sFile in asFiles:
                        if not pathExists(os.path.join(sCurPath, sFile)):
                            self.printError(f"File '{sFile} not found in '{sCurPath}'");
                            return False;
                    sDDKPath = sCurPath;

        if sDDKPath:
            g_oEnv.set('PATH_SDK_WINDDK71', sDDKPath);
            g_oEnv.set('SDK_WINDDK71_VERSION', sDDKVer);
            self.sVer = sDDKVer;
            self.sCmdPath = sDDKPath;

        return True if sDDKPath else False;

    def checkCallback_kBuild(self):
        """
        Checks for kBuild stuff and sets the paths.
        """

        #
        # Git submodules can only mirror whole repositories, not sub directories,
        # meaning that kBuild is residing a level deeper than with svn externals.
        #
        fFound = False;

        sPath = self.sRootPath;
        if not sPath:
            sPath = g_oEnv['KBUILD_PATH'];
        if not sPath:
            sPath = os.environ.get('KBUILD_DEVTOOLS');
        if not sPath:
            sPath = os.path.join(g_sScriptPath, 'kBuild');

        if sPath:
            sPathBin = os.path.join(sPath, 'bin', self.enmBuildTarget + "." + self.enmBuildArch);
            if pathExists(sPathBin):
                if  checkWhich('kmk', 'kBuild kmk', sPathBin) \
                and checkWhich('kmk_ash', 'kBuild kmk_ash', sPathBin) \
                and isFile(os.path.join(sPath, 'footer.kmk')) \
                and isFile(os.path.join(sPath, 'header.kmk')) \
                and isFile(os.path.join(sPath, 'rules.kmk')):
                    g_oEnv.set('KBUILD_PATH', sPath);
                    self.sCmdPath = sPathBin;
                    fFound = True;

        if not fFound:
            self.printError('No suitable kBuild path found, giving up');

        return fFound;

    def checkCallback_NASM(self):
        """
        Checks for NASM.
        """
        self.sCmdPath, self.sVer = checkWhich('nasm', sCustomPath = self.sRootPath);
        if self.sCmdPath:
            g_oEnv.set('PATH_TOOL_NASM', os.path.dirname(self.sCmdPath));
            # Disable YASM tool if NASM is found, prefer NASM.
            g_oArgs.config_tools_disable_yasm = True;

        return True if self.sCmdPath else False;

    def checkCallback_clang(self):
        """
        Checks for clang.
        """
        self.sCmdPath, self.sVer = checkWhich('clang-20');
        if not self.sCmdPath:
            self.sCmdPath, self.sVer = checkWhich('clang');

        if  self.sCmdPath \
        and self.enmBuildTarget == BuildTarget.DARWIN:
            g_oArgs.config_c_compiler   = self.sCmdPath;
            self.sCmdPath, self.sVer = checkWhich('clang++');
            g_oArgs.config_cpp_compiler = self.sCmdPath;

        return True if self.sCmdPath else False;

    def checkCallback_gcc(self):
        """
        Checks for gcc.
        """
        class gccTools:
            """ Structure for the GCC tools. """
            def __init__(self, name, switches):
                self.sName = name;
                self.asVerSwitches = switches;
                self.sVer = None;
                self.sPath = None;
        asToolsToCheck = {
            'gcc' : gccTools( "gcc", [ '-dumpfullversion', '-dumpversion' ] ),
            'g++' : gccTools( "g++", [ '-dumpfullversion', '-dumpversion' ] )
        };

        for _, (sName, curEntry) in enumerate(asToolsToCheck.items()):
            asToolsToCheck[sName].sPath, asToolsToCheck[sName].sVer = \
                checkWhich(curEntry.sName, curEntry.sName, asVersionSwitches = curEntry.asVerSwitches);
            if not asToolsToCheck[sName].sPath:
                self.printError(f'{curEntry.sName} not found');
                return False;

        if asToolsToCheck['gcc'].sVer != asToolsToCheck['g++'].sVer:
            self.printError('GCC and G++ versions do not match!');
            return False;

        g_oEnv.set('CC32',  os.path.basename(asToolsToCheck['gcc'].sPath));
        g_oEnv.set('CXX32', os.path.basename(asToolsToCheck['g++'].sPath));
        if g_enmHostArch == BuildArch.AMD64:
            g_oEnv.append('CC32',  ' -m32');
            g_oEnv.append('CXX32', ' -m32');
        elif g_enmHostArch == BuildArch.X86 \
        and  self.enmBuildArch == BuildArch.AMD64: ## @todo Still needed?
            g_oEnv.append('CC32',  ' -m64');
            g_oEnv.append('CXX32', ' -m64');
        elif self.enmBuildArch == BuildArch.AMD64:
            g_oEnv.unset('CC32');
            g_oEnv.unset('CXX32');

        sCC = os.path.basename(asToolsToCheck['gcc'].sPath);
        if sCC != 'gcc':
            g_oEnv.set('TOOL_GCC3_CC', sCC);
            g_oEnv.set('TOOL_GCC3_AS', sCC);
            g_oEnv.set('TOOL_GCC3_LD', sCC);
            g_oEnv.set('TOOL_GXX3_CC', sCC);
            g_oEnv.set('TOOL_GXX3_AS', sCC);
        sCXX = os.path.basename(asToolsToCheck['g++'].sPath);
        if sCXX != 'gxx':
            g_oEnv.set('TOOL_GCC3_CXX', sCXX);
            g_oEnv.set('TOOL_GXX3_CXX', sCXX);
            g_oEnv.set('TOOL_GXX3_LD' , sCXX);

        sCC32 = g_oEnv['CC32'];
        if  sCC32 != 'gcc -m32' \
        and sCC32 != '':
            g_oEnv.set('TOOL_GCC3_CC', sCC32);
            g_oEnv.set('TOOL_GCC3_AS', sCC32);
            g_oEnv.set('TOOL_GCC3_LD', sCC32);
            g_oEnv.set('TOOL_GXX3_CC', sCC32);
            g_oEnv.set('TOOL_GXX3_AS', sCC32);

        sCXX32 = g_oEnv['CXX32'];
        if  sCXX32 != 'g++ -m32' \
        and sCXX32 != '':
            g_oEnv.set('TOOL_GCC32_CXX', sCXX32);
            g_oEnv.set('TOOL_GXX32_CXX', sCXX32);
            g_oEnv.set('TOOL_GXX32_LD' , sCXX32);

        sCC64  = g_oEnv['CC64'];
        sCXX64 = g_oEnv['CXX64'];
        g_oEnv.set('TOOL_Bs3Gcc64Elf64_CC', sCC64 if sCC64 else sCC);
        g_oEnv.set('TOOL_Bs3Gcc64Elf64_CXX', sCXX64 if sCXX64 else sCXX);

        # Solaris sports a 32-bit gcc/g++.
        if  self.enmBuildTarget == BuildTarget.SOLARIS \
        and self.enmBuildArch   == BuildArch.AMD64:
            g_oEnv.set('CC' , 'gcc -m64' if sCC == 'gcc' else None);
            g_oEnv.set('CXX', 'gxx -m64' if sCC == 'gxx' else None);

        self.sCmdPath = asToolsToCheck['gcc'].sPath;
        self.sVer     = asToolsToCheck['gcc'].sVer;

        g_oArgs.config_c_compiler   = 'gcc';   ## @todo Fix this.
        g_oArgs.config_cpp_compiler = 'g++';
        return True;

    def checkCallback_devtools(self):
        """
        Checks for devtools and sets the paths.
        """

        sPathBase = self.sRootPath if self.sRootPath else g_oEnv['PATH_DEVTOOLS'];
        if not sPathBase:
            sPathBase = os.path.join(g_sScriptPath, 'tools');

        if pathExists(sPathBase):
            sPathBin = os.path.join(sPathBase, self.enmBuildTarget + "." + self.enmBuildArch);
            if pathExists(sPathBin):
                self.sCmdPath = sPathBin;

            g_oEnv.set('PATH_DEVTOOLS', sPathBase);
            return True;

        return False;

    def checkCallback_OpenWatcom(self):
        """
        Checks for Open Watcom tools.
        """

        if  self.enmBuildTarget == BuildTarget.DARWIN \
        and self.enmBuildArch   == BuildArch.ARM64:
            self.printVerbose(1, 'Open Watcom not used here (yet), skipping');
            return True;

        # Watcom v2.x mappings -- this maps the targets / archs pairs to sub directories
        #                         OpenWatcom ships its binaries in.
        # Those are backwards-compatible to v1.9 we currently only support.
        mapBuildTarget2Bin = {
            (BuildTarget.DOS,     BuildArch.X86):   "binw",
            (BuildTarget.OS2,     BuildArch.X86):   "binp",
            (BuildTarget.WINDOWS, BuildArch.X86):   "binnt",
            (BuildTarget.WINDOWS, BuildArch.AMD64): "binnt64",
            (BuildTarget.LINUX,   BuildArch.X86):   "binl",
            (BuildTarget.LINUX,   BuildArch.AMD64): "binl64",
            (BuildTarget.LINUX,   BuildArch.ARM64): "arml64",
            (BuildTarget.FREEBSD, BuildArch.AMD64): "binb64",
            (BuildTarget.NETBSD,  BuildArch.AMD64): "binb64",
            (BuildTarget.OPENBSD, BuildArch.AMD64): "binb64",
            (BuildTarget.DARWIN,  BuildArch.X86):   "binosx",
            (BuildTarget.DARWIN,  BuildArch.AMD64): "bino64",
            (BuildTarget.DARWIN,  BuildArch.ARM64): "armo64",
        };

        sBinSubdir = mapBuildTarget2Bin.get((g_enmHostOS, g_enmHostArch), None);
        if not sBinSubdir: ## The toolchain is for cross compiling.
            self.print(f"Open Watcom not supported on host OS {g_enmHostOS}.{g_enmHostArch}.");
            return False;

        sPath = self.sRootPath;
        if not sPath:
            if self.enmBuildTarget == BuildTarget.LINUX:
                # Modern distros might have Snap installed for which there is an Open Watcom package.
                # Check for this.
                sPath = os.path.join('/', 'snap', 'open-watcom', 'current');
                if pathExists(sPath):
                    self.printVerbose(1, f"Detected snap package at '{sPath}'");

        for sCmdCur in self.asCmd:
            # wcl386 and wcl.exe respons to /version and wlink to -? with exit code 0.
            asVersionSwitches = ['/version'] if sCmdCur.startswith("wcl") else ['-?' ];
            # Some Open Watcom 2.x tools prints its version info on the second line, so we have to use multiline output
            # and fish out the line containing the version.
            self.sVer = '';
            self.sCmdPath, asOutput = checkWhich(sCmdCur, 'OpenWatcom', os.path.join(sPath, sBinSubdir) if sPath else None,
                                                 asVersionSwitches = asVersionSwitches, fMultiline = True);
            if not self.sCmdPath:
                return False;

            if not isinstance(asOutput, list):
                asOutput = [asOutput];
            for sLine in asOutput:
                off = sLine.lower().find('version');
                if off >= 0:
                    self.sVer = sLine[off + 7:].strip();
                    break;

            if self.sVer.startswith('2.'): # We don't support Open Watcom 2.0 (yet).
                self.printWarn('Open Watcom 2.x found, but is not supported yet!');
                return False;

        g_oEnv.set('PATH_TOOL_OPENWATCOM', sPath);
        return True;

    def checkCallback_PythonC_API(self):
        """
        Checks for required Python C API development files.
        """

        # If Python is disabled, skip.
        if g_oArgs.config_tools_disable_python:
            self.printVerbose(1, 'Python disabled, skipping Python C API');
            return True;

        # On darwin (macOS), just enable Python support.
        if self.enmBuildTarget == BuildTarget.DARWIN:
            return True;

        if g_enmPythonArch != self.enmBuildArch:
            self.printWarn(f"Mismatch between detected platform/architecture '{g_enmPythonArch}' and kBuild Python target/architecture '{self.enmBuildArch}'");
            self.printWarn( 'Make sure that the correct Python version is installed for the target architecture.');
            # Continue anyway.

        # Due to Windows App sandboxing and permissions, the include directory returned by a Python installation
        # from the Microsoft Store (or App packages) will point to the inaccessible WindowsApp directory.
        # So detect that and refuse to continue.
        asPathInc = sysconfig.get_paths()[ 'include' ];
        if not asPathInc:
            self.printError('Python installation invalid (include path) not found');
            return False;
        if '\\WindowsApps\\' in asPathInc: # Lazy me.
            self.printError('Incompatible Python installation detected (placed in WindowsApps directory), can\'t continue');
            return False;

        sCode = """
#include <Python.h>
int main()
{
    Py_Initialize();
    Py_Finalize();
    return 0;
}""";
        asLibDir = [];
        asLibDir.extend([ sysconfig.get_config_var("LIBDIR") ]);

        asLib = [];
        asLib.extend([ stripLibSuff(sysconfig.get_config_var("LDLIBRARY")) ]);

        # Make sure that the Python .dll / .so files are in PATH.
        g_oEnv.prependPath('PATH', sysconfig.get_paths()[ 'data' ]);

        if compileAndRun('Python C API', [ asPathInc ], asLibDir, [ ], asLib, sCode, \
                         enmBuildTarget = self.enmBuildTarget, enmBuildArch = self.enmBuildArch):
            g_oEnv.set('VBOX_PATH_PYTHON_INC', asPathInc);
            g_oEnv.set('VBOX_LIB_PYTHON', asLibDir[0] if len(asLibDir) > 0 else None);
            return True;

        return False;

    def checkCallback_PythonModules(self):
        """
        Checks for required Python modules installed.
        """

        # If Python is disabled, skip.
        if g_oArgs.config_tools_disable_python:
            self.printVerbose(1, 'Python disbled, skipping');
            return True;

        asModulesToCheck = [ 'packaging' ]; # Required by VirtualBox API bindings (both COM and XPCOM).

        self.printVerbose(1, 'Checking modules ...');

        for sCurMod in asModulesToCheck:
            try:
                self.printVerbose(1, f"Checking module '{sCurMod}'");
                importlib.import_module(sCurMod);
            except ImportError:
                # Hack to accept distutils for now as a substitute to the packaging module.
                if sCurMod == 'packaging':
                    try:
                        self.printVerbose(1, "Checking module 'distutils'");
                        importlib.import_module('distutils');
                    except ImportError:
                        self.printWarn(f"Python module '{sCurMod}' or 'distutils' is not installed");
                        self.print(f"Hint: Try running 'pip install {sCurMod}' (or 'pip install distutils')");
                        return False;
                    continue;
                self.printWarn(f"Python module '{sCurMod}' is not installed");
                self.print    (f"Hint: Try running 'pip install {sCurMod}'");
                return False;
        return True;

    def checkCallback_XCode(self):
        """
        Checks for Xcode and Command Line Tools on macOS.
        """

        asPathsToCheck = [];
        if self.sRootPath:
            asPathsToCheck.append(self.sRootPath);

        #
        # Detect Xcode.
        #
        try:
            oProc = subprocess.run([ 'xcode-select', '-p' ], capture_output = True, check = False, universal_newlines = True)
            if oProc.returncode == 0:
                asPathsToCheck.extend([ oProc.stdout.strip() ]);
        except subprocess.SubprocessError:
            pass;

        fRc = False;

        for sPathCur in asPathsToCheck:
            if isDir(sPathCur):
                sPathClang = os.path.join(sPathCur, 'usr/bin/clang');
                self.printVerbose(1, f"Checking for CommandLineTools at '{sPathCur}'");
                if isFile(sPathClang):
                    self.printVerbose(1, f"Found CommandLineTools at '{sPathCur}'");
                    fRc = True;
                    break;

        if fRc:
            self.sCmdPath, self.sVer = checkWhich('xcodebuild');
            if self.sCmdPath: # Note: Does not emit a version.
                g_oEnv.set('VBOX_WITH_EVEN_NEWER_XCODE', '1');
                return True;

        self.printError('CommandLineTools not found.');
        return False;

    def checkCallback_YASM(self):
        """
        Checks for YASM.
        """

        self.sCmdPath, self.sVer = checkWhich('yasm', sCustomPath = self.sRootPath);
        if self.sCmdPath:
            g_oEnv.set('PATH_TOOL_YASM', os.path.dirname(self.sCmdPath));

        return True if self.sCmdPath else False;

    def checkCallback_WinNSIS(self):
        """
        Checks for NSIS (Nullsoft Scriptable Install System).
        """
        sPath = self.sRootPath;
        if not sPath:
            asRegKey = [ r'SOFTWARE\WOW6432Node\NSIS',
                         r'SOFTWARE\NSIS', # x86 only, so unlikely.
                       ];

            import winreg;
            for hive in (winreg.HKEY_LOCAL_MACHINE,):
                for key_path in asRegKey:
                    try:
                        key = winreg.OpenKey(hive, key_path);
                        sPath, _ = winreg.QueryValueEx(key, ''); # Query (Default) key.
                        winreg.CloseKey(key);
                        break;
                    except FileNotFoundError:
                        continue;

        asFile = [ 'makensis.exe' ];
        asDir  = [ 'Include',
                   'Plugins',
                   'Stubs' ];

        for sFile in asFile:
            if not isFile(os.path.join(sPath, sFile)):
                return False;
        for sDir in asDir:
            if not isDir(os.path.join(sPath, sDir)):
                return False;

        sIncludePath = os.path.join(sPath, 'Include')
        if not any(f.endswith('.nsh') for f in os.listdir(sIncludePath)):
            return False;

        self.sCmdPath, self.sVer = checkWhich('makensis.exe', 'NSIS', sPath);

        return True if self.sCmdPath else False;

    def checkCallback_WinMSI(self):
        """
        Checks for Microsoft MSI tools.
        """
        sPath = self.sRootPath;
        if sPath:
            asFile = [ 'MsiDb.exe',
                       'MsiInfo.exe',
                       'Msimerg.exe',
                       'MsiTran.exe' ];
            for sFile in asFile:
                if not isFile(os.path.join(sPath, sFile)):
                    break;

            if sPath:
                self.sCmdPath, self.sVer = checkWhich('');

        return True if self.sCmdPath else False;

    def checkCallback_WinWIX(self):
        """
        Checks for WiX (Windows Installer XML, >= 5.0).
        """
        sPath = self.sRootPath;
        if not sPath:
            # Search default installation paths.
            for sProgramPath in self.getWinProgramFiles():
                asCandidates = glob.glob(os.path.join(sProgramPath, 'WiX Toolset *'));
                for sPathCandidate in asCandidates:
                    if pathExists(sPathCandidate):
                        sPath = sPathCandidate;
                        break;
                if sPath:
                    break;

        asDir  = [ 'bin' ];
        asFile = [ 'bin/wix.exe' ]; # Since WIX >= 5.0.
        for sDir in asDir:
            if not isDir(os.path.join(sPath, sDir)):
                return False;
        for sFile in asFile:
            if not isFile(os.path.join(sPath, sFile)):
                return False;

        return True if sPath else False;

class FeatureCheck(CheckBase):
    """
    Describes and checks for a VirtualBox feature.
    """
    def __init__(self, sName, fnCallback, aeTargets = None, aeArchs = None,
                 enmBuildTarget = g_enmHostOS, enmBuildArch = g_enmHostArch,
                 aeTargetsExcluded = None):
        """
        Constructor.
        """
        super().__init__(sName, enmBuildTarget, enmBuildArch, aeTargets, aeArchs, aeTargetsExcluded);

        # Callback function to assist handling the feature check.
        assert fnCallback is not None;
        self.fnCallback = fnCallback;

    def performCheck(self):
        """
        Performs the actual check of the feature.

        Returns status:
            - None if not available and not required (optional or not needed)
            - False if not available but required
            - True if check is successful.
        """
        self.print('Performing feature check ...');

        return self.fnCallback(self);

    def checkValidationKit(self):
        """
        Checks for the Validation Kit requirements.
        """

        # We currently need NASM 2.16 for building the bs3 sources.
        fDisable = False;
        oToolNASM = next((lib for lib in g_aoTools if lib.sName == 'nasm'), None)
        if oToolNASM:
            if oToolNASM.sVer:
                oMatch = re.search(r"version\s+([0-9]+(?:\.[0-9]+)*)", oToolNASM.sVer);
                if oMatch:
                    sVer = oMatch.group(1);
                    if tuple(map(int, sVer.split('.'))) < (2, 16, 0):
                        fDisable = True;

        if fDisable:
            g_oArgs.config_disable_validationkit = True;

        return True; # Always return success here, we just disable it above.

class FileWriter:
    """ Base class for writing output files. """
    def __init__(self, sFileName, cchKeyAlign = None):
        """
        Initializes the FileWriter with a target filename.
        """
        self.filename = sFileName;
        self.asLines = [];
        self.cchKeyAlign = cchKeyAlign if cchKeyAlign else 0;

    def write_raw(self, content):
        """
        Add raw content (string or list of strings) to the in-memory buffer.
        """
        if isinstance(content, str):
            self.asLines.append(content);
        else:
            for sLine in content:
                self.asLines.append(sLine);

    def save(self):
        """
        Writes all buffered lines to the output file.
        """
        with open(self.filename, 'w', encoding = 'utf-8') as f:
            for sLine in self.asLines:
                if sLine:
                    f.write(sLine + '\n');

class EnvMgrWriter(FileWriter):
    """ Abstract class to write key=value pairs from the EnvManager class. """
    def __init__(self, sFileName, enmBuildTarget = g_enmHostArch, oEnvMgr = None, cchKeyAlign = None):
        super().__init__(sFileName, cchKeyAlign);
        self.enmBuildTarget = enmBuildTarget;
        self.oEnvMgr = oEnvMgr;
        self.setKeys = set();

class MakefileWriter(EnvMgrWriter):
    """ Class for writing (kBuild) Makefiles. """
    def __init__(self, sFileName, enmBuildTarget = None, oEnvMgr = None, cchKeyAlign = None):
        """
        Initialize the MakefileWriter class.
        """
        super().__init__(sFileName, enmBuildTarget, oEnvMgr, cchKeyAlign);

    def write(self, sKey, oVal = None):
        """
        Writes a Makefile define (key := value), skipping if the key was already defined.

        If value is a list, elements are joined with spaces.
        """
        if sKey not in self.oEnvMgr.env:
            if g_fDebug:
                printVerbose(1, f'Key {sKey} does not exist (yet)');
            if isinstance(oVal, list):
                sValStr = ' '.join(map(str, oVal));
            else:
                sValStr = str(oVal);
            super().write_raw(f"{sKey.ljust(self.cchKeyAlign)} := {sValStr}");
            return;

        if  sKey in self.oEnvMgr.env \
        and sKey is not None:
            super().write_raw(f"{sKey.ljust(self.cchKeyAlign)} := {oVal if oVal else self.oEnvMgr[sKey]}");

    def write_all(self, asPrefixInclude = None, asPrefixExclude = None):
        """
        Writes all environment variables based on the include / exclude paramters.
        """
        for sKey, sVal in self.oEnvMgr.env.items():
            if asPrefixExclude and any(sKey.startswith(p) for p in asPrefixExclude):
                continue;
            if asPrefixInclude and not any(sKey.startswith(p) for p in asPrefixInclude):
                continue;
            self.write(sKey, sVal);

class EnvFileWriter(EnvMgrWriter):
    """ Class for writing environment files. """
    def __init__(self, asFilename, enmBuildTarget = None, oEnvMgr = None, cchKeyAlign = None):
        """
        Initializes the EnvFileWriter class.
        """
        super().__init__(asFilename, enmBuildTarget, oEnvMgr, cchKeyAlign);
        self.sKeyword = 'set' if enmBuildTarget == BuildTarget.WINDOWS else 'export';

    def write(self, sKey, oVal = None):
        """
        Writes an environment variable appropriate for the platform.
        """
        if sKey not in self.oEnvMgr.env:
            if g_fDebug:
                printVerbose(1, f'Key {sKey} does not exist (yet)');
            if isinstance(oVal, list):
                sValStr = ' '.join(map(str, oVal));
            else:
                sValStr = str(oVal);
            super().write_raw(f"{self.sKeyword} {sKey}={sValStr}");
            return;

        if  sKey in self.oEnvMgr.env \
        and sKey is not None:
            super().write_raw(f"{self.sKeyword} {sKey}={oVal if oVal else self.oEnvMgr[sKey]}");

    def write_all(self, asPrefixInclude = None, asPrefixExclude = None):
        """
        Writes all environment variables based on the include / exclude paramters.
        """
        for sKey, sVal in self.oEnvMgr.env.items():
            if asPrefixExclude and any(sKey.startswith(p) for p in asPrefixExclude):
                continue;
            if asPrefixInclude and not any(sKey.startswith(p) for p in asPrefixInclude):
                continue;
            self.write(sKey, sVal);

class EnvManager:
    """
    A simple manager for environment variables.

    Note: This does *not* modify this process' environment!
    """

    def __init__(self, env = None):
        """
        Initializes an environment variable store.

        If not environment block is defined, the process' default environment will be applied.
        """
        self.env = env.copy() if env else os.environ.copy();

    def set(self, sKey, sVal):
        """
        Set the value for a given environment variable key.
        Empty values are allowed.
        None values skips setting altogether (practical for inline comparison).
        """
        if sVal is None:
            return;
        assert isinstance(sVal, str);
        printVerbose(2, f"EnvManager: Setting {sKey}={sVal}");
        self.env[sKey] = sVal;

    def unset(self, sKey):
        """
        Unsets (deletes) a key from the set.
        """
        if sKey in self.env:
            del self.env[sKey];

    def append(self, sKey, sVal):
        """
        Appends a value to an existing key.
        If the key does not exist yet, it will be created.
        """
        return self.set(sKey, self.env[sKey] + sVal if sKey in self.env else sVal);

    def prependPath(self, sKey, sPath, enmBuildTarget = g_enmHostOS):
        """
        Prepends a path to a given key.
        """
        if not sPath or len(sPath) == 0:
            return True;
        if sKey not in self.env:
            return self.set(sKey, sPath);
        sDelim = ';' if enmBuildTarget == BuildTarget.WINDOWS else ':';
        sPath = re.sub(r'[\\/]+$', '', sPath); # Strip trailing slash, if any.
        if  g_fDebug \
        and not pathExists(sPath, fNoLog = True):
            printVerbose(1, f"EnvManager: Path '{sPath}' does not exist!");
        printVerbose(1, f"EnvManager: Prepending to '{sKey}': {sPath}");
        return self.set(sKey, sPath + sDelim + self.env[sKey]);

    def get(self, key, default=None):
        """
        Retrieves the value of an environment variable, or a default if not set (optional).
        """
        return self.env.get(key, default);

    def modify(self, sKey, func):
        """
        Modifies the value of an existing environment variable using a function.
        """
        if sKey in self.env:
            self.env[sKey] = str(func(self.env[sKey]));
        else:
            raise KeyError(f"{sKey} not set in environment");

    def updateFromArgs(self, oArgs):
        """
        Updates environment variable store using a Namespace object from argparse.
        Each argument becomes an environment variable, set only if its value is not None.
        """
        for sKey, aValue in vars(oArgs).items():
            if aValue:
                if not sKey.startswith('config_'): # Ignore internal stuff.
                    idxSep =  sKey.find("=");
                    if not idxSep:
                        break;
                    sKeyNew   = sKey[:idxSep];
                    aValueNew = sKey[idxSep + 1:];
                    self.set(sKeyNew, str(aValueNew));

    def transform(self, aTransforms):
        """
        Evaluates mapping expressions and updates the affected environment variables.
        """
        for exprCur in aTransforms:
            result = exprCur(self.env);
            if isinstance(result, dict):
                for sKey, sValue in result.items():
                    self.set(sKey, sValue);

    def printLog(self, sPrefix, asKeys = None):
        """
        Prints items to the log.
        """
        if asKeys:
            oProcEnvFiltered = { k: self.env[k] for k in asKeys if k in self.env };
        else:
            oProcEnvFiltered = self.env;
        cchKeyAlign = max((len(k) for k in oProcEnvFiltered), default = 0)
        for k, v in oProcEnvFiltered.items():
            printLog(f"{sPrefix}{k.ljust(cchKeyAlign)} = {v}");

    def __getitem__(self, sName):
        """
        Magic function to return an environment variable if found, None if not found.
        """
        return self.get(sName, None);

# Global instance of the environment manager.
# This hold the configuration we later serialize into files.
g_oEnv = EnvManager();

class SimpleTable:
    """
    A simple table for outputting aligned text.
    """
    def __init__(self, asHeaders):
        """
        Constructor.
        """
        self.asHeaders = asHeaders;
        self.aRows = [];
        self.sFmt = '';
        self.aiWidths = [];

    def addRow(self, asCells):
        """
        Adds a row to the table.
        """
        assert len(asCells) == len(self.asHeaders);
        #self.aRows.append(asCells);
        self.aRows.append(tuple(str(cell) for cell in asCells))

    def print(self):
        """
        Prints the table to the given file handle.
        """

        # Compute maximum width for each column.
        aRows = [self.asHeaders] + self.aRows;
        aColWidths = [max(len(str(row[i])) for row in aRows) for i in range(len(self.asHeaders))];
        sFmt = '  '.join('{{:<{}}}'.format(w) for w in aColWidths);

        print(sFmt.format(*self.asHeaders));
        print('-' * (sum(aColWidths) + 2*(len(self.asHeaders)-1)));
        for row in self.aRows:
            print(sFmt.format(*row));

def print_targets(aeTargets):
    """
    Returns the given build targets list as a string.
    """
    if len(aeTargets) == 1:
        return aeTargets[0];
    return ', '.join(aeTargets[:-1]) + ' and ' + aeTargets[-1]

def show_syntax_help():
    """
    Prints syntax help.
    """
    print("Supported libraries (with configure options):\n");

    for oLibCur in g_aoLibs:
        sDisable     = f"--disable-{oLibCur.sName}";
        sWith        = f"--with-{oLibCur.sName}-path=<path>";
        sOnlyTargets = f" (only on {print_targets(oLibCur.aeTargets)})" if oLibCur.aeTargets != [ BuildTarget.ANY ] else "";
        print(f"    {sDisable:<30}{sWith:<40}{sOnlyTargets}");

    print("\nSupported tools (with configure options):\n");

    for oToolCur in g_aoTools:
        sDisable     = f"--disable-{oToolCur.sName}";
        sOnlyTargets = f" (only on {print_targets(oToolCur.aeTargets)})" if oToolCur.aeTargets != [ BuildTarget.ANY ] else "";
        sWith        = f"--with-{oToolCur.sName}-path=<path>";
        print(f"    {sDisable:<30}{sWith:<40}{sOnlyTargets}");
    print(f"""
    --help                         Show this help message and exit

Examples:
    {g_sScriptName} --disable-libvpx
    {g_sScriptName} --with-libpng-path=/usr/local
    {g_sScriptName} --disable-yasm --disable-openwatcom
    {g_sScriptName} --disable-libstdc++
    {g_sScriptName} --disable-qt

Hint: Combine any supported --disable-<lib|tool> and --with-<lib>-path=PATH options.
""");

# The sorting order is important here -- don't change without proper testing!
# Also note: The library names can be arbitrary and should match the library name.
g_aoLibs = [
    LibraryCheck("linux-kernel-headers", [ "linux/version.h" ], [ ],  aeTargets = [ BuildTarget.LINUX ],
                 sCode = '#include <linux/version.h>\nint printf(const char *f,...);\nint main(void) { printf("%d.%d.%d", LINUX_VERSION_CODE / 65536, (LINUX_VERSION_CODE % 65536) / 256,LINUX_VERSION_CODE % 256);\n#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,32)\nreturn 0;\n#else\nprintf("Expected version 2.6.32 or higher"); return 1;\n#endif\n }\n'),
    # Must come first, as some libraries below depend on libstdc++.
    # Also is required for the Linux Guest Additions.
    LibraryCheck("libstdc++", [ "iostream" ], [ ], aeTargets = [ BuildTarget.LINUX, BuildTarget.SOLARIS ],
                 sCode = 'int main() { \nstd::string s = \"test";\n#ifdef __GLIBCXX__\nstd::cout << __GLIBCXX__;\n#elif defined(__GLIBCPP__)\nstd::cout << __GLIBCPP__;\n#else\nreturn 1\n#endif\nreturn 0; }\n'),
    ## @todo Undefined symbols for architecture arm64: _f32_add
    LibraryCheck("softfloat", [ "softfloat.h", "iprt/cdefs.h" ], [ "libsoftfloat" ], aeTargets = [ BuildTarget.ANY ], aeArchs = [ BuildArch.AMD64, BuildArch.X86 ], fUseInTree = True,
                 sCode = '#define IN_RING3\n#include <softfloat.h>\nint main() { softfloat_state_t s; float32_t x, y; f32_add(x, y, &s); printf("<found>"); return 0; }\n',
                 asIncPaths = [ os.path.join(g_sScriptPath, 'include') ]),
    LibraryCheck("dxmt", [ "version.h" ], [ "libdxmt" ], aeTargets = [ BuildTarget.LINUX, BuildTarget.SOLARIS ], fUseInTree = True,
                 sCode = '#include <version.h>\nint main() { return 0; }\n',
                 dictArgsToSetIfFailed = { 'config_libs_disable_dxmt' : True }),
    LibraryCheck("dxvk", [ "version.h" ], [ "libdxvk" ],  aeTargets = [ BuildTarget.LINUX ], fUseInTree = True,
                 sCode = '#include <version.h>\nint main() { printf(DXVK_VERSION); return 0; }\n',
                 dictArgsToSetIfFailed = { 'config_libs_disable_dxvk' : True }),
    LibraryCheck("libasound", [ "alsa/asoundlib.h", "alsa/version.h" ], [ "libasound" ], aeTargets = [ BuildTarget.LINUX ],
                 sCode = '#include <alsa/asoundlib.h>\n#include <alsa/version.h>\nint main() { snd_pcm_info_sizeof(); printf("%s", SND_LIB_VERSION_STR); return 0; }\n'),
    LibraryCheck("libcap", [ "sys/capability.h" ], [ "libcap" ], aeTargets = [ BuildTarget.LINUX ],
                 sCode = '#include <sys/capability.h>\nint main() { cap_t c = cap_init(); printf("<found>"); return 0; }\n'),
    LibraryCheck("libXcursor", [ "X11/cursorfont.h" ], [ "libXcursor" ], aeTargets = [ BuildTarget.LINUX, BuildTarget.SOLARIS ],
                 sCode = '#include <X11/Xcursor/Xcursor.h>\nint main() { XcursorImage *cursor = XcursorImageCreate (10, 10); XcursorImageDestroy(cursor); printf("%d.%d", XCURSOR_LIB_MAJOR, XCURSOR_LIB_MINOR); return 0; }\n'),
    LibraryCheck("curl", [ "curl/curl.h" ], [ "libcurl" ], aeTargets = [ BuildTarget.ANY ], fUseInTree = True,
                 sCode = '#include <curl/curl.h>\nint main() { printf("%s", LIBCURL_VERSION); return 0; }\n',
                 sSdkName = "VBoxLibCurl"),
    LibraryCheck("libdevmapper", [ "libdevmapper.h" ], [ "libdevmapper" ], aeTargets = [ BuildTarget.LINUX ],
                 sCode = '#include <libdevmapper.h>\nint main() { char v[64]; dm_get_library_version(v, sizeof(v)); printf("%s", v); return 0; }\n'),
    LibraryCheck("libjpeg-turbo", [ "jpeglib.h" ], [ "libjpeg" ], aeTargets = [ BuildTarget.ANY ], fUseInTree = True,
                 sCode = '#include <jpeglib.h>\n#ifndef LIBJPEG_TURBO_VERSION\n#error "libjpeg-turbo required"\n#endif\n#define VBOX_JPEG_STRINGIFY_INNER(a) #a\n#define VBOX_JPEG_STRINGIFY(a) VBOX_JPEG_STRINGIFY_INNER(a)\nint main() { struct jpeg_error_mgr error; jpeg_std_error(&error); printf("%s", VBOX_JPEG_STRINGIFY(LIBJPEG_TURBO_VERSION)); return 0; }\n',
                 sSdkName = "VBoxLibJpeg"),
    LibraryCheck("liblzf", [ "lzf.h" ], [ "liblzf" ], aeTargets = [ BuildTarget.ANY ], fUseInTree = True,
                 sCode = '#include <liblzf/lzf.h>\nint main() { printf("%d.%d", LZF_VERSION >> 8, LZF_VERSION & 0xff);\n#if LZF_VERSION >= 0x0105\nreturn 0;\n#else\nreturn 1;\n#endif\n }\n'),
    LibraryCheck("liblzma", [ "lzma.h" ], [ "liblzma" ], aeTargets = [ BuildTarget.ANY ], fUseInTree = True,
                 sCode = '#include <lzma.h>\nint main() { printf("%s", lzma_version_string()); return 0; }\n'),
    LibraryCheck("libogg", [ "ogg/ogg.h" ], [ "libogg" ], aeTargets = [ BuildTarget.ANY ], fUseInTree = True,
                 sCode = '#include <ogg/ogg.h>\nint main() { oggpack_buffer o; oggpack_get_buffer(&o); printf("<found>"); return 0; }\n',
                 sSdkName = "VBoxLibOgg"),
    LibraryCheck("libpam", [ "security/pam_appl.h" ], [ "libpam" ], aeTargets = [ BuildTarget.LINUX ],
                 sCode = '#include <security/pam_appl.h>\nint main() { \n#ifdef __LINUX_PAM__\nprintf("%d.%d", __LINUX_PAM__, __LINUX_PAM_MINOR__); if (__LINUX_PAM__ >= 1) return 0;\n#endif\nreturn 1; }\n'),
    LibraryCheck("libpng", [ "png.h" ], [ "libpng" ], aeTargets = [ BuildTarget.ANY ], fUseInTree = True,
                 sCode = '#include <png.h>\nint main() { printf("%s", PNG_LIBPNG_VER_STRING); return 0; }\n'),
    LibraryCheck("libpthread", [ "pthread.h" ], [ "libpthread" ], aeTargets = [ BuildTarget.LINUX, BuildTarget.SOLARIS ],
                 sCode = '#include <unistd.h>\n#include <pthread.h>\nint main() { pthread_mutex_t mutex; if (pthread_mutex_init(&mutex, NULL))\n{ printf("pthread_mutex_init() failed"); return 1; }\nif (pthread_mutex_lock(&mutex))\n{ printf("pthread_mutex_lock() failed"); return 1; }\nif (pthread_mutex_unlock(&mutex))\n{ printf("pthread_mutex_unlock() failed"); return 1; }\n#ifdef _POSIX_VERSION\nprintf("%d", (long)_POSIX_VERSION); return 0;\n#endif\nreturn 1;\n }'),
    LibraryCheck("libpulse", [ "pulse/pulseaudio.h", "pulse/version.h" ], [ "libpulse" ], aeTargets = [ BuildTarget.LINUX, BuildTarget.SOLARIS ],
                 sCode = '#include <pulse/version.h>\nint main() { printf("%s", pa_get_library_version()); return 0; }\n'),
    LibraryCheck("libslirp", [ "slirp/libslirp.h", "slirp/libslirp-version.h" ], [ "libslirp" ], aeTargets = [ BuildTarget.ANY ], fUseInTree = True,
                 sCode = '#include <slirp/libslirp.h>\n#include <slirp/libslirp-version.h>\nint main() { printf("%d.%d.%d", SLIRP_MAJOR_VERSION, SLIRP_MINOR_VERSION, SLIRP_MICRO_VERSION); return 0; }\n'),
    LibraryCheck("libssh", [ "libssh/libssh.h" ], [ "libssh" ], aeTargets = [ BuildTarget.DARWIN, BuildTarget.LINUX, BuildTarget.WINDOWS ], fUseInTree = True,
                 sCode = '#include <libssh/libssh.h>\n#include <libssh/libssh_version.h>\nint main() { printf("%d.%d.%d", LIBSSH_VERSION_MAJOR, LIBSSH_VERSION_MINOR, LIBSSH_VERSION_MICRO); return 0; }\n',
                 dictArgsToSetIfFailed = { 'config_libs_disable_libssh' : True }),
    LibraryCheck("libtpms", [ "libtpms/tpm_library.h" ], [ "libtpms" ], aeTargets = [ BuildTarget.ANY ], fUseInTree = True,
                 sCode = '#include <libtpms/tpm_library.h>\nint main() { printf("%d.%d.%d", TPM_LIBRARY_VER_MAJOR, TPM_LIBRARY_VER_MINOR, TPM_LIBRARY_VER_MICRO); return 0; }\n'),
    LibraryCheck("libvncserver", [ "rfb/rfb.h", "rfb/rfbclient.h" ], [ "libvncserver" ], aeTargets = [ BuildTarget.LINUX, BuildTarget.SOLARIS ],
                 sCode = '#include <rfb/rfb.h>\nint main() { printf("%s", LIBVNCSERVER_PACKAGE_VERSION); return 0; }\n',
                 dictArgsToSetIfFailed = { 'config_libs_disable_libvncserver' : True }),
    LibraryCheck("libvorbis", [ "vorbis/vorbisenc.h" ], [ "libvorbis", "libvorbisenc" ], aeTargets = [ BuildTarget.ANY ], fUseInTree = True,
                 sCode = '#include <vorbis/vorbisenc.h>\nint main() { vorbis_info v; vorbis_info_init(&v); int vorbis_rc = vorbis_encode_init_vbr(&v, 2 /* channels */, 44100 /* hz */, (float).4 /* quality */); printf("<found>"); return 0; }\n',
                 sSdkName = "VBoxLibVorbis"),
    LibraryCheck("libvpx", [ "vpx/vpx_decoder.h" ], [ "libvpx" ], aeTargets = [ BuildTarget.ANY ], fUseInTree = True,
                 sCode = '#include <vpx/vpx_codec.h>\nint main() { printf("%s", vpx_codec_version_str()); return 0; }\n',
                 sSdkName = "VBoxLibVpx"),
    LibraryCheck("libxml2", [ "libxml/parser.h" ] , [ "libxml2" ], aeTargets = [ BuildTarget.ANY ], fUseInTree = True,
                 sCode = '#include <libxml/xmlversion.h>\nint main() { printf("%s", LIBXML_DOTTED_VERSION); return 0; }\n',
                 sSdkName = "VBoxLibXml2"),
    LibraryCheck("libxslt", [], [], [ BuildTarget.ANY ], None, fnCallback = LibraryCheck.checkCallback_libxslt),
    LibraryCheck("zlib", [ "zlib.h" ], [ "libz" ], aeTargets = [ BuildTarget.ANY ], fUseInTree = True,
                 sCode = '#include <zlib.h>\nint main() { printf("%s", ZLIB_VERSION); return 0; }\n'),
    ## @todo Compiling in-tree lib fails because of dragging in too much stuff like IN_RING3 and other includes.
    #LibraryCheck("lwip", [ "lwip/init.h" ], [ "liblwip" ], [ BuildTarget.ANY ],
    #             '#include <lwip/init.h>\nint main() { printf("%d.%d.%d", LWIP_VERSION_MAJOR, LWIP_VERSION_MINOR, LWIP_VERSION_REVISION); return 0; }\n'),
    LibraryCheck("libgl", [ "GL/gl.h" ], [ "libGL" ], aeTargets = [ BuildTarget.LINUX, BuildTarget.SOLARIS ],
                 sCode = '#include <GL/gl.h>\n#include <stdio.h>\nint main() { const GLubyte *s = glGetString(GL_VERSION); printf("%s", s ? (const char *)s : "<found>"); return 0; }\n'),
    LibraryCheck("openssl", [ "openssl/crypto.h" ], [ "libcrypto", "libssl", "libz", "libzstd" ], fUseInTree = True,
                 sCode = '#include <openssl/crypto.h>\n#include <stdio.h>\nint main() { printf("%s", OpenSSL_version(OPENSSL_VERSION)); return 0; }\n',
                 sSdkName = "VBoxOpenSslStatic"),
    # Note: The required libs for Qt can differ (VBox infix and whatnot), and thus will
    #       be resolved in the check callback.
    LibraryCheck("qt", [ "QtCore/QtGlobal" ], [ ], aeTargets = [ BuildTarget.ANY ],
                 sCode = '#define IN_RING3\n#include <QtCore/QtGlobal>\nint main() { std::cout << QT_VERSION_STR << std::endl;\n#if QT_VERSION >= 6 * 65536 + 8 * 256\nreturn 0;\n#else\nreturn 1;\n#endif\n}',
                 fnCallback = LibraryCheck.checkCallback_qt,
                 sSdkName = 'QT6', dictArgsToSetIfFailed = { 'config_libs_disable_qt' : True }),
    LibraryCheck("libsdl2", [ "SDL2/SDL.h" ], [ "libSDL2" ], aeTargets = [ BuildTarget.LINUX, BuildTarget.SOLARIS ],
                 sCode = '#include <SDL2/SDL.h>\nint main() { printf("%d.%d.%d", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL); return 0; }\n',
                 dictArgsToSetIfFailed = { 'config_libs_disable_libsdl2' : '' }),
    LibraryCheck("libsdl2_ttf", [ "SDL2/SDL_ttf.h" ], [ "libSDL2_ttf" ],
                 sCode = '#include <SDL2/SDL_ttf.h>\nint main() { printf("%d.%d.%d", SDL_TTF_MAJOR_VERSION, SDL_TTF_MINOR_VERSION, SDL_TTF_PATCHLEVEL); return 0; }\n',
                 dictArgsToSetIfFailed = { 'config_libs_disable_libsdl2_ttf' : True }),
    LibraryCheck("libx11", [ "X11/Xlib.h" ], [ "libX11" ], aeTargets = [ BuildTarget.LINUX, BuildTarget.SOLARIS ], fRun = False,
                 sCode = '#include <X11/Xlib.h>\nint main() { Display *d = XOpenDisplay(NULL); XCloseDisplay(d); printf("<found>"); return 0; }\n'),
    LibraryCheck("libxext", [ "X11/extensions/Xext.h" ], [ "libXext" ], aeTargets = [ BuildTarget.LINUX, BuildTarget.SOLARIS ],
                 sCode = '#include <X11/Xlib.h>\n#include <X11/extensions/Xext.h>\nint main() { XSetExtensionErrorHandler(NULL); printf("<found>"); return 0; }\n'),
    LibraryCheck("libxmu", [ "X11/Xmu/Xmu.h" ], [ "libXmu" ], aeTargets = [ BuildTarget.LINUX, BuildTarget.SOLARIS ], fRun = False,
                 sCode = '#include <X11/Xmu/Xmu.h>\nint main() { XmuMakeAtom("test"); printf("<found>"); return 0; }\n', aeTargetsExcluded=[ BuildTarget.DARWIN ]),
    LibraryCheck("libxrandr", [ "X11/extensions/Xrandr.h" ], [ "libXrandr", "libX11" ], aeTargets = [ BuildTarget.LINUX, BuildTarget.SOLARIS ], fRun = False,
                 sCode = '#include <X11/Xlib.h>\n#include <X11/extensions/Xrandr.h>\nint main() { Display *dpy = XOpenDisplay(NULL); Window root = RootWindow(dpy, 0); XRRScreenConfiguration *c = XRRGetScreenInfo(dpy, root); printf("<found>"); return 0; }\n'),
    LibraryCheck("libxinerama", [ "X11/extensions/Xinerama.h" ], [ "libXinerama", "libX11" ], aeTargets = [ BuildTarget.LINUX, BuildTarget.SOLARIS ], fRun = False,
                 sCode = '#include <X11/Xlib.h>\n#include <X11/extensions/Xinerama.h>\nint main() { Display *dpy = XOpenDisplay(NULL); XineramaIsActive(dpy); printf("<found>"); return 0; }\n')
];

# Note: The order is important here for subsequent checks.
#       Don't change without proper testing!
# The naming of a tool must match our dev tools folders in order to be found there.
g_aoTools = [
    ToolCheck("clang", asCmd = [ "clang" ], fnCallback = ToolCheck.checkCallback_clang, aeTargets = [ BuildTarget.DARWIN ] ),
    ToolCheck("gcc", asCmd = [ "gcc" ], fnCallback = ToolCheck.checkCallback_gcc, aeTargets = [ BuildTarget.LINUX, BuildTarget.SOLARIS ] ),
    ToolCheck("kbuild", asCmd = [ "kbuild" ], fnCallback = ToolCheck.checkCallback_kBuild ),
    ToolCheck("win-visualcpp", asCmd = [ ], fnCallback = ToolCheck.checkCallback_WinVisualCPP, aeTargets = [ BuildTarget.WINDOWS ] ),
    ToolCheck("glslang", asCmd = [ "glslangValidator" ], aeTargets = [ BuildTarget.LINUX ],
              dictArgsToSetIfFailed = { 'config_tools_disable_glslang' : True }),
    ToolCheck("macossdk", asCmd = [ ], fnCallback = ToolCheck.checkCallback_MacOSSDK, aeTargets = [ BuildTarget.DARWIN ] ),
    ToolCheck("devtools", asCmd = [ ], fnCallback = ToolCheck.checkCallback_devtools ),
    ToolCheck("gsoap", asCmd = [ ], fnCallback = ToolCheck.checkCallback_GSOAP ),
    ToolCheck("gsoapsources", asCmd = [ ], fnCallback = ToolCheck.checkCallback_GSOAPSources ),
    ToolCheck("java", asCmd = [ ], fnCallback = ToolCheck.checkCallback_Java,
              dictArgsToSetIfFailed = { 'config_tools_disable_java' : True }),
    ToolCheck("makeself", asCmd = [ ], fnCallback = ToolCheck.checkCallback_makeself, aeTargets = [ BuildTarget.LINUX ]),
    ToolCheck("nasm", asCmd = [ "nasm" ], fnCallback = ToolCheck.checkCallback_NASM,
              dictArgsToSetIfFailed = { 'config_tools_disable_nasm' : True }),
    ToolCheck("yasm", asCmd = [ 'yasm' ], fnCallback = ToolCheck.checkCallback_YASM,
              dictArgsToSetIfFailed = { 'config_tools_disable_yasm' : True }),
    ToolCheck("openwatcom", asCmd = [ "wcl", "wcl386", "wlink" ], fnCallback = ToolCheck.checkCallback_OpenWatcom,
              dictArgsToSetIfFailed = { 'config_tools_disable_openwatcom' : True }),
    ToolCheck("python_c_api", asCmd = [ ], fnCallback = ToolCheck.checkCallback_PythonC_API,
              dictArgsToSetIfFailed = { 'config_tools_disable_python' : True }),
    # Note: Currently only required for XPCOM.
    ToolCheck("python_modules", asCmd = [ ], fnCallback = ToolCheck.checkCallback_PythonModules,
              aeTargets = [ BuildTarget.DARWIN, BuildTarget.LINUX, BuildTarget.SOLARIS ],
              dictArgsToSetIfFailed = { 'config_tools_disable_python' : True }),
    ToolCheck("xcode", asCmd = [], fnCallback = ToolCheck.checkCallback_XCode, aeTargets = [ BuildTarget.DARWIN ]),
    # Windows exclusive tools below (so that it can be invoked with --with-win-nsis-path, for instance).
    ToolCheck("win-sdk10", asCmd = [ ], fnCallback = ToolCheck.checkCallback_Win10SDK, aeTargets = [ BuildTarget.WINDOWS ] ),
    ToolCheck("win-ddk", asCmd = [ ], fnCallback = ToolCheck.checkCallback_WinDDK, aeTargets = [ BuildTarget.WINDOWS ] ),
    ToolCheck("win-nsis", asCmd = [ ], fnCallback = ToolCheck.checkCallback_WinNSIS, aeTargets = [ BuildTarget.WINDOWS ]),
    ToolCheck("win-msi", asCmd = [ ], fnCallback = ToolCheck.checkCallback_WinMSI, aeTargets = [ BuildTarget.WINDOWS ]),
    ToolCheck("win-wix", asCmd = [ ], fnCallback = ToolCheck.checkCallback_WinWIX, aeTargets = [ BuildTarget.WINDOWS ])
];

g_aoFeatures = [
    FeatureCheck("validationkit", fnCallback = FeatureCheck.checkValidationKit),
];

def write_autoconfig_kmk(sFilePath, enmBuildTarget, oEnv, aoLibs, aoTools):
    """
    Writes the AutoConfig.kmk file with SDK paths and enable/disable flags.
    Each library/tool gets VBOX_WITH_<NAME>.
    """

    _ = enmBuildTarget, aoTools; # Unused for now.

    w = MakefileWriter(sFilePath, enmBuildTarget, oEnv, 36);
    w.write_raw(f"""
# -*- Makefile -*-
#
# Automatically generated by
#
#   {g_sScriptName} """ + ' '.join(sys.argv[1:]) + f"""
#
# DO NOT EDIT THIS FILE MANUALLY
# It will be completely overwritten if {g_sScriptName} is executed again.
#
# Generated on """ + datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S") + """
#
\n""");
    # General stuff
    w.write_all(asPrefixInclude = ['VBOX_' ], asPrefixExclude= ['VBOX_WITH_']);
    w.write_raw('\n');

    # Features
    w.write_raw('# Features related to library presence');
    for oLibCur in aoLibs:
        if      oLibCur.isInTarget() \
        and not oLibCur.fHave:
            sVarBase = oLibCur.sName.upper().replace("+", "PLUS").replace("-", "_");
            w.write_raw(f"VBOX_WITH_{sVarBase.ljust(26)} :=");
    w.write_raw('\n');

    w.write_raw('# Features derived from arguments');
    w.write_all(asPrefixInclude = ['VBOX_WITH_', 'VBOX_ONLY_' ]);
    w.write_raw('\n');

    # Tools
    w.write_raw('# Tools');
    w.write_all(asPrefixInclude = ['PATH_TOOL_' ]);
    w.write_raw('\n');

    # SDKs
    w.write_raw('# SDKs');
    w.write_all(asPrefixInclude = ['SDK_' ]);
    w.write_raw('\n');
    w.write_raw('# SDK Paths');
    w.write_all(asPrefixInclude = ['PATH_SDK_' ]);
    w.write_raw('\n');

    # Serialize all changes to disk.
    w.save();

    if g_fDebug or True: ## @todo remove 'or True'.
        abBuf = None;
        print(f'Contents of {sFilePath}:');
        with open(sFilePath, 'r', encoding = 'utf-8') as fh:
            abBuf = fh.read();
        if abBuf:
            print(abBuf);

    return True;

def write_env(sFilePath, enmBuildTarget, enmBuildArch, oEnv, aoLibs, aoTools):
    """
    Writes the env.sh file with kBuild configuration and other tools stuff.
    """

    _ = aoLibs, aoTools; # Unused for now.

    sTimestamp  = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S");
    sScriptArgs = ' '.join(sys.argv[1:]);

    w = EnvFileWriter(sFilePath, enmBuildTarget, oEnv, 32);
    if enmBuildTarget != BuildTarget.WINDOWS:
        w.write_raw(f"""
#!/bin/bash
# -*- Environment -*-
#
# Automatically generated by
#
#   {g_sScriptName} """ + sScriptArgs + f"""
#
# DO NOT EDIT THIS FILE MANUALLY
# It will be completely overwritten if {g_sScriptName} is executed again.
#
# Generated on """ + sTimestamp + """
#\n""");
    else: # non-Windows.
        w.write_raw(f"""
@echo off
rem -*- Environment -*-
rem
rem Automatically generated by
rem
rem   {g_sScriptName} """ + sScriptArgs + f"""
rem
rem DO NOT EDIT THIS FILE MANUALLY
rem It will be completely overwritten if {g_sScriptName} is executed again.
rem
rem Generated on """ +  sTimestamp + """
rem\n""");

    # AUTOCFG defines the path to AutoConfig.kmk and can be specified via '--output-file-autoconfig'.
    w.write('AUTOCFG');

    w.write_all(asPrefixInclude = [ 'KBUILD_' ]);

    w.write('PATH_DEVTOOLS');
    w.write('PATH_OUT_BASE');

    if g_oEnv['KBUILD_PATH']:
        oEnv.prependPath('PATH', os.path.join(g_oEnv['KBUILD_PATH'], 'bin', f'{enmBuildTarget}.{enmBuildArch}'));
    w.write('PATH');

    w.save(); # Serialize all changes to disk.

    if g_fDebug or True: ## @todo remove 'or True'.
        abBuf = None;
        print(f'Contents of {sFilePath}:');
        with open(sFilePath, 'r', encoding = 'utf-8') as fh:
            abBuf = fh.read();
        if abBuf:
            print(abBuf);

    return True;

def testMain():
    """
    Main entry point for (self-)teting.
    """
    print('Self testing ...');

    import unittest;
    from unittest import mock; # For mocking file system accesses.

    # Class instance for testing.
    checkBase = CheckBase('mylib', [ BuildTarget.ANY ]);

    #
    # Test version parsing.
    #
    class tstGetVersionFromString(unittest.TestCase):
        """ Class for testing version string parsing. """
        def testVersions(self):
            """ Tests version string parsing. """
            aTestcase = [
                ("release_1.2.3.txt", "1.2.3"),
                ("foo-2.3.4", "2.3.4"),
                ("v_3.0", "3.0"),
                ("myproject-4.10.7-beta", "4.10.7"),
                ("module1.0.1", "1.0.1"),
                ("patch-1.2", "1.2"),
                ("something_else", None),
                ("5.6.7-hotfix", "5.6.7"),
                ("stable/v1.2.3.4", "1.2.3.4"),
                ("xyz-0.0.1-alpha", "0.0.1"),
            ]
            for sVer, sExpected in aTestcase:
                self.assertEqual(checkBase.getVersionFromString(sVer, fAsString = True), sExpected);
    #
    # Test finding files.
    #
    class tstFindFiles(unittest.TestCase):
        """ Class for testing finding files. """
        def testFindFiles(self):
            """ Tests finding files. """
            class tstFindFilesCase():
                """ Class for testing finding files """
                def __init__(self, fnCheckBase, sBaseDir, asFilesToSearch, asWalkStruct, asExpected, fAbsoluteDirs, fStripFilenames):
                    self.fn = fnCheckBase;
                    self.sBaseDir = sBaseDir;
                    self.asFilesToSearch = asFilesToSearch;
                    self.asWalkStruct = asWalkStruct;
                    self.asExpected = asExpected;
                    self.fAbsoluteDirs = fAbsoluteDirs;
                    self.fStripFilenames = fStripFilenames;

            aTestcases = [
                tstFindFilesCase('findFiles', '/vbox', [ 'feature1/myinc.h' ],
                                 [ ('/vbox/src/libs/mylib-1.2.3/include/feature1', [], ['myinc.h']) ],
                                 [ 'feature1/myinc.h' ], fAbsoluteDirs = False, fStripFilenames = False),
                tstFindFilesCase('findFiles', '/vbox', [ 'feature1/myinc.h' ],
                                 [ ('/vbox/src/libs/mylib-1.2.3/include/feature1', [], ['myinc.h']) ],
                                 [ '/vbox/src/libs/mylib-1.2.3/include' ], fAbsoluteDirs = True, fStripFilenames = True)
            ];

            for idx, curTst in enumerate(aTestcases):
                with \
                    mock.patch('os.walk', return_value=curTst.asWalkStruct), \
                    mock.patch('os.path.exists', side_effect=lambda *args, **kwargs: True), \
                    mock.patch('os.path.isfile', side_effect=lambda *args, **kwargs: True):
                    aRes = getattr(checkBase, curTst.fn)(curTst.sBaseDir, curTst.asFilesToSearch, curTst.fAbsoluteDirs, curTst.fStripFilenames)
                    for sCurFile in curTst.asFilesToSearch:
                        aCurResFile = aRes[0][sCurFile];
                        aPaths = checkBase.findFilesGetUniquePaths(aRes);
                        print(f"Test {idx}: Unique found paths: {aPaths}");
                        aPaths = checkBase.findFilesGetPaths(aRes);
                        print(f"Test {idx}: Unique found files: {aPaths}");
                        self.assertIn(aCurResFile['found_path'], curTst.asExpected);

    oTstSuite = unittest.TestSuite();
    oTstSuite.addTests(unittest.TestLoader().loadTestsFromTestCase(tstGetVersionFromString));
    oTstSuite.addTests(unittest.TestLoader().loadTestsFromTestCase(tstFindFiles));
    oTstSuiteRes = unittest.TextTestRunner(verbosity=2).run(oTstSuite);
    return 0 if oTstSuiteRes.wasSuccessful() else 1;

def main():
    """
    Main entry point.
    """
    global g_fhLog;
    global g_cVerbosity;
    global g_fDebug;
    global g_fContOnErr;
    global g_sFileLog;
    global g_oArgs;

    #
    # Special case:
    # Before doing any real arg parsing, check for self-testing.
    #
    if len(sys.argv) >= 2:
        if sys.argv[1] == 'selftest':
            return testMain();
    #
    # argparse config namespace rules:
    # - Everything internally used is prefixed with 'config_'.
    # - Library options are prefixed with 'config_libs_'.
    # - Tool options are prefixed with 'config_tools_'.
    # - Setting environment variables directly in the oParser arguments is forbidden!
    #   Use the envTransforms block below instead.
    #
    oParser = argparse.ArgumentParser(description='Checks and configures the build environment', add_help=False);
    oParser.add_argument('-h', '--help', help="Displays this help", action='store_true');
    oParser.add_argument('-v', '--verbose', help="Enables verbose output", action='count', default=0, dest='config_verbose');
    oParser.add_argument('-V', '--version', help="Prints the version of this script", action='store_true');
    for oLibCur in g_aoLibs:
        sLibName = oLibCur.name2Attr(); # So that we can use variables directly w/o getattr.
        oParser.add_argument(f'--build-{oLibCur.sName}', help=f'Explicitly build {oLibCur.sName} from in-tree sources', action='store_true', default=None, dest=f'config_libs_build_{sLibName}');
        oParser.add_argument(f'--disable-{oLibCur.sName}', f'--without-{oLibCur.sName}', help=f'Disables using {oLibCur.sName}', action='store_true', default=None, dest=f'config_libs_disable_{sLibName}');
        oParser.add_argument(f'--with-{oLibCur.sName}-path', help=f'Sets the (root) path for {oLibCur.sName}', dest=f'config_libs_path_{sLibName}');
        # For debugging / development only. We don't expose this in the syntax help.
        oParser.add_argument(f'--only-{oLibCur.sName}', help=argparse.SUPPRESS, action='store_true', default=None, dest=f'config_libs_only_{sLibName}');
    for oToolCur in g_aoTools:
        sToolName = oToolCur.name2Attr(); # So that we can use variables directly w/o getattr.
        oParser.add_argument(f'--disable-{oToolCur.sName}', f'--without-{oToolCur.sName}', help=f'Disables using {oToolCur.sName}', action='store_true', default=None, dest=f'config_tools_disable_{sToolName}');
        oParser.add_argument(f'--with-{oToolCur.sName}-path', help=f'Sets the (root) path for {oToolCur.sName}', dest=f'config_tools_path_{sToolName}');
        # For debugging / development only. We don't expose this in the syntax help.
        oParser.add_argument(f'--only-{oToolCur.sName}', help=argparse.SUPPRESS, action='store_true', default=None, dest=f'config_tools_only_{sToolName}');

    oParser.add_argument('--disable-docs', '--without-docs', help='Disables building the documentation', action='store_true', default=None, dest='config_disable_docs');
    oParser.add_argument('--disable-dtrace', '--without-dtrace', help='Disables building features requiring DTrace ', action='store_true', default=None, dest='config_disable_dtrace');
    oParser.add_argument('--disable-python', '--without-python', help='Disables building the Python bindings', action='store_true', default=None, dest='config_tools_disable_python');
    oParser.add_argument('--disable-pylint', '--without-pylint', help='Disables using pylint', action='store_true', default=None, dest='config_disable_pylint');
    oParser.add_argument('--disable-sdl', '--without-sdl', help='Disables building the SDL frontend', action='store_true', default=None, dest='config_libs_disable_libsdl2');
    oParser.add_argument('--disable-udptunnel', '--without-udptunnel', help='Disables building UDP tunnel support', action='store_true', default=None, dest='config_disable_udptunnel');
    oParser.add_argument('--disable-additions', '--without-additions', help='Disables building the Guest Additions', action='store_true', default=None, dest='config_disable_additions');
    oParser.add_argument('--disable-opengl', '--without-opengl', help='Disables building features which require OpenGL', action='store_true', default=None, dest='config_disable_opengl');
    oParser.add_argument('--disable-validationkit', help='Disables building the Validation Kit', action='store_true', default=None, dest='config_disable_validationkit');
    oParser.add_argument('--ignore-in-tree-libs', help='Ignores all in-tree libs', action='store_true', default=None, dest='config_ignore_in_tree_libs');
    # Disables building the Extension Pack explicitly. Only makes sense for the non-OSE build.
    oParser.add_argument('--disable-extpack', '--without-extpack', help='Disables building the Extension Pack', action='store_true', default=None, dest='config_disable_extpack');
    # If explicitly specified, we set this to '2', otherwise we default to '1'. This resembles the behavior of the old configure script.
    # The '2' acts as a marker for us to know that this was explcitly set by the configure script (rather than the default).
    oParser.add_argument('--with-hardening', help='Enables hardening', action='store_const', const='2', default='1', dest='config_with_hardening');
    oParser.add_argument('--disable-hardening', '--without-hardening', help='Disables hardening', action='store_true', default=None, dest='config_without_hardening');
    oParser.add_argument('--output-file-autoconfig', help='Path to output AutoConfig.kmk file', default=None, dest='config_file_autoconfig');
    oParser.add_argument('--output-file-env', help='Path to output env[.bat|.sh] file', default=None, dest='config_file_env');
    oParser.add_argument('--output-file-log', help='Path to output log file', default=None, dest='config_file_log');
    oParser.add_argument('--only-additions', help='Only build Guest Additions related libraries and tools', action='store_true', default=None, dest='config_only_additions');
    oParser.add_argument('--only-docs', help='Only build the documentation', action='store_true', default=None, dest='config_only_docs');
    # Note: '--odir' is kept for backwards compatibility.
    oParser.add_argument('--output-dir', '--odir', help='Specifies the output directory for all output files', default=g_sScriptPath, dest='config_out_dir');
    # Note: '--out-base-dir' is kept for backwards compatibility.
    oParser.add_argument('--output-build-dir', '--out-base-dir', help='Specifies the build output directory', default=os.path.join(g_sScriptPath, 'out'), dest='config_build_dir');
    oParser.add_argument('--ose', help='Builds the OSE version', action='store_true', default=None, dest='config_ose');
    oParser.add_argument('--debug', help='Runs in debug mode. Only use for development', action='store_true', dest='config_debug');
    oParser.add_argument('--nofatal', '--continue-on-error', help='Continues execution on fatal errors', action='store_true', dest='config_nofatal');
    oParser.add_argument('--build-arch', help='Specifies the build architecture', default=g_enmHostArch, dest='config_build_arch');
    oParser.add_argument('--build-debug', help='Build with debugging symbols and assertions', action='store_const', const=BuildType.DEBUG, dest='config_build_type');
    oParser.add_argument('--build-headless', help='Build headless (without any GUI frontend)', action='store_true', dest='config_build_headless');
    oParser.add_argument('--build-profile', help='Build with profiling support', action='store_true', dest='config_build_profile');
    oParser.add_argument('--build-target', help='Specifies the build target', default=g_enmHostOS, dest='config_build_target');
    oParser.add_argument('--build-type', help='Specifies the build type', dest='config_build_type');
    oParser.add_argument('--internal-first', help='Check internal tools (tools/win.*) first (default)', action='store_true', dest='config_internal_first');
    oParser.add_argument('--internal-last', help='Check internal tools (tools/win.*) last', action='store_true', dest='config_internal_last');
    oParser.add_argument('--append-ewdk-path', '--append-ewdk-dir', help='Adds an EWDK drive to search.', dest='config_path_append_ewdk');
    oParser.add_argument('--prepend-ewdk-path', '--prepend-ewdk-dir', help='Adds an EWDK drive to search.', dest='config_path_prepend_ewdk');
    oParser.add_argument('--append-programfiles-path', '--append-programfiles-dir', help='Adds an alternative Program Files directory to search.', dest='config_path_append_programfiles');
    oParser.add_argument('--prepend-programfiles-path', '--prepend-programfiles-dir', help='Adds an alternative Program Files directory to search.', dest='config_path_prepend_programfiles');
    oParser.add_argument('--append-tools-path', '--append-tools-dir', help='Adds an alternative tools directory to search.', dest='config_path_append_tools');
    oParser.add_argument('--prepend-tools-path', '--prepend-tools-dir', help='Adds an alternative tools directory to search.', dest='config_path_prepend_tools');
    oParser.add_argument('--with-python', '--with-python-path', help='Where the Python installation is to be found', dest='config_python_path');
    # Windows-specific arguments (the second arguments points to legacy versions kept for backwards compatibility).
    oParser.add_argument('--disable-com', '--disable-com', help='Disable building components which require COM', action='store_true', dest='config_disable_com');
    oParser.add_argument('--with-win-midl-path', '--with-midl', help='Where midl.exe is to be found', dest='config_win_midl_path');
    oParser.add_argument('--with-win-vcpkg-root', help='Where the VCPKG root directory to be found', dest='config_win_vcpkg_root');
    # The following arguments are deprecated and undocumented -- kept for backwards compatibility.
    oParser.add_argument('--build-libssl', help=argparse.SUPPRESS, action='store_true', dest='config_libs_build_openssl');
    oParser.add_argument('--disable-qt6', help=argparse.SUPPRESS, dest='config_libs_disable_qt');
    oParser.add_argument('--enable-webservice', help=argparse.SUPPRESS, action='store_true', default=None, dest='config_with_webservice');
    oParser.add_argument('--passive-mesa', help=argparse.SUPPRESS, action='store_true', default=None, dest='DISPLAY=');
    oParser.add_argument('--with-ddk', help=argparse.SUPPRESS, dest='config_tools_path_win_ddk');
    oParser.add_argument('--with-qt', '--with-qt-dir', help=argparse.SUPPRESS, dest='config_libs_path_qt');
    oParser.add_argument('--with-sdk10', help=argparse.SUPPRESS, dest='config_tools_path_win_sdk10');
    oParser.add_argument('--with-libsdl', help=argparse.SUPPRESS, dest='config_libs_path_sdl');
    oParser.add_argument('--with-vc', help=argparse.SUPPRESS, dest='config_tools_path_win_visualcpp');
    oParser.add_argument('--with-vc-common', help=argparse.SUPPRESS, dest='config_tools_path_win_visualcpp_common');
    oParser.add_argument('--with-ow-dir', help=argparse.SUPPRESS, dest='config_tools_path_openwatcom');
    oParser.add_argument('--with-yasm', help=argparse.SUPPRESS, dest='config_tools_path_yasm');
    # MacOS-specific arguments.
    oParser.add_argument('--with-macos-sdk-path', help='Where the macOS SDK is to be found', dest='config_macos_sdk_path');

    try:
        g_oArgs = oParser.parse_args();
    except SystemExit:
        print('Invalid argument(s) -- try --help for more information.');
        return 2;

    if g_oArgs.help:
        show_syntax_help();
        return 2;
    if g_oArgs.version:
        print(__revision__);
        return 0;

    print(f'VirtualBox configuration script - r{__revision__ }');
    print();
    print(f'Running on {platform.system()} {platform.release()} ({platform.machine()})');
    print(f'Using Python {sys.version} (platform: {sysconfig.get_platform()})');
    print();

    g_cVerbosity = g_oArgs.config_verbose;
    g_fDebug = g_oArgs.config_debug;
    g_fContOnErr = g_oArgs.config_nofatal;

    if g_cVerbosity > 0:
        print(f'Verbosity level set to {g_cVerbosity}');
        print();

    if g_fDebug:
        print('\n'.join(f"{k}: {v}" for k, v in sysconfig.get_config_vars().items()));
        print();

    if not g_oArgs.config_file_log:
        g_sFileLog = os.path.join(g_oArgs.config_out_dir, 'configure.log');
    else:
        g_sFileLog = g_oArgs.config_file_log;
    try:
        g_fhLog = open(g_sFileLog, "w", encoding="utf-8");
    except OSError as ex:
        printError(f"Failed to open log file '{g_sFileLog}' for writing: {str(ex)}");
        return 3;
    sys.stdout = Log(sys.stdout, g_fhLog);
    sys.stderr = Log(sys.stderr, g_fhLog);

    printLogHeader();

    # Defaults to release build if not specified explicitly.
    g_oArgs.config_build_type = g_oArgs.config_build_type if g_oArgs.config_build_type else BuildType.RELEASE;
    if g_oArgs.config_build_profile:
        g_oArgs.config_build_type = BuildType.PROFILE;

    # Set defaults.
    g_oEnv.set('KBUILD_HOST', g_enmHostOS);
    g_oEnv.set('KBUILD_HOST_ARCH', g_enmHostArch);
    g_oEnv.set('KBUILD_TYPE', g_oArgs.config_build_type);
    g_oEnv.set('KBUILD_TARGET', g_oArgs.config_build_target);
    g_oEnv.set('KBUILD_TARGET_ARCH', g_oArgs.config_build_arch);
    g_oEnv.set('KBUILD_TARGET_CPU', 'blend'); ## @todo Check this.
    g_oEnv.set('KBUILD_PATH', g_oArgs.config_tools_path_kbuild);

    # If hardening is not explicitly enabled or disabled, use the default from the source tree.
    if g_oArgs.config_with_hardening == '2':
        g_oEnv.set('VBOX_WITH_HARDENING', '2'); # Set to 2 as an indicator that our script has modified it.
    elif g_oArgs.config_without_hardening:
        g_oEnv.set('VBOX_WITHOUT_HARDENING', '1');

    # Handle the configure out directory.
    if  g_oArgs.config_out_dir \
    and not isDir(g_oArgs.config_out_dir):
        printWarn(f"Output directory '{g_oArgs.config_out_dir}' does not exist -- using script directory as output base");
        g_oArgs.config_out_dir = g_sScriptPath;

    # Handle build directory.
    # Set to 'g_sScriptPath/out' by default if not expclitly specified otherwise.
    g_oEnv.set('PATH_OUT_BASE', g_oArgs.config_build_dir);

    # Handle prepending / appending certain paths ('--[prepend|append]-<whatever>-path') arguments.
    for sArgCur, _ in g_asPathsPrepend.items(): # ASSUMES that g_asPathsAppend and g_asPathsPrepend are in sync.
        sPath = getattr(g_oArgs, f'config_path_append_{sArgCur}');
        if sPath:
            g_asPathsAppend[ sArgCur ].extend( [ sPath ] );
        sPath = getattr(g_oArgs, f'config_path_prepend_{sArgCur}');
        if sPath:
            g_asPathsPrepend[ sArgCur ].extend( [ sPath ] );

    g_oArgs.config_libs_path_python_c_api = g_oArgs.config_python_path;
    g_oArgs.config_c_compiler             = '' # Set later.
    g_oArgs.config_cpp_compiler           = '' # Ditto.

    #
    # Check build type / target / architecture.
    #
    if g_oArgs.config_build_type and g_oArgs.config_build_type not in g_aeBuildTypes:
        printError(f"Unsupported build type '{g_oArgs.config_build_type}'");
        print();
    if g_oArgs.config_build_target and g_oArgs.config_build_target not in g_aeBuildTargets:
        printError(f"Unsupported build target '{g_oArgs.config_build_target}'");
        print();
    if g_oArgs.config_build_arch and g_oArgs.config_build_arch not in g_aeBuildArchs:
        printError(f"Unsupported build architecture '{g_oArgs.config_build_arch}\'");
        print();

    print(f'Host OS / arch     : { g_sHostOS}.{g_sHostArch}');
    print(f'Building for target: { g_oArgs.config_build_target }.{ g_oArgs.config_build_arch }');
    print(f'Build type         : { g_oArgs.config_build_type }');
    print();

    # Handle env[.sh|.bat] output file.
    sEnvFile = 'env.bat' if g_oArgs.config_build_target == BuildTarget.WINDOWS else 'env.sh';
    if not g_oArgs.config_file_env:
        g_oArgs.config_file_env = os.path.join(g_oArgs.config_out_dir, sEnvFile);
    else:
        sEnvDir = os.path.dirname(g_oArgs.config_file_env);
        if not isDir(sEnvDir):
            printWarn(f"Directory for environment file '{sEnvDir}' does not exist -- using output directory as base");
            g_oArgs.config_file_env = os.path.join(g_oArgs.config_out_dir, sEnvFile);

    # Handle AutoConfig.kmk output file.
    if not g_oArgs.config_file_autoconfig:
        g_oArgs.config_file_autoconfig = os.path.join(g_oArgs.config_out_dir, 'AutoConfig.kmk');
    else: # AutoConfig.kmk location will be written to the env[.sh|.bat] file.
        sAutoConfigDir = os.path.dirname(g_oArgs.config_file_autoconfig);
        if not isDir(sAutoConfigDir):
            printWarn(f"Directory for AutoConfig.kmk '{sAutoConfigDir}' does not exist -- using output directory as base");
            g_oArgs.config_file_autoconfig = os.path.join(g_oArgs.config_out_dir, 'AutoConfig.kmk');
    g_oEnv.set('AUTOCFG', g_oArgs.config_file_autoconfig);

    # Apply updates from command line arguments.
    # This can override the defaults set above.
    g_oEnv.updateFromArgs(g_oArgs);

    # Filter libs and tools based on --only-XXX flags.
    # Replace '-' with '_' so that we can use variables directly w/o getattr lateron.
    aoOnlyLibs = [lib for lib in g_aoLibs if getattr(g_oArgs, f'config_libs_only_{lib.name2Attr()}', False)];
    aoOnlyTools = [tool for tool in g_aoTools if getattr(g_oArgs, f'config_tools_only_{tool.name2Attr()}', False)];
    aoLibsToCheck = aoOnlyLibs if aoOnlyLibs else g_aoLibs;
    aoToolsToCheck = aoOnlyTools if aoOnlyTools else g_aoTools;
    # Filter libs and tools based on build target.
    aoLibsToCheck  = [lib for lib in aoLibsToCheck if lib.isInTarget()];
    aoToolsToCheck = [tool for tool in aoToolsToCheck if tool.isInTarget()];

    #
    # Handle OSE building.
    #
    fOSE = True if g_oArgs.config_ose else None;
    if  not fOSE  \
    and pathExists('src/VBox/ExtPacks/Puel/ExtPack.xml'):
        print('Found ExtPack, assuming to build PUEL version');
        fOSE = False;
    else:
        fOSE = True; # Default

    if fOSE:
        g_oEnv.set('VBOX_OSE', '1' if fOSE else '');

    print('Building %s version' % ('OSE' if (fOSE is None or fOSE is True) else 'PUEL'));
    print();

    if g_cVerbosity >= 2:
        printVerbose(2, 'Environment manager variables:');
        print(g_oEnv.env);
        print();

    print(f'Log file                    : {g_sFileLog }');
    print(f'Configure output directory  : {g_oArgs.config_out_dir}');
    print(f'Build directory             : {g_oArgs.config_build_dir}');
    print(f'Location of environment file: {g_oArgs.config_file_env}');
    print(f'Location of AutoConfig.kmk  : {g_oArgs.config_file_autoconfig}');
    print();

    #
    # Perform OS tool checks.
    # These are essential and must be present for all following checks.
    # Sorted by importance.
    #
    aOsTools = {
        BuildTarget.LINUX:   [ 'pkg-config', 'gcc', 'make', 'xsltproc' ],
        BuildTarget.DARWIN:  [ 'pkg-config', 'clang', 'make', 'xsltproc' ],
        BuildTarget.WINDOWS: [ ], # Done via own callbacks in the ToolCheck class down below.
        BuildTarget.SOLARIS: [ 'pkg-config', 'gcc', 'gmake' ]
    };
    aOsToolsToCheck = aOsTools.get(g_oArgs.config_build_target, None);
    printVerbose(1, f'Checking for essential OS tools: {aOsToolsToCheck}');
    if aOsToolsToCheck is None:
        printWarn(f"Unsupported build target \'{ g_oArgs.config_build_target }\' for OS tool checks, probably leading to build errors");
    else:
        oOsToolsTable = SimpleTable([ 'Tool', 'Status', 'Version', 'Path' ]);
        for sBinary in aOsToolsToCheck:
            printVerbose(1, f'Checking for OS tool: {sBinary}');
            sCmdPath, sVer = checkWhich(sBinary, sBinary);
            oOsToolsTable.addRow(( sBinary,
                                'ok' if sCmdPath else 'failed',
                                sVer if sVer else "-",
                                "-" ));
            if not sCmdPath:
                printError(f"OS tool '{sBinary}' not found");

        oOsToolsTable.print();

    #
    # Perform tool checks.
    #
    if g_cErrors == 0 \
    or g_fContOnErr:
        print();
        for oToolCur in aoToolsToCheck:
            if not oToolCur.setArgs(g_oArgs):
                break;
            fRc = oToolCur.performCheck();
            if      fRc is False \
            and not g_fContOnErr:
                break;

    if g_oArgs.config_tools_disable_nasm and g_oArgs.config_tools_disable_yasm:
        printError('ASM tool not found, one of NASM or YASM is required (NASM preferred)');

    #
    # Perform library checks.
    #
    if g_cErrors == 0 \
    or g_fContOnErr:
        print();
        for oLibCur in aoLibsToCheck:
            if not oLibCur.setArgs(g_oArgs):
                break;
            fRc = oLibCur.performCheck();
            if      fRc is False \
            and not g_fContOnErr:
                break;

    #
    # Perform feature checks.
    #
    if g_cErrors == 0 \
    or g_fContOnErr:
        print();
        for oFeatureCur in g_aoFeatures:
            fRc = oFeatureCur.performCheck();
            if      fRc is False \
            and not g_fContOnErr:
                break;

    #
    # Handle environment variable transformations.
    #
    # This is needed to set/unset/change other environment variables on already set ones.
    # For instance, building OSE requires certain components to be disabled. Same when a certain library gets disabled.
    #
    aEnvTransformations = [
        #
        # Generic
        #
        # Only build the Guest Additions if explicitly specified.
        lambda env: { 'VBOX_ONLY_ADDITIONS': '1' } if g_oArgs.config_only_additions else {},
        # Only build the documentation if explicitly specified.
        lambda env: { 'VBOX_ONLY_DOCS': '1' } if g_oArgs.config_only_additions else {},
        # Disabling building the docs when only building Additions or explicitly disabled building the docs.
        lambda env: { 'VBOX_WITH_DOCS_PACKING': '' } if g_oArgs.config_only_additions
                                                     or g_oArgs.config_disable_docs else {},
        lambda env: { 'VBOX_WITH_WEBSERVICES': '' } if g_oArgs.config_only_additions else {},
        lambda env: { 'VBOX_WITH_WEBSERVICES': '1' } if g_oArgs.config_with_webservice else {},
        # Disable stuff which aren't available in OSE or if building the Validation Kit is disabled.
        lambda env: { 'VBOX_WITH_VALIDATIONKIT': '' , 'VBOX_WITH_WIN32_ADDITIONS': '' } if g_oArgs.config_ose
                                                                                        or g_oArgs.config_disable_validationkit else {},
        # Disable building the Extension Pack VNC feature when only building Additions.
        lambda env: { 'VBOX_WITH_EXTPACK_VNC': '' } if g_oArgs.config_only_additions
                                                    or g_oArgs.config_ose else {},
        # Disable Extension Pack PUEL features when building OSE.
        lambda env: { 'VBOX_WITH_EXTPACK_PUEL': '',
                      'VBOX_WITH_EXTPACK_PUEL_BUILD': '' } if g_oArgs.config_ose else {},
        # Disable Extension Pack feature (plus PUEL stuff) when building only Guest Additions
        # or with Extension Pack feature disabled.
        lambda env: { 'VBOX_WITH_EXTPACK_PUEL_BUILD': '' } if g_oArgs.config_only_additions
                                                           or g_oArgs.config_disable_extpack else {},
        lambda env: { 'VBOX_WITH_DXMT': '' } if g_oArgs.config_libs_disable_dxmt else {},
        lambda env: { 'VBOX_WITH_DXVK': '' } if g_oArgs.config_libs_disable_dxvk else {},
        # Disable FE/Qt + NLS translations (requires lrelease) if Qt is disabled.
        lambda env: { 'VBOX_WITH_QTGUI': '',
                      'VBOX_WITH_NLS': '',
                      'VBOX_WITH_MAIN_NLS': '',
                      'VBOX_WITH_PUEL_NLS': '',
                      'VBOX_WITH_VBOXMANAGE_NLS': '' } if g_oArgs.config_libs_disable_qt else {},
        lambda env: { 'VBOX_WITH_VBOXSDL': '' } if g_oArgs.config_libs_disable_libsdl2 else {},
        lambda env: { 'VBOX_WITH_SECURE_LABEL': '' } if g_oArgs.config_libs_disable_libsdl2_ttf else {},
        lambda env: { 'VBOX_WITH_LIBSSH': '' } if g_oArgs.config_libs_disable_libssh else {},
        lambda env: { 'VBOX_WITH_EXTPACK_VNC': '' } if g_oArgs.config_libs_disable_libvncserver else {},
        # Disable components if we want to build headless.
        lambda env: { 'VBOX_WITH_HEADLESS': '1',
                      'VBOX_WITH_QTGUI': '',
                      'VBOX_WITH_SECURELABEL': '',
                      'VBOX_WITH_VMSVGA3D': '',
                      'VBOX_WITH_3D_ACCELERATION' : '',
                      'VBOX_GUI_USE_QGL' : '' } if g_oArgs.config_build_headless else {},
        # Disable building the Guest Additions.
        lambda env: { 'VBOX_WITH_ADDITIONS': '' } if g_oArgs.config_disable_additions else {},
        # Disable features when OpenGL is disabled.
        lambda env: { 'VBOX_WITH_VMSVGA3D': '',
                      'VBOX_WITH_3D_ACCELERATION' : '',
                      'VBOX_GUI_USE_QGL' : '' } if g_oArgs.config_disable_opengl else {},
        # Disable recording if libvpx is disabled.
        lambda env: { 'VBOX_WITH_LIBVPX': '',
                      'VBOX_WITH_RECORDING': '' } if g_oArgs.config_libs_disable_libvpx else {},
        # Disable audio recording if libvpx is disabled.
        lambda env: { 'VBOX_WITH_LIBOGG': '',
                      'VBOX_WITH_LIBVORBIS': '',
                      'VBOX_WITH_AUDIO_RECORDING': '' } if  g_oArgs.config_libs_disable_libogg
                                                        and g_oArgs.config_libs_disable_libvorbis else {},
        # Disable building the documentation.
        lambda env: { 'VBOX_WITH_DOCS': '' } if g_oArgs.config_disable_docs else {},
        # Disable building with pylint.
        lambda env: { 'VBOX_WITH_PYLINT': '' } if g_oArgs.config_disable_pylint else {},
        # Disable building the udptunnel feature.
        lambda env: { 'VBOX_WITH_UDPTUNNEL': '' } if g_oArgs.config_disable_udptunnel else {},
        # Disable DXVK if glslangValidator is disabled.
        lambda env: { 'VBOX_WITH_DXVK': '' } if g_oArgs.config_tools_disable_glslang else {},
        # Disable building webservices if GSOAP is disabled.
        lambda env: { 'VBOX_WITH_GSOAP': '',
                      'VBOX_WITH_WEBSERVICES': '' } if g_oArgs.config_tools_disable_gsoap else {},
        # Disable building documentation (requires ant) + Java webservices if java is disabled.
        lambda env: { 'VBOX_WITH_DOCS' : '',
                      'VBOX_WITH_JWS' : '',
                      'VBOX_WITH_JMSCOM': '',
                      'VBOX_WITH_JXPCOM' : '' } if g_oArgs.config_tools_disable_java else {},
        # Disable Open Watcom if specified.
        lambda env: { 'VBOX_WITH_OPEN_WATCOM': '' } if g_oArgs.config_tools_disable_openwatcom else {},
        # Disable components which require COM.
        lambda env: { 'VBOX_WITH_MAIN': '',
                      'VBOX_WITH_QTGUI': '',
                      'VBOX_WITH_VBOXSDL': '',
                      'VBOX_WITH_DEBUGGER_GUI': '' } if g_oArgs.config_disable_com else {},
        # Disable components which require Python. Most likely this will blow up the build, as Python is mandatory nowadays.
        lambda env: { 'VBOX_WITH_PYTHON': '' } if g_oArgs.config_tools_disable_python else {},
        # Python is mandatory nowadays.
        lambda env: { 'VBOX_BLD_PYTHON': os.path.join(g_oArgs.config_python_path, 'python' + getExeSuff() ) } if g_oArgs.config_python_path else {},
        # Assembler detection: configure what to avoid.
        lambda env: { 'DONT_USE_YASM': '1' } if g_oArgs.config_tools_disable_yasm else {},
        lambda env: { 'DONT_USE_NASM': '1' } if g_oArgs.config_tools_disable_nasm else {},

        # Disable DTrace stuff if specified.
        lambda env: { 'VBOX_WITH_EXTPACK_VBOXDTRACE': '',
                      'VBOX_WITH_DTRACE': ''  } if g_oArgs.config_disable_dtrace else {},
        # Disable other stuff depending on SDL if SDL is disabled (like libsdl2_ttf).
        lambda env: { 'VBOX_WITH_SDL': '',
                      'VBOX_WITH_SECURE_LABEL': '' } if g_oArgs.config_libs_disable_libsdl2 else {},

        #
        # Windows
        #
        lambda env: { 'VBOX_PATH_WIN_DDK_ROOT': g_oArgs.config_tools_path_win_ddk } if g_oArgs.config_tools_path_win_ddk else {},
        lambda env: { 'VBOX_PATH_WIN_SDK_ROOT': g_oArgs.config_tools_path_win_sdk10 } if g_oArgs.config_tools_path_win_sdk10 else {},
        lambda env: { 'VBOX_PATH_WIN_SDK10_ROOT': g_oArgs.config_tools_path_win_sdk10 } if g_oArgs.config_tools_path_win_sdk10 else {},
        # Note: Pre-defined environment variable by vcpkg. Do not change.
        lambda env: { 'VCPKG_ROOT': g_oArgs.config_win_vcpkg_root } if g_oArgs.config_win_vcpkg_root else {},

        #
        # macOS
        #
        # Sets the macOS SDK path.
        lambda env: { 'VBOX_PATH_MACOSX_SDK_ROOT': g_oArgs.config_macos_sdk_path } if g_oArgs.config_macos_sdk_path else {},
    ];
    g_oEnv.transform(aEnvTransformations);

    #
    # Print summary.
    #
    oToolsTable = SimpleTable([ 'Tool', 'Status', 'Version', 'Path' ]);
    for oToolCur in aoToolsToCheck:
        oToolsTable.addRow(( oToolCur.sName,
                             oToolCur.getStatusString().split()[0],
                             oToolCur.sVer if oToolCur.sVer else '-',
                             oToolCur.sCmdPath if oToolCur.sCmdPath else '-' ));
    print();
    oToolsTable.print();
    print();

    oLibsTable = SimpleTable([ 'Library', 'Status', 'Version', 'Include Path(s)' ]);
    for oLibCur in aoLibsToCheck:
        oLibsTable.addRow(( oLibCur.sName,
                            oLibCur.getStatusString(),
                            oLibCur.sVer if oLibCur.sVer else '-',
                            oLibCur.asIncPaths[0] if oLibCur.asIncPaths else '-' ));
        if oLibCur.asIncPaths and len(oLibCur.asIncPaths) > 1:
            for sIncPath in oLibCur.asIncPaths[1:]:
                oLibsTable.addRow(( ' ', ' ', ' ', sIncPath));

    print();
    oLibsTable.print();
    print();

    if g_cVerbosity >= 2:
        printVerbose(2, 'Environment manager variables:');
        print(g_oEnv.env);
        print();

    # Delete output files when in debug mode.
    if g_fDebug:
        try: os.remove(g_oArgs.config_file_autoconfig);
        except: pass;
        try: os.remove(g_oArgs.config_file_env);
        except: pass;

    if g_cErrors == 0 \
    or g_fContOnErr:
        if write_autoconfig_kmk(g_oArgs.config_file_autoconfig, g_oArgs.config_build_target, g_oEnv, g_aoLibs, g_aoTools):
            if write_env(g_oArgs.config_file_env, g_oArgs.config_build_target, g_oArgs.config_build_arch, g_oEnv, g_aoLibs, g_aoTools):
                print();
                print(f'Successfully generated \"{g_oArgs.config_file_autoconfig}\" and \"{g_oArgs.config_file_env}\".');
                print();
                if g_oArgs.config_build_target == BuildTarget.WINDOWS:
                    print();
                    print('Execute env.bat once before you starting to build VirtualBox:');
                    print();
                    print('  env.bat');
                else:
                    print(f'Source {g_oArgs.config_file_env} once before you starting to build VirtualBox:');
                    print();
                    print(f'  source "{g_oArgs.config_file_env}"');

                print();
                print( 'Then run the build with:');
                print();
                print( '  kmk');
                print();

        if g_oArgs.config_build_target == BuildTarget.LINUX:
            print('To compile the kernel modules, do:');
            print();
            print(f"  cd {g_sOutPath}/{ g_oArgs.config_build_target }.{ g_oArgs.config_build_arch }/{ g_oArgs.config_build_type }/bin/src");
            print('  make');
            print();

        if g_oArgs.config_only_additions:
            print('Tree configured to build only the Guest Additions');
            print();

        if      g_oArgs.config_with_hardening \
        and not g_oArgs.config_without_hardening:
            print('  +++ WARNING +++ WARNING +++ WARNING +++ WARNING +++ WARNING +++ WARNING +++');
            print('  Hardening is enabled which means that the VBox binaries will not run from');
            print('  the binary directory. The binaries have to be installed suid root and some');
            print('  more prerequisites have to be fulfilled which is normally done by installing');
            print('  the final package. For development, the hardening feature can be disabled');
            print('  by specifying the --disable-hardening parameter. Please never disable that');
            print('  feature for the final distribution!');
            print('  +++ WARNING +++ WARNING +++ WARNING +++ WARNING +++ WARNING +++ WARNING +++');
            print();
        else:
            print('  +++ WARNING +++ WARNING +++ WARNING +++ WARNING +++ WARNING +++ WARNING +++');
            print('  Hardening is disabled. Please do NOT build packages for distribution with');
            print('  disabled hardening!');
            print('  +++ WARNING +++ WARNING +++ WARNING +++ WARNING +++ WARNING +++ WARNING +++');
            print();

    if g_cWarnings:
        print(f'Configuration completed with {g_cWarnings} warning(s). See {g_sFileLog} for details.');
        print('');
        for sWarn in g_asWarnings:
            print(f'    [WARN]: {sWarn}');
    if g_cErrors:
        print('');
        print(f'Configuration failed with {g_cErrors} error(s). See {g_sFileLog} for details.');
        print('');
        for sErr in g_asErrors:
            print(f'    [ERROR]: {sErr}');
    if  g_fContOnErr \
    and g_cErrors:
        print('');
        print('Note: Errors occurred but non-fatal mode active -- check build carefully!');

    if g_cErrors == 0:
        print('');
        print('Enjoy!');
    else:
        print('');
        print(f'Ended with {g_cErrors} error(s) and {g_cWarnings} warning(s)');

    print('');

    g_fhLog.close();
    return 0 if g_cErrors == 0 else 1;

if __name__ == "__main__":
    sys.exit(main());
