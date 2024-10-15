# -*- coding: utf-8 -*-
# $Id: testboxcontroller.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager Core - Web Server Abstraction Base Class.
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
import re;
import os;
import string;                          # pylint: disable=deprecated-module
import sys;
import uuid;

# Validation Kit imports.
from common                             import constants;
from testmanager                        import config;
from testmanager.core                   import coreconsts;
from testmanager.core.db                import TMDatabaseConnection;
from testmanager.core.base              import TMExceptionBase;
from testmanager.core.globalresource    import GlobalResourceLogic;
from testmanager.core.testboxstatus     import TestBoxStatusData, TestBoxStatusLogic;
from testmanager.core.testbox           import TestBoxData, TestBoxLogic;
from testmanager.core.testresults       import TestResultLogic, TestResultFileData;
from testmanager.core.testset           import TestSetData, TestSetLogic;
from testmanager.core.systemlog         import SystemLogData, SystemLogLogic;
from testmanager.core.schedulerbase     import SchedulerBase;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int;     # pylint: disable=redefined-builtin,invalid-name


class TestBoxControllerException(TMExceptionBase):
    """
    Exception class for TestBoxController.
    """
    pass;                               # pylint: disable=unnecessary-pass


class TestBoxController(object): # pylint: disable=too-few-public-methods
    """
    TestBox Controller class.
    """

    ## Applicable testbox commands to an idle TestBox.
    kasIdleCmds = [TestBoxData.ksTestBoxCmd_Reboot,
                   TestBoxData.ksTestBoxCmd_Upgrade,
                   TestBoxData.ksTestBoxCmd_UpgradeAndReboot,
                   TestBoxData.ksTestBoxCmd_Special];
    ## Applicable testbox commands to a busy TestBox.
    kasBusyCmds = [TestBoxData.ksTestBoxCmd_Abort, TestBoxData.ksTestBoxCmd_Reboot];
    ## Commands that can be ACK'ed.
    kasAckableCmds  = [constants.tbresp.CMD_EXEC, constants.tbresp.CMD_ABORT, constants.tbresp.CMD_REBOOT,
                       constants.tbresp.CMD_UPGRADE, constants.tbresp.CMD_UPGRADE_AND_REBOOT, constants.tbresp.CMD_SPECIAL];
    ## Commands that can be NACK'ed or NOTSUP'ed.
    kasNackableCmds = kasAckableCmds + [kasAckableCmds, constants.tbresp.CMD_IDLE, constants.tbresp.CMD_WAIT];

    ## Mapping from TestBoxCmd_T to TestBoxState_T
    kdCmdToState = \
    { \
        TestBoxData.ksTestBoxCmd_Abort:             None,
        TestBoxData.ksTestBoxCmd_Reboot:            TestBoxStatusData.ksTestBoxState_Rebooting,
        TestBoxData.ksTestBoxCmd_Upgrade:           TestBoxStatusData.ksTestBoxState_Upgrading,
        TestBoxData.ksTestBoxCmd_UpgradeAndReboot:  TestBoxStatusData.ksTestBoxState_UpgradingAndRebooting,
        TestBoxData.ksTestBoxCmd_Special:           TestBoxStatusData.ksTestBoxState_DoingSpecialCmd,
    };

    ## Mapping from TestBoxCmd_T to TestBox responses commands.
    kdCmdToTbRespCmd = \
    {
        TestBoxData.ksTestBoxCmd_Abort:             constants.tbresp.CMD_ABORT,
        TestBoxData.ksTestBoxCmd_Reboot:            constants.tbresp.CMD_REBOOT,
        TestBoxData.ksTestBoxCmd_Upgrade:           constants.tbresp.CMD_UPGRADE,
        TestBoxData.ksTestBoxCmd_UpgradeAndReboot:  constants.tbresp.CMD_UPGRADE_AND_REBOOT,
        TestBoxData.ksTestBoxCmd_Special:           constants.tbresp.CMD_SPECIAL,
    };

    ## Mapping from TestBox responses to TestBoxCmd_T commands.
    kdTbRespCmdToCmd = \
    {
        constants.tbresp.CMD_IDLE:               None,
        constants.tbresp.CMD_WAIT:               None,
        constants.tbresp.CMD_EXEC:               None,
        constants.tbresp.CMD_ABORT:              TestBoxData.ksTestBoxCmd_Abort,
        constants.tbresp.CMD_REBOOT:             TestBoxData.ksTestBoxCmd_Reboot,
        constants.tbresp.CMD_UPGRADE:            TestBoxData.ksTestBoxCmd_Upgrade,
        constants.tbresp.CMD_UPGRADE_AND_REBOOT: TestBoxData.ksTestBoxCmd_UpgradeAndReboot,
        constants.tbresp.CMD_SPECIAL:            TestBoxData.ksTestBoxCmd_Special,
    };


    ## The path to the upgrade zip, relative WebServerGlueBase.getBaseUrl().
    ksUpgradeZip = 'htdocs/upgrade/VBoxTestBoxScript.zip';

    ## Valid TestBox result values.
    kasValidResults = list(constants.result.g_kasValidResults);
    ## Mapping TestBox result values to TestStatus_T values.
    kadTbResultToStatus = \
    {
        constants.result.PASSED:        TestSetData.ksTestStatus_Success,
        constants.result.SKIPPED:       TestSetData.ksTestStatus_Skipped,
        constants.result.ABORTED:       TestSetData.ksTestStatus_Aborted,
        constants.result.BAD_TESTBOX:   TestSetData.ksTestStatus_BadTestBox,
        constants.result.FAILED:        TestSetData.ksTestStatus_Failure,
        constants.result.TIMED_OUT:     TestSetData.ksTestStatus_TimedOut,
        constants.result.REBOOTED:      TestSetData.ksTestStatus_Rebooted,
    };


    def __init__(self, oSrvGlue):
        """
        Won't raise exceptions.
        """
        self._oSrvGlue          = oSrvGlue;
        self._sAction           = None; # _getStandardParams / dispatchRequest sets this later on.
        self._idTestBox         = None; # _getStandardParams / dispatchRequest sets this later on.
        self._sTestBoxUuid      = None; # _getStandardParams / dispatchRequest sets this later on.
        self._sTestBoxAddr      = None; # _getStandardParams / dispatchRequest sets this later on.
        self._idTestSet         = None; # _getStandardParams / dispatchRequest sets this later on.
        self._dParams           = None; # _getStandardParams / dispatchRequest sets this later on.
        self._asCheckedParams   = [];
        self._dActions          = \
        { \
            constants.tbreq.SIGNON              : self._actionSignOn,
            constants.tbreq.REQUEST_COMMAND_BUSY: self._actionRequestCommandBusy,
            constants.tbreq.REQUEST_COMMAND_IDLE: self._actionRequestCommandIdle,
            constants.tbreq.COMMAND_ACK         : self._actionCommandAck,
            constants.tbreq.COMMAND_NACK        : self._actionCommandNack,
            constants.tbreq.COMMAND_NOTSUP      : self._actionCommandNotSup,
            constants.tbreq.LOG_MAIN            : self._actionLogMain,
            constants.tbreq.UPLOAD              : self._actionUpload,
            constants.tbreq.XML_RESULTS         : self._actionXmlResults,
            constants.tbreq.EXEC_COMPLETED      : self._actionExecCompleted,
        };

    def _getStringParam(self, sName, asValidValues = None, fStrip = False, sDefValue = None):
        """
        Gets a string parameter (stripped).

        Raises exception if not found and no default is provided, or if the
        value isn't found in asValidValues.
        """
        if sName not in self._dParams:
            if sDefValue is None:
                raise TestBoxControllerException('%s parameter %s is missing' % (self._sAction, sName));
            return sDefValue;
        sValue = self._dParams[sName];
        if fStrip:
            sValue = sValue.strip();

        if sName not in self._asCheckedParams:
            self._asCheckedParams.append(sName);

        if asValidValues is not None and sValue not in asValidValues:
            raise TestBoxControllerException('%s parameter %s value "%s" not in %s ' \
                                             % (self._sAction, sName, sValue, asValidValues));
        return sValue;

    def _getBoolParam(self, sName, fDefValue = None):
        """
        Gets a boolean parameter.

        Raises exception if not found and no default is provided, or if not a
        valid boolean.
        """
        sValue = self._getStringParam(sName, [ 'True', 'true', '1', 'False', 'false', '0'], sDefValue = str(fDefValue));
        return sValue in ('True', 'true', '1',);

    def _getIntParam(self, sName, iMin = None, iMax = None):
        """
        Gets a string parameter.
        Raises exception if not found, not a valid integer, or if the value
        isn't in the range defined by iMin and iMax.
        """
        sValue = self._getStringParam(sName);
        try:
            iValue = int(sValue, 0);
        except:
            raise TestBoxControllerException('%s parameter %s value "%s" cannot be convert to an integer' \
                                             % (self._sAction, sName, sValue));

        if   (iMin is not None and iValue < iMin) \
          or (iMax is not None and iValue > iMax):
            raise TestBoxControllerException('%s parameter %s value %d is out of range [%s..%s]' \
                                             % (self._sAction, sName, iValue, iMin, iMax));
        return iValue;

    def _getLongParam(self, sName, lMin = None, lMax = None, lDefValue = None):
        """
        Gets a string parameter.
        Raises exception if not found, not a valid long integer, or if the value
        isn't in the range defined by lMin and lMax.
        """
        sValue = self._getStringParam(sName, sDefValue = (str(lDefValue) if lDefValue is not None else None));
        try:
            lValue = long(sValue, 0);
        except Exception as oXcpt:
            raise TestBoxControllerException('%s parameter %s value "%s" cannot be convert to an integer (%s)' \
                                             % (self._sAction, sName, sValue, oXcpt));

        if   (lMin is not None and lValue < lMin) \
          or (lMax is not None and lValue > lMax):
            raise TestBoxControllerException('%s parameter %s value %d is out of range [%s..%s]' \
                                             % (self._sAction, sName, lValue, lMin, lMax));
        return lValue;

    def _checkForUnknownParameters(self):
        """
        Check if we've handled all parameters, raises exception if anything
        unknown was found.
        """

        if len(self._asCheckedParams) != len(self._dParams):
            sUnknownParams = '';
            for sKey in self._dParams:
                if sKey not in self._asCheckedParams:
                    sUnknownParams += ' ' + sKey + '=' + self._dParams[sKey];
            raise TestBoxControllerException('Unknown parameters: ' + sUnknownParams);

        return True;

    def _writeResponse(self, dParams):
        """
        Makes a reply to the testbox script.
        Will raise exception on failure.
        """
        self._oSrvGlue.writeParams(dParams);
        self._oSrvGlue.flush();
        return True;

    def _resultResponse(self, sResultValue):
        """
        Makes a simple reply to the testbox script.
        Will raise exception on failure.
        """
        return self._writeResponse({constants.tbresp.ALL_PARAM_RESULT: sResultValue});


    def _idleResponse(self):
        """
        Makes an IDLE reply to the testbox script.
        Will raise exception on failure.
        """
        return self._writeResponse({ constants.tbresp.ALL_PARAM_RESULT: constants.tbresp.CMD_IDLE });

    def _cleanupOldTest(self, oDb, oStatusData):
        """
        Cleans up any old test set that may be left behind and changes the
        state to 'idle'.  See scenario #9:
        file://../../docs/AutomaticTestingRevamp.html#cleaning-up-abandoned-testcase

        Note. oStatusData.enmState is set to idle, but tsUpdated is not changed.
        """

        # Cleanup any abandoned test.
        if oStatusData.idTestSet is not None:
            SystemLogLogic(oDb).addEntry(SystemLogData.ksEvent_TestSetAbandoned,
                                         "idTestSet=%u idTestBox=%u enmState=%s %s"
                                         % (oStatusData.idTestSet, oStatusData.idTestBox,
                                            oStatusData.enmState, self._sAction),
                                         fCommit = False);
            TestSetLogic(oDb).completeAsAbandoned(oStatusData.idTestSet, fCommit = False);
            GlobalResourceLogic(oDb).freeGlobalResourcesByTestBox(self._idTestBox, fCommit = False);

        # Change to idle status
        if oStatusData.enmState != TestBoxStatusData.ksTestBoxState_Idle:
            TestBoxStatusLogic(oDb).updateState(self._idTestBox, TestBoxStatusData.ksTestBoxState_Idle, fCommit = False);
            oStatusData.tsUpdated = oDb.getCurrentTimestamp();
            oStatusData.enmState  = TestBoxStatusData.ksTestBoxState_Idle;

        # Commit.
        oDb.commit();

        return True;

    def _connectToDbAndValidateTb(self, asValidStates = None):
        """
        Connects to the database and validates the testbox.

        Returns (TMDatabaseConnection, TestBoxStatusData, TestBoxData) on success.
        Returns (None, None, None) on failure after sending the box an appropriate response.
        May raise exception on DB error.
        """
        oDb    = TMDatabaseConnection(self._oSrvGlue.dprint);
        oLogic = TestBoxStatusLogic(oDb);
        (oStatusData, oTestBoxData) = oLogic.tryFetchStatusAndConfig(self._idTestBox, self._sTestBoxUuid, self._sTestBoxAddr);
        if oStatusData is None:
            self._resultResponse(constants.tbresp.STATUS_DEAD);
        elif asValidStates is not None and oStatusData.enmState not in asValidStates:
            self._resultResponse(constants.tbresp.STATUS_NACK);
        elif self._idTestSet is not None and self._idTestSet != oStatusData.idTestSet:
            self._resultResponse(constants.tbresp.STATUS_NACK);
        else:
            return (oDb, oStatusData, oTestBoxData);
        return (None, None, None);

    def writeToMainLog(self, oTestSet, sText, fIgnoreSizeCheck = False):
        """ Writes the text to the main log file. """

        # Calc the file name and open the file.
        sFile = os.path.join(config.g_ksFileAreaRootDir, oTestSet.sBaseFilename + '-main.log');
        if not os.path.exists(os.path.dirname(sFile)):
            os.makedirs(os.path.dirname(sFile), 0o755);

        with open(sFile, 'ab') as oFile:
            # Check the size.
            fSizeOk = True;
            if not fIgnoreSizeCheck:
                oStat = os.fstat(oFile.fileno());
                fSizeOk = oStat.st_size / (1024 * 1024) < config.g_kcMbMaxMainLog;

            # Write the text.
            if fSizeOk:
                if sys.version_info[0] >= 3:
                    oFile.write(bytes(sText, 'utf-8'));
                else:
                    oFile.write(sText);

        return fSizeOk;

    def _actionSignOn(self):        # pylint: disable=too-many-locals
        """ Implement sign-on """

        #
        # Validate parameters (raises exception on failure).
        #
        sOs                 = self._getStringParam(constants.tbreq.SIGNON_PARAM_OS, coreconsts.g_kasOses);
        sOsVersion          = self._getStringParam(constants.tbreq.SIGNON_PARAM_OS_VERSION);
        sCpuVendor          = self._getStringParam(constants.tbreq.SIGNON_PARAM_CPU_VENDOR);
        sCpuArch            = self._getStringParam(constants.tbreq.SIGNON_PARAM_CPU_ARCH, coreconsts.g_kasCpuArches);
        sCpuName            = self._getStringParam(constants.tbreq.SIGNON_PARAM_CPU_NAME, fStrip = True, sDefValue = ''); # new
        lCpuRevision        = self._getLongParam(  constants.tbreq.SIGNON_PARAM_CPU_REVISION, lMin = 0, lDefValue = 0);   # new
        cCpus               = self._getIntParam(   constants.tbreq.SIGNON_PARAM_CPU_COUNT, 1, 16384);
        fCpuHwVirt          = self._getBoolParam(  constants.tbreq.SIGNON_PARAM_HAS_HW_VIRT);
        fCpuNestedPaging    = self._getBoolParam(  constants.tbreq.SIGNON_PARAM_HAS_NESTED_PAGING);
        fCpu64BitGuest      = self._getBoolParam(  constants.tbreq.SIGNON_PARAM_HAS_64_BIT_GUEST, fDefValue = True);
        fChipsetIoMmu       = self._getBoolParam(  constants.tbreq.SIGNON_PARAM_HAS_IOMMU);
        fRawMode            = self._getBoolParam(  constants.tbreq.SIGNON_PARAM_WITH_RAW_MODE, fDefValue = None);
        fNativeApi          = self._getBoolParam(  constants.tbreq.SIGNON_PARAM_HAS_NATIVE_API, fDefValue = None);
        cMbMemory           = self._getLongParam(  constants.tbreq.SIGNON_PARAM_MEM_SIZE,     8, 1073741823); # 8MB..1PB
        cMbScratch          = self._getLongParam(  constants.tbreq.SIGNON_PARAM_SCRATCH_SIZE, 0, 1073741823); # 0..1PB
        sReport             = self._getStringParam(constants.tbreq.SIGNON_PARAM_REPORT, fStrip = True, sDefValue = '');   # new
        iTestBoxScriptRev   = self._getIntParam(   constants.tbreq.SIGNON_PARAM_SCRIPT_REV, 1, 100000000);
        iPythonHexVersion   = self._getIntParam(   constants.tbreq.SIGNON_PARAM_PYTHON_VERSION, 0x020300f0, 0x030f00f0);
        self._checkForUnknownParameters();

        # Null conversions for new parameters.
        if not sReport:
            sReport = None;
        if not sCpuName:
            sCpuName = None;
        if lCpuRevision <= 0:
            lCpuRevision = None;

        #
        # Connect to the database and validate the testbox.
        #
        oDb = TMDatabaseConnection(self._oSrvGlue.dprint);
        oTestBoxLogic = TestBoxLogic(oDb);
        oTestBox      = oTestBoxLogic.tryFetchTestBoxByUuid(self._sTestBoxUuid);
        if oTestBox is None:
            oSystemLogLogic = SystemLogLogic(oDb);
            oSystemLogLogic.addEntry(SystemLogData.ksEvent_TestBoxUnknown,
                                     'addr=%s  uuid=%s  os=%s  %d cpus' \
                                     % (self._sTestBoxAddr, self._sTestBoxUuid, sOs, cCpus),
                                     24, fCommit = True);
            return self._resultResponse(constants.tbresp.STATUS_NACK);

        #
        # Update the row in TestBoxes if something changed.
        #
        if oTestBox.cMbScratch is not None and oTestBox.cMbScratch != 0:
            cPctScratchDiff = (cMbScratch - oTestBox.cMbScratch) * 100 / oTestBox.cMbScratch;
        else:
            cPctScratchDiff = 100;

        # pylint: disable=too-many-boolean-expressions
        if   self._sTestBoxAddr != oTestBox.ip \
          or sOs                != oTestBox.sOs \
          or sOsVersion         != oTestBox.sOsVersion \
          or sCpuVendor         != oTestBox.sCpuVendor \
          or sCpuArch           != oTestBox.sCpuArch \
          or sCpuName           != oTestBox.sCpuName \
          or lCpuRevision       != oTestBox.lCpuRevision \
          or cCpus              != oTestBox.cCpus \
          or fCpuHwVirt         != oTestBox.fCpuHwVirt \
          or fCpuNestedPaging   != oTestBox.fCpuNestedPaging \
          or fCpu64BitGuest     != oTestBox.fCpu64BitGuest \
          or fChipsetIoMmu      != oTestBox.fChipsetIoMmu \
          or fRawMode           != oTestBox.fRawMode \
          or fNativeApi         != oTestBox.fNativeApi \
          or cMbMemory          != oTestBox.cMbMemory \
          or abs(cPctScratchDiff) >= min(4 + cMbScratch / 10240, 12) \
          or sReport            != oTestBox.sReport \
          or iTestBoxScriptRev  != oTestBox.iTestBoxScriptRev \
          or iPythonHexVersion  != oTestBox.iPythonHexVersion:
            oTestBoxLogic.updateOnSignOn(oTestBox.idTestBox,
                                         oTestBox.idGenTestBox,
                                         sTestBoxAddr      = self._sTestBoxAddr,
                                         sOs               = sOs,
                                         sOsVersion        = sOsVersion,
                                         sCpuVendor        = sCpuVendor,
                                         sCpuArch          = sCpuArch,
                                         sCpuName          = sCpuName,
                                         lCpuRevision      = lCpuRevision,
                                         cCpus             = cCpus,
                                         fCpuHwVirt        = fCpuHwVirt,
                                         fCpuNestedPaging  = fCpuNestedPaging,
                                         fCpu64BitGuest    = fCpu64BitGuest,
                                         fChipsetIoMmu     = fChipsetIoMmu,
                                         fRawMode          = fRawMode,
                                         fNativeApi        = fNativeApi,
                                         cMbMemory         = cMbMemory,
                                         cMbScratch        = cMbScratch,
                                         sReport           = sReport,
                                         iTestBoxScriptRev = iTestBoxScriptRev,
                                         iPythonHexVersion = iPythonHexVersion);

        #
        # Update the testbox status, making sure there is a status.
        #
        oStatusLogic = TestBoxStatusLogic(oDb);
        oStatusData  = oStatusLogic.tryFetchStatus(oTestBox.idTestBox);
        if oStatusData is not None:
            self._cleanupOldTest(oDb, oStatusData);
        else:
            oStatusLogic.insertIdleStatus(oTestBox.idTestBox, oTestBox.idGenTestBox, fCommit = True);

        #
        # ACK the request.
        #
        dResponse = \
        {
            constants.tbresp.ALL_PARAM_RESULT:  constants.tbresp.STATUS_ACK,
            constants.tbresp.SIGNON_PARAM_ID:   oTestBox.idTestBox,
            constants.tbresp.SIGNON_PARAM_NAME: oTestBox.sName,
        }
        return self._writeResponse(dResponse);

    def _doGangCleanup(self, oDb, oStatusData):
        """
        _doRequestCommand worker for handling a box in gang-cleanup.
        This will check if all testboxes has completed their run, pretending to
        be busy until that happens.  Once all are completed, resources will be
        freed and the testbox returns to idle state (we update oStatusData).
        """
        oStatusLogic = TestBoxStatusLogic(oDb)
        oTestSet = TestSetData().initFromDbWithId(oDb, oStatusData.idTestSet);
        if oStatusLogic.isWholeGangDoneTesting(oTestSet.idTestSetGangLeader):
            oDb.begin();

            GlobalResourceLogic(oDb).freeGlobalResourcesByTestBox(self._idTestBox, fCommit = False);
            TestBoxStatusLogic(oDb).updateState(self._idTestBox, TestBoxStatusData.ksTestBoxState_Idle, fCommit = False);

            oStatusData.tsUpdated = oDb.getCurrentTimestamp();
            oStatusData.enmState = TestBoxStatusData.ksTestBoxState_Idle;

            oDb.commit();
        return None;

    def _doGangGatheringTimedOut(self, oDb, oStatusData):
        """
        _doRequestCommand worker for handling a box in gang-gathering-timed-out state.
        This will do clean-ups similar to _cleanupOldTest and update the state likewise.
        """
        oDb.begin();

        TestSetLogic(oDb).completeAsGangGatheringTimeout(oStatusData.idTestSet, fCommit = False);
        GlobalResourceLogic(oDb).freeGlobalResourcesByTestBox(self._idTestBox, fCommit = False);
        TestBoxStatusLogic(oDb).updateState(self._idTestBox, TestBoxStatusData.ksTestBoxState_Idle, fCommit = False);

        oStatusData.tsUpdated = oDb.getCurrentTimestamp();
        oStatusData.enmState  = TestBoxStatusData.ksTestBoxState_Idle;

        oDb.commit();
        return None;

    def _doGangGathering(self, oDb, oStatusData):
        """
        _doRequestCommand worker for handling a box in gang-gathering state.
        This only checks for timeout.  It will update the oStatusData if a
        timeout is detected, so that the box will be idle upon return.
        """
        oStatusLogic = TestBoxStatusLogic(oDb);
        if     oStatusLogic.timeSinceLastChangeInSecs(oStatusData) > config.g_kcSecGangGathering \
           and SchedulerBase.tryCancelGangGathering(oDb, oStatusData): # <-- Updates oStatusData.
            self._doGangGatheringTimedOut(oDb, oStatusData);
        return None;

    def _doRequestCommand(self, fIdle):
        """
        Common code for handling command request.
        """

        (oDb, oStatusData, oTestBoxData) = self._connectToDbAndValidateTb();
        if oDb is None:
            return False;

        #
        # Status clean up.
        #
        # Only when BUSY will the TestBox Script request and execute commands
        # concurrently.  So, it must be idle when sending REQUEST_COMMAND_IDLE.
        #
        if fIdle:
            if oStatusData.enmState == TestBoxStatusData.ksTestBoxState_GangGathering:
                self._doGangGathering(oDb, oStatusData);
            elif oStatusData.enmState == TestBoxStatusData.ksTestBoxState_GangGatheringTimedOut:
                self._doGangGatheringTimedOut(oDb, oStatusData);
            elif oStatusData.enmState == TestBoxStatusData.ksTestBoxState_GangTesting:
                dResponse = SchedulerBase.composeExecResponse(oDb, oTestBoxData.idTestBox, self._oSrvGlue.getBaseUrl());
                if dResponse is not None:
                    return dResponse;
            elif oStatusData.enmState == TestBoxStatusData.ksTestBoxState_GangCleanup:
                self._doGangCleanup(oDb, oStatusData);
            elif oStatusData.enmState != TestBoxStatusData.ksTestBoxState_Idle: # (includes ksTestBoxState_GangGatheringTimedOut)
                self._cleanupOldTest(oDb, oStatusData);

        #
        # Check for pending command.
        #
        if oTestBoxData.enmPendingCmd != TestBoxData.ksTestBoxCmd_None:
            asValidCmds = TestBoxController.kasIdleCmds if fIdle else TestBoxController.kasBusyCmds;
            if oTestBoxData.enmPendingCmd in asValidCmds:
                dResponse = { constants.tbresp.ALL_PARAM_RESULT: TestBoxController.kdCmdToTbRespCmd[oTestBoxData.enmPendingCmd] };
                if oTestBoxData.enmPendingCmd in [TestBoxData.ksTestBoxCmd_Upgrade, TestBoxData.ksTestBoxCmd_UpgradeAndReboot]:
                    dResponse[constants.tbresp.UPGRADE_PARAM_URL] = self._oSrvGlue.getBaseUrl() + TestBoxController.ksUpgradeZip;
                return self._writeResponse(dResponse);

            if oTestBoxData.enmPendingCmd == TestBoxData.ksTestBoxCmd_Abort and fIdle:
                TestBoxLogic(oDb).setCommand(self._idTestBox, sOldCommand = oTestBoxData.enmPendingCmd,
                                             sNewCommand = TestBoxData.ksTestBoxCmd_None, fCommit = True);

        #
        # If doing gang stuff, return 'CMD_WAIT'.
        #
        ## @todo r=bird: Why is GangTesting included here? Figure out when testing gang testing.
        if oStatusData.enmState in [TestBoxStatusData.ksTestBoxState_GangGathering,
                                    TestBoxStatusData.ksTestBoxState_GangTesting,
                                    TestBoxStatusData.ksTestBoxState_GangCleanup]:
            return self._resultResponse(constants.tbresp.CMD_WAIT);

        #
        # If idling and enabled try schedule a new task.
        #
        if    fIdle \
          and oTestBoxData.fEnabled \
          and not TestSetLogic(oDb).isTestBoxExecutingTooRapidly(oTestBoxData.idTestBox) \
          and oStatusData.enmState == TestBoxStatusData.ksTestBoxState_Idle: # (paranoia)
            dResponse = SchedulerBase.scheduleNewTask(oDb, oTestBoxData, oStatusData.iWorkItem, self._oSrvGlue.getBaseUrl());
            if dResponse is not None:
                return self._writeResponse(dResponse);

        #
        # Touch the status row every couple of mins so we can tell that the box is alive.
        #
        oStatusLogic = TestBoxStatusLogic(oDb);
        if    oStatusData.enmState != TestBoxStatusData.ksTestBoxState_GangGathering \
          and oStatusLogic.timeSinceLastChangeInSecs(oStatusData) >= TestBoxStatusLogic.kcSecIdleTouchStatus:
            oStatusLogic.touchStatus(oTestBoxData.idTestBox, fCommit = True);

        return self._idleResponse();

    def _actionRequestCommandBusy(self):
        """ Implement request for command. """
        self._checkForUnknownParameters();
        return self._doRequestCommand(False);

    def _actionRequestCommandIdle(self):
        """ Implement request for command. """
        self._checkForUnknownParameters();
        return self._doRequestCommand(True);

    def _doCommandAckNck(self, sCmd):
        """ Implements ACK, NACK and NACK(ENOTSUP). """

        (oDb, _, _) = self._connectToDbAndValidateTb();
        if oDb is None:
            return False;

        #
        # If the command maps to a TestBoxCmd_T value, it means we have to
        # check and update TestBoxes.  If it's an ACK, the testbox status will
        # need updating as well.
        #
        sPendingCmd = TestBoxController.kdTbRespCmdToCmd[sCmd];
        if sPendingCmd is not None:
            oTestBoxLogic = TestBoxLogic(oDb)
            oTestBoxLogic.setCommand(self._idTestBox, sOldCommand = sPendingCmd,
                                     sNewCommand = TestBoxData.ksTestBoxCmd_None, fCommit = False);

            if    self._sAction == constants.tbreq.COMMAND_ACK \
              and TestBoxController.kdCmdToState[sPendingCmd] is not None:
                oStatusLogic = TestBoxStatusLogic(oDb);
                oStatusLogic.updateState(self._idTestBox, TestBoxController.kdCmdToState[sPendingCmd], fCommit = False);

            # Commit the two updates.
            oDb.commit();

        #
        # Log NACKs.
        #
        if self._sAction != constants.tbreq.COMMAND_ACK:
            oSysLogLogic = SystemLogLogic(oDb);
            oSysLogLogic.addEntry(SystemLogData.ksEvent_CmdNacked,
                                  'idTestBox=%s sCmd=%s' % (self._idTestBox, sPendingCmd),
                                  24, fCommit = True);

        return self._resultResponse(constants.tbresp.STATUS_ACK);

    def _actionCommandAck(self):
        """ Implement command ACK'ing """
        sCmd = self._getStringParam(constants.tbreq.COMMAND_ACK_PARAM_CMD_NAME, TestBoxController.kasAckableCmds);
        self._checkForUnknownParameters();
        return self._doCommandAckNck(sCmd);

    def _actionCommandNack(self):
        """ Implement command NACK'ing """
        sCmd = self._getStringParam(constants.tbreq.COMMAND_ACK_PARAM_CMD_NAME, TestBoxController.kasNackableCmds);
        self._checkForUnknownParameters();
        return self._doCommandAckNck(sCmd);

    def _actionCommandNotSup(self):
        """ Implement command NACK(ENOTSUP)'ing """
        sCmd = self._getStringParam(constants.tbreq.COMMAND_ACK_PARAM_CMD_NAME, TestBoxController.kasNackableCmds);
        self._checkForUnknownParameters();
        return self._doCommandAckNck(sCmd);

    def _actionLogMain(self):
        """ Implement submitting log entries to the main log file. """
        #
        # Parameter validation.
        #
        sBody = self._getStringParam(constants.tbreq.LOG_PARAM_BODY, fStrip = False);
        if not sBody:
            return self._resultResponse(constants.tbresp.STATUS_NACK);
        self._checkForUnknownParameters();

        (oDb, oStatusData, _) = self._connectToDbAndValidateTb([TestBoxStatusData.ksTestBoxState_Testing,
                                                                TestBoxStatusData.ksTestBoxState_GangTesting]);
        if oStatusData is None:
            return False;

        #
        # Write the text to the log file.
        #
        oTestSet = TestSetData().initFromDbWithId(oDb, oStatusData.idTestSet);
        self.writeToMainLog(oTestSet, sBody);
        ## @todo Overflow is a hanging offence, need to note it and fail whatever is going on...

        # Done.
        return self._resultResponse(constants.tbresp.STATUS_ACK);

    def _actionUpload(self):
        """ Implement uploading of files. """
        #
        # Parameter validation.
        #
        sName = self._getStringParam(constants.tbreq.UPLOAD_PARAM_NAME);
        sMime = self._getStringParam(constants.tbreq.UPLOAD_PARAM_MIME);
        sKind = self._getStringParam(constants.tbreq.UPLOAD_PARAM_KIND);
        sDesc = self._getStringParam(constants.tbreq.UPLOAD_PARAM_DESC);
        self._checkForUnknownParameters();

        (oDb, oStatusData, _) = self._connectToDbAndValidateTb([TestBoxStatusData.ksTestBoxState_Testing,
                                                                TestBoxStatusData.ksTestBoxState_GangTesting]);
        if oStatusData is None:
            return False;

        if len(sName) > 128 or len(sName) < 3:
            raise TestBoxControllerException('Invalid file name "%s"' % (sName,));
        if re.match(r'^[a-zA-Z0-9_\-(){}#@+,.=]*$', sName) is None:
            raise TestBoxControllerException('Invalid file name "%s"' % (sName,));

        if sMime not in [ 'text/plain', #'text/html', 'text/xml',
                          'application/octet-stream',
                          'image/png', #'image/gif', 'image/jpeg',
                          'video/webm', #'video/mpeg', 'video/mpeg4-generic',
                          ]:
            raise TestBoxControllerException('Invalid MIME type "%s"' % (sMime,));

        if sKind not in TestResultFileData.kasKinds:
            raise TestBoxControllerException('Invalid kind "%s"' % (sKind,));

        if len(sDesc) > 256:
            raise TestBoxControllerException('Invalid description "%s"' % (sDesc,));
        if not set(sDesc).issubset(set(string.printable)):
            raise TestBoxControllerException('Invalid description "%s"' % (sDesc,));

        if ('application/octet-stream', {}) != self._oSrvGlue.getContentType():
            raise TestBoxControllerException('Unexpected content type: %s; %s' % self._oSrvGlue.getContentType());

        cbFile = self._oSrvGlue.getContentLength();
        if cbFile <= 0:
            raise TestBoxControllerException('File "%s" is empty or negative in size (%s)' % (sName, cbFile));
        if (cbFile + 1048575) / 1048576 > config.g_kcMbMaxUploadSingle:
            raise TestBoxControllerException('File "%s" is too big %u bytes (max %u MiB)'
                                             % (sName, cbFile, config.g_kcMbMaxUploadSingle,));

        #
        # Write the text to the log file.
        #
        oTestSet = TestSetData().initFromDbWithId(oDb, oStatusData.idTestSet);
        oDstFile = TestSetLogic(oDb).createFile(oTestSet, sName = sName, sMime = sMime, sKind = sKind, sDesc = sDesc,
                                                cbFile = cbFile, fCommit = True);

        offFile  = 0;
        oSrcFile = self._oSrvGlue.getBodyIoStreamBinary();
        while offFile < cbFile:
            cbToRead = cbFile - offFile;
            if cbToRead > 256*1024:
                cbToRead = 256*1024;
            offFile += cbToRead;

            abBuf = oSrcFile.read(cbToRead);
            oDstFile.write(abBuf); # pylint: disable=maybe-no-member
            del abBuf;

        oDstFile.close(); # pylint: disable=maybe-no-member

        # Done.
        return self._resultResponse(constants.tbresp.STATUS_ACK);

    def _actionXmlResults(self):
        """ Implement submitting "XML" like test result stream. """
        #
        # Parameter validation.
        #
        sXml = self._getStringParam(constants.tbreq.XML_RESULT_PARAM_BODY, fStrip = False);
        self._checkForUnknownParameters();
        if not sXml: # Used for link check by vboxinstaller.py on Windows.
            return self._resultResponse(constants.tbresp.STATUS_ACK);

        (oDb, oStatusData, _) = self._connectToDbAndValidateTb([TestBoxStatusData.ksTestBoxState_Testing,
                                                                TestBoxStatusData.ksTestBoxState_GangTesting]);
        if oStatusData is None:
            return False;

        #
        # Process the XML.
        #
        (sError, fUnforgivable) = TestResultLogic(oDb).processXmlStream(sXml, self._idTestSet);
        if sError is not None:
            oTestSet = TestSetData().initFromDbWithId(oDb, oStatusData.idTestSet);
            self.writeToMainLog(oTestSet, '\n!!XML error: %s\n%s\n\n' % (sError, sXml,));
            if fUnforgivable:
                return self._resultResponse(constants.tbresp.STATUS_NACK);
        return self._resultResponse(constants.tbresp.STATUS_ACK);


    def _actionExecCompleted(self):
        """
        Implement EXEC completion.

        Because the action is request by the worker thread of the testbox
        script we cannot pass pending commands back to it like originally
        planned.  So, we just complete the test set and update the status.
        """
        #
        # Parameter validation.
        #
        sStatus = self._getStringParam(constants.tbreq.EXEC_COMPLETED_PARAM_RESULT, TestBoxController.kasValidResults);
        self._checkForUnknownParameters();

        (oDb, oStatusData, _) = self._connectToDbAndValidateTb([TestBoxStatusData.ksTestBoxState_Testing,
                                                                TestBoxStatusData.ksTestBoxState_GangTesting]);
        if oStatusData is None:
            return False;

        #
        # Complete the status.
        #
        oDb.rollback();
        oDb.begin();
        oTestSetLogic = TestSetLogic(oDb);
        idTestSetGangLeader = oTestSetLogic.complete(oStatusData.idTestSet, self.kadTbResultToStatus[sStatus], fCommit = False);

        oStatusLogic = TestBoxStatusLogic(oDb);
        if oStatusData.enmState == TestBoxStatusData.ksTestBoxState_Testing:
            assert idTestSetGangLeader is None;
            GlobalResourceLogic(oDb).freeGlobalResourcesByTestBox(self._idTestBox);
            oStatusLogic.updateState(self._idTestBox, TestBoxStatusData.ksTestBoxState_Idle, fCommit = False);
        else:
            assert idTestSetGangLeader is not None;
            oStatusLogic.updateState(self._idTestBox, TestBoxStatusData.ksTestBoxState_GangCleanup, oStatusData.idTestSet,
                                     fCommit = False);
            if oStatusLogic.isWholeGangDoneTesting(idTestSetGangLeader):
                GlobalResourceLogic(oDb).freeGlobalResourcesByTestBox(self._idTestBox);
                oStatusLogic.updateState(self._idTestBox, TestBoxStatusData.ksTestBoxState_Idle, fCommit = False);

        oDb.commit();
        return self._resultResponse(constants.tbresp.STATUS_ACK);



    def _getStandardParams(self, dParams):
        """
        Gets the standard parameters and validates them.

        The parameters are returned as a tuple: sAction, idTestBox, sTestBoxUuid.
        Note! the sTextBoxId can be None if it's a SIGNON request.

        Raises TestBoxControllerException on invalid input.
        """
        #
        # Get the action parameter and validate it.
        #
        if constants.tbreq.ALL_PARAM_ACTION not in dParams:
            raise TestBoxControllerException('No "%s" parameter in request (params: %s)' \
                                             % (constants.tbreq.ALL_PARAM_ACTION, dParams,));
        sAction = dParams[constants.tbreq.ALL_PARAM_ACTION];

        if sAction not in self._dActions:
            raise TestBoxControllerException('Unknown action "%s" in request (params: %s; action: %s)' \
                                             % (sAction, dParams, self._dActions));

        #
        # TestBox UUID.
        #
        if constants.tbreq.ALL_PARAM_TESTBOX_UUID not in dParams:
            raise TestBoxControllerException('No "%s" parameter in request (params: %s)' \
                                             % (constants.tbreq.ALL_PARAM_TESTBOX_UUID, dParams,));
        sTestBoxUuid = dParams[constants.tbreq.ALL_PARAM_TESTBOX_UUID];
        try:
            sTestBoxUuid = str(uuid.UUID(sTestBoxUuid));
        except Exception as oXcpt:
            raise TestBoxControllerException('Invalid %s parameter value "%s": %s ' \
                                             % (constants.tbreq.ALL_PARAM_TESTBOX_UUID, sTestBoxUuid, oXcpt));
        if sTestBoxUuid == '00000000-0000-0000-0000-000000000000':
            raise TestBoxControllerException('Invalid %s parameter value "%s": NULL UUID not allowed.' \
                                             % (constants.tbreq.ALL_PARAM_TESTBOX_UUID, sTestBoxUuid));

        #
        # TestBox ID.
        #
        if constants.tbreq.ALL_PARAM_TESTBOX_ID in dParams:
            sTestBoxId = dParams[constants.tbreq.ALL_PARAM_TESTBOX_ID];
            try:
                idTestBox = int(sTestBoxId);
                if idTestBox <= 0 or idTestBox >= 0x7fffffff:
                    raise Exception;
            except:
                raise TestBoxControllerException('Bad value for "%s": "%s"' \
                                                 % (constants.tbreq.ALL_PARAM_TESTBOX_ID, sTestBoxId));
        elif sAction == constants.tbreq.SIGNON:
            idTestBox = None;
        else:
            raise TestBoxControllerException('No "%s" parameter in request (params: %s)' \
                                             % (constants.tbreq.ALL_PARAM_TESTBOX_ID, dParams,));

        #
        # Test Set ID.
        #
        if constants.tbreq.RESULT_PARAM_TEST_SET_ID in dParams:
            sTestSetId = dParams[constants.tbreq.RESULT_PARAM_TEST_SET_ID];
            try:
                idTestSet = int(sTestSetId);
                if idTestSet <= 0 or idTestSet >= 0x7fffffff:
                    raise Exception;
            except:
                raise TestBoxControllerException('Bad value for "%s": "%s"' \
                                                 % (constants.tbreq.RESULT_PARAM_TEST_SET_ID, sTestSetId));
        elif sAction not in [ constants.tbreq.XML_RESULTS, ]: ## More later.
            idTestSet = None;
        else:
            raise TestBoxControllerException('No "%s" parameter in request (params: %s)' \
                                             % (constants.tbreq.RESULT_PARAM_TEST_SET_ID, dParams,));

        #
        # The testbox address.
        #
        sTestBoxAddr = self._oSrvGlue.getClientAddr();
        if sTestBoxAddr is None or sTestBoxAddr.strip() == '':
            raise TestBoxControllerException('Invalid client address "%s"' % (sTestBoxAddr,));

        #
        # Update the list of checked parameters.
        #
        self._asCheckedParams.extend([constants.tbreq.ALL_PARAM_TESTBOX_UUID, constants.tbreq.ALL_PARAM_ACTION]);
        if idTestBox is not None:
            self._asCheckedParams.append(constants.tbreq.ALL_PARAM_TESTBOX_ID);
        if idTestSet is not None:
            self._asCheckedParams.append(constants.tbreq.RESULT_PARAM_TEST_SET_ID);

        return (sAction, idTestBox, sTestBoxUuid, sTestBoxAddr, idTestSet);

    def dispatchRequest(self):
        """
        Dispatches the incoming request.

        Will raise TestBoxControllerException on failure.
        """

        #
        # Must be a POST request.
        #
        try:
            sMethod = self._oSrvGlue.getMethod();
        except Exception as oXcpt:
            raise TestBoxControllerException('Error retriving request method: %s' % (oXcpt,));
        if sMethod != 'POST':
            raise TestBoxControllerException('Error expected POST request not "%s"' % (sMethod,));

        #
        # Get the parameters and checks for duplicates.
        #
        try:
            dParams = self._oSrvGlue.getParameters();
        except Exception as oXcpt:
            raise TestBoxControllerException('Error retriving parameters: %s' % (oXcpt,));
        for sKey in dParams.keys():
            if len(dParams[sKey]) > 1:
                raise TestBoxControllerException('Parameter "%s" is given multiple times: %s' % (sKey, dParams[sKey]));
            dParams[sKey] = dParams[sKey][0];
        self._dParams = dParams;

        #
        # Get+validate the standard action parameters and dispatch the request.
        #
        (self._sAction, self._idTestBox, self._sTestBoxUuid, self._sTestBoxAddr, self._idTestSet) = \
            self._getStandardParams(dParams);
        return self._dActions[self._sAction]();
