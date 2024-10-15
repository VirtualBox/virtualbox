# -*- coding: utf-8 -*-
# $Id: testboxcommand.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
TestBox Script - Command Processor.
"""

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
__version__ = "$Revision: 106320 $"

# Standard python imports.
import os;
import sys;
import threading;

# Validation Kit imports.
from common             import constants;
from common             import utils, webutils;
import testboxcommons;
from testboxcommons     import TestBoxException;
from testboxscript      import TBS_EXITCODE_NEED_UPGRADE;
from testboxupgrade     import upgradeFromZip;
from testboxtasks       import TestBoxExecTask, TestBoxCleanupTask, TestBoxTestDriverTask;

# Figure where we are.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0];
g_ksTestScriptDir = os.path.dirname(os.path.abspath(__file__));



class TestBoxCommand(object):
    """
    Implementation of Test Box command.
    """

    ## The time to wait on the current task to abort.
    kcSecStopTimeout = 360
    ## The time to wait on the current task to abort before rebooting.
    kcSecStopBeforeRebootTimeout = 360

    def __init__(self, oTestBoxScript):
        """
        Class instance init
        """
        self._oTestBoxScript = oTestBoxScript;
        self._oCurTaskLock   = threading.RLock();
        self._oCurTask       = None;

        # List of available commands and their handlers
        self._dfnCommands    = \
        {
            constants.tbresp.CMD_IDLE:                 self._cmdIdle,
            constants.tbresp.CMD_WAIT:                 self._cmdWait,
            constants.tbresp.CMD_EXEC:                 self._cmdExec,
            constants.tbresp.CMD_ABORT:                self._cmdAbort,
            constants.tbresp.CMD_REBOOT:               self._cmdReboot,
            constants.tbresp.CMD_UPGRADE:              self._cmdUpgrade,
            constants.tbresp.CMD_UPGRADE_AND_REBOOT:   self._cmdUpgradeAndReboot,
            constants.tbresp.CMD_SPECIAL:              self._cmdSpecial,
        }

    def _cmdIdle(self, oResponse, oConnection):
        """
        Idle response, no ACK.
        """
        oResponse.checkParameterCount(1);

        # The dispatch loop will delay for us, so nothing to do here.
        _ = oConnection; # Leave the connection open.
        return True;

    def _cmdWait(self, oResponse, oConnection):
        """
        Gang scheduling wait response, no ACK.
        """
        oResponse.checkParameterCount(1);

        # The dispatch loop will delay for us, so nothing to do here.
        _ = oConnection; # Leave the connection open.
        return True;

    def _cmdExec(self, oResponse, oConnection):
        """
        Execute incoming command
        """

        # Check if required parameters given and make a little sense.
        idResult        = oResponse.getIntChecked(   constants.tbresp.EXEC_PARAM_RESULT_ID, 1);
        sScriptZips     = oResponse.getStringChecked(constants.tbresp.EXEC_PARAM_SCRIPT_ZIPS);
        sScriptCmdLine  = oResponse.getStringChecked(constants.tbresp.EXEC_PARAM_SCRIPT_CMD_LINE);
        cSecTimeout     = oResponse.getIntChecked(   constants.tbresp.EXEC_PARAM_TIMEOUT, 30);
        oResponse.checkParameterCount(5);

        sScriptFile     = utils.argsGetFirst(sScriptCmdLine);
        if sScriptFile is None:
            raise TestBoxException('Bad script command line: "%s"' % (sScriptCmdLine,));
        if len(os.path.basename(sScriptFile)) < len('t.py'):
            raise TestBoxException('Script file name too short: "%s"' % (sScriptFile,));
        if len(sScriptZips) < len('x.zip'):
            raise TestBoxException('Script zip name too short: "%s"' % (sScriptFile,));

        # One task at the time.
        if self.isRunning():
            raise TestBoxException('Already running other command');

        # Don't bother running the task without the shares mounted.
        self._oTestBoxScript.mountShares(); # Raises exception on failure.

        # Kick off the task and ACK the command.
        with self._oCurTaskLock:
            self._oCurTask = TestBoxExecTask(self._oTestBoxScript, idResult = idResult, sScriptZips = sScriptZips,
                                             sScriptCmdLine = sScriptCmdLine, cSecTimeout = cSecTimeout);
        oConnection.sendAckAndClose(constants.tbresp.CMD_EXEC);
        return True;

    def _cmdAbort(self, oResponse, oConnection):
        """
        Abort background task
        """
        oResponse.checkParameterCount(1);
        oConnection.sendAck(constants.tbresp.CMD_ABORT);

        oCurTask = self._getCurTask();
        if oCurTask is not None:
            oCurTask.terminate();
            oCurTask.flushLogOnConnection(oConnection);
            oConnection.close();
            oCurTask.wait(self.kcSecStopTimeout);

        return True;

    def doReboot(self):
        """
        Worker common to _cmdReboot and _doUpgrade that performs a system reboot.
        """
        # !! Not more exceptions beyond this point !!
        testboxcommons.log('Rebooting');

        # Stop anything that might be executing at this point.
        oCurTask = self._getCurTask();
        if oCurTask is not None:
            oCurTask.terminate();
            oCurTask.wait(self.kcSecStopBeforeRebootTimeout);

        # Invoke shutdown command line utility.
        sOs = utils.getHostOs();
        asCmd2 = None;
        if sOs == 'win':
            asCmd = ['shutdown', '/r', '/t', '0', '/c', '"ValidationKit triggered reboot"', '/d', '4:1'];
        elif sOs == 'os2':
            asCmd = ['setboot', '/B'];
        elif sOs in ('solaris',):
            asCmd = ['/usr/sbin/reboot', '-p'];
            asCmd2 = ['/usr/sbin/reboot']; # Hack! S10 doesn't have -p, but don't know how to reliably detect S10.
        else:
            asCmd = ['/sbin/shutdown', '-r', 'now'];
        try:
            utils.sudoProcessOutputChecked(asCmd);
        except Exception as oXcpt:
            if asCmd2 is not None:
                try:
                    utils.sudoProcessOutputChecked(asCmd2);
                except Exception as oXcpt2:
                    testboxcommons.log('Error executing reboot command "%s" as well as "%s": %s' % (asCmd, asCmd2, oXcpt2));
                    return False;
            testboxcommons.log('Error executing reboot command "%s": %s' % (asCmd, oXcpt));
            return False;

        # Quit the script.
        while True:
            sys.exit(32);
        return True;

    def _cmdReboot(self, oResponse, oConnection):
        """
        Reboot Test Box
        """
        oResponse.checkParameterCount(1);
        oConnection.sendAckAndClose(constants.tbresp.CMD_REBOOT);
        return self.doReboot();

    def _doUpgrade(self, oResponse, oConnection, fReboot):
        """
        Common worker for _cmdUpgrade and _cmdUpgradeAndReboot.
        Will sys.exit on success!
        """

        #
        # The server specifies a ZIP archive with the new scripts. It's ASSUMED
        # that the zip is of selected files at g_ksValidationKitDir in SVN.  It's
        # further ASSUMED that we're executing from
        #
        sZipUrl = oResponse.getStringChecked(constants.tbresp.UPGRADE_PARAM_URL)
        oResponse.checkParameterCount(2);

        if utils.isRunningFromCheckout():
            raise TestBoxException('Cannot upgrade when running from the tree!');
        oConnection.sendAckAndClose(constants.tbresp.CMD_UPGRADE_AND_REBOOT if fReboot else constants.tbresp.CMD_UPGRADE);

        testboxcommons.log('Upgrading...');

        #
        # Download the file and install it.
        #
        sDstFile = os.path.join(g_ksTestScriptDir, 'VBoxTestBoxScript.zip');
        if os.path.exists(sDstFile):
            os.unlink(sDstFile);
        fRc = webutils.downloadFile(sZipUrl, sDstFile, self._oTestBoxScript.getPathBuilds(), testboxcommons.log);
        if fRc is not True:
            return False;

        if upgradeFromZip(sDstFile) is not True:
            return False;

        #
        # Restart the system or the script (we have a parent script which
        # respawns us when we quit).
        #
        if fReboot:
            self.doReboot();
        sys.exit(TBS_EXITCODE_NEED_UPGRADE);
        return False;  # shuts up older pylint. 2.16.2+:  pylint: disable=unreachable

    def _cmdUpgrade(self, oResponse, oConnection):
        """
        Upgrade Test Box Script
        """
        return self._doUpgrade(oResponse, oConnection, False);

    def _cmdUpgradeAndReboot(self, oResponse, oConnection):
        """
        Upgrade Test Box Script
        """
        return self._doUpgrade(oResponse, oConnection, True);

    def _cmdSpecial(self, oResponse, oConnection):
        """
        Reserved for future fun.
        """
        oConnection.sendReplyAndClose(constants.tbreq.COMMAND_NOTSUP, constants.tbresp.CMD_SPECIAL);
        testboxcommons.log('Special command %s not supported...' % (oResponse,));
        return False;


    def handleCommand(self, oResponse, oConnection):
        """
        Handles a command from the test manager.

        Some commands will close the connection, others (generally the simple
        ones) wont, leaving the caller the option to use it for log flushing.

        Returns success indicator.
        Raises no exception.
        """
        try:
            sCmdName = oResponse.getStringChecked(constants.tbresp.ALL_PARAM_RESULT);
        except:
            oConnection.close();
            return False;

        # Do we know the command?
        fRc = False;
        if sCmdName in self._dfnCommands:
            testboxcommons.log(sCmdName);
            try:
                # Execute the handler.
                fRc = self._dfnCommands[sCmdName](oResponse, oConnection)
            except Exception as oXcpt:
                # NACK the command if an exception is raised during parameter validation.
                testboxcommons.log1Xcpt('Exception executing "%s": %s' % (sCmdName, oXcpt));
                if oConnection.isConnected():
                    try:
                        oConnection.sendReplyAndClose(constants.tbreq.COMMAND_NACK, sCmdName);
                    except Exception as oXcpt2:
                        testboxcommons.log('Failed to NACK "%s": %s' % (sCmdName, oXcpt2));
        elif sCmdName in [constants.tbresp.STATUS_DEAD, constants.tbresp.STATUS_NACK]:
            testboxcommons.log('Received status instead of command: %s' % (sCmdName, ));
        else:
            # NOTSUP the unknown command.
            testboxcommons.log('Received unknown command: %s' % (sCmdName, ));
            try:
                oConnection.sendReplyAndClose(constants.tbreq.COMMAND_NOTSUP, sCmdName);
            except Exception as oXcpt:
                testboxcommons.log('Failed to NOTSUP "%s": %s' % (sCmdName, oXcpt));
        return fRc;

    def resumeIncompleteCommand(self):
        """
        Resumes an incomplete command at startup.

        The EXEC commands saves essential state information in the scratch area
        so we can resume them in case the testbox panics or is rebooted.
        Current "resume" means doing cleanups, but we may need to implement
        test scenarios involving rebooting the testbox later.

        Returns (idTestBox, sTestBoxName, True) if a command was resumed,
        otherwise (-1, '', False).  Raises no exceptions.
        """

        try:
            oTask = TestBoxCleanupTask(self._oTestBoxScript);
        except:
            return (-1, '', False);

        with self._oCurTaskLock:
            self._oCurTask = oTask;

        return (oTask.idTestBox, oTask.sTestBoxName, True);

    def isRunning(self):
        """
        Check if we're running a task or not.
        """
        oCurTask = self._getCurTask();
        return oCurTask is not None and oCurTask.isRunning();

    def flushLogOnConnection(self, oGivenConnection):
        """
        Flushes the log of any running task with a log buffer.
        """
        oCurTask = self._getCurTask();
        if oCurTask is not None and isinstance(oCurTask, TestBoxTestDriverTask):
            return oCurTask.flushLogOnConnection(oGivenConnection);
        return None;

    def _getCurTask(self):
        """ Gets the current task in a paranoidly safe manny. """
        with self._oCurTaskLock:
            oCurTask = self._oCurTask;
        return oCurTask;

