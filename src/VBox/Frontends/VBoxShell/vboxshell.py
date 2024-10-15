#!/bin/sh
# -*- coding: utf-8 -*-
# pylint: disable=line-too-long
# pylint: disable=too-many-statements
# pylint: disable=deprecated-module
# $Id: vboxshell.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

# The following checks for the right (i.e. most recent) Python binary available
# and re-starts the script using that binary (like a shell wrapper).
#
# Using a shebang like "#!/bin/env python" on newer Fedora/Debian distros is banned [1]
# and also won't work on other newer distros (Ubuntu >= 23.10), as those only ship
# python3 without a python->python3 symlink anymore.
#
# Note: As Python 2 is EOL, we consider this last (and hope for the best).
#
# [1] https://lists.fedoraproject.org/archives/list/devel@lists.fedoraproject.org/message/2PD5RNJRKPN2DVTNGJSBHR5RUSVZSDZI/
''':'
for python_bin in python3 python python2
do
    type "$python_bin" > /dev/null 2>&1 && exec "$python_bin" "$0" "$@"
done
echo >&2 "ERROR: Python not found! Please install this first in order to run this program."
exit 1
':'''

from __future__ import print_function

# VirtualBox Python Shell.
#
# This program is a simple interactive shell for VirtualBox. You can query
# information and issue commands from a simple command line.
#
# It also provides you with examples on how to use VirtualBox's Python API.
# This shell is even somewhat documented, supports TAB-completion and
# history if you have Python readline installed.
#
# Finally, shell allows arbitrary custom extensions, just create
# .VirtualBox/shexts/ and drop your extensions there.
#                                                Enjoy.
#
# P.S. Our apologies for the code quality.

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

SPDX-License-Identifier: GPL-3.0-only
"""
__version__ = "$Revision: 106320 $"


import gc
import os
import sys
import traceback
import shlex
import tempfile
import time
import re
import platform
from optparse import OptionParser


#
# Global Variables
#
g_fBatchMode = False
g_sScriptFile = None
g_sCmd = None
g_fHasReadline = True
try:
    import readline
    import rlcompleter
except ImportError:
    g_fHasReadline = False

g_sPrompt = "vbox> "

g_fHasColors  = True
g_dTermColors = {
    'red':      '\033[31m',
    'blue':     '\033[94m',
    'green':    '\033[92m',
    'yellow':   '\033[93m',
    'magenta':  '\033[35m',
    'cyan':     '\033[36m'
}



def colored(strg, color):
    """
    Translates a string to one including coloring settings, if enabled.
    """
    if not g_fHasColors:
        return strg
    col = g_dTermColors.get(color, None)
    if col:
        return col+str(strg)+'\033[0m'
    return strg

if g_fHasReadline:
    class CompleterNG(rlcompleter.Completer):
        def __init__(self, dic, ctx):
            self.ctx = ctx
            rlcompleter.Completer.__init__(self, dic)

        def complete(self, text, state):
            """
            taken from:
            http://aspn.activestate.com/ASPN/Cookbook/Python/Recipe/496812
            """
            if text == "":
                return ['\t', None][state]
            return rlcompleter.Completer.complete(self, text, state)

        def canBePath(self, _phrase, word):
            return word.startswith('/')

        def canBeCommand(self, phrase, _word):
            spaceIdx = phrase.find(" ")
            begIdx = readline.get_begidx()
            firstWord = (spaceIdx == -1 or begIdx < spaceIdx)
            if firstWord:
                return True
            if phrase.startswith('help'):
                return True
            return False

        def canBeMachine(self, phrase, word):
            return not self.canBePath(phrase, word) and not self.canBeCommand(phrase, word)

        def global_matches(self, text):
            """
            Compute matches when text is a simple name.
            Return a list of all names currently defined
            in self.namespace that match.
            """

            matches = []
            phrase = readline.get_line_buffer()

            try:
                if self.canBePath(phrase, text):
                    (directory, rest) = os.path.split(text)
                    c = len(rest)
                    for word in os.listdir(directory):
                        if c == 0 or word[:c] == rest:
                            matches.append(os.path.join(directory, word))

                if self.canBeCommand(phrase, text):
                    c = len(text)
                    for lst in [ self.namespace ]:
                        for word in lst:
                            if word[:c] == text:
                                matches.append(word)

                if self.canBeMachine(phrase, text):
                    c = len(text)
                    for mach in getMachines(self.ctx, False, True):
                        # although it has autoconversion, we need to cast
                        # explicitly for subscripts to work
                        word = re.sub("(?<!\\\\) ", "\\ ", str(mach.name))
                        if word[:c] == text:
                            matches.append(word)
                        word = str(mach.id)
                        if word[:c] == text:
                            matches.append(word)

            except Exception as e:
                printErr(self.ctx, e)
                if g_fVerbose:
                    traceback.print_exc()

            return matches

def autoCompletion(cmds, ctx):
    if not g_fHasReadline:
        return

    comps = {}
    for (key, _value) in list(cmds.items()):
        comps[key] = None
    completer = CompleterNG(comps, ctx)
    readline.set_completer(completer.complete)
    delims = readline.get_completer_delims()
    readline.set_completer_delims(re.sub("[\\./-]", "", delims)) # remove some of the delimiters
    readline.parse_and_bind("set editing-mode emacs")
    # OSX need it
    if platform.system() == 'Darwin':
        # see http://www.certif.com/spec_help/readline.html
        readline.parse_and_bind ("bind ^I rl_complete")
        readline.parse_and_bind ("bind ^W ed-delete-prev-word")
        # Doesn't work well
        # readline.parse_and_bind ("bind ^R em-inc-search-prev")
    readline.parse_and_bind("tab: complete")


g_fVerbose = False

def split_no_quotes(s):
    return shlex.split(s)

def progressBar(ctx, progress, wait=1000):
    try:
        while not progress.completed:
            print("%s %%\r" % (colored(str(progress.percent), 'red')), end="")
            sys.stdout.flush()
            progress.waitForCompletion(wait)
            ctx['global'].waitForEvents(0)
        if int(progress.resultCode) != 0:
            reportError(ctx, progress)
        return 1
    except KeyboardInterrupt:
        print("Interrupted.")
        ctx['interrupt'] = True
        if progress.cancelable:
            print("Canceling task...")
            progress.cancel()
        return 0

def printErr(_ctx, e):
    oVBoxMgr = _ctx['global']
    if oVBoxMgr.xcptIsOurXcptKind(e):
        print(colored('%s: %s' % (oVBoxMgr.xcptToString(e), oVBoxMgr.xcptGetMessage(e)), 'red'))
    else:
        print(colored(str(e), 'red'))

def reportError(_ctx, progress):
    errorinfo = progress.errorInfo
    if errorinfo:
        print(colored("Error in module '%s': %s" % (errorinfo.component, errorinfo.text), 'red'))

def colCat(_ctx, strg):
    return colored(strg, 'magenta')

def colVm(_ctx, vmname):
    return colored(vmname, 'blue')

def colPath(_ctx, path):
    return colored(path, 'green')

def colSize(_ctx, byte):
    return colored(byte, 'red')

def colPci(_ctx, pcidev):
    return colored(pcidev, 'green')

def colDev(_ctx, pcidev):
    return colored(pcidev, 'cyan')

def colSizeM(_ctx, mbyte):
    return colored(str(mbyte)+'M', 'red')

def platformArchFromString(ctx, arch):
    if arch in [ 'x86', 'x86_64', 'x64' ]:
        return ctx['global'].constants.PlatformArchitecture_x86
    if arch in ['arm', 'aarch32', 'aarch64' ]:
        return ctx['global'].constants.PlatformArchitecture_ARM
    return ctx['global'].constants.PlatformArchitecture_None

def createVm(ctx, name, arch, kind):
    vbox = ctx['vb']
    enmArch = platformArchFromString(ctx, arch)
    if enmArch == ctx['global'].constants.PlatformArchitecture_None:
        print("wrong / invalid platform architecture specified!")
        return
    sFlags = ''
    sCipher = '' ## @todo No encryption support here yet!
    sPasswordID = ''
    sPassword = ''
    mach = vbox.createMachine("", name, enmArch, [], kind, sFlags, sCipher, sPasswordID, sPassword)
    mach.saveSettings()
    print("created machine with UUID", mach.id)
    vbox.registerMachine(mach)
    # update cache
    getMachines(ctx, True)

def removeVm(ctx, mach):
    uuid = mach.id
    print("removing machine ", mach.name, "with UUID", uuid)
    cmdClosedVm(ctx, mach, detachVmDevice, ["ALL"])
    disks = mach.unregister(ctx['global'].constants.CleanupMode_Full)
    if mach:
        progress = mach.deleteConfig(disks)
        if progressBar(ctx, progress, 100) and int(progress.resultCode) == 0:
            print("Success!")
        else:
            reportError(ctx, progress)
    # update cache
    getMachines(ctx, True)

def startVm(ctx, mach, vmtype):
    perf = ctx['perf']
    session = ctx['global'].getSessionObject()
    asEnv = []
    progress = mach.launchVMProcess(session, vmtype, asEnv)
    if progressBar(ctx, progress, 100) and int(progress.resultCode) == 0:
        # we ignore exceptions to allow starting VM even if
        # perf collector cannot be started
        if perf:
            try:
                perf.setup(['*'], [mach], 10, 15)
            except Exception as e:
                printErr(ctx, e)
                if g_fVerbose:
                    traceback.print_exc()
        session.unlockMachine()

class CachedMach:
    def __init__(self, mach):
        if mach.accessible:
            self.name = mach.name
        else:
            self.name = '<inaccessible>'
        self.id = mach.id # pylint: disable=invalid-name

def cacheMachines(_ctx, lst):
    result = []
    for mach in lst:
        elem = CachedMach(mach)
        result.append(elem)
    return result

def getMachines(ctx, invalidate = False, simple=False):
    if ctx['vb'] is not None:
        if ctx['_machlist'] is None or invalidate:
            ctx['_machlist'] = ctx['global'].getArray(ctx['vb'], 'machines')
            ctx['_machlistsimple'] = cacheMachines(ctx, ctx['_machlist'])
        if simple:
            return ctx['_machlistsimple']
        return ctx['_machlist']
    return []

def asState(var):
    if var:
        return colored('on', 'green')
    return colored('off', 'green')

def asFlag(var):
    if var:
        return 'yes'
    return 'no'

def getFacilityStatus(ctx, guest, facilityType):
    (status, _timestamp) = guest.getFacilityStatus(facilityType)
    return asEnumElem(ctx, 'AdditionsFacilityStatus', status)

def perfStats(ctx, mach):
    if not ctx['perf']:
        return
    for metric in ctx['perf'].query(["*"], [mach]):
        print(metric['name'], metric['values_as_string'])

def guestExec(_ctx, _machine, _console, cmds):
    exec(cmds) # pylint: disable=exec-used

def printMouseEvent(_ctx, mev):
    print("Mouse: mode=%d x=%d y=%d z=%d w=%d buttons=%x" % (mev.mode, mev.x, mev.y, mev.z, mev.w, mev.buttons))

def printKbdEvent(ctx, kev):
    print("Kbd: ", ctx['global'].getArray(kev, 'scancodes'))

def printMultiTouchEvent(ctx, mtev):
    print("MultiTouch: %s contacts=%d time=%d" \
        % ("touchscreen" if mtev.isTouchScreen else "touchpad", mtev.contactCount, mtev.scanTime))
    xPositions = ctx['global'].getArray(mtev, 'xPositions')
    yPositions = ctx['global'].getArray(mtev, 'yPositions')
    contactIds = ctx['global'].getArray(mtev, 'contactIds')
    contactFlags = ctx['global'].getArray(mtev, 'contactFlags')

    for i in range(0, mtev.contactCount):
        print("  [%d] %d,%d %d %d" % (i, xPositions[i], yPositions[i], contactIds[i], contactFlags[i]))

def monitorSource(ctx, eventSource, active, dur):
    def handleEventImpl(event):
        evtype = event.type
        print("got event: %s %s" % (str(evtype), asEnumElem(ctx, 'VBoxEventType', evtype)))
        if evtype == ctx['global'].constants.VBoxEventType_OnMachineStateChanged:
            scev = ctx['global'].queryInterface(event, 'IMachineStateChangedEvent')
            if scev:
                print("machine state event: mach=%s state=%s" % (scev.machineId, scev.state))
        elif  evtype == ctx['global'].constants.VBoxEventType_OnSnapshotTaken:
            stev = ctx['global'].queryInterface(event, 'ISnapshotTakenEvent')
            if stev:
                print("snapshot taken event: mach=%s snap=%s" % (stev.machineId, stev.snapshotId))
        elif  evtype == ctx['global'].constants.VBoxEventType_OnGuestPropertyChanged:
            gpcev = ctx['global'].queryInterface(event, 'IGuestPropertyChangedEvent')
            if gpcev:
                if gpcev.fWasDeleted is True:
                    print("property %s was deleted" % (gpcev.name))
                else:
                    print("guest property change: name=%s value=%s flags='%s'" %
                          (gpcev.name, gpcev.value, gpcev.flags))
        elif  evtype == ctx['global'].constants.VBoxEventType_OnMousePointerShapeChanged:
            psev = ctx['global'].queryInterface(event, 'IMousePointerShapeChangedEvent')
            if psev:
                shape = ctx['global'].getArray(psev, 'shape')
                if shape is None:
                    print("pointer shape event - empty shape")
                else:
                    print("pointer shape event: w=%d h=%d shape len=%d" % (psev.width, psev.height, len(shape)))
        elif evtype == ctx['global'].constants.VBoxEventType_OnGuestMouse:
            mev = ctx['global'].queryInterface(event, 'IGuestMouseEvent')
            if mev:
                printMouseEvent(ctx, mev)
        elif evtype == ctx['global'].constants.VBoxEventType_OnGuestKeyboard:
            kev = ctx['global'].queryInterface(event, 'IGuestKeyboardEvent')
            if kev:
                printKbdEvent(ctx, kev)
        elif evtype == ctx['global'].constants.VBoxEventType_OnGuestMultiTouch:
            mtev = ctx['global'].queryInterface(event, 'IGuestMultiTouchEvent')
            if mtev:
                printMultiTouchEvent(ctx, mtev)

    class EventListener(object):
        def __init__(self, arg):
            pass

        def handleEvent(self, event):
            try:
                # a bit convoluted QI to make it work with MS COM
                handleEventImpl(ctx['global'].queryInterface(event, 'IEvent'))
            except:
                traceback.print_exc()

    if active:
        listener = ctx['global'].createListener(EventListener)
    else:
        listener = eventSource.createListener()
    registered = False
    if dur == -1:
        # not infinity, but close enough
        dur = 100000
    try:
        eventSource.registerListener(listener, [ctx['global'].constants.VBoxEventType_Any], active)
        registered = True
        end = time.time() + dur
        while  time.time() < end:
            if active:
                ctx['global'].waitForEvents(500)
            else:
                event = eventSource.getEvent(listener, 500)
                if event:
                    handleEventImpl(event)
                    # otherwise waitable events will leak (active listeners ACK automatically)
                    eventSource.eventProcessed(listener, event)
    # We need to catch all exceptions here, otherwise listener will never be unregistered
    except:
        traceback.print_exc()

    if listener and registered:
        eventSource.unregisterListener(listener)


g_tsLast = 0
def recordDemo(ctx, console, filename, dur):
    global g_tsLast
    g_tsLast = time.time()

    def stamp():
        global g_tsLast
        tsCur = time.time()
        timePassed = int((tsCur-g_tsLast)*1000)
        g_tsLast = tsCur
        return timePassed

    def handleEventImpl(event):
        evtype = event.type
        #print("got event: %s %s" % (str(evtype), asEnumElem(ctx, 'VBoxEventType', evtype)))
        if evtype == ctx['global'].constants.VBoxEventType_OnGuestMouse:
            mev = ctx['global'].queryInterface(event, 'IGuestMouseEvent')
            if mev:
                line = "%d: m %d %d %d %d %d %d\n" % (stamp(), mev.mode, mev.x, mev.y, mev.z, mev.w, mev.buttons)
                demo.write(line)
        elif evtype == ctx['global'].constants.VBoxEventType_OnGuestKeyboard:
            kev = ctx['global'].queryInterface(event, 'IGuestKeyboardEvent')
            if kev:
                line = "%d: k %s\n" % (stamp(), str(ctx['global'].getArray(kev, 'scancodes')))
                demo.write(line)

    listener = console.eventSource.createListener()
    registered = False
    # we create an aggregated event source to listen for multiple event sources (keyboard and mouse in our case)
    agg = console.eventSource.createAggregator([console.keyboard.eventSource, console.mouse.eventSource])
    with open(filename, 'w', encoding='utf-8') as demo:
        header = "VM=" + console.machine.name + "\n"
        demo.write(header)
        if dur == -1:
            # not infinity, but close enough
            dur = 100000
        try:
            agg.registerListener(listener, [ctx['global'].constants.VBoxEventType_Any], False)
            registered = True
            end = time.time() + dur
            while  time.time() < end:
                event = agg.getEvent(listener, 1000)
                if event:
                    handleEventImpl(event)
                    # keyboard/mouse events aren't waitable, so no need for eventProcessed
        # We need to catch all exceptions here, otherwise listener will never be unregistered
        except:
            traceback.print_exc()

        demo.close()
    if listener and registered:
        agg.unregisterListener(listener)


def playbackDemo(ctx, console, filename, dur):
    if dur == -1:
        # not infinity, but close enough
        dur = 100000
    with open(filename, 'r', encoding='utf-8') as demo:
        header = demo.readline()
        if g_fVerbose:
            print("Header is", header)
        basere = re.compile(r'(?P<s>\d+): (?P<t>[km]) (?P<p>.*)')
        mre = re.compile(r'(?P<a>\d+) (?P<x>-*\d+) (?P<y>-*\d+) (?P<z>-*\d+) (?P<w>-*\d+) (?P<b>-*\d+)')
        kre = re.compile(r'\d+')

        kbd = console.keyboard
        mouse = console.mouse

        try:
            end = time.time() + dur
            for line in demo:
                if time.time() > end:
                    break
                match = basere.search(line)
                if match is None:
                    continue

                rdict = match.groupdict()
                stamp = rdict['s']
                params = rdict['p']
                rtype = rdict['t']

                time.sleep(float(stamp)/1000)

                if rtype == 'k':
                    codes = kre.findall(params)
                    if g_fVerbose:
                        print("KBD:", codes)
                    kbd.putScancodes(codes)
                elif rtype == 'm':
                    mouseEvent = mre.search(params)
                    if mouseEvent is not None:
                        mdict = mouseEvent.groupdict()
                        if mdict['a'] == '1':
                            if g_fVerbose:
                                print("MA: ", mdict['x'], mdict['y'], mdict['z'], mdict['b'])
                            mouse.putMouseEventAbsolute(int(mdict['x']), int(mdict['y']), int(mdict['z']), int(mdict['w']), int(mdict['b']))
                        else:
                            if g_fVerbose:
                                print("MR: ", mdict['x'], mdict['y'], mdict['b'])
                            mouse.putMouseEvent(int(mdict['x']), int(mdict['y']), int(mdict['z']), int(mdict['w']), int(mdict['b']))

        # We need to catch all exceptions here, to close file
        except KeyboardInterrupt:
            ctx['interrupt'] = True
        except:
            traceback.print_exc()

        demo.close()

def takeScreenshot(ctx, console, args):
    display = console.display
    if len(args) > 0:
        filename = args[0]
    else:
        filename = os.path.join(tempfile.gettempdir(), "screenshot.png")
    if len(args) > 3:
        screen = int(args[3])
    else:
        screen = 0
    (fbw, fbh, _fbbpp, _fbx, _fby, _) = display.getScreenResolution(screen)
    if len(args) > 1:
        width = int(args[1])
    else:
        width = fbw
    if len(args) > 2:
        height = int(args[2])
    else:
        height = fbh

    print("Saving screenshot (%d x %d) screen %d in %s..." % (width, height, screen, filename))
    data = display.takeScreenShotToArray(screen, width, height, ctx['const'].BitmapFormat_PNG)
    with open(filename, 'wb') as pngfile:
        pngfile.write(data)
        pngfile.close()

def teleport(ctx, _session, console, args):
    if args[0].find(":") == -1:
        print("Use host:port format for teleport target")
        return
    (host, port) = args[0].split(":")
    if len(args) > 1:
        passwd = args[1]
    else:
        passwd = ""

    if len(args) > 2:
        maxDowntime = int(args[2])
    else:
        maxDowntime = 250

    port = int(port)
    print("Teleporting to %s:%d..." % (host, port))
    progress = console.teleport(host, port, passwd, maxDowntime)
    if progressBar(ctx, progress, 100) and int(progress.resultCode) == 0:
        print("Success!")
    else:
        reportError(ctx, progress)


def guestStats(ctx, console, args):
    guest = console.guest
    if not guest:
        print("Guest is not in a running state")
        return
    # we need to set up guest statistics
    if len(args) > 0 :
        update = args[0]
    else:
        update = 1
    if guest.statisticsUpdateInterval != update:
        guest.statisticsUpdateInterval = update
        try:
            time.sleep(float(update)+0.1)
        except:
            # to allow sleep interruption
            pass
    all_stats = ctx['const'].all_values('GuestStatisticType')
    cpu = 0
    for s in list(all_stats.keys()):
        try:
            val = guest.getStatistic( cpu, all_stats[s])
            print("%s: %d" % (s, val))
        except:
            # likely not implemented
            pass

def plugCpu(_ctx, machine, _session, args):
    cpu = int(args[0])
    print("Adding CPU %d..." % (cpu))
    machine.hotPlugCPU(cpu)

def unplugCpu(_ctx, machine, _session, args):
    cpu = int(args[0])
    print("Removing CPU %d..." % (cpu))
    machine.hotUnplugCPU(cpu)

def mountIso(_ctx, machine, _session, args):
    machine.mountMedium(args[0], args[1], args[2], args[3], args[4])
    machine.saveSettings()

def cond(condToCheck, resTrue, resFalse):
    if condToCheck:
        return resTrue
    return resFalse

def printHostUsbDev(ctx, usbdev):
    print("  %s: %s (vendorId=%d productId=%d serial=%s) %s" \
          % (usbdev.id, colored(usbdev.product, 'blue'), usbdev.vendorId, usbdev.productId, usbdev.serialNumber, asEnumElem(ctx, 'USBDeviceState', usbdev.state)))

def printUsbDev(_ctx, usbdev):
    print("  %s: %s (vendorId=%d productId=%d serial=%s)" \
          % (usbdev.id,  colored(usbdev.product, 'blue'), usbdev.vendorId, usbdev.productId, usbdev.serialNumber))

def printSf(ctx, sharedfolder):
    print("    name=%s host=%s %s %s" \
          % (sharedfolder.name, colPath(ctx, sharedfolder.hostPath), cond(sharedfolder.accessible, "accessible", "not accessible"), cond(sharedfolder.writable, "writable", "read-only")))

def ginfo(ctx, console, _args):
    guest = console.guest
    if not guest:
        print("Guest is not in a running state")
        return
    if guest.additionsRunLevel != ctx['const'].AdditionsRunLevelType_None:
        print("Additions active, version %s" % (guest.additionsVersion))
        print("Support seamless: %s" % (getFacilityStatus(ctx, guest, ctx['const'].AdditionsFacilityType_Seamless)))
        print("Support graphics: %s" % (getFacilityStatus(ctx, guest, ctx['const'].AdditionsFacilityType_Graphics)))
        print("Balloon size: %d" % (guest.memoryBalloonSize))
        print("Statistic update interval: %d" % (guest.statisticsUpdateInterval))
    else:
        print("No additions")
    usbs = ctx['global'].getArray(console, 'USBDevices')
    print("Attached USB:")
    for usbdev in usbs:
        printUsbDev(ctx, usbdev)
    rusbs = ctx['global'].getArray(console, 'remoteUSBDevices')
    print("Remote USB:")
    for usbdev in rusbs:
        printHostUsbDev(ctx, usbdev)
    print("Transient shared folders:")
    sfs = rusbs = ctx['global'].getArray(console, 'sharedFolders')
    for sharedfolder in sfs:
        printSf(ctx, sharedfolder)

def cmdExistingVm(ctx, mach, cmd, args):
    session = None
    try:
        session = ctx['global'].openMachineSession(mach, fPermitSharing=True)
    except Exception as e:
        printErr(ctx, "Session to '%s' not open: %s" % (mach.name, str(e)))
        if g_fVerbose:
            traceback.print_exc()
        return
    if session.state != ctx['const'].SessionState_Locked:
        print("Session to '%s' in wrong state: %s" % (mach.name, session.state))
        session.unlockMachine()
        return
    # this could be an example how to handle local only (i.e. unavailable
    # in Webservices) functionality
    if ctx['remote'] and cmd == 'some_local_only_command':
        print('Trying to use local only functionality, ignored')
        session.unlockMachine()
        return
    console = session.console
    ops = {'pause':           console.pause(),
           'resume':          console.resume(),
           'powerdown':       console.powerDown(),
           'powerbutton':     console.powerButton(),
           'stats':           lambda: perfStats(ctx, mach),
           'guest':           lambda: guestExec(ctx, mach, console, args),
           'ginfo':           lambda: ginfo(ctx, console, args),
           'guestlambda':     lambda: args[0](ctx, mach, console, args[1:]),
           'save':            lambda: progressBar(ctx, session.machine.saveState()),
           'screenshot':      lambda: takeScreenshot(ctx, console, args),
           'teleport':        lambda: teleport(ctx, session, console, args),
           'gueststats':      lambda: guestStats(ctx, console, args),
           'plugcpu':         lambda: plugCpu(ctx, session.machine, session, args),
           'unplugcpu':       lambda: unplugCpu(ctx, session.machine, session, args),
           'mountiso':        lambda: mountIso(ctx, session.machine, session, args),
           }
    try:
        ops[cmd]()
    except KeyboardInterrupt:
        ctx['interrupt'] = True
    except Exception as e:
        printErr(ctx, e)
        if g_fVerbose:
            traceback.print_exc()

    session.unlockMachine()


def cmdClosedVm(ctx, mach, cmd, args=None, save=True):
    session = ctx['global'].openMachineSession(mach, fPermitSharing=True)
    mach = session.machine
    try:
        cmd(ctx, mach, args)
    except Exception as e:
        save = False
        printErr(ctx, e)
        if g_fVerbose:
            traceback.print_exc()
    if save:
        try:
            mach.saveSettings()
        except Exception as e:
            printErr(ctx, e)
            if g_fVerbose:
                traceback.print_exc()
    ctx['global'].closeMachineSession(session)


def cmdAnyVm(ctx, mach, cmd, args=None, save=False):
    session = ctx['global'].openMachineSession(mach, fPermitSharing=True)
    mach = session.machine
    try:
        cmd(ctx, mach, session.console, args)
    except Exception as e:
        save = False
        printErr(ctx, e)
        if g_fVerbose:
            traceback.print_exc()
    if save:
        mach.saveSettings()
    ctx['global'].closeMachineSession(session)

def machById(ctx, uuid):
    mach = ctx['vb'].findMachine(uuid)
    return mach

class XPathNode:
    def __init__(self, parent, obj, ntype):
        self.parent = parent
        self.obj = obj
        self.ntype = ntype
    def lookup(self, subpath):
        children = self.enum()
        matches = []
        for e in children:
            if e.matches(subpath):
                matches.append(e)
        return matches
    def enum(self):
        return []
    def matches(self, subexp):
        if subexp == self.ntype:
            return True
        if not subexp.startswith(self.ntype):
            return False
        match = re.search(r"@(?P<a>\w+)=(?P<v>[^\'\[\]]+)", subexp)
        matches = False
        try:
            if match is not None:
                xdict = match.groupdict()
                attr = xdict['a']
                val = xdict['v']
                matches = str(getattr(self.obj, attr)) == val
        except:
            pass
        return matches
    def apply(self, cmd):
        exec(cmd, {'obj':self.obj, 'node':self, 'ctx':self.getCtx()}, {}) # pylint: disable=exec-used
    def getCtx(self):
        if hasattr(self, 'ctx'):
            return self.ctx
        return self.parent.getCtx()

class XPathNodeHolder(XPathNode):
    def __init__(self, parent, obj, attr, heldClass, xpathname):
        XPathNode.__init__(self, parent, obj, 'hld '+xpathname)
        self.attr = attr
        self.heldClass = heldClass
        self.xpathname = xpathname
    def enum(self):
        children = []
        for node in self.getCtx()['global'].getArray(self.obj, self.attr):
            nodexml = self.heldClass(self, node)
            children.append(nodexml)
        return children
    def matches(self, subexp):
        return subexp == self.xpathname

class XPathNodeValue(XPathNode):
    def __init__(self, parent, obj, xpathname):
        XPathNode.__init__(self, parent, obj, 'val '+xpathname)
        self.xpathname = xpathname
    def matches(self, subexp):
        return subexp == self.xpathname

class XPathNodeHolderVM(XPathNodeHolder):
    def __init__(self, parent, vbox):
        XPathNodeHolder.__init__(self, parent, vbox, 'machines', XPathNodeVM, 'vms')

class XPathNodeVM(XPathNode):
    def __init__(self, parent, obj):
        XPathNode.__init__(self, parent, obj, 'vm')
    #def matches(self, subexp):
    #    return subexp=='vm'
    def enum(self):
        return [XPathNodeHolderNIC(self, self.obj),
                XPathNodeValue(self, self.obj.BIOSSettings,  'bios'), ]

class XPathNodeHolderNIC(XPathNodeHolder):
    def __init__(self, parent, mach):
        XPathNodeHolder.__init__(self, parent, mach, 'nics', XPathNodeVM, 'nics')
        self.maxNic = mach.platform.properties.getMaxNetworkAdapters(mach.platform.chipsetType)
    def enum(self):
        children = []
        for i in range(0, self.maxNic):
            node = XPathNodeNIC(self, self.obj.getNetworkAdapter(i))
            children.append(node)
        return children

class XPathNodeNIC(XPathNode):
    def __init__(self, parent, obj):
        XPathNode.__init__(self, parent, obj, 'nic')
    def matches(self, subexp):
        return subexp == 'nic'

class XPathNodeRoot(XPathNode):
    def __init__(self, ctx):
        XPathNode.__init__(self, None, None, 'root')
        self.ctx = ctx
    def enum(self):
        return [XPathNodeHolderVM(self, self.ctx['vb'])]
    def matches(self, subexp):
        return True

def eval_xpath(ctx, scope):
    pathnames = scope.split("/")[2:]
    nodes = [XPathNodeRoot(ctx)]
    for path in pathnames:
        seen = []
        while len(nodes) > 0:
            node = nodes.pop()
            seen.append(node)
        for s in seen:
            matches = s.lookup(path)
            for match in matches:
                nodes.append(match)
        if len(nodes) == 0:
            break
    return nodes

def argsToMach(ctx, args):
    if len(args) < 2:
        print("usage: %s <vmname|uuid>" % (args[0]))
        return None
    uuid = args[1]
    mach = machById(ctx, uuid)
    if not mach:
        print("Machine '%s' is unknown, use list command to find available machines" % (uuid))
    return mach

def helpSingleCmd(cmd, help_text, from_ext):
    if from_ext != 0:
        spec = " [ext from "+from_ext+"]"
    else:
        spec = ""
    print("    %s: %s%s" % (colored(cmd, 'blue'), help_text, spec))

def helpCmd(_ctx, args):
    if len(args) == 1:
        print("Help page:")
        names = list(commands.keys())
        names.sort()
        for i in names:
            helpSingleCmd(i, commands[i][0], commands[i][2])
    else:
        cmd = args[1]
        c = commands.get(cmd)
        if not c:
            print("Command '%s' not known" % (cmd))
        else:
            helpSingleCmd(cmd, c[0], c[2])
    return 0

def asEnumElem(ctx, enum, elem):
    enumVals = ctx['const'].all_values(enum)
    for e in list(enumVals.keys()):
        if str(elem) == str(enumVals[e]):
            return colored(e, 'green')
    return colored("<unknown>", 'green')

def enumFromString(ctx, enum, strg):
    enumVals = ctx['const'].all_values(enum)
    return enumVals.get(strg, None)

def listCmd(ctx, _args):
    for mach in getMachines(ctx, True):
        try:
            if mach.teleporterEnabled:
                tele = "[T] "
            else:
                tele = "    "
                print("%sMachine '%s' [%s], machineState=%s, sessionState=%s" % (tele, colVm(ctx, mach.name), mach.id, asEnumElem(ctx, "MachineState", mach.state), asEnumElem(ctx, "SessionState", mach.sessionState)))
        except Exception as e:
            printErr(ctx, e)
            if g_fVerbose:
                traceback.print_exc()
    return 0

def infoCmd(ctx, args):
    if len(args) < 2:
        print("usage: info <vmname|uuid>")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    try:
        vmos = ctx['vb'].getGuestOSType(mach.OSTypeId)
    except:
        vmos = None
    print(" One can use setvar <mach> <var> <value> to change variable, using name in [].")
    print("  Name [name]: %s" % (colVm(ctx, mach.name)))
    print("  Description [description]: %s" % (mach.description))
    print("  ID [n/a]: %s" % (mach.id))
    print("  OS Type [via OSTypeId]: %s" % (vmos.description if vmos is not None else mach.OSTypeId))
    print("  Firmware [firmwareType]: %s (%s)" % (asEnumElem(ctx, "FirmwareType", mach.firmwareSettings.firmwareType), mach.firmwareSettings.firmwareType))
    print()
    print("  CPUs [CPUCount]: %d" % (mach.CPUCount))
    print("  RAM [memorySize]: %dM" % (mach.memorySize))
    print("  VRAM [VRAMSize]: %dM" % (mach.graphicsAdapter.VRAMSize))
    print("  Monitors [monitorCount]: %d" % (mach.graphicsAdapter.monitorCount))
    print("  Chipset [chipsetType]: %s (%s)" % (asEnumElem(ctx, "ChipsetType", mach.platform.chipsetType), mach.platform.chipsetType))
    print()
    print("  Clipboard mode [clipboardMode]: %s (%s)" % (asEnumElem(ctx, "ClipboardMode", mach.clipboardMode), mach.clipboardMode))
    print("  Machine status [n/a]: %s (%s)" % (asEnumElem(ctx, "SessionState", mach.sessionState), mach.sessionState))
    print()
    if mach.teleporterEnabled:
        print("  Teleport target on port %d (%s)" % (mach.teleporterPort, mach.teleporterPassword))
        print()
    print("  ACPI [BIOSSettings.ACPIEnabled]: %s" % (asState(mach.firmwareSettings.ACPIEnabled)))
    print("  APIC [BIOSSettings.IOAPICEnabled]: %s" % (asState(mach.firmwareSettings.IOAPICEnabled)))
    if mach.platform.architecture == ctx['global'].constants.PlatformArchitecture_x86:
        hwVirtEnabled = mach.platform.x86.getHWVirtExProperty(ctx['global'].constants.HWVirtExPropertyType_Enabled)
        print("  Hardware virtualization [guest win machine.setHWVirtExProperty(ctx[\\'const\\'].HWVirtExPropertyType_Enabled, value)]: " + asState(hwVirtEnabled))
        hwVirtVPID = mach.platform.x86.getHWVirtExProperty(ctx['const'].HWVirtExPropertyType_VPID)
        print("  VPID support [guest win machine.setHWVirtExProperty(ctx[\\'const\\'].HWVirtExPropertyType_VPID, value)]: " + asState(hwVirtVPID))
        hwVirtNestedPaging = mach.platform.x86.getHWVirtExProperty(ctx['const'].HWVirtExPropertyType_NestedPaging)
        print("  Nested paging [guest win machine.setHWVirtExProperty(ctx[\\'const\\'].HWVirtExPropertyType_NestedPaging, value)]: " + asState(hwVirtNestedPaging))
        print("  HPET [HPETEnabled]: %s" % (asState(mach.platform.x86.HPETEnabled)))

    print("  Hardware 3d acceleration [accelerate3DEnabled]: " + asState(mach.graphicsAdapter.isFeatureEnabled(ctx['const'].GraphicsFeature_Acceleration3D)))
    print("  Hardware 2d video acceleration [accelerate2DVideoEnabled]: " + asState(mach.graphicsAdapter.isFeatureEnabled(ctx['const'].GraphicsFeature_Acceleration2DVideo)))
    print("  Use universal time [RTCUseUTC]: %s" % (asState(mach.platform.RTCUseUTC)))
    audioAdp = mach.audioSettings.adapter
    if audioAdp.enabled:
        print("  Audio [via audioAdapter]: chip %s; host driver %s" % (asEnumElem(ctx, "AudioControllerType", audioAdp.audioController), asEnumElem(ctx, "AudioDriverType",  audioAdp.audioDriver)))
    print("  CPU hotplugging [CPUHotPlugEnabled]: %s" % (asState(mach.CPUHotPlugEnabled)))

    print("  Keyboard [keyboardHIDType]: %s (%s)" % (asEnumElem(ctx, "KeyboardHIDType", mach.keyboardHIDType), mach.keyboardHIDType))
    print("  Pointing device [pointingHIDType]: %s (%s)" % (asEnumElem(ctx, "PointingHIDType", mach.pointingHIDType), mach.pointingHIDType))
    print("  Last changed [n/a]: " + time.asctime(time.localtime(int(mach.lastStateChange)/1000)))
    # OSE has no VRDE
    try:
        print("  VRDE server [VRDEServer.enabled]: %s" % (asState(mach.VRDEServer.enabled)))
    except:
        pass

    print()
    print(colCat(ctx, "  USB Controllers:"))
    for oUsbCtrl in ctx['global'].getArray(mach, 'USBControllers'):
        print("    '%s': type %s  standard: %#x" \
            % (oUsbCtrl.name, asEnumElem(ctx, "USBControllerType", oUsbCtrl.type), oUsbCtrl.USBStandard))

    print()
    print(colCat(ctx, "  I/O subsystem info:"))
    print("   Cache enabled [IOCacheEnabled]: %s" % (asState(mach.IOCacheEnabled)))
    print("   Cache size [IOCacheSize]: %dM" % (mach.IOCacheSize))

    controllers = ctx['global'].getArray(mach, 'storageControllers')
    if controllers:
        print()
        print(colCat(ctx, "  Storage Controllers:"))
    for controller in controllers:
        print("    '%s': bus %s type %s" % (controller.name, asEnumElem(ctx, "StorageBus", controller.bus), asEnumElem(ctx, "StorageControllerType", controller.controllerType)))

    attaches = ctx['global'].getArray(mach, 'mediumAttachments')
    if attaches:
        print()
        print(colCat(ctx, "  Media:"))
    for att in attaches:
        print("   Controller: '%s' port/device: %d:%d type: %s (%s):" % (att.controller, att.port, att.device, asEnumElem(ctx, "DeviceType", att.type), att.type))
        medium = att.medium
        if att.type == ctx['global'].constants.DeviceType_HardDisk:
            print("   HDD:")
            print("    Id: %s" % (medium.id))
            print("    Location: %s" % (colPath(ctx, medium.location)))
            print("    Name: %s" % (medium.name))
            print("    Format: %s" % (medium.format))

        if att.type == ctx['global'].constants.DeviceType_DVD:
            print("   DVD:")
            if medium:
                print("    Id: %s" % (medium.id))
                print("    Name: %s" % (medium.name))
                if medium.hostDrive:
                    print("    Host DVD %s" % (colPath(ctx, medium.location)))
                    if att.passthrough:
                        print("    [passthrough mode]")
                else:
                    print("    Virtual image at %s" % (colPath(ctx, medium.location)))
                    print("    Size: %s" % (medium.size))

        if att.type == ctx['global'].constants.DeviceType_Floppy:
            print("   Floppy:")
            if medium:
                print("    Id: %s" % (medium.id))
                print("    Name: %s" % (medium.name))
                if medium.hostDrive:
                    print("    Host floppy %s" % (colPath(ctx, medium.location)))
                else:
                    print("    Virtual image at %s" % (colPath(ctx, medium.location)))
                    print("    Size: %s" % (medium.size))

    print()
    print(colCat(ctx, "  Shared folders:"))
    for sharedfolder in ctx['global'].getArray(mach, 'sharedFolders'):
        printSf(ctx, sharedfolder)

    return 0

def startCmd(ctx, args):
    if len(args) < 2:
        print("usage: start <vmname|uuid> <frontend>")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    if len(args) > 2:
        vmtype = args[2]
    else:
        vmtype = "gui"
    startVm(ctx, mach, vmtype)
    return 0

def createVmCmd(ctx, args):
    if len(args) != 4:
        print("usage: createvm <name> <arch> <ostype>")
        return 0
    name = args[1]
    arch = args[2]
    oskind = args[3]
    try:
        ctx['vb'].getGuestOSType(oskind)
    except Exception:
        print('Unknown OS type:', oskind)
        return 0
    createVm(ctx, name, arch, oskind)
    return 0

def ginfoCmd(ctx, args):
    if len(args) < 2:
        print("usage: ginfo <vmname|uuid>")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    cmdExistingVm(ctx, mach, 'ginfo', '')
    return 0

def gstctlPrintOk(_ctx, string):
    return print(colored(string, 'green'))

def gstctlPrintErr(_ctx, string):
    return print(colored(string, 'red'))

def execInGuest(ctx, console, args, env, user, passwd, tmo, inputPipe=None, _outputPipe=None):
    if len(args) < 1:
        print("exec in guest needs at least program name")
        return 1
    guest = console.guest
    # shall contain program name as argv[0]
    gargs = args
    if g_fVerbose:
        gstctlPrintOk(ctx, "starting guest session for user '%s' (password '%s')" % (user, passwd))
    else:
        gstctlPrintOk(ctx, ("starting guest session for user '%s' ..." % (user)))
    try:
        guestSession = guest.createSession(user, passwd, "", "vboxshell guest exec")
        guestSession.waitForArray([ ctx['global'].constants.GuestSessionWaitForFlag_Start ], 30 * 1000)
    except Exception as e:
        gstctlPrintErr(ctx, "starting guest session failed:")
        printErr(ctx, e)
        return 1
    if g_fVerbose:
        gstctlPrintOk(ctx, "guest session %d started" % guestSession.id)
    aProcCreateFlags = [ ctx['global'].constants.ProcessCreateFlag_WaitForStdOut, \
                         ctx['global'].constants.ProcessCreateFlag_WaitForStdErr ]
    if inputPipe is not None:
        aProcCreateFlags.extend([ ctx['global'].constants.ProcessCreateFlag_WaitForStdIn ])
    if g_fVerbose:
        gstctlPrintOk(ctx, "starting process '%s' with args '%s' as user '%s' (password '%s')" % (args[0], gargs, user, passwd))
    process = guestSession.processCreate(args[0], gargs, '', env, aProcCreateFlags, tmo)
    try:
        waitResult = process.waitForArray([ ctx['global'].constants.ProcessWaitForFlag_Start ], 30 * 1000)
    except Exception as e:
        gstctlPrintErr(ctx, "waiting for guest process start failed:")
        printErr(ctx, e)
        return 1
    if waitResult != ctx['global'].constants.ProcessWaitResult_Start:
        gstctlPrintErr(ctx, "process start failed: got wait result %d, expected %d" \
                       % (waitResult, ctx['global'].constants.ProcessWaitResult_Start) )
        return 1
    procStatus = process.status
    if procStatus != ctx['global'].constants.ProcessStatus_Started:
        gstctlPrintErr(ctx, "process start failed: got process status %d, expected %d" \
                       % (procStatus, ctx['global'].constants.ProcessStatus_Started) )
        return 1
    if g_fVerbose:
        gstctlPrintOk(ctx, "process %d started" % (process.PID))
    if process.PID != 0:
        try:
            fCompleted  = False
            fReadStdOut = False
            fReadStdErr = False
            while not fCompleted:
                waitResult = process.waitForArray([ ctx['global'].constants.ProcessWaitForFlag_Terminate, \
                                                    ctx['global'].constants.ProcessWaitForFlag_StdOut, \
                                                    ctx['global'].constants.ProcessWaitForFlag_StdErr ], 1000)
                if waitResult == ctx['global'].constants.ProcessWaitResult_WaitFlagNotSupported:
                    fReadStdOut = True
                    fReadStdErr = True
                elif waitResult == ctx['global'].constants.ProcessWaitResult_Terminate:
                    fCompleted = True
                    break
                elif waitResult == ctx['global'].constants.ProcessWaitResult_Timeout:
                    gstctlPrintErr(ctx, "timeout while waiting for process")
                    break
                else:
                    gstctlPrintErr(ctx, "got unhandled wait result %d" % (waitResult))
                if inputPipe:
                    indata = inputPipe(ctx)
                    if indata is not None:
                        write = len(indata)
                        off = 0
                        while write > 0:
                            written = process.write(0, 10*1000, indata[off:])
                            off = off + written
                            write = write - written
                    else:
                        # EOF
                        try:
                            process.write(0, 10*1000, " ")
                        except:
                            pass
                if fReadStdOut:
                    data = process.read(1, 64 * 1024, 10*1000)
                    if data and len(data):
                        sys.stdout.write(bytes(data).decode('utf-8'))
                    fReadStdOut = False
                if fReadStdErr:
                    data = process.read(2, 64 * 1024, 10*1000)
                    if data and len(data):
                        sys.stderr.write(bytes(data).decode('utf-8'))
                    fReadStdErr = False
                ctx['global'].waitForEvents(0)

            if fCompleted:
                exitCode = process.exitCode
                if exitCode == 0:
                    gstctlPrintOk(ctx, "process exit code: %d" % (exitCode))
                else:
                    gstctlPrintErr(ctx, "process exit code: %d" % (exitCode))

        except KeyboardInterrupt:
            print("Interrupted.")
            ctx['interrupt'] = True

        except Exception as e:
            printErr(ctx, e)

    if guestSession:
        try:
            if g_fVerbose:
                gstctlPrintOk(ctx, "closing guest session ...")
            guestSession.close()
        except:
            printErr(ctx, e)

    return 0


def copyToGuest(ctx, console, args, user, passwd):
    src = args[0]
    dst = args[1]
    flags = 0
    print("Copying host %s to guest %s" % (src, dst))
    progress = console.guest.copyToGuest(src, dst, user, passwd, flags)
    progressBar(ctx, progress)

def nh_raw_input(prompt=""):
    if prompt:
        sys.stdout.write(prompt)
        sys.stdout.flush()
    line = sys.stdin.readline()
    if not line:
        raise EOFError
    if line[-1] == '\n':
        line = line[:-1]
    return line

def getCred(_ctx):
    import getpass
    user = getpass.getuser()
    if user:
        user_inp = nh_raw_input("User (%s): " % (user))
    else:
        user_inp = nh_raw_input("User: ")
    if len(user_inp) > 0:
        user = user_inp
    passwd = getpass.getpass()

    return (user, passwd)

def gexecCmd(ctx, args):
    if len(args) < 2:
        print("usage: gexec <vmname|uuid> command args")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    gargs = args[2:]
    env = [] # ["DISPLAY=:0"]
    (user, passwd) = getCred(ctx)
    gargs.insert(0, lambda ctx, mach, console, args: execInGuest(ctx, console, args, env, user, passwd, 10000))
    cmdExistingVm(ctx, mach, 'guestlambda', gargs)
    return 0

def gcopyCmd(ctx, args):
    if len(args) < 2:
        print("usage: gcopy <vmname|uuid> host_path guest_path")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    gargs = args[2:]
    (user, passwd) = getCred(ctx)
    gargs.insert(0, lambda ctx, mach, console, args: copyToGuest(ctx, console, args, user, passwd))
    cmdExistingVm(ctx, mach, 'guestlambda', gargs)
    return 0

def readCmdPipe(ctx, _hcmd):
    try:
        return ctx['process'].communicate()[0]
    except:
        return None

def gpipeCmd(ctx, args):
    if len(args) < 4:
        print("usage: gpipe <vmname|uuid> hostProgram guestProgram, such as gpipe linux  '/bin/uname -a' '/bin/sh -c \"/usr/bin/tee; /bin/uname -a\"'")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    hcmd = args[2]
    gcmd = args[3]
    (user, passwd) = getCred(ctx)
    import subprocess
    with subprocess.Popen(split_no_quotes(hcmd), stdout=subprocess.PIPE) as ctx['process']:
        gargs = split_no_quotes(gcmd)
        env = []
        gargs.insert(0, lambda ctx, mach, console, args: execInGuest(ctx, console, args, env, user, passwd, 10000, lambda ctx:readCmdPipe(ctx, hcmd)))
        cmdExistingVm(ctx, mach, 'guestlambda', gargs)
        try:
            ctx['process'].terminate()
        except:
            pass
        ctx['process'] = None
    return 0


def removeVmCmd(ctx, args):
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    removeVm(ctx, mach)
    return 0

def pauseCmd(ctx, args):
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    cmdExistingVm(ctx, mach, 'pause', '')
    return 0

def powerdownCmd(ctx, args):
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    cmdExistingVm(ctx, mach, 'powerdown', '')
    return 0

def powerbuttonCmd(ctx, args):
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    cmdExistingVm(ctx, mach, 'powerbutton', '')
    return 0

def resumeCmd(ctx, args):
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    cmdExistingVm(ctx, mach, 'resume', '')
    return 0

def saveCmd(ctx, args):
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    cmdExistingVm(ctx, mach, 'save', '')
    return 0

def statsCmd(ctx, args):
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    cmdExistingVm(ctx, mach, 'stats', '')
    return 0

def guestCmd(ctx, args):
    if len(args) < 3:
        print("usage: guest <vmname|uuid> commands")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    if mach.state != ctx['const'].MachineState_Running:
        cmdClosedVm(ctx, mach, lambda ctx, mach, a: guestExec (ctx, mach, None, ' '.join(args[2:])))
    else:
        cmdExistingVm(ctx, mach, 'guest', ' '.join(args[2:]))
    return 0

def screenshotCmd(ctx, args):
    if len(args) < 2:
        print("usage: screenshot <vmname|uuid> <file> <width> <height> <monitor>")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    cmdExistingVm(ctx, mach, 'screenshot', args[2:])
    return 0

def teleportCmd(ctx, args):
    if len(args) < 3:
        print("usage: teleport <vmname|uuid> host:port <password>")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    cmdExistingVm(ctx, mach, 'teleport', args[2:])
    return 0

def portalsettings(_ctx, mach, args):
    enabled = args[0]
    mach.teleporterEnabled = enabled
    if enabled:
        port = args[1]
        passwd = args[2]
        mach.teleporterPort = port
        mach.teleporterPassword = passwd

def openportalCmd(ctx, args):
    if len(args) < 3:
        print("usage: openportal <vmname|uuid> port <password>")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    port = int(args[2])
    if len(args) > 3:
        passwd = args[3]
    else:
        passwd = ""
    if not mach.teleporterEnabled or mach.teleporterPort != port or passwd:
        cmdClosedVm(ctx, mach, portalsettings, [True, port, passwd])
    startVm(ctx, mach, "gui")
    return 0

def closeportalCmd(ctx, args):
    if len(args) < 2:
        print("usage: closeportal <vmname|uuid>")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    if mach.teleporterEnabled:
        cmdClosedVm(ctx, mach, portalsettings, [False])
    return 0

def gueststatsCmd(ctx, args):
    if len(args) < 2:
        print("usage: gueststats <vmname|uuid> <check interval>")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    cmdExistingVm(ctx, mach, 'gueststats', args[2:])
    return 0

def plugcpu(_ctx, mach, args):
    plug = args[0]
    cpu = args[1]
    if plug:
        print("Adding CPU %d..." % (cpu))
        mach.hotPlugCPU(cpu)
    else:
        print("Removing CPU %d..." % (cpu))
        mach.hotUnplugCPU(cpu)

def plugcpuCmd(ctx, args):
    if len(args) < 2:
        print("usage: plugcpu <vmname|uuid> <cpuid>")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    if str(mach.sessionState) != str(ctx['const'].SessionState_Locked):
        if mach.CPUHotPlugEnabled:
            cmdClosedVm(ctx, mach, plugcpu, [True, int(args[2])])
    else:
        cmdExistingVm(ctx, mach, 'plugcpu', args[2])
    return 0

def unplugcpuCmd(ctx, args):
    if len(args) < 2:
        print("usage: unplugcpu <vmname|uuid> <cpuid>")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    if str(mach.sessionState) != str(ctx['const'].SessionState_Locked):
        if mach.CPUHotPlugEnabled:
            cmdClosedVm(ctx, mach, plugcpu, [False, int(args[2])])
    else:
        cmdExistingVm(ctx, mach, 'unplugcpu', args[2])
    return 0

def setvar(_ctx, _mach, args):
    expr = 'mach.'+args[0]+' = '+args[1]
    print("Executing", expr)
    exec(expr) # pylint: disable=exec-used

def setvarCmd(ctx, args):
    if len(args) < 4:
        print("usage: setvar <vmname|uuid> <expr> <value>")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    cmdClosedVm(ctx, mach, setvar, args[2:])
    return 0

def setvmextra(_ctx, mach, args):
    key = args[0]
    value = args[1]
    print("%s: setting %s to %s" % (mach.name, key, value if value else None))
    mach.setExtraData(key, value)

def setExtraDataCmd(ctx, args):
    if len(args) < 3:
        print("usage: setextra [vmname|uuid|global] key <value>")
        return 0
    key = args[2]
    if len(args) == 4:
        value = args[3]
    else:
        value = ''
    if args[1] == 'global':
        ctx['vb'].setExtraData(key, value)
        return 0

    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    cmdClosedVm(ctx, mach, setvmextra, [key, value])
    return 0

def printExtraKey(obj, key, value):
    print("%s: '%s' = '%s'" % (obj, key, value))

def getExtraDataCmd(ctx, args):
    if len(args) < 2:
        print("usage: getextra [vmname|uuid|global] <key>")
        return 0
    if len(args) == 3:
        key = args[2]
    else:
        key = None

    if args[1] == 'global':
        obj = ctx['vb']
    else:
        obj = argsToMach(ctx, args)
        if not obj:
            return 0

    if not key:
        keys = obj.getExtraDataKeys()
    else:
        keys = [ key ]
    for k in keys:
        printExtraKey(args[1], k, obj.getExtraData(k))

    return 0

def quitCmd(_ctx, _args):
    return 1

def aliasCmd(_ctx, args):
    if len(args) == 3:
        aliases[args[1]] = args[2]
        return 0

    for (key, value) in list(aliases.items()):
        print("'%s' is an alias for '%s'" % (key, value))
    return 0

def verboseCmd(_ctx, args):
    global g_fVerbose
    if len(args) > 1:
        g_fVerbose = args[1]=='on'
    else:
        g_fVerbose = not g_fVerbose
    return 0

def colorsCmd(_ctx, args):
    global g_fHasColors
    if len(args) > 1:
        g_fHasColors = args[1] == 'on'
    else:
        g_fHasColors = not g_fHasColors
    return 0

def hostCmd(ctx, _args):
    vbox = ctx['vb']
    try:
        print("VirtualBox version %s" % (colored(vbox.version, 'blue')))
    except Exception as e:
        printErr(ctx, e)
        if g_fVerbose:
            traceback.print_exc()
    props = vbox.systemProperties
    print("Machines: %s" % (colPath(ctx, props.defaultMachineFolder)))

    #print("Global shared folders:")
    #for ud in ctx['global'].getArray(vbox, 'sharedFolders'):
    #    printSf(ctx, sf)
    host = vbox.host
    cnt = host.processorCount
    print(colCat(ctx, "Processors:"))
    print("  available/online: %d/%d " % (cnt, host.processorOnlineCount))
    for i in range(0, cnt):
        print("  processor #%d speed: %dMHz %s" % (i, host.getProcessorSpeed(i), host.getProcessorDescription(i)))

    print(colCat(ctx, "RAM:"))
    print("  %dM (free %dM)" % (host.memorySize, host.memoryAvailable))
    print(colCat(ctx, "OS:"))
    print("  %s (%s)" % (host.operatingSystem, host.OSVersion))

    print(colCat(ctx, "Network interfaces:"))
    for iface in ctx['global'].getArray(host, 'networkInterfaces'):
        print("  %s (%s)" % (iface.name, iface.IPAddress))

    print(colCat(ctx, "DVD drives:"))
    for drive in ctx['global'].getArray(host, 'DVDDrives'):
        print("  %s - %s" % (drive.name, drive.description))

    print(colCat(ctx, "Floppy drives:"))
    for drive in ctx['global'].getArray(host, 'floppyDrives'):
        print("  %s - %s" % (drive.name, drive.description))

    print(colCat(ctx, "USB devices:"))
    for usbdev in ctx['global'].getArray(host, 'USBDevices'):
        printHostUsbDev(ctx, usbdev)

    if ctx['perf']:
        for metric in ctx['perf'].query(["*"], [host]):
            print(metric['name'], metric['values_as_string'])

    return 0

def monitorGuestCmd(ctx, args):
    if len(args) < 2:
        print("usage: monitorGuest <vmname|uuid> (duration)")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    dur = 5
    if len(args) > 2:
        dur = float(args[2])
    active = False
    cmdExistingVm(ctx, mach, 'guestlambda', [lambda ctx, mach, console, args:  monitorSource(ctx, console.eventSource, active, dur)])
    return 0

def monitorGuestKbdCmd(ctx, args):
    if len(args) < 2:
        print("usage: monitorGuestKbd name (duration)")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    dur = 5
    if len(args) > 2:
        dur = float(args[2])
    active = False
    cmdExistingVm(ctx, mach, 'guestlambda', [lambda ctx, mach, console, args:  monitorSource(ctx, console.keyboard.eventSource, active, dur)])
    return 0

def monitorGuestMouseCmd(ctx, args):
    if len(args) < 2:
        print("usage: monitorGuestMouse name (duration)")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    dur = 5
    if len(args) > 2:
        dur = float(args[2])
    active = False
    cmdExistingVm(ctx, mach, 'guestlambda', [lambda ctx, mach, console, args:  monitorSource(ctx, console.mouse.eventSource, active, dur)])
    return 0

def monitorGuestMultiTouchCmd(ctx, args):
    if len(args) < 2:
        print("usage: monitorGuestMultiTouch name (duration)")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    dur = 5
    if len(args) > 2:
        dur = float(args[2])
    active = False
    cmdExistingVm(ctx, mach, 'guestlambda', [lambda ctx, mach, console, args:  monitorSource(ctx, console.mouse.eventSource, active, dur)])
    return 0

def monitorVBoxCmd(ctx, args):
    if len(args) > 2:
        print("usage: monitorVBox (duration)")
        return 0
    dur = 5
    if len(args) > 1:
        dur = float(args[1])
    vbox = ctx['vb']
    active = False
    monitorSource(ctx, vbox.eventSource, active, dur)
    return 0

def getAdapterType(ctx, natype):
    if (natype in (  ctx['global'].constants.NetworkAdapterType_Am79C970A
                   , ctx['global'].constants.NetworkAdapterType_Am79C973
                   , ctx['global'].constants.NetworkAdapterType_Am79C960)):
        return "pcnet"
    if (natype in (  ctx['global'].constants.NetworkAdapterType_I82540EM
                   , ctx['global'].constants.NetworkAdapterType_I82545EM
                   , ctx['global'].constants.NetworkAdapterType_I82543GC)):
        return "e1000"
    if natype == ctx['global'].constants.NetworkAdapterType_Virtio:
        return "virtio"
    if natype == ctx['global'].constants.NetworkAdapterType_Null:
        return None
    raise Exception("Unknown adapter type: "+natype)

def portForwardCmd(ctx, args):
    if len(args) != 5:
        print("usage: portForward <vmname|uuid> <adapter> <hostPort> <guestPort>")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    adapterNum = int(args[2])
    hostPort = int(args[3])
    guestPort = int(args[4])
    proto = "TCP"
    session = ctx['global'].openMachineSession(mach, fPermitSharing=True)
    mach = session.machine

    adapter = mach.getNetworkAdapter(adapterNum)
    adapterType = getAdapterType(ctx, adapter.adapterType)

    profile_name = proto+"_"+str(hostPort)+"_"+str(guestPort)
    config = "VBoxInternal/Devices/" + adapterType + "/"
    config = config + str(adapter.slot)  +"/LUN#0/Config/" + profile_name

    mach.setExtraData(config + "/Protocol", proto)
    mach.setExtraData(config + "/HostPort", str(hostPort))
    mach.setExtraData(config + "/GuestPort", str(guestPort))

    mach.saveSettings()
    session.unlockMachine()

    return 0


def showLogCmd(ctx, args):
    if len(args) < 2:
        print("usage: showLog <vmname|uuid> <num>")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0

    log = 0
    if len(args) > 2:
        log = args[2]

    uOffset = 0
    while True:
        data = mach.readLog(log, uOffset, 4096)
        if len(data) == 0:
            break
        # print adds either NL or space to chunks not ending with a NL
        sys.stdout.write(str(data))
        uOffset += len(data)

    return 0

def findLogCmd(ctx, args):
    if len(args) < 3:
        print("usage: findLog <vmname|uuid> <pattern> <num>")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0

    log = 0
    if len(args) > 3:
        log = args[3]

    pattern = args[2]
    uOffset = 0
    while True:
        # to reduce line splits on buffer boundary
        data = mach.readLog(log, uOffset, 512*1024)
        if len(data) == 0:
            break
        buf = str(data).split("\n")
        for line in buf:
            match = re.findall(pattern, line)
            if len(match) > 0:
                for cur_match in match:
                    line = line.replace(cur_match, colored(cur_match, 'red'))
                print(line)
        uOffset += len(data)

    return 0


def findAssertCmd(ctx, args):
    if len(args) < 2:
        print("usage: findAssert <vmname|uuid> <num>")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0

    log = 0
    if len(args) > 2:
        log = args[2]

    uOffset = 0
    ere = re.compile(r'(Expression:|\!\!\!\!\!\!)')
    active = False
    context = 0
    while True:
        # to reduce line splits on buffer boundary
        data = mach.readLog(log, uOffset, 512*1024)
        if len(data) == 0:
            break
        buf = str(data).split("\n")
        for line in buf:
            if active:
                print(line)
                if context == 0:
                    active = False
                else:
                    context = context - 1
                continue
            match = ere.findall(line)
            if len(match) > 0:
                active = True
                context = 50
                print(line)
        uOffset += len(data)

    return 0

def evalCmd(ctx, args):
    expr = ' '.join(args[1:])
    try:
        exec(expr) # pylint: disable=exec-used
    except Exception as e:
        printErr(ctx, e)
        if g_fVerbose:
            traceback.print_exc()
    return 0

def reloadExtCmd(ctx, _args):
    # maybe will want more args smartness
    checkUserExtensions(ctx, commands, getHomeFolder(ctx))
    autoCompletion(commands, ctx)
    return 0

def runScriptCmd(ctx, args):
    if len(args) != 2:
        print("usage: runScript <script>")
        return 0

    try:
        with open(args[1], 'r', encoding='utf-8') as file:
            try:
                lines = file.readlines()
                ctx['scriptLine'] = 0
                ctx['interrupt'] = False
                while ctx['scriptLine'] < len(lines):
                    line = lines[ctx['scriptLine']]
                    ctx['scriptLine'] = ctx['scriptLine'] + 1
                    done = runCommand(ctx, line)
                    if done != 0 or ctx['interrupt']:
                        break

            except Exception as e:
                printErr(ctx, e)
                if g_fVerbose:
                    traceback.print_exc()
            file.close()
    except IOError as e:
        print("cannot open:", args[1], ":", e)
        return 1
    return 0

def sleepCmd(_ctx, args):
    if len(args) != 2:
        print("usage: sleep <secs>")
        return 0

    try:
        time.sleep(float(args[1]))
    except:
        # to allow sleep interrupt
        pass
    return 0


def shellCmd(_ctx, args):
    if len(args) < 2:
        print("usage: shell <commands>")
        return 0
    cmd = ' '.join(args[1:])

    try:
        os.system(cmd)
    except KeyboardInterrupt:
        # to allow shell command interruption
        pass
    return 0


def connectCmd(ctx, args):
    if len(args) > 4:
        print("usage: connect url <username> <passwd>")
        return 0

    if ctx['vb'] is not None:
        print("Already connected, disconnect first...")
        return 0

    if len(args) > 1:
        url = args[1]
    else:
        url = None

    if len(args) > 2:
        user = args[2]
    else:
        user = ""

    if len(args) > 3:
        passwd = args[3]
    else:
        passwd = ""

    ctx['wsinfo'] = [url, user, passwd]
    ctx['vb'] = ctx['global'].platform.connect(url, user, passwd)
    try:
        print("Running VirtualBox version %s" % (ctx['vb'].version))
    except Exception as e:
        printErr(ctx, e)
        if g_fVerbose:
            traceback.print_exc()
    ctx['perf'] = ctx['global'].getPerfCollector(ctx['vb'])
    return 0

def disconnectCmd(ctx, args):
    if len(args) != 1:
        print("usage: disconnect")
        return 0

    if ctx['vb'] is None:
        print("Not connected yet.")
        return 0

    try:
        ctx['global'].platform.disconnect()
    except:
        ctx['vb'] = None
        raise

    ctx['vb'] = None
    return 0

def reconnectCmd(ctx, _args):
    if ctx['wsinfo'] is None:
        print("Never connected...")
        return 0

    try:
        ctx['global'].platform.disconnect()
    except:
        pass

    [url, user, passwd] = ctx['wsinfo']
    ctx['vb'] = ctx['global'].platform.connect(url, user, passwd)
    try:
        print("Running VirtualBox version %s" % (ctx['vb'].version))
    except Exception as e:
        printErr(ctx, e)
        if g_fVerbose:
            traceback.print_exc()
    ctx['perf'] = ctx['global'].getPerfCollector(ctx['vb'])
    return 0

def exportVMCmd(ctx, args):
    if len(args) < 3:
        print("usage: exportVm <machine> <path> <format> <license>")
        return 0
    mach = argsToMach(ctx, args)
    if mach is None:
        return 0
    path = args[2]
    if len(args) > 3:
        fmt = args[3]
    else:
        fmt = "ovf-1.0"
    if len(args) > 4:
        lic = args[4]
    else:
        lic = "GPL"

    app = ctx['vb'].createAppliance()
    desc = mach.export(app)
    desc.addDescription(ctx['global'].constants.VirtualSystemDescriptionType_License, lic, "")
    progress = app.write(fmt, path)
    if (progressBar(ctx, progress) and int(progress.resultCode) == 0):
        print("Exported to %s in format %s" % (path, fmt))
    else:
        reportError(ctx, progress)
    return 0

# PC XT scancodes
scancodes = {
    'a':  0x1e,
    'b':  0x30,
    'c':  0x2e,
    'd':  0x20,
    'e':  0x12,
    'f':  0x21,
    'g':  0x22,
    'h':  0x23,
    'i':  0x17,
    'j':  0x24,
    'k':  0x25,
    'l':  0x26,
    'm':  0x32,
    'n':  0x31,
    'o':  0x18,
    'p':  0x19,
    'q':  0x10,
    'r':  0x13,
    's':  0x1f,
    't':  0x14,
    'u':  0x16,
    'v':  0x2f,
    'w':  0x11,
    'x':  0x2d,
    'y':  0x15,
    'z':  0x2c,
    '0':  0x0b,
    '1':  0x02,
    '2':  0x03,
    '3':  0x04,
    '4':  0x05,
    '5':  0x06,
    '6':  0x07,
    '7':  0x08,
    '8':  0x09,
    '9':  0x0a,
    ' ':  0x39,
    '-':  0xc,
    '=':  0xd,
    '[':  0x1a,
    ']':  0x1b,
    ';':  0x27,
    '\'': 0x28,
    ',':  0x33,
    '.':  0x34,
    '/':  0x35,
    '\t': 0xf,
    '\n': 0x1c,
    '`':  0x29
}

extScancodes = {
    'ESC' :    [0x01],
    'BKSP':    [0xe],
    'SPACE':   [0x39],
    'TAB':     [0x0f],
    'CAPS':    [0x3a],
    'ENTER':   [0x1c],
    'LSHIFT':  [0x2a],
    'RSHIFT':  [0x36],
    'INS':     [0xe0, 0x52],
    'DEL':     [0xe0, 0x53],
    'END':     [0xe0, 0x4f],
    'HOME':    [0xe0, 0x47],
    'PGUP':    [0xe0, 0x49],
    'PGDOWN':  [0xe0, 0x51],
    'LGUI':    [0xe0, 0x5b], # GUI, aka Win, aka Apple key
    'RGUI':    [0xe0, 0x5c],
    'LCTR':    [0x1d],
    'RCTR':    [0xe0, 0x1d],
    'LALT':    [0x38],
    'RALT':    [0xe0, 0x38],
    'APPS':    [0xe0, 0x5d],
    'F1':      [0x3b],
    'F2':      [0x3c],
    'F3':      [0x3d],
    'F4':      [0x3e],
    'F5':      [0x3f],
    'F6':      [0x40],
    'F7':      [0x41],
    'F8':      [0x42],
    'F9':      [0x43],
    'F10':     [0x44 ],
    'F11':     [0x57],
    'F12':     [0x58],
    'UP':      [0xe0, 0x48],
    'LEFT':    [0xe0, 0x4b],
    'DOWN':    [0xe0, 0x50],
    'RIGHT':   [0xe0, 0x4d],
}

def keyDown(ch):
    code = scancodes.get(ch, 0x0)
    if code != 0:
        return [code]
    extCode = extScancodes.get(ch, [])
    if len(extCode) == 0:
        print("bad ext", ch)
    return extCode

def keyUp(ch):
    codes = keyDown(ch)[:] # make a copy
    if len(codes) > 0:
        codes[len(codes)-1] += 0x80
    return codes

def typeInGuest(console, text, delay):
    pressed = []
    group = False
    modGroupEnd = True
    i = 0
    kbd = console.keyboard
    while i < len(text):
        ch = text[i]
        i = i+1
        if ch == '{':
            # start group, all keys to be pressed at the same time
            group = True
            continue
        if ch == '}':
            # end group, release all keys
            for c in pressed:
                kbd.putScancodes(keyUp(c))
            pressed = []
            group = False
            continue
        if ch == 'W':
            # just wait a bit
            time.sleep(0.3)
            continue
        if ch in ('^', '|', '$', '_'):
            if ch == '^':
                ch = 'LCTR'
            if ch == '|':
                ch = 'LSHIFT'
            if ch == '_':
                ch = 'LALT'
            if ch == '$':
                ch = 'LGUI'
            if not group:
                modGroupEnd = False
        else:
            if ch == '\\':
                if i < len(text):
                    ch = text[i]
                    i = i+1
                    if ch == 'n':
                        ch = '\n'
            elif ch == '&':
                combo = ""
                while i  < len(text):
                    ch = text[i]
                    i = i+1
                    if ch == ';':
                        break
                    combo += ch
                ch = combo
            modGroupEnd = True
        kbd.putScancodes(keyDown(ch))
        pressed.insert(0, ch)
        if not group and modGroupEnd:
            for c in pressed:
                kbd.putScancodes(keyUp(c))
            pressed = []
            modGroupEnd = True
        time.sleep(delay)

def typeGuestCmd(ctx, args):
    if len(args) < 3:
        print("usage: typeGuest <machine> <text> <charDelay>")
        return 0
    mach = argsToMach(ctx, args)
    if mach is None:
        return 0

    text = args[2]

    if len(args) > 3:
        delay = float(args[3])
    else:
        delay = 0.1

    gargs = [lambda ctx, mach, console, args: typeInGuest(console, text, delay)]
    cmdExistingVm(ctx, mach, 'guestlambda', gargs)

    return 0

def optId(verbose, uuid):
    if verbose:
        return ": "+uuid
    return ""

def asSize(val, inBytes):
    if inBytes:
        return int(val)/(1024*1024)
    return int(val)

def listMediaCmd(ctx, args):
    if len(args) > 1:
        verbose = int(args[1])
    else:
        verbose = False
    hdds = ctx['global'].getArray(ctx['vb'], 'hardDisks')
    print(colCat(ctx, "Hard disks:"))
    for hdd in hdds:
        if hdd.state != ctx['global'].constants.MediumState_Created:
            hdd.refreshState()
        print("   %s (%s)%s %s [logical %s]" % (colPath(ctx, hdd.location), hdd.format, optId(verbose, hdd.id), colSizeM(ctx, asSize(hdd.size, True)), colSizeM(ctx, asSize(hdd.logicalSize, True))))

    dvds = ctx['global'].getArray(ctx['vb'], 'DVDImages')
    print(colCat(ctx, "CD/DVD disks:"))
    for dvd in dvds:
        if dvd.state != ctx['global'].constants.MediumState_Created:
            dvd.refreshState()
        print("   %s (%s)%s %s" % (colPath(ctx, dvd.location), dvd.format, optId(verbose, dvd.id), colSizeM(ctx, asSize(dvd.size, True))))

    floppys = ctx['global'].getArray(ctx['vb'], 'floppyImages')
    print(colCat(ctx, "Floppy disks:"))
    for floppy in floppys:
        if floppy.state != ctx['global'].constants.MediumState_Created:
            floppy.refreshState()
        print("   %s (%s)%s %s" % (colPath(ctx, floppy.location), floppy.format, optId(verbose, floppy.id), colSizeM(ctx, asSize(floppy.size, True))))

    return 0

def listUsbCmd(ctx, args):
    if len(args) > 1:
        print("usage: listUsb")
        return 0

    host = ctx['vb'].host
    for usbdev in ctx['global'].getArray(host, 'USBDevices'):
        printHostUsbDev(ctx, usbdev)

    return 0

def findDevOfType(ctx, mach, devtype):
    attachments = ctx['global'].getArray(mach, 'mediumAttachments')
    for att in attachments:
        if att.type == devtype:
            return [att.controller, att.port, att.device]
    return [None, 0, 0]

def createHddCmd(ctx, args):
    if len(args) < 3:
        print("usage: createHdd sizeM location type")
        return 0

    size = int(args[1])
    loc = args[2]
    if len(args) > 3:
        fmt = args[3]
    else:
        fmt = "vdi"

    hdd = ctx['vb'].createMedium(fmt, loc, ctx['global'].constants.AccessMode_ReadWrite, ctx['global'].constants.DeviceType_HardDisk)
    progress = hdd.createBaseStorage(size, (ctx['global'].constants.MediumVariant_Standard, ))
    if progressBar(ctx,progress) and hdd.id:
        print("created HDD at %s as %s" % (colPath(ctx,hdd.location), hdd.id))
    else:
        print("cannot create disk (file %s exist?)" % (loc))
        reportError(ctx,progress)
        return 0

    return 0

def registerHddCmd(ctx, args):
    if len(args) < 2:
        print("usage: registerHdd location")
        return 0

    vbox = ctx['vb']
    loc = args[1]
    hdd = vbox.openMedium(loc, ctx['global'].constants.DeviceType_HardDisk, ctx['global'].constants.AccessMode_ReadWrite, False)
    print("registered HDD as %s" % (hdd.id))
    return 0

def controldevice(_ctx, mach, args):
    [ctr, port, slot, devtype, uuid] = args
    mach.attachDevice(ctr, port, slot, devtype, uuid)

def attachHddCmd(ctx, args):
    if len(args) < 3:
        print("usage: attachHdd <vmname|uuid> <hdd> <controller> <port:slot>")
        return 0

    mach = argsToMach(ctx, args)
    if mach is None:
        return 0
    vbox = ctx['vb']
    loc = args[2]
    try:
        hdd = vbox.openMedium(loc, ctx['global'].constants.DeviceType_HardDisk, ctx['global'].constants.AccessMode_ReadWrite, False)
    except:
        print("no HDD with path %s registered" % (loc))
        return 0
    if len(args) > 3:
        ctr = args[3]
        (port, slot) = args[4].split(":")
    else:
        [ctr, port, slot] = findDevOfType(ctx, mach, ctx['global'].constants.DeviceType_HardDisk)

    cmdClosedVm(ctx, mach, lambda ctx, mach, args: mach.attachDevice(ctr, port, slot, ctx['global'].constants.DeviceType_HardDisk, hdd.id))
    return 0

def detachVmDevice(ctx, mach, args):
    attachments = ctx['global'].getArray(mach, 'mediumAttachments')
    hid = args[0]
    for att in attachments:
        if att.medium:
            if hid in ('ALL', att.medium.id):
                mach.detachDevice(att.controller, att.port, att.device)

def detachMedium(ctx, mid, medium):
    cmdClosedVm(ctx, machById(ctx, mid), detachVmDevice, [medium])

def detachHddCmd(ctx, args):
    if len(args) < 3:
        print("usage: detachHdd <vmname|uuid> <hdd>")
        return 0

    mach = argsToMach(ctx, args)
    if mach is None:
        return 0
    vbox = ctx['vb']
    loc = args[2]
    try:
        hdd = vbox.openMedium(loc, ctx['global'].constants.DeviceType_HardDisk, ctx['global'].constants.AccessMode_ReadWrite, False)
    except:
        print("no HDD with path %s registered" % (loc))
        return 0

    detachMedium(ctx, mach.id, hdd)
    return 0

def unregisterHddCmd(ctx, args):
    if len(args) < 2:
        print("usage: unregisterHdd path <vmunreg>")
        return 0

    vbox = ctx['vb']
    loc = args[1]
    if len(args) > 2:
        vmunreg = int(args[2])
    else:
        vmunreg = 0
    try:
        hdd = vbox.openMedium(loc, ctx['global'].constants.DeviceType_HardDisk, ctx['global'].constants.AccessMode_ReadWrite, False)
    except:
        print("no HDD with path %s registered" % (loc))
        return 0

    if vmunreg != 0:
        machs = ctx['global'].getArray(hdd, 'machineIds')
        try:
            for mach in machs:
                print("Trying to detach from %s" % (mach))
                detachMedium(ctx, mach, hdd)
        except Exception as e:
            print('failed: ', e)
            return 0
    hdd.close()
    return 0

def removeHddCmd(ctx, args):
    if len(args) != 2:
        print("usage: removeHdd path")
        return 0

    vbox = ctx['vb']
    loc = args[1]
    try:
        hdd = vbox.openMedium(loc, ctx['global'].constants.DeviceType_HardDisk, ctx['global'].constants.AccessMode_ReadWrite, False)
    except:
        print("no HDD with path %s registered" % (loc))
        return 0

    progress = hdd.deleteStorage()
    progressBar(ctx, progress)

    return 0

def registerIsoCmd(ctx, args):
    if len(args) < 2:
        print("usage: registerIso location")
        return 0

    vbox = ctx['vb']
    loc = args[1]
    iso = vbox.openMedium(loc, ctx['global'].constants.DeviceType_DVD, ctx['global'].constants.AccessMode_ReadOnly, False)
    print("registered ISO as %s" % (iso.id))
    return 0

def unregisterIsoCmd(ctx, args):
    if len(args) != 2:
        print("usage: unregisterIso path")
        return 0

    vbox = ctx['vb']
    loc = args[1]
    try:
        vbox.openMedium(loc, ctx['global'].constants.DeviceType_DVD, ctx['global'].constants.AccessMode_ReadOnly, False)
    except:
        print("no DVD with path %s registered" % (loc))
        return 0

    print("Unregistered ISO at %s" % (colPath(ctx, loc)))
    return 0

def removeIsoCmd(ctx, args):
    if len(args) != 2:
        print("usage: removeIso path")
        return 0

    vbox = ctx['vb']
    loc = args[1]
    try:
        dvd = vbox.openMedium(loc, ctx['global'].constants.DeviceType_DVD, ctx['global'].constants.AccessMode_ReadOnly, False)
    except:
        print("no DVD with path %s registered" % (loc))
        return 0

    progress = dvd.deleteStorage()
    if progressBar(ctx, progress):
        print("Removed ISO at %s" % (colPath(ctx, dvd.location)))
    else:
        reportError(ctx, progress)
    return 0

def attachIsoCmd(ctx, args):
    if len(args) < 3:
        print("usage: attachIso <vmname|uuid> <iso> <controller> <port:slot>")
        return 0

    mach = argsToMach(ctx, args)
    if mach is None:
        return 0
    vbox = ctx['vb']
    loc = args[2]
    try:
        dvd = vbox.openMedium(loc, ctx['global'].constants.DeviceType_DVD, ctx['global'].constants.AccessMode_ReadOnly, False)
    except:
        print("no DVD with path %s registered" % (loc))
        return 0
    if len(args) > 3:
        ctr = args[3]
        (port, slot) = args[4].split(":")
    else:
        [ctr, port, slot] = findDevOfType(ctx, mach, ctx['global'].constants.DeviceType_DVD)
    cmdClosedVm(ctx, mach, lambda ctx, mach, args: mach.attachDevice(ctr, port, slot, ctx['global'].constants.DeviceType_DVD, dvd))
    return 0

def detachIsoCmd(ctx, args):
    if len(args) < 3:
        print("usage: detachIso <vmname|uuid> <iso>")
        return 0

    mach = argsToMach(ctx, args)
    if mach is None:
        return 0
    vbox = ctx['vb']
    loc = args[2]
    try:
        dvd = vbox.openMedium(loc, ctx['global'].constants.DeviceType_DVD, ctx['global'].constants.AccessMode_ReadOnly, False)
    except:
        print("no DVD with path %s registered" % (loc))
        return 0

    detachMedium(ctx, mach.id, dvd)
    return 0

def mountIsoCmd(ctx, args):
    if len(args) < 3:
        print("usage: mountIso <vmname|uuid> <iso> <controller> <port:slot>")
        return 0

    mach = argsToMach(ctx, args)
    if mach is None:
        return 0
    vbox = ctx['vb']
    loc = args[2]
    try:
        dvd = vbox.openMedium(loc, ctx['global'].constants.DeviceType_DVD, ctx['global'].constants.AccessMode_ReadOnly, False)
    except:
        print("no DVD with path %s registered" % (loc))
        return 0

    if len(args) > 3:
        ctr = args[3]
        (port, slot) = args[4].split(":")
    else:
        # autodetect controller and location, just find first controller with media == DVD
        [ctr, port, slot] = findDevOfType(ctx, mach, ctx['global'].constants.DeviceType_DVD)

    cmdExistingVm(ctx, mach, 'mountiso', [ctr, port, slot, dvd, True])

    return 0

def unmountIsoCmd(ctx, args):
    if len(args) < 2:
        print("usage: unmountIso <vmname|uuid> <controller> <port:slot>")
        return 0

    mach = argsToMach(ctx, args)
    if mach is None:
        return 0

    if len(args) > 3:
        ctr = args[2]
        (port, slot) = args[3].split(":")
    else:
        # autodetect controller and location, just find first controller with media == DVD
        [ctr, port, slot] = findDevOfType(ctx, mach, ctx['global'].constants.DeviceType_DVD)

    cmdExistingVm(ctx, mach, 'mountiso', [ctr, port, slot, None, True])

    return 0

def attachCtr(_ctx, mach, args):
    [name, bus, ctrltype] = args
    ctr = mach.addStorageController(name, bus)
    if ctrltype:
        ctr.controllerType = ctrltype

def attachCtrCmd(ctx, args):
    if len(args) < 4:
        print("usage: attachCtr <vmname|uuid> <controller name> <bus> <type>")
        return 0

    if len(args) > 4:
        ctrltype = enumFromString(ctx, 'StorageControllerType', args[4])
        if not ctrltype:
            print("Controller type %s unknown" % (args[4]))
            return 0
    else:
        ctrltype = None

    mach = argsToMach(ctx, args)
    if mach is None:
        return 0
    bus = enumFromString(ctx, 'StorageBus', args[3])
    if bus is None:
        print("Bus type %s unknown" % (args[3]))
        return 0
    name = args[2]
    cmdClosedVm(ctx, mach, attachCtr, [name, bus, ctrltype])
    return 0

def detachCtrCmd(ctx, args):
    if len(args) < 3:
        print("usage: detachCtr <vmname|uuid> <controller name>")
        return 0

    mach = argsToMach(ctx, args)
    if mach is None:
        return 0
    ctr = args[2]
    cmdClosedVm(ctx, mach, lambda ctx, mach, args: mach.removeStorageController(ctr))
    return 0

def usbctr(_ctx, _mach, console, args):
    if args[0]:
        console.attachUSBDevice(args[1], "")
    else:
        console.detachUSBDevice(args[1])

def attachUsbCmd(ctx, args):
    if len(args) < 3:
        print("usage: attachUsb <vmname|uuid> <device uid>")
        return 0

    mach = argsToMach(ctx, args)
    if mach is None:
        return 0
    dev = args[2]
    cmdExistingVm(ctx, mach, 'guestlambda', [usbctr, True, dev])
    return 0

def detachUsbCmd(ctx, args):
    if len(args) < 3:
        print("usage: detachUsb <vmname|uuid> <device uid>")
        return 0

    mach = argsToMach(ctx, args)
    if mach is None:
        return 0
    dev = args[2]
    cmdExistingVm(ctx, mach, 'guestlambda', [usbctr, False, dev])
    return 0


def guiCmd(ctx, args):
    if len(args) > 1:
        print("usage: gui")
        return 0

    binDir = ctx['global'].getBinDir()

    vbox = os.path.join(binDir, 'VirtualBox')
    try:
        os.system(vbox)
    except KeyboardInterrupt:
        # to allow interruption
        pass
    return 0

def shareFolderCmd(ctx, args):
    if len(args) < 4:
        print("usage: shareFolder <vmname|uuid> <path> <name> <writable|persistent>")
        return 0

    mach = argsToMach(ctx, args)
    if mach is None:
        return 0
    path = args[2]
    name = args[3]
    writable = False
    persistent = False
    if len(args) > 4:
        for cur_arg in args[4:]:
            if cur_arg == 'writable':
                writable = True
            if cur_arg == 'persistent':
                persistent = True
    if persistent:
        cmdClosedVm(ctx, mach, lambda ctx, mach, args: mach.createSharedFolder(name, path, writable), [])
    else:
        cmdExistingVm(ctx, mach, 'guestlambda', [lambda ctx, mach, console, args: console.createSharedFolder(name, path, writable)])
    return 0

def unshareFolderCmd(ctx, args):
    if len(args) < 3:
        print("usage: unshareFolder <vmname|uuid> <name>")
        return 0

    mach = argsToMach(ctx, args)
    if mach is None:
        return 0
    name = args[2]
    found = False
    for sharedfolder in ctx['global'].getArray(mach, 'sharedFolders'):
        if sharedfolder.name == name:
            cmdClosedVm(ctx, mach, lambda ctx, mach, args: mach.removeSharedFolder(name), [])
            found = True
            break
    if not found:
        cmdExistingVm(ctx, mach, 'guestlambda', [lambda ctx, mach, console, args: console.removeSharedFolder(name)])
    return 0


def snapshotCmd(ctx, args):
    if (len(args) < 2 or args[1] == 'help'):
        print("Take snapshot:    snapshot <vmname|uuid> take <name> <description>")
        print("Restore snapshot: snapshot <vmname|uuid> restore <name>")
        print("Merge snapshot:   snapshot <vmname|uuid> merge <name>")
        return 0

    mach = argsToMach(ctx, args)
    if mach is None:
        return 0
    cmd = args[2]
    if cmd == 'take':
        if len(args) < 4:
            print("usage: snapshot <vmname|uuid> take <name> <description>")
            return 0
        name = args[3]
        if len(args) > 4:
            desc = args[4]
        else:
            desc = ""
        cmdAnyVm(ctx, mach, lambda ctx, mach, console, args: progressBar(ctx, mach.takeSnapshot(name, desc, True)[0]))
        return 0

    if cmd == 'restore':
        if len(args) < 4:
            print("usage: snapshot <vmname|uuid> restore <name>")
            return 0
        name = args[3]
        snap = mach.findSnapshot(name)
        cmdAnyVm(ctx, mach, lambda ctx, mach, console, args: progressBar(ctx, mach.restoreSnapshot(snap)))
        return 0

    if cmd == 'restorecurrent':
        if len(args) < 4:
            print("usage: snapshot <vmname|uuid> restorecurrent")
            return 0
        snap = mach.currentSnapshot()
        cmdAnyVm(ctx, mach, lambda ctx, mach, console, args: progressBar(ctx, mach.restoreSnapshot(snap)))
        return 0

    if cmd == 'delete':
        if len(args) < 4:
            print("usage: snapshot <vmname|uuid> delete <name>")
            return 0
        name = args[3]
        snap = mach.findSnapshot(name)
        cmdAnyVm(ctx, mach, lambda ctx, mach, console, args: progressBar(ctx, mach.deleteSnapshot(snap.id)))
        return 0

    print("Command '%s' is unknown" % (cmd))
    return 0

def natAlias(_ctx, _mach, _nicnum, nat, args=None):
    """This command shows/alters NAT's alias settings.
    usage: nat <vmname|uuid> <nicnum> alias [default|[log] [proxyonly] [sameports]]
    default - set settings to default values
    log - switch on alias logging
    proxyonly - switch proxyonly mode on
    sameports - enforces NAT using the same ports
    """
    alias = {
        'log': 0x1,
        'proxyonly': 0x2,
        'sameports': 0x4
    }
    if len(args) == 1:
        first = 0
        msg = ''
        for aliasmode, aliaskey in list(alias.items()):
            if first == 0:
                first = 1
            else:
                msg += ', '
            if int(nat.aliasMode) & aliaskey:
                msg += '%s: %s' % (aliasmode, 'on')
            else:
                msg += '%s: %s' % (aliasmode, 'off')
        return (0, [msg])

    nat.aliasMode = 0
    if 'default' not in args:
        for idx in range(1, len(args)):
            if args[idx] not in alias:
                print('Invalid alias mode: ' + args[idx])
                print(natAlias.__doc__)
                return (1, None)
            nat.aliasMode = int(nat.aliasMode) | alias[args[idx]]
    return (0, None)

def natSettings(_ctx, _mach, _nicnum, nat, args):
    """
    This command shows/alters NAT settings.
    usage: nat <vmname|uuid> <nicnum> settings [<mtu> [[<socsndbuf> <sockrcvbuf> [<tcpsndwnd> <tcprcvwnd>]]]]
    mtu - set mtu <= 16000
    socksndbuf/sockrcvbuf - sets amount of kb for socket sending/receiving buffer
    tcpsndwnd/tcprcvwnd - sets size of initial tcp sending/receiving window
    """
    if len(args) == 1:
        (mtu, socksndbuf, sockrcvbuf, tcpsndwnd, tcprcvwnd) = nat.getNetworkSettings()
        if mtu == 0: mtu = 1500
        if socksndbuf == 0: socksndbuf = 64
        if sockrcvbuf == 0: sockrcvbuf = 64
        if tcpsndwnd == 0: tcpsndwnd = 64
        if tcprcvwnd == 0: tcprcvwnd = 64
        msg = 'mtu:%s socket(snd:%s, rcv:%s) tcpwnd(snd:%s, rcv:%s)' % (mtu, socksndbuf, sockrcvbuf, tcpsndwnd, tcprcvwnd)
        return (0, [msg])

    if args[1] < 16000:
        print('invalid mtu value (%s not in range [65 - 16000])' % (args[1]))
        return (1, None)
    for i in range(2, len(args)):
        if not args[i].isdigit() or int(args[i]) < 8 or int(args[i]) > 1024:
            print('invalid %s parameter (%i not in range [8-1024])' % (i, args[i]))
            return (1, None)
    nic_args = [args[1]]
    if len(args) < 6:
        for i in range(2, len(args)): nic_args.append(args[i])
        for i in range(len(args), 6): nic_args.append(0)
    else:
        for i in range(2, len(args)): nic_args.append(args[i])
    #print(a)
    nat.setNetworkSettings(int(nic_args[0]), int(nic_args[1]), int(nic_args[2]), int(nic_args[3]), int(nic_args[4]))
    return (0, None)

def natDns(_ctx, _mach, _nicnum, nat, args):
    """This command shows/alters DNS's NAT settings
    usage: nat <vmname|uuid> <nicnum> dns [passdomain] [proxy] [usehostresolver]
    passdomain - enforces builtin DHCP server to pass domain
    proxy - switch on builtin NAT DNS proxying mechanism
    usehostresolver - proxies all DNS requests to Host Resolver interface
    """
    yesno = {0: 'off', 1: 'on'}
    if len(args) == 1:
        msg = 'passdomain:%s, proxy:%s, usehostresolver:%s' % (yesno[int(nat.DNSPassDomain)], yesno[int(nat.DNSProxy)], yesno[int(nat.DNSUseHostResolver)])
        return (0, [msg])

    nat.DNSPassDomain = 'passdomain' in args
    nat.DNSProxy = 'proxy' in args
    nat.DNSUseHostResolver = 'usehostresolver' in args
    return (0, None)

def natTftp(ctx, mach, nicnum, nat, args):
    """This command shows/alters TFTP settings
    usage nat <vmname|uuid> <nicnum> tftp [prefix <prefix>| bootfile <bootfile>| server <server>]
    prefix - alters prefix TFTP settings
    bootfile - alters bootfile TFTP settings
    server - sets booting server
    """
    if len(args) == 1:
        server = nat.TFTPNextServer
        if server is None:
            server = nat.network
            if server is None:
                server = '10.0.%d/24' % (int(nicnum) + 2)
            (server, _mask) = server.split('/')
            while server.count('.') != 3:
                server += '.0'
            (ipA, ipB, ipC, _ipD) = server.split('.')
            server = '%d.%d.%d.4' % (ipA, ipB, ipC)
        prefix = nat.TFTPPrefix
        if prefix is None:
            prefix = '%s/TFTP/' % (ctx['vb'].homeFolder)
        bootfile = nat.TFTPBootFile
        if bootfile is None:
            bootfile = '%s.pxe' % (mach.name)
        msg = 'server:%s, prefix:%s, bootfile:%s' % (server, prefix, bootfile)
        return (0, [msg])

    cmd = args[1]
    if len(args) != 3:
        print('invalid args:', args)
        print(natTftp.__doc__)
        return (1, None)
    if   cmd == 'prefix': nat.TFTPPrefix = args[2]
    elif cmd == 'bootfile': nat.TFTPBootFile = args[2]
    elif cmd == 'server': nat.TFTPNextServer = args[2]
    else:
        print("invalid cmd:", cmd)
        return (1, None)
    return (0, None)

def natPortForwarding(ctx, _mach, _nicnum, nat, args):
    """This command shows/manages port-forwarding settings
    usage:
        nat <vmname|uuid> <nicnum> <pf> [ simple tcp|udp <hostport> <guestport>]
            |[no_name tcp|udp <hostip> <hostport> <guestip> <guestport>]
            |[ex tcp|udp <pf-name> <hostip> <hostport> <guestip> <guestport>]
            |[delete <pf-name>]
    """
    if len(args) == 1:
        # note: keys/values are swapped in defining part of the function
        proto = {0: 'udp', 1: 'tcp'}
        msg = []
        port_forwardings = ctx['global'].getArray(nat, 'redirects')
        for forwarding in port_forwardings:
            (pfnme, pfp, pfhip, pfhp, pfgip, pfgp) = str(forwarding).split(', ')
            msg.append('%s: %s %s:%s => %s:%s' % (pfnme, proto[int(pfp)], pfhip, pfhp, pfgip, pfgp))
        return (0, msg) # msg is array

    proto = {'udp': 0, 'tcp': 1}
    pfcmd = {
        'simple': {
            'validate': lambda: args[1] in list(pfcmd) and args[2] in list(proto) and len(args) == 5,
            'func':lambda: nat.addRedirect('', proto[args[2]], '', int(args[3]), '', int(args[4]))
        },
        'no_name': {
            'validate': lambda: args[1] in list(pfcmd) and args[2] in list(proto) and len(args) == 7,
            'func': lambda: nat.addRedirect('', proto[args[2]], args[3], int(args[4]), args[5], int(args[6]))
        },
        'ex': {
            'validate': lambda: args[1] in list(pfcmd) and args[2] in list(proto) and len(args) == 8,
            'func': lambda: nat.addRedirect(args[3], proto[args[2]], args[4], int(args[5]), args[6], int(args[7]))
        },
        'delete': {
            'validate': lambda: len(args) == 3,
            'func': lambda: nat.removeRedirect(args[2])
        }
    }

    if not pfcmd[args[1]]['validate']():
        print('invalid port-forwarding or args of sub command ', args[1])
        print(natPortForwarding.__doc__)
        return (1, None)

    _not_sure_for_what_this_is = pfcmd[args[1]]['func']()
    return (0, None)

def natNetwork(_ctx, _mach, nicnum, nat, args):
    """This command shows/alters NAT network settings
    usage: nat <vmname|uuid> <nicnum> network [<network>]
    """
    if len(args) == 1:
        if nat.network is not None and len(str(nat.network)) != 0:
            msg = '\'%s\'' % (nat.network)
        else:
            msg = '10.0.%d.0/24' % (int(nicnum) + 2)
        return (0, [msg])

    (addr, mask) = args[1].split('/')
    if addr.count('.') > 3 or int(mask) < 0 or int(mask) > 32:
        print('Invalid arguments')
        return (1, None)
    nat.network = args[1]
    return (0, None)

def natCmd(ctx, args):
    """This command is entry point to NAT settins management
    usage: nat <vmname|uuid> <nicnum> <cmd> <cmd-args>
    cmd - [alias|settings|tftp|dns|pf|network]
    for more information about commands:
    nat help <cmd>
    """

    natcommands = {
        'alias' : natAlias,
        'settings' : natSettings,
        'tftp': natTftp,
        'dns': natDns,
        'pf': natPortForwarding,
        'network': natNetwork
    }

    if len(args) < 2 or args[1] == 'help':
        if len(args) > 2:
            print(natcommands[args[2]].__doc__)
        else:
            print(natCmd.__doc__)
        return 0
    if len(args) == 1 or len(args) < 4 or args[3] not in natcommands:
        print(natCmd.__doc__)
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        print("please specify vm")
        return 0
    platformProps = mach.platform.properties
    if len(args) < 3 or not args[2].isdigit() or int(args[2]) not in list(range(0, platformProps.getMaxNetworkAdapters(mach.platform.chipsetType))):
        print('please specify adapter num %d isn\'t in range [0-%d]' % (args[2], platformProps.getMaxNetworkAdapters(mach.platform.chipsetType)))
        return 0
    nicnum = int(args[2])
    cmdargs = []
    for i in range(3, len(args)):
        cmdargs.append(args[i])

    # @todo vvl if nicnum is missed but command is entered
    # use NAT func for every adapter on machine.
    func = args[3]
    rosession = 1
    session = None
    if len(cmdargs) > 1:
        rosession = 0
        session = ctx['global'].openMachineSession(mach, fPermitSharing=False)
        mach = session.machine

    adapter = mach.getNetworkAdapter(nicnum)
    natEngine = adapter.NATEngine
    (rc, reports) = natcommands[func](ctx, mach, nicnum, natEngine, cmdargs)
    if rosession == 0:
        if rc == 0:
            mach.saveSettings()
        session.unlockMachine()
    elif reports:
        for cur_report in reports:
            msg ='%s nic%d %s: %s' % (mach.name, nicnum, func, cur_report)
            print(msg)
    return 0

def nicSwitchOnOff(adapter, attr, args):
    if len(args) == 1:
        yesno = {0: 'off', 1: 'on'}
        resp = yesno[int(adapter.getattr(attr))]
        return (0, resp)

    yesno = {'off' : 0, 'on' : 1}
    if args[1] not in yesno:
        print('%s isn\'t acceptable, please choose %s' % (args[1], list(yesno.keys())))
        return (1, None)
    adapter.setsetattr(attr, yesno[args[1]])
    return (0, None)

def nicTraceSubCmd(_ctx, _vm, _nicnum, adapter, args):
    '''
    usage: nic <vmname|uuid> <nicnum> trace [on|off [file]]
    '''
    (rc, resp) = nicSwitchOnOff(adapter, 'traceEnabled', args)
    if len(args) == 1 and rc == 0:
        resp = '%s file:%s' % (resp, adapter.traceFile)
        return (0, resp)
    if len(args) == 3 and rc == 0:
        adapter.traceFile = args[2]
    return (0, None)

def nicLineSpeedSubCmd(_ctx, _vm, _nicnum, adapter, args):
    if len(args) == 1:
        resp = '%d kbps'% (adapter.lineSpeed)
        return (0, resp)

    if not args[1].isdigit():
        print('%s isn\'t a number' % (args[1]))
        return (1, None)
    adapter.lineSpeed = int(args[1])
    return (0, None)

def nicCableSubCmd(_ctx, _vm, _nicnum, adapter, args):
    '''
    usage: nic <vmname|uuid> <nicnum> cable [on|off]
    '''
    return nicSwitchOnOff(adapter, 'cableConnected', args)

def nicEnableSubCmd(_ctx, _vm, _nicnum, adapter, args):
    '''
    usage: nic <vmname|uuid> <nicnum> enable [on|off]
    '''
    return nicSwitchOnOff(adapter, 'enabled', args)

def nicTypeSubCmd(ctx, _vm, _nicnum, adapter, args):
    '''
    usage: nic <vmname|uuid> <nicnum> type [Am79c970A|Am79c970A|I82540EM|I82545EM|I82543GC|Virtio]
    '''
    if len(args) == 1:
        nictypes = ctx['const'].all_values('NetworkAdapterType')
        for key in list(nictypes.keys()):
            if str(adapter.adapterType) == str(nictypes[key]):
                return (0, str(key))
        return (1, None)

    nictypes = ctx['const'].all_values('NetworkAdapterType')
    if args[1] not in list(nictypes.keys()):
        print('%s not in acceptable values (%s)' % (args[1], list(nictypes.keys())))
        return (1, None)
    adapter.adapterType = nictypes[args[1]]
    return (0, None)

def nicAttachmentSubCmd(ctx, _vm, _nicnum, adapter, args):
    '''
    usage: nic <vmname|uuid> <nicnum> attachment [Null|NAT|Bridged <interface>|Internal <name>|HostOnly <interface>
    '''
    if len(args) == 1:
        nicAttachmentType = {
            ctx['global'].constants.NetworkAttachmentType_Null: ('Null', ''),
            ctx['global'].constants.NetworkAttachmentType_NAT: ('NAT', ''),
            ctx['global'].constants.NetworkAttachmentType_Bridged: ('Bridged', adapter.bridgedInterface),
            ctx['global'].constants.NetworkAttachmentType_Internal: ('Internal', adapter.internalNetwork),
            ctx['global'].constants.NetworkAttachmentType_HostOnly: ('HostOnly', adapter.hostOnlyInterface),
            # @todo show details of the generic network attachment type
            ctx['global'].constants.NetworkAttachmentType_Generic: ('Generic', ''),
        }
        if not isinstance(adapter.attachmentType, int):
            t = str(adapter.attachmentType)
        else:
            t = adapter.attachmentType
        (resp, name) = nicAttachmentType[t]
        return (0, 'attachment:%s, name:%s' % (resp, name))

    nicAttachmentType = {
        'Null': {
            'v': lambda: len(args) == 2,
            'p': lambda: 'do nothing',
            'f': lambda: ctx['global'].constants.NetworkAttachmentType_Null},
        'NAT': {
            'v': lambda: len(args) == 2,
            'p': lambda: 'do nothing',
            'f': lambda: ctx['global'].constants.NetworkAttachmentType_NAT},
        'Bridged': {
            'v': lambda: len(args) == 3,
            'p': lambda: adapter.setattr('bridgedInterface', args[2]),
            'f': lambda: ctx['global'].constants.NetworkAttachmentType_Bridged},
        'Internal': {
            'v': lambda: len(args) == 3,
            'p': lambda: adapter.setattr('internalNetwork', args[2]),
            'f': lambda: ctx['global'].constants.NetworkAttachmentType_Internal},
        'HostOnly': {
            'v': lambda: len(args) == 2,
            'p': lambda: adapter.setattr('hostOnlyInterface', args[2]),
            'f': lambda: ctx['global'].constants.NetworkAttachmentType_HostOnly},
        # @todo implement setting the properties of a generic attachment
        'Generic': {
            'v': lambda: len(args) == 3,
            'p': lambda: 'do nothing',
            'f': lambda: ctx['global'].constants.NetworkAttachmentType_Generic}
    }
    if args[1] not in list(nicAttachmentType):
        print('%s not in acceptable values (%s)' % (args[1], list(nicAttachmentType.keys())))
        return (1, None)
    if not nicAttachmentType[args[1]]['v']():
        ## @todo r=andy Log this properly!
        return (1, None)
    nicAttachmentType[args[1]]['p']()
    adapter.attachmentType = nicAttachmentType[args[1]]['f']()
    return (0, None)

def nicCmd(ctx, args):
    '''
    This command to manage network adapters
    usage: nic <vmname|uuid> <nicnum> <cmd> <cmd-args>
    where cmd : attachment, trace, linespeed, cable, enable, type
    '''
    # 'command name':{'runtime': is_callable_at_runtime, 'op': function_name}
    niccomand = {
        'attachment': nicAttachmentSubCmd,
        'trace':  nicTraceSubCmd,
        'linespeed': nicLineSpeedSubCmd,
        'cable': nicCableSubCmd,
        'enable': nicEnableSubCmd,
        'type': nicTypeSubCmd
    }
    if  len(args) < 2 \
        or args[1] == 'help' \
        or (len(args) > 2 and args[3] not in niccomand):
        if len(args) == 3 \
           and args[2] in niccomand:
            print(niccomand[args[2]].__doc__)
        else:
            print(nicCmd.__doc__)
        return 0

    mach = ctx['argsToMach'](args)
    if not mach:
        return 1

    platformProps = mach.platform.properties
    if    len(args) < 3 \
       or int(args[2]) not in list(range(0, platformProps.getMaxNetworkAdapters(mach.platform.chipsetType))):
        print('please specify adapter num %d isn\'t in range [0-%d]'% (args[2], platformProps.getMaxNetworkAdapters(mach.platform.chipsetType)))
        return 1
    nicnum = int(args[2])
    cmdargs = args[3:]
    func = args[3]
    session = None
    session = ctx['global'].openMachineSession(mach, fPermitSharing=True)
    mach = session.machine
    adapter = mach.getNetworkAdapter(nicnum)
    (rc, report) = niccomand[func](ctx, mach, nicnum, adapter, cmdargs)
    if rc == 0:
        mach.saveSettings()
    if report is not None:
        print('%s nic %d %s: %s' % (mach.name, nicnum, args[3], report))
    session.unlockMachine()
    return 0


def promptCmd(ctx, args):
    if    len(args) < 2:
        print("Current prompt: '%s'" % (ctx['prompt']))
        return 0

    ctx['prompt'] = args[1]
    return 0

def foreachCmd(ctx, args):
    if len(args) < 3:
        print("usage: foreach scope command, where scope is XPath-like expression //vms/vm[@CPUCount='2']")
        return 0

    scope = args[1]
    cmd = args[2]
    elems = eval_xpath(ctx, scope)
    try:
        for e in elems:
            e.apply(cmd)
    except:
        print("Error executing")
        traceback.print_exc()
    return 0

def foreachvmCmd(ctx, args):
    if len(args) < 2:
        print("foreachvm command <args>")
        return 0
    cmdargs = args[1:]
    cmdargs.insert(1, '')
    for mach in getMachines(ctx):
        cmdargs[1] = mach.id
        runCommandArgs(ctx, cmdargs)
    return 0

def recordDemoCmd(ctx, args):
    if len(args) < 3:
        print("usage: recordDemo <vmname|uuid> <filename> [duration in s]")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    filename = args[2]
    dur = 10000
    if len(args) > 3:
        dur = float(args[3])
    cmdExistingVm(ctx, mach, 'guestlambda', [lambda ctx, mach, console, args:  recordDemo(ctx, console, filename, dur)])
    return 0

def playbackDemoCmd(ctx, args):
    if len(args) < 3:
        print("usage: playbackDemo <vmname|uuid> <filename> [duration in s]")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    filename = args[2]
    dur = 10000
    if len(args) > 3:
        dur = float(args[3])
    cmdExistingVm(ctx, mach, 'guestlambda', [lambda ctx, mach, console, args:  playbackDemo(ctx, console, filename, dur)])
    return 0


def pciAddr(ctx, addr):
    strg = "%02x:%02x.%d" % (addr >> 8, (addr & 0xff) >> 3, addr & 7)
    return colPci(ctx, strg)

def lspci(ctx, console):
    assigned = ctx['global'].getArray(console.machine, 'PCIDeviceAssignments')
    for assignment in assigned:
        if assignment.isPhysicalDevice:
            print("%s: assigned host device %s guest %s" % (colDev(ctx, assignment.name), pciAddr(ctx, assignment.hostAddress), pciAddr(ctx, assignment.guestAddress)))

    atts = ctx['global'].getArray(console, 'attachedPCIDevices')
    for att in atts:
        if att.isPhysicalDevice:
            print("%s: physical, guest %s, host %s" % (colDev(ctx, att.name), pciAddr(ctx, att.guestAddress), pciAddr(ctx, att.hostAddress)))
        else:
            print("%s: virtual, guest %s" % (colDev(ctx, att.name), pciAddr(ctx, att.guestAddress)))
    return

def parsePci(strg):
    pcire = re.compile(r'(?P<b>[0-9a-fA-F]+):(?P<d>[0-9a-fA-F]+)\.(?P<f>\d)')
    match = pcire.search(strg)
    if match is None:
        return -1
    pdict = match.groupdict()
    return ((int(pdict['b'], 16)) << 8) | ((int(pdict['d'], 16)) << 3) | int(pdict['f'])

def lspciCmd(ctx, args):
    if len(args) < 2:
        print("usage: lspci vm")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    cmdExistingVm(ctx, mach, 'guestlambda', [lambda ctx, mach, console, args:  lspci(ctx, console)])
    return 0

def attachpciCmd(ctx, args):
    if len(args) < 3:
        print("usage: attachpci <vmname|uuid> <host pci address> <guest pci address>")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    hostaddr = parsePci(args[2])
    if hostaddr == -1:
        print("invalid host PCI %s, accepted format 01:02.3 for bus 1, device 2, function 3" % (args[2]))
        return 0

    if len(args) > 3:
        guestaddr = parsePci(args[3])
        if guestaddr == -1:
            print("invalid guest PCI %s, accepted format 01:02.3 for bus 1, device 2, function 3" % (args[3]))
            return 0
    else:
        guestaddr = hostaddr
    cmdClosedVm(ctx, mach, lambda ctx, mach, a: mach.attachHostPCIDevice(hostaddr, guestaddr, True))
    return 0

def detachpciCmd(ctx, args):
    if len(args) < 3:
        print("usage: detachpci <vmname|uuid> <host pci address>")
        return 0
    mach = argsToMach(ctx, args)
    if not mach:
        return 0
    hostaddr = parsePci(args[2])
    if hostaddr == -1:
        print("invalid host PCI %s, accepted format 01:02.3 for bus 1, device 2, function 3" % (args[2]))
        return 0

    cmdClosedVm(ctx, mach, lambda ctx, mach, a: mach.detachHostPCIDevice(hostaddr))
    return 0

def gotoCmd(ctx, args):
    if len(args) < 2:
        print("usage: goto line")
        return 0

    line = int(args[1])

    ctx['scriptLine'] = line

    return 0

aliases = {'s':'start',
           'i':'info',
           'l':'list',
           'h':'help',
           'a':'alias',
           'q':'quit', 'exit':'quit',
           'tg': 'typeGuest',
           'v':'verbose'}

commands = {'help':['Prints help information', helpCmd, 0],
            'start':['Start virtual machine by name or uuid: start mytestvm headless', startCmd, 0],
            'createVm':['Create virtual machine: createVm myvmname x86 MacOS', createVmCmd, 0],
            'removeVm':['Remove virtual machine', removeVmCmd, 0],
            'pause':['Pause virtual machine', pauseCmd, 0],
            'resume':['Resume virtual machine', resumeCmd, 0],
            'save':['Save execution state of virtual machine', saveCmd, 0],
            'stats':['Stats for virtual machine', statsCmd, 0],
            'powerdown':['Power down virtual machine', powerdownCmd, 0],
            'powerbutton':['Effectively press power button', powerbuttonCmd, 0],
            'list':['Shows known virtual machines', listCmd, 0],
            'info':['Shows info on machine', infoCmd, 0],
            'ginfo':['Shows info on guest', ginfoCmd, 0],
            'gexec':['Executes program in the guest', gexecCmd, 0],
            'gcopy':['Copy file to the guest', gcopyCmd, 0],
            'gpipe':['Pipe between host and guest', gpipeCmd, 0],
            'alias':['Control aliases', aliasCmd, 0],
            'verbose':['Toggle verbosity', verboseCmd, 0],
            'setvar':['Set VM variable: setvar mytestvm firmwareSettings.ACPIEnabled True', setvarCmd, 0],
            'eval':['Evaluate arbitrary Python construction: eval \'for m in getMachines(ctx): print(m.name, "has", m.memorySize, "M")\'', evalCmd, 0],
            'quit':['Exits', quitCmd, 0],
            'host':['Show host information', hostCmd, 0],
            'guest':['Execute command for guest: guest mytestvm \'console.mouse.putMouseEvent(20, 20, 0, 0, 0)\'', guestCmd, 0],
            'monitorGuest':['Monitor what happens with the guest for some time: monitorGuest mytestvm 10', monitorGuestCmd, 0],
            'monitorGuestKbd':['Monitor guest keyboard for some time: monitorGuestKbd mytestvm 10', monitorGuestKbdCmd, 0],
            'monitorGuestMouse':['Monitor guest mouse for some time: monitorGuestMouse mytestvm 10', monitorGuestMouseCmd, 0],
            'monitorGuestMultiTouch':['Monitor guest touch screen for some time: monitorGuestMultiTouch mytestvm 10', monitorGuestMultiTouchCmd, 0],
            'monitorVBox':['Monitor what happens with VirtualBox for some time: monitorVBox 10', monitorVBoxCmd, 0],
            'portForward':['Setup permanent port forwarding for a VM, takes adapter number host port and guest port: portForward mytestvm 0 8080 80', portForwardCmd, 0],
            'showLog':['Show log file of the VM, : showLog mytestvm', showLogCmd, 0],
            'findLog':['Show entries matching pattern in log file of the VM, : findLog mytestvm PDM|CPUM', findLogCmd, 0],
            'findAssert':['Find assert in log file of the VM, : findAssert mytestvm', findAssertCmd, 0],
            'reloadExt':['Reload custom extensions: reloadExt', reloadExtCmd, 0],
            'runScript':['Run VBox script: runScript script.vbox', runScriptCmd, 0],
            'sleep':['Sleep for specified number of seconds: sleep 3.14159', sleepCmd, 0],
            'shell':['Execute external shell command: shell "ls /etc/rc*"', shellCmd, 0],
            'exportVm':['Export VM in OVF format: exportVm mytestvm /tmp/win.ovf', exportVMCmd, 0],
            'screenshot':['Take VM screenshot to a file: screenshot mytestvm /tmp/win.png 1024 768 0', screenshotCmd, 0],
            'teleport':['Teleport VM to another box (see openportal): teleport mytestvm anotherhost:8000 <passwd> <maxDowntime>', teleportCmd, 0],
            'typeGuest':['Type arbitrary text in guest: typeGuest Linux "^lls\\n&UP;&BKSP;ess /etc/hosts\\nq^c" 0.7', typeGuestCmd, 0],
            'openportal':['Open portal for teleportation of VM from another box (see teleport): openportal mytestvm 8000 <passwd>', openportalCmd, 0],
            'closeportal':['Close teleportation portal (see openportal, teleport): closeportal Win', closeportalCmd, 0],
            'getextra':['Get extra data, empty key lists all: getextra <vm|global> <key>', getExtraDataCmd, 0],
            'setextra':['Set extra data, empty value removes key: setextra <vm|global> <key> <value>', setExtraDataCmd, 0],
            'gueststats':['Print available guest stats (only Windows guests with additions so far): gueststats mytestvm', gueststatsCmd, 0],
            'plugcpu':['Add a CPU to a running VM: plugcpu mytestvm 1', plugcpuCmd, 0],
            'unplugcpu':['Remove a CPU from a running VM (additions required, Windows cannot unplug): unplugcpu Linux 1', unplugcpuCmd, 0],
            'createHdd': ['Create virtual HDD:  createHdd 1000 /disk.vdi ', createHddCmd, 0],
            'removeHdd': ['Permanently remove virtual HDD: removeHdd /disk.vdi', removeHddCmd, 0],
            'registerHdd': ['Register HDD image with VirtualBox instance: registerHdd /disk.vdi', registerHddCmd, 0],
            'unregisterHdd': ['Unregister HDD image with VirtualBox instance: unregisterHdd /disk.vdi', unregisterHddCmd, 0],
            'attachHdd': ['Attach HDD to the VM: attachHdd mytestvm /disk.vdi "IDE Controller" 0:1', attachHddCmd, 0],
            'detachHdd': ['Detach HDD from the VM: detachHdd mytestvm /disk.vdi', detachHddCmd, 0],
            'registerIso': ['Register CD/DVD image with VirtualBox instance: registerIso /os.iso', registerIsoCmd, 0],
            'unregisterIso': ['Unregister CD/DVD image with VirtualBox instance: unregisterIso /os.iso', unregisterIsoCmd, 0],
            'removeIso': ['Permanently remove CD/DVD image: removeIso /os.iso', removeIsoCmd, 0],
            'attachIso': ['Attach CD/DVD to the VM: attachIso mytestvm /os.iso "IDE Controller" 0:1', attachIsoCmd, 0],
            'detachIso': ['Detach CD/DVD from the VM: detachIso mytestvm /os.iso', detachIsoCmd, 0],
            'mountIso': ['Mount CD/DVD to the running VM: mountIso mytestvm /os.iso "IDE Controller" 0:1', mountIsoCmd, 0],
            'unmountIso': ['Unmount CD/DVD from running VM: unmountIso mytestvm "IDE Controller" 0:1', unmountIsoCmd, 0],
            'attachCtr': ['Attach storage controller to the VM: attachCtr mytestvm Ctr0 IDE ICH6', attachCtrCmd, 0],
            'detachCtr': ['Detach HDD from the VM: detachCtr mytestvm Ctr0', detachCtrCmd, 0],
            'attachUsb': ['Attach USB device to the VM (use listUsb to show available devices): attachUsb mytestvm uuid', attachUsbCmd, 0],
            'detachUsb': ['Detach USB device from the VM: detachUsb mytestvm uuid', detachUsbCmd, 0],
            'listMedia': ['List media known to this VBox instance', listMediaCmd, 0],
            'listUsb': ['List known USB devices', listUsbCmd, 0],
            'shareFolder': ['Make host\'s folder visible to guest: shareFolder mytestvm /share share writable', shareFolderCmd, 0],
            'unshareFolder': ['Remove folder sharing', unshareFolderCmd, 0],
            'gui': ['Start GUI frontend', guiCmd, 0],
            'colors':['Toggle colors', colorsCmd, 0],
            'snapshot':['VM snapshot manipulation, snapshot help for more info', snapshotCmd, 0],
            'nat':['NAT (network address translation engine) manipulation, nat help for more info', natCmd, 0],
            'nic' : ['Network adapter management', nicCmd, 0],
            'prompt' : ['Control shell prompt', promptCmd, 0],
            'foreachvm' : ['Perform command for each VM', foreachvmCmd, 0],
            'foreach' : ['Generic "for each" construction, using XPath-like notation: foreach //vms/vm[@OSTypeId=\'MacOS\'] "print(obj.name)"', foreachCmd, 0],
            'recordDemo':['Record demo: recordDemo mytestvm file.dmo 10', recordDemoCmd, 0],
            'playbackDemo':['Playback demo: playbackDemo mytestvm file.dmo 10', playbackDemoCmd, 0],
            'lspci': ['List PCI devices attached to the VM: lspci mytestvm', lspciCmd, 0],
            'attachpci': ['Attach host PCI device to the VM: attachpci mytestvm 01:00.0', attachpciCmd, 0],
            'detachpci': ['Detach host PCI device from the VM: detachpci mytestvm 01:00.0', detachpciCmd, 0],
            'goto': ['Go to line in script (script-only)', gotoCmd, 0]
            }

def runCommandArgs(ctx, args):
    c = args[0]
    if aliases.get(c, None):
        c = aliases[c]
    cmd_internal = commands.get(c, None)
    if not cmd_internal:
        print("Unknown command: '%s', type 'help' for list of known commands" % (c))
        return 0
    if ctx['remote'] and ctx['vb'] is None:
        if c not in ['connect', 'reconnect', 'help', 'quit']:
            print("First connect to remote server with %s command." % (colored('connect', 'blue')))
            return 0
    return cmd_internal[1](ctx, args)


def runCommand(ctx, cmd):
    if not cmd: return 0
    args = split_no_quotes(cmd)
    if len(args) == 0: return 0
    return runCommandArgs(ctx, args)

#
# To write your own custom commands to vboxshell, create
# file ~/.VirtualBox/shellext.py with content like
#
# def runTestCmd(ctx, args):
#    print("Testy test", ctx['vb'])
#    return 0
#
# commands = {
#    'test': ['Test help', runTestCmd]
# }
# and issue reloadExt shell command.
# This file also will be read automatically on startup or 'reloadExt'.
#
# Also one can put shell extensions into ~/.VirtualBox/shexts and
# they will also be picked up, so this way one can exchange
# shell extensions easily.
def addExtsFromFile(_ctx, cmds, filename):
    if not os.path.isfile(filename):
        return
    extDict = {}
    try:
        with open(filename, encoding='utf-8') as file:
            file_buf = file.read()
            exec(compile(file_buf, filename, 'exec'), extDict, extDict) # pylint: disable=exec-used
        for (key, value) in list(extDict['commands'].items()):
            if g_fVerbose:
                print("customize: adding \"%s\" - %s" % (key, value[0]))
            cmds[key] = [value[0], value[1], filename]
    except:
        print("Error loading user extensions from %s" % (filename))
        traceback.print_exc()


def checkUserExtensions(ctx, cmds, folder):
    folder = str(folder)
    name = os.path.join(folder, "shellext.py")
    addExtsFromFile(ctx, cmds, name)
    # also check 'exts' directory for all files
    shextdir = os.path.join(folder, "shexts")
    if not os.path.isdir(shextdir):
        return
    exts = os.listdir(shextdir)
    for e in exts:
        # not editor temporary files, please.
        if e.endswith('.py'):
            addExtsFromFile(ctx, cmds, os.path.join(shextdir, e))

def getHomeFolder(ctx):
    if ctx['remote'] or ctx['vb'] is None:
        if 'VBOX_USER_HOME' in os.environ:
            return os.path.join(os.environ['VBOX_USER_HOME'])
        return os.path.join(os.path.expanduser("~"), ".VirtualBox")

    return ctx['vb'].homeFolder

def interpret(ctx):
    if ctx['remote']:
        commands['connect'] = ["Connect to remote VBox instance: connect http://server:18083 user password", connectCmd, 0]
        commands['disconnect'] = ["Disconnect from remote VBox instance", disconnectCmd, 0]
        commands['reconnect'] = ["Reconnect to remote VBox instance", reconnectCmd, 0]
        ctx['wsinfo'] = ["http://localhost:18083", "", ""]

    vbox = ctx['vb']
    if vbox is not None:
        try:
            print("Running VirtualBox version %s" % (vbox.version))
        except Exception as e:
            printErr(ctx, e)
            if g_fVerbose:
                traceback.print_exc()
        ctx['perf'] = None # ctx['global'].getPerfCollector(vbox)
    else:
        ctx['perf'] = None

    home = getHomeFolder(ctx)
    checkUserExtensions(ctx, commands, home)
    if platform.system() in ['Windows', 'Microsoft']:
        global g_fHasColors
        g_fHasColors = False
    hist_file = os.path.join(home, ".vboxshellhistory")
    autoCompletion(commands, ctx)

    if g_fHasReadline and os.path.exists(hist_file):
        readline.read_history_file(hist_file)

    # to allow to print actual host information, we collect info for
    # last 150 secs maximum, (sample every 10 secs and keep up to 15 samples)
    if ctx['perf']:
        try:
            ctx['perf'].setup(['*'], [vbox.host], 10, 15)
        except:
            pass
    cmds = []

    if g_sCmd is not None:
        cmds = g_sCmd.split(';')
    itCmd = iter(cmds)

    while True:
        try:
            if g_fBatchMode:
                cmd = 'runScript %s'% (g_sScriptFile)
            elif g_sCmd is not None:
                cmd = next(itCmd)
            else:
                if sys.version_info[0] <= 2:
                    cmd = raw_input(ctx['prompt']) # pylint: disable=undefined-variable
                else:
                    cmd = input(ctx['prompt'])
            done = runCommand(ctx, cmd)
            if done != 0:
                break
            if g_fBatchMode:
                break
        except KeyboardInterrupt:
            print('====== You can type quit or q to leave')
        except StopIteration:
            break
        except EOFError:
            break
        except Exception as e:
            printErr(ctx, e)
            if g_fVerbose:
                traceback.print_exc()
        ctx['global'].waitForEvents(0)
    try:
        # There is no need to disable metric collection. This is just an example.
        if ctx['perf']:
            ctx['perf'].disable(['*'], [vbox.host])
    except:
        pass
    if g_fHasReadline:
        readline.write_history_file(hist_file)

def runCommandCb(ctx, cmd, args):
    args.insert(0, cmd)
    return runCommandArgs(ctx, args)

def runGuestCommandCb(ctx, uuid, guestLambda, args):
    mach = machById(ctx, uuid)
    if not mach:
        return 0
    args.insert(0, guestLambda)
    cmdExistingVm(ctx, mach, 'guestlambda', args)
    return 0

def main(_argv):

    #
    # Parse command line arguments.
    #
    parse = OptionParser()
    parse.add_option("-v", "--verbose", dest="verbose", action="store_true", default=False, help = "switch on verbose")
    parse.add_option("-a", "--autopath", dest="autopath", action="store_true", default=False, help = "switch on autopath")
    parse.add_option("-w", "--webservice", dest="style", action="store_const", const="WEBSERVICE", help = "connect to webservice")
    parse.add_option("-b", "--batch", dest="batch_file", help = "script file to execute")
    parse.add_option("-c", dest="command_line", help = "command sequence to execute")
    parse.add_option("-o", dest="opt_line", help = "option line")
    global g_fVerbose, g_sScriptFile, g_fBatchMode, g_fHasColors, g_fHasReadline, g_sCmd
    (options, _args) = parse.parse_args()
    g_fVerbose = options.verbose
    style = options.style
    if options.batch_file is not None:
        g_fBatchMode = True
        g_fHasColors = False
        g_fHasReadline = False
        g_sScriptFile = options.batch_file
    if options.command_line is not None:
        g_fHasColors = False
        g_fHasReadline = False
        g_sCmd = options.command_line

    params = None
    if options.opt_line is not None:
        params = {}
        strparams = options.opt_line
        strparamlist = strparams.split(',')
        for strparam in strparamlist:
            (key, value) = strparam.split('=')
            params[key] = value

    if options.autopath:
        asLocations = [ os.getcwd(), ]
        try:    sScriptDir = os.path.dirname(os.path.abspath(__file__))
        except: pass # In case __file__ isn't there.
        else:
            if platform.system() in [ 'SunOS', ]:
                asLocations.append(os.path.join(sScriptDir, 'amd64'))
            asLocations.append(sScriptDir)


        sPath = os.environ.get("VBOX_PROGRAM_PATH")
        if sPath is None:
            for sCurLoc in asLocations:
                if   os.path.isfile(os.path.join(sCurLoc, "VirtualBox")) \
                  or os.path.isfile(os.path.join(sCurLoc, "VirtualBox.exe")):
                    print("Autodetected VBOX_PROGRAM_PATH as", sCurLoc)
                    os.environ["VBOX_PROGRAM_PATH"] = sCurLoc
                    sPath = sCurLoc
                    break
        if sPath:
            sys.path.append(os.path.join(sPath, "sdk", "installer"))

        sPath = os.environ.get("VBOX_SDK_PATH")
        if sPath is None:
            for sCurLoc in asLocations:
                if os.path.isfile(os.path.join(sCurLoc, "sdk", "bindings", "VirtualBox.xidl")):
                    sCurLoc = os.path.join(sCurLoc, "sdk")
                    print("Autodetected VBOX_SDK_PATH as", sCurLoc)
                    os.environ["VBOX_SDK_PATH"] = sCurLoc
                    sPath = sCurLoc
                    break
        if sPath:
            sCurLoc = sPath
            sTmp = os.path.join(sCurLoc, 'bindings', 'xpcom', 'python')
            if os.path.isdir(sTmp):
                sys.path.append(sTmp)
            del sTmp
        del sPath, asLocations

    #
    # Set up the shell interpreter context and start working.
    #
    from vboxapi import VirtualBoxManager
    oVBoxMgr = VirtualBoxManager(style, params)
    ctx = {
        'global':       oVBoxMgr,
        'vb':           oVBoxMgr.getVirtualBox(),
        'const':        oVBoxMgr.constants,
        'remote':       oVBoxMgr.remote,
        'type':         oVBoxMgr.type,
        'run':          lambda cmd, args: runCommandCb(ctx, cmd, args),
        'guestlambda':  lambda uuid, guestLambda, args: runGuestCommandCb(ctx, uuid, guestLambda, args),
        'machById':     lambda uuid: machById(ctx, uuid),
        'argsToMach':   lambda args: argsToMach(ctx, args),
        'progressBar':  lambda p: progressBar(ctx, p),
        'typeInGuest':  typeInGuest,
        '_machlist':    None,
        'prompt':       g_sPrompt,
        'scriptLine':   0,
        'interrupt':    False,
    }
    interpret(ctx)

    #
    # Release the interfaces references in ctx before cleaning up.
    #
    for sKey in list(ctx.keys()):
        del ctx[sKey]
    ctx = None
    gc.collect()

    oVBoxMgr.deinit()
    del oVBoxMgr

if __name__ == '__main__':
    main(sys.argv)
