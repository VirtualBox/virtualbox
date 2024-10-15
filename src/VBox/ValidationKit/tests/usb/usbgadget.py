# -*- coding: utf-8 -*-
# $Id: usbgadget.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=too-many-lines

"""
UTS (USB Test Service) client.
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
import array
import errno
import select
import socket
import sys;
import threading
import time
import zlib

# Validation Kit imports.
from common     import utils;
from testdriver import base;
from testdriver import reporter;
from testdriver.base    import TdTaskBase;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int;     # pylint: disable=redefined-builtin,invalid-name


## @name USB gadget impersonation string constants.
## @{
g_ksGadgetImpersonationInvalid = 'Invalid';
g_ksGadgetImpersonationTest    = 'Test';
g_ksGadgetImpersonationMsd     = 'Msd';
g_ksGadgetImpersonationWebcam  = 'Webcam';
g_ksGadgetImpersonationEther   = 'Ether';
## @}

## @name USB gadget type used in the UTS protocol.
## @{
g_kiGadgetTypeTest             = 1;
## @}

## @name USB gadget access methods used in the UTS protocol.
## @{
g_kiGadgetAccessUsbIp          = 1;
## @}

## @name USB gadget config types.
## @{
g_kiGadgetCfgTypeBool          = 1;
g_kiGadgetCfgTypeString        = 2;
g_kiGadgetCfgTypeUInt8         = 3;
g_kiGadgetCfgTypeUInt16        = 4;
g_kiGadgetCfgTypeUInt32        = 5;
g_kiGadgetCfgTypeUInt64        = 6;
g_kiGadgetCfgTypeInt8          = 7;
g_kiGadgetCfgTypeInt16         = 8;
g_kiGadgetCfgTypeInt32         = 9;
g_kiGadgetCfgTypeInt64         = 10;
## @}

#
# Helpers for decoding data received from the UTS.
# These are used both the Session and Transport classes.
#

def getU64(abData, off):
    """Get a U64 field."""
    return abData[off] \
         + abData[off + 1] * 256 \
         + abData[off + 2] * 65536 \
         + abData[off + 3] * 16777216 \
         + abData[off + 4] * 4294967296 \
         + abData[off + 5] * 1099511627776 \
         + abData[off + 6] * 281474976710656 \
         + abData[off + 7] * 72057594037927936;

def getU32(abData, off):
    """Get a U32 field."""
    return abData[off] \
         + abData[off + 1] * 256 \
         + abData[off + 2] * 65536 \
         + abData[off + 3] * 16777216;

def getU16(abData, off):
    """Get a U16 field."""
    return abData[off] \
         + abData[off + 1] * 256;

def getU8(abData, off):
    """Get a U8 field."""
    return abData[off];

def getSZ(abData, off, sDefault = None):
    """
    Get a zero-terminated string field.
    Returns sDefault if the string is invalid.
    """
    cchStr = getSZLen(abData, off);
    if cchStr >= 0:
        abStr = abData[off:(off + cchStr)];
        try:
            return abStr.tostring().decode('utf_8');
        except:
            reporter.errorXcpt('getSZ(,%u)' % (off));
    return sDefault;

def getSZLen(abData, off):
    """
    Get the length of a zero-terminated string field, in bytes.
    Returns -1 if off is beyond the data packet or not properly terminated.
    """
    cbData = len(abData);
    if off >= cbData:
        return -1;

    offCur = off;
    while abData[offCur] != 0:
        offCur = offCur + 1;
        if offCur >= cbData:
            return -1;

    return offCur - off;

def isValidOpcodeEncoding(sOpcode):
    """
    Checks if the specified opcode is valid or not.
    Returns True on success.
    Returns False if it is invalid, details in the log.
    """
    sSet1 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    sSet2 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_ ";
    if len(sOpcode) != 8:
        reporter.error("invalid opcode length: %s" % (len(sOpcode)));
        return False;
    for i in range(0, 1):
        if sSet1.find(sOpcode[i]) < 0:
            reporter.error("invalid opcode char #%u: %s" % (i, sOpcode));
            return False;
    for i in range(2, 7):
        if sSet2.find(sOpcode[i]) < 0:
            reporter.error("invalid opcode char #%u: %s" % (i, sOpcode));
            return False;
    return True;

#
# Helper for encoding data sent to the UTS.
#

def u32ToByteArray(u32):
    """Encodes the u32 value as a little endian byte (B) array."""
    return array.array('B', \
                       (  u32              % 256, \
                         (u32 // 256)      % 256, \
                         (u32 // 65536)    % 256, \
                         (u32 // 16777216) % 256) );

def u16ToByteArray(u16):
    """Encodes the u16 value as a little endian byte (B) array."""
    return array.array('B', \
                       (  u16         % 256, \
                         (u16 // 256) % 256) );

def u8ToByteArray(uint8):
    """Encodes the u8 value as a little endian byte (B) array."""
    return array.array('B', (uint8 % 256));

def zeroByteArray(cb):
    """Returns an array with the given size containing 0."""
    abArray = array.array('B', (0, ));
    cb = cb - 1;
    for i in range(cb): # pylint: disable=unused-variable
        abArray.append(0);
    return abArray;

def strToByteArry(sStr):
    """Encodes the string as a little endian byte (B) array including the terminator."""
    abArray = array.array('B');
    sUtf8 = sStr.encode('utf_8');
    for ch in sUtf8:
        abArray.append(ord(ch));
    abArray.append(0);
    return abArray;

def cfgListToByteArray(lst):
    """Encodes the given config list as a little endian byte (B) array."""
    abArray = array.array('B');
    if lst is not None:
        for t3Item in lst:
            # Encode they key size
            abArray.extend(u32ToByteArray(len(t3Item[0]) + 1)); # Include terminator
            abArray.extend(u32ToByteArray(t3Item[1]))           # Config type
            abArray.extend(u32ToByteArray(len(t3Item[2]) + 1)); # Value size including temrinator.
            abArray.extend(u32ToByteArray(0));                  # Reserved field.

            abArray.extend(strToByteArry(t3Item[0]));
            abArray.extend(strToByteArry(t3Item[2]));

    return abArray;

class TransportBase(object):
    """
    Base class for the transport layer.
    """

    def __init__(self, sCaller):
        self.sDbgCreated    = '%s: %s' % (utils.getTimePrefix(), sCaller);
        self.fDummy         = 0;
        self.abReadAheadHdr = array.array('B');

    def toString(self):
        """
        Stringify the instance for logging and debugging.
        """
        return '<%s: abReadAheadHdr=%s, sDbgCreated=%s>' % (type(self).__name__, self.abReadAheadHdr, self.sDbgCreated);

    def __str__(self):
        return self.toString();

    def cancelConnect(self):
        """
        Cancels any pending connect() call.
        Returns None;
        """
        return None;

    def connect(self, cMsTimeout):
        """
        Quietly attempts to connect to the UTS.

        Returns True on success.
        Returns False on retryable errors (no logging).
        Returns None on fatal errors with details in the log.

        Override this method, don't call super.
        """
        _ = cMsTimeout;
        return False;

    def disconnect(self, fQuiet = False):
        """
        Disconnect from the UTS.

        Returns True.

        Override this method, don't call super.
        """
        _ = fQuiet;
        return True;

    def sendBytes(self, abBuf, cMsTimeout):
        """
        Sends the bytes in the buffer abBuf to the UTS.

        Returns True on success.
        Returns False on failure and error details in the log.

        Override this method, don't call super.

        Remarks: len(abBuf) is always a multiple of 16.
        """
        _ = abBuf; _ = cMsTimeout;
        return False;

    def recvBytes(self, cb, cMsTimeout, fNoDataOk):
        """
        Receive cb number of bytes from the UTS.

        Returns the bytes (array('B')) on success.
        Returns None on failure and error details in the log.

        Override this method, don't call super.

        Remarks: cb is always a multiple of 16.
        """
        _ = cb; _ = cMsTimeout; _ = fNoDataOk;
        return None;

    def isConnectionOk(self):
        """
        Checks if the connection is OK.

        Returns True if it is.
        Returns False if it isn't (caller should call diconnect).

        Override this method, don't call super.
        """
        return True;

    def isRecvPending(self, cMsTimeout = 0):
        """
        Checks if there is incoming bytes, optionally waiting cMsTimeout
        milliseconds for something to arrive.

        Returns True if there is, False if there isn't.

        Override this method, don't call super.
        """
        _ = cMsTimeout;
        return False;

    def sendMsgInt(self, sOpcode, cMsTimeout, abPayload = array.array('B')):
        """
        Sends a message (opcode + encoded payload).

        Returns True on success.
        Returns False on failure and error details in the log.
        """
        # Fix + check the opcode.
        if len(sOpcode) < 2:
            reporter.fatal('sendMsgInt: invalid opcode length: %d (\"%s\")' % (len(sOpcode), sOpcode));
            return False;
        sOpcode = sOpcode.ljust(8);
        if not isValidOpcodeEncoding(sOpcode):
            reporter.fatal('sendMsgInt: invalid opcode encoding: \"%s\"' % (sOpcode));
            return False;

        # Start construct the message.
        cbMsg = 16 + len(abPayload);
        abMsg = array.array('B');
        abMsg.extend(u32ToByteArray(cbMsg));
        abMsg.extend((0, 0, 0, 0));     # uCrc32
        try:
            abMsg.extend(array.array('B', \
                                     ( ord(sOpcode[0]), \
                                       ord(sOpcode[1]), \
                                       ord(sOpcode[2]), \
                                       ord(sOpcode[3]), \
                                       ord(sOpcode[4]), \
                                       ord(sOpcode[5]), \
                                       ord(sOpcode[6]), \
                                       ord(sOpcode[7]) ) ) );
            if abPayload:
                abMsg.extend(abPayload);
        except:
            reporter.fatalXcpt('sendMsgInt: packing problem...');
            return False;

        # checksum it, padd it and send it off.
        uCrc32 = zlib.crc32(abMsg[8:]);
        abMsg[4:8] = u32ToByteArray(uCrc32);

        while len(abMsg) % 16:
            abMsg.append(0);

        reporter.log2('sendMsgInt: op=%s len=%d to=%d' % (sOpcode, len(abMsg), cMsTimeout));
        return self.sendBytes(abMsg, cMsTimeout);

    def recvMsg(self, cMsTimeout, fNoDataOk = False):
        """
        Receives a message from the UTS.

        Returns the message three-tuple: length, opcode, payload.
        Returns (None, None, None) on failure and error details in the log.
        """

        # Read the header.
        if self.abReadAheadHdr:
            assert(len(self.abReadAheadHdr) == 16);
            abHdr = self.abReadAheadHdr;
            self.abReadAheadHdr = array.array('B');
        else:
            abHdr = self.recvBytes(16, cMsTimeout, fNoDataOk); # (virtual method) # pylint: disable=assignment-from-none
            if abHdr is None:
                return (None, None, None);
        if len(abHdr) != 16:
            reporter.fatal('recvBytes(16) returns %d bytes!' % (len(abHdr)));
            return (None, None, None);

        # Unpack and validate the header.
        cbMsg   = getU32(abHdr, 0);
        uCrc32  = getU32(abHdr, 4);
        sOpcode = abHdr[8:16].tostring().decode('ascii');

        if cbMsg < 16:
            reporter.fatal('recvMsg: message length is out of range: %s (min 16 bytes)' % (cbMsg));
            return (None, None, None);
        if cbMsg > 1024*1024:
            reporter.fatal('recvMsg: message length is out of range: %s (max 1MB)' % (cbMsg));
            return (None, None, None);
        if not isValidOpcodeEncoding(sOpcode):
            reporter.fatal('recvMsg: invalid opcode \"%s\"' % (sOpcode));
            return (None, None, None);

        # Get the payload (if any), dropping the padding.
        abPayload = array.array('B');
        if cbMsg > 16:
            if cbMsg % 16:
                cbPadding = 16 - (cbMsg % 16);
            else:
                cbPadding = 0;
            abPayload = self.recvBytes(cbMsg - 16 + cbPadding, cMsTimeout, False); # pylint: disable=assignment-from-none
            if abPayload is None:
                self.abReadAheadHdr = abHdr;
                if not fNoDataOk    :
                    reporter.log('recvMsg: failed to recv payload bytes!');
                return (None, None, None);

            while cbPadding > 0:
                abPayload.pop();
                cbPadding = cbPadding - 1;

        # Check the CRC-32.
        if uCrc32 != 0:
            uActualCrc32 = zlib.crc32(abHdr[8:]);
            if cbMsg > 16:
                uActualCrc32 = zlib.crc32(abPayload, uActualCrc32);
            uActualCrc32 = uActualCrc32 & 0xffffffff;
            if uCrc32 != uActualCrc32:
                reporter.fatal('recvMsg: crc error: expected %s, got %s' % (hex(uCrc32), hex(uActualCrc32)));
                return (None, None, None);

        reporter.log2('recvMsg: op=%s len=%d' % (sOpcode, len(abPayload)));
        return (cbMsg, sOpcode, abPayload);

    def sendMsg(self, sOpcode, cMsTimeout, aoPayload = ()):
        """
        Sends a message (opcode + payload tuple).

        Returns True on success.
        Returns False on failure and error details in the log.
        Returns None if you pass the incorrectly typed parameters.
        """
        # Encode the payload.
        abPayload = array.array('B');
        for o in aoPayload:
            try:
                if utils.isString(o):
                    # the primitive approach...
                    sUtf8 = o.encode('utf_8');
                    for ch in sUtf8:
                        abPayload.append(ord(ch))
                    abPayload.append(0);
                elif isinstance(o, long):
                    if o < 0 or o > 0xffffffff:
                        reporter.fatal('sendMsg: uint32_t payload is out of range: %s' % (hex(o)));
                        return None;
                    abPayload.extend(u32ToByteArray(o));
                elif isinstance(o, int):
                    if o < 0 or o > 0xffffffff:
                        reporter.fatal('sendMsg: uint32_t payload is out of range: %s' % (hex(o)));
                        return None;
                    abPayload.extend(u32ToByteArray(o));
                elif isinstance(o, array.array):
                    abPayload.extend(o);
                else:
                    reporter.fatal('sendMsg: unexpected payload type: %s (%s) (aoPayload=%s)' % (type(o), o, aoPayload));
                    return None;
            except:
                reporter.fatalXcpt('sendMsg: screwed up the encoding code...');
                return None;
        return self.sendMsgInt(sOpcode, cMsTimeout, abPayload);


class Session(TdTaskBase):
    """
    A USB Test Service (UTS) client session.
    """

    def __init__(self, oTransport, cMsTimeout, cMsIdleFudge, fTryConnect = False):
        """
        Construct a UTS session.

        This starts by connecting to the UTS and will enter the signalled state
        when connected or the timeout has been reached.
        """
        TdTaskBase.__init__(self, utils.getCallerName());
        self.oTransport     = oTransport;
        self.sStatus        = "";
        self.cMsTimeout     = 0;
        self.fErr           = True;     # Whether to report errors as error.
        self.msStart        = 0;
        self.oThread        = None;
        self.fnTask         = self.taskDummy;
        self.aTaskArgs      = None;
        self.oTaskRc        = None;
        self.t3oReply       = (None, None, None);
        self.fScrewedUpMsgState = False;
        self.fTryConnect    = fTryConnect;

        if not self.startTask(cMsTimeout, False, "connecting", self.utsTaskConnect, (cMsIdleFudge,)):
            raise base.GenError("startTask failed");

    def __del__(self):
        """Make sure to cancel the task when deleted."""
        self.cancelTask();

    def toString(self):
        return '<%s fnTask=%s, aTaskArgs=%s, sStatus=%s, oTaskRc=%s, cMsTimeout=%s,' \
               ' msStart=%s, fTryConnect=%s, fErr=%s, fScrewedUpMsgState=%s, t3oReply=%s oTransport=%s, oThread=%s>' \
             % (TdTaskBase.toString(self), self.fnTask, self.aTaskArgs, self.sStatus, self.oTaskRc, self.cMsTimeout,
                self.msStart, self.fTryConnect, self.fErr, self.fScrewedUpMsgState, self.t3oReply, self.oTransport, self.oThread);

    def taskDummy(self):
        """Place holder to catch broken state handling."""
        raise Exception();

    def startTask(self, cMsTimeout, fIgnoreErrors, sStatus, fnTask, aArgs = ()):
        """
        Kicks of a new task.

        cMsTimeout:         The task timeout in milliseconds. Values less than
                            500 ms will be adjusted to 500 ms. This means it is
                            OK to use negative value.
        sStatus:            The task status.
        fnTask:             The method that'll execute the task.
        aArgs:              Arguments to pass to fnTask.

        Returns True on success, False + error in log on failure.
        """
        if not self.cancelTask():
            reporter.maybeErr(not fIgnoreErrors, 'utsclient.Session.startTask: failed to cancel previous task.');
            return False;

        # Change status and make sure we're the
        self.lockTask();
        if self.sStatus != "":
            self.unlockTask();
            reporter.maybeErr(not fIgnoreErrors, 'utsclient.Session.startTask: race.');
            return False;
        self.sStatus        = "setup";
        self.oTaskRc        = None;
        self.t3oReply       = (None, None, None);
        self.resetTaskLocked();
        self.unlockTask();

        self.cMsTimeout     = max(cMsTimeout, 500);
        self.fErr           = not fIgnoreErrors;
        self.fnTask         = fnTask;
        self.aTaskArgs      = aArgs;
        self.oThread        = threading.Thread(target=self.utsTaskThread, args=(), name='UTS-%s' % (sStatus,));
        self.oThread.setDaemon(True); # pylint: disable=deprecated-method
        self.msStart        = base.timestampMilli();

        self.lockTask();
        self.sStatus        = sStatus;
        self.unlockTask();
        self.oThread.start();

        return True;

    def cancelTask(self, fSync = True):
        """
        Attempts to cancel any pending tasks.
        Returns success indicator (True/False).
        """
        self.lockTask();

        if self.sStatus == "":
            self.unlockTask();
            return True;
        if self.sStatus == "setup":
            self.unlockTask();
            return False;
        if self.sStatus == "cancelled":
            self.unlockTask();
            return False;

        reporter.log('utsclient: cancelling "%s"...' % (self.sStatus));
        if self.sStatus == 'connecting':
            self.oTransport.cancelConnect();

        self.sStatus = "cancelled";
        oThread = self.oThread;
        self.unlockTask();

        if not fSync:
            return False;

        oThread.join(61.0);

        if sys.version_info < (3, 9, 0):
            # Removed since Python 3.9.
            return oThread.isAlive(); # pylint: disable=no-member,deprecated-method
        return oThread.is_alive();

    def utsTaskThread(self):
        """
        The task thread function.
        This does some housekeeping activities around the real task method call.
        """
        if not self.isCancelled():
            try:
                fnTask = self.fnTask;
                oTaskRc = fnTask(*self.aTaskArgs);
            except:
                reporter.fatalXcpt('utsTaskThread', 15);
                oTaskRc = None;
        else:
            reporter.log('utsTaskThread: cancelled already');

        self.lockTask();

        reporter.log('utsTaskThread: signalling task with status "%s", oTaskRc=%s' % (self.sStatus, oTaskRc));
        self.oTaskRc = oTaskRc;
        self.oThread = None;
        self.sStatus = '';
        self.signalTaskLocked();

        self.unlockTask();
        return None;

    def isCancelled(self):
        """Internal method for checking if the task has been cancelled."""
        self.lockTask();
        sStatus = self.sStatus;
        self.unlockTask();
        if sStatus == "cancelled":
            return True;
        return False;

    def hasTimedOut(self):
        """Internal method for checking if the task has timed out or not."""
        cMsLeft = self.getMsLeft();
        if cMsLeft <= 0:
            return True;
        return False;

    def getMsLeft(self, cMsMin = 0, cMsMax = -1):
        """Gets the time left until the timeout."""
        cMsElapsed = base.timestampMilli() - self.msStart;
        if cMsElapsed < 0:
            return cMsMin;
        cMsLeft = self.cMsTimeout - cMsElapsed;
        if cMsLeft <= cMsMin:
            return cMsMin;
        if cMsLeft > cMsMax > 0:
            return cMsMax
        return cMsLeft;

    def recvReply(self, cMsTimeout = None, fNoDataOk = False):
        """
        Wrapper for TransportBase.recvMsg that stashes the response away
        so the client can inspect it later on.
        """
        if cMsTimeout is None:
            cMsTimeout = self.getMsLeft(500);
        cbMsg, sOpcode, abPayload = self.oTransport.recvMsg(cMsTimeout, fNoDataOk);
        self.lockTask();
        self.t3oReply = (cbMsg, sOpcode, abPayload);
        self.unlockTask();
        return (cbMsg, sOpcode, abPayload);

    def recvAck(self, fNoDataOk = False):
        """
        Receives an ACK or error response from the UTS.

        Returns True on success.
        Returns False on timeout or transport error.
        Returns (sOpcode, sDetails) tuple on failure.  The opcode is stripped
        and there are always details of some sort or another.
        """
        cbMsg, sOpcode, abPayload = self.recvReply(None, fNoDataOk);
        if cbMsg is None:
            return False;
        sOpcode = sOpcode.strip()
        if sOpcode == "ACK":
            return True;
        return (sOpcode, getSZ(abPayload, 16, sOpcode));

    def recvAckLogged(self, sCommand, fNoDataOk = False):
        """
        Wrapper for recvAck and logging.
        Returns True on success (ACK).
        Returns False on time, transport error and errors signalled by UTS.
        """
        rc = self.recvAck(fNoDataOk);
        if rc is not True  and  not fNoDataOk:
            if rc is False:
                reporter.maybeErr(self.fErr, 'recvAckLogged: %s transport error' % (sCommand));
            else:
                reporter.maybeErr(self.fErr, 'recvAckLogged: %s response was %s: %s' % (sCommand, rc[0], rc[1]));
                rc = False;
        return rc;

    def recvTrueFalse(self, sCommand):
        """
        Receives a TRUE/FALSE response from the UTS.
        Returns True on TRUE, False on FALSE and None on error/other (logged).
        """
        cbMsg, sOpcode, abPayload = self.recvReply();
        if cbMsg is None:
            reporter.maybeErr(self.fErr, 'recvAckLogged: %s transport error' % (sCommand));
            return None;

        sOpcode = sOpcode.strip()
        if sOpcode == "TRUE":
            return True;
        if sOpcode == "FALSE":
            return False;
        reporter.maybeErr(self.fErr, 'recvAckLogged: %s response was %s: %s' % \
                                     (sCommand, sOpcode, getSZ(abPayload, 16, sOpcode)));
        return None;

    def sendMsg(self, sOpcode, aoPayload = (), cMsTimeout = None):
        """
        Wrapper for TransportBase.sendMsg that inserts the correct timeout.
        """
        if cMsTimeout is None:
            cMsTimeout = self.getMsLeft(500);
        return self.oTransport.sendMsg(sOpcode, cMsTimeout, aoPayload);

    def asyncToSync(self, fnAsync, *aArgs):
        """
        Wraps an asynchronous task into a synchronous operation.

        Returns False on failure, task return status on success.
        """
        rc = fnAsync(*aArgs);
        if rc is False:
            reporter.log2('asyncToSync(%s): returns False (#1)' % (fnAsync));
            return rc;

        rc = self.waitForTask(self.cMsTimeout + 5000);
        if rc is False:
            reporter.maybeErrXcpt(self.fErr, 'asyncToSync: waitForTask failed...');
            self.cancelTask();
            #reporter.log2('asyncToSync(%s): returns False (#2)' % (fnAsync, rc));
            return False;

        rc = self.getResult();
        #reporter.log2('asyncToSync(%s): returns %s' % (fnAsync, rc));
        return rc;

    #
    # Connection tasks.
    #

    def utsTaskConnect(self, cMsIdleFudge):
        """Tries to connect to the UTS"""
        while not self.isCancelled():
            reporter.log2('utsTaskConnect: connecting ...');
            rc = self.oTransport.connect(self.getMsLeft(500));
            if rc is True:
                reporter.log('utsTaskConnect: succeeded');
                return self.taskGreet(cMsIdleFudge);
            if rc is None:
                reporter.log2('utsTaskConnect: unable to connect');
                return None;
            if self.hasTimedOut():
                reporter.log2('utsTaskConnect: timed out');
                if not self.fTryConnect:
                    reporter.maybeErr(self.fErr, 'utsTaskConnect: timed out');
                return False;
            time.sleep(self.getMsLeft(1, 1000) / 1000.0);
        if not self.fTryConnect:
            reporter.maybeErr(self.fErr, 'utsTaskConnect: cancelled');
        return False;

    def taskGreet(self, cMsIdleFudge):
        """Greets the UTS"""
        sHostname = socket.gethostname().lower();
        cbFill = 68 - len(sHostname) - 1;
        rc = self.sendMsg("HOWDY", ((1 << 16) | 0, 0x1, len(sHostname), sHostname, zeroByteArray(cbFill)));
        if rc is True:
            rc = self.recvAckLogged("HOWDY", self.fTryConnect);
        if rc is True:
            while cMsIdleFudge > 0:
                cMsIdleFudge -= 1000;
                time.sleep(1);
        else:
            self.oTransport.disconnect(self.fTryConnect);
        return rc;

    def taskBye(self):
        """Says goodbye to the UTS"""
        rc = self.sendMsg("BYE");
        if rc is True:
            rc = self.recvAckLogged("BYE");
        self.oTransport.disconnect();
        return rc;

    #
    # Gadget tasks.
    #

    def taskGadgetCreate(self, iGadgetType, iGadgetAccess, lstCfg = None):
        """Creates a new gadget on UTS"""
        cCfgItems = 0;
        if lstCfg is not None:
            cCfgItems = len(lstCfg);
        fRc = self.sendMsg("GDGTCRT", (iGadgetType, iGadgetAccess, cCfgItems, 0, cfgListToByteArray(lstCfg)));
        if fRc is True:
            fRc = self.recvAckLogged("GDGTCRT");
        return fRc;

    def taskGadgetDestroy(self, iGadgetId):
        """Destroys the given gadget handle on UTS"""
        fRc = self.sendMsg("GDGTDTOR", (iGadgetId, zeroByteArray(12)));
        if fRc is True:
            fRc = self.recvAckLogged("GDGTDTOR");
        return fRc;

    def taskGadgetConnect(self, iGadgetId):
        """Connects the given gadget handle on UTS"""
        fRc = self.sendMsg("GDGTCNCT", (iGadgetId, zeroByteArray(12)));
        if fRc is True:
            fRc = self.recvAckLogged("GDGTCNCT");
        return fRc;

    def taskGadgetDisconnect(self, iGadgetId):
        """Disconnects the given gadget handle from UTS"""
        fRc = self.sendMsg("GDGTDCNT", (iGadgetId, zeroByteArray(12)));
        if fRc is True:
            fRc = self.recvAckLogged("GDGTDCNT");
        return fRc;

    #
    # Public methods - generic task queries
    #

    def isSuccess(self):
        """Returns True if the task completed successfully, otherwise False."""
        self.lockTask();
        sStatus = self.sStatus;
        oTaskRc = self.oTaskRc;
        self.unlockTask();
        if sStatus != "":
            return False;
        if oTaskRc is False or oTaskRc is None:
            return False;
        return True;

    def getResult(self):
        """
        Returns the result of a completed task.
        Returns None if not completed yet or no previous task.
        """
        self.lockTask();
        sStatus = self.sStatus;
        oTaskRc = self.oTaskRc;
        self.unlockTask();
        if sStatus != "":
            return None;
        return oTaskRc;

    def getLastReply(self):
        """
        Returns the last reply three-tuple: cbMsg, sOpcode, abPayload.
        Returns a None, None, None three-tuple if there was no last reply.
        """
        self.lockTask();
        t3oReply = self.t3oReply;
        self.unlockTask();
        return t3oReply;

    #
    # Public methods - connection.
    #

    def asyncDisconnect(self, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a disconnect task.

        Returns True on success, False on failure (logged).

        The task returns True on success and False on failure.
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "bye", self.taskBye);

    def syncDisconnect(self, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncDisconnect, cMsTimeout, fIgnoreErrors);

    #
    # Public methods - gadget API
    #

    def asyncGadgetCreate(self, iGadgetType, iGadgetAccess, lstCfg = None, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a gadget create task.

        Returns True on success, False on failure (logged).

        The task returns True on success and False on failure.
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "GadgetCreate", self.taskGadgetCreate, \
                              (iGadgetType, iGadgetAccess, lstCfg));

    def syncGadgetCreate(self, iGadgetType, iGadgetAccess, lstCfg = None, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncGadgetCreate, iGadgetType, iGadgetAccess, lstCfg, cMsTimeout, fIgnoreErrors);

    def asyncGadgetDestroy(self, iGadgetId, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a gadget destroy task.

        Returns True on success, False on failure (logged).

        The task returns True on success and False on failure.
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "GadgetDestroy", self.taskGadgetDestroy, \
                              (iGadgetId, ));

    def syncGadgetDestroy(self, iGadgetId, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncGadgetDestroy, iGadgetId, cMsTimeout, fIgnoreErrors);

    def asyncGadgetConnect(self, iGadgetId, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a gadget connect task.

        Returns True on success, False on failure (logged).

        The task returns True on success and False on failure.
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "GadgetConnect", self.taskGadgetConnect, \
                              (iGadgetId, ));

    def syncGadgetConnect(self, iGadgetId, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncGadgetConnect, iGadgetId, cMsTimeout, fIgnoreErrors);

    def asyncGadgetDisconnect(self, iGadgetId, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a gadget disconnect task.

        Returns True on success, False on failure (logged).

        The task returns True on success and False on failure.
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "GadgetDisconnect", self.taskGadgetDisconnect, \
                              (iGadgetId, ));

    def syncGadgetDisconnect(self, iGadgetId, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncGadgetDisconnect, iGadgetId, cMsTimeout, fIgnoreErrors);


class TransportTcp(TransportBase):
    """
    TCP transport layer for the UTS client session class.
    """

    def __init__(self, sHostname, uPort):
        """
        Save the parameters. The session will call us back to make the
        connection later on its worker thread.
        """
        TransportBase.__init__(self, utils.getCallerName());
        self.sHostname        = sHostname;
        self.uPort            = uPort if uPort is not None else 6042;
        self.oSocket          = None;
        self.oWakeupW         = None;
        self.oWakeupR         = None;
        self.fConnectCanceled = False;
        self.fIsConnecting    = False;
        self.oCv              = threading.Condition();
        self.abReadAhead      = array.array('B');

    def toString(self):
        return '<%s sHostname=%s, uPort=%s, oSocket=%s,'\
               ' fConnectCanceled=%s, fIsConnecting=%s, oCv=%s, abReadAhead=%s>' \
             % (TransportBase.toString(self), self.sHostname, self.uPort, self.oSocket,
                self.fConnectCanceled, self.fIsConnecting, self.oCv, self.abReadAhead);

    def __isInProgressXcpt(self, oXcpt):
        """ In progress exception? """
        try:
            if isinstance(oXcpt, socket.error):
                try:
                    if oXcpt[0] == errno.EINPROGRESS:
                        return True;
                except: pass;
                try:
                    if oXcpt[0] == errno.EWOULDBLOCK:
                        return True;
                    if utils.getHostOs() == 'win' and oXcpt[0] == errno.WSAEWOULDBLOCK: # pylint: disable=no-member
                        return True;
                except: pass;
        except:
            pass;
        return False;

    def __isWouldBlockXcpt(self, oXcpt):
        """ Would block exception? """
        try:
            if isinstance(oXcpt, socket.error):
                try:
                    if oXcpt[0] == errno.EWOULDBLOCK:
                        return True;
                except: pass;
                try:
                    if oXcpt[0] == errno.EAGAIN:
                        return True;
                except: pass;
        except:
            pass;
        return False;

    def __isConnectionReset(self, oXcpt):
        """ Connection reset by Peer or others. """
        try:
            if isinstance(oXcpt, socket.error):
                try:
                    if oXcpt[0] == errno.ECONNRESET:
                        return True;
                except: pass;
                try:
                    if oXcpt[0] == errno.ENETRESET:
                        return True;
                except: pass;
        except:
            pass;
        return False;

    def _closeWakeupSockets(self):
        """ Closes the wakup sockets.  Caller should own the CV. """
        oWakeupR = self.oWakeupR;
        self.oWakeupR = None;
        if oWakeupR is not None:
            oWakeupR.close();

        oWakeupW = self.oWakeupW;
        self.oWakeupW = None;
        if oWakeupW is not None:
            oWakeupW.close();

        return None;

    def cancelConnect(self):
        # This is bad stuff.
        self.oCv.acquire();
        reporter.log2('TransportTcp::cancelConnect: fIsConnecting=%s oSocket=%s' % (self.fIsConnecting, self.oSocket));
        self.fConnectCanceled = True;
        if self.fIsConnecting:
            oSocket = self.oSocket;
            self.oSocket = None;
            if oSocket is not None:
                reporter.log2('TransportTcp::cancelConnect: closing the socket');
                oSocket.close();

            oWakeupW = self.oWakeupW;
            self.oWakeupW = None;
            if oWakeupW is not None:
                reporter.log2('TransportTcp::cancelConnect: wakeup call');
                try:    oWakeupW.send('cancelled!\n');
                except: reporter.logXcpt();
                try:    oWakeupW.shutdown(socket.SHUT_WR);
                except: reporter.logXcpt();
                oWakeupW.close();
        self.oCv.release();

    def _connectAsClient(self, oSocket, oWakeupR, cMsTimeout):
        """ Connects to the UTS server as client. """

        # Connect w/ timeouts.
        rc = None;
        try:
            oSocket.connect((self.sHostname, self.uPort));
            rc = True;
        except socket.error as oXcpt:
            iRc = oXcpt.errno;
            if self.__isInProgressXcpt(oXcpt):
                # Do the actual waiting.
                reporter.log2('TransportTcp::connect: operation in progress (%s)...' % (oXcpt,));
                try:
                    ttRc = select.select([oWakeupR], [oSocket], [oSocket, oWakeupR], cMsTimeout / 1000.0);
                    if len(ttRc[1]) + len(ttRc[2]) == 0:
                        raise socket.error(errno.ETIMEDOUT, 'select timed out');
                    iRc = oSocket.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR);
                    rc = iRc == 0;
                except socket.error as oXcpt2:
                    iRc = oXcpt2.errno;
                except:
                    iRc = -42;
                    reporter.fatalXcpt('socket.select() on connect failed');

            if rc is True:
                pass;
            elif iRc in (errno.ECONNREFUSED, errno.EHOSTUNREACH, errno.EINTR, errno.ENETDOWN, errno.ENETUNREACH, errno.ETIMEDOUT):
                rc = False; # try again.
            else:
                if iRc != errno.EBADF  or  not self.fConnectCanceled:
                    reporter.fatalXcpt('socket.connect((%s,%s)) failed; iRc=%s' % (self.sHostname, self.uPort, iRc));
            reporter.log2('TransportTcp::connect: rc=%s iRc=%s' % (rc, iRc));
        except:
            reporter.fatalXcpt('socket.connect((%s,%s)) failed' % (self.sHostname, self.uPort));
        return rc;


    def connect(self, cMsTimeout):
        # Create a non-blocking socket.
        reporter.log2('TransportTcp::connect: cMsTimeout=%s sHostname=%s uPort=%s' % (cMsTimeout, self.sHostname, self.uPort));
        try:
            oSocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0);
        except:
            reporter.fatalXcpt('socket.socket() failed');
            return None;
        try:
            oSocket.setblocking(0);
        except:
            oSocket.close();
            reporter.fatalXcpt('socket.socket() failed');
            return None;

        # Create wakeup socket pair for unix (select doesn't wake up on socket close on Linux).
        oWakeupR = None;
        oWakeupW = None;
        if hasattr(socket, 'socketpair'):
            try:    (oWakeupR, oWakeupW) = socket.socketpair();         # pylint: disable=no-member
            except: reporter.logXcpt('socket.socketpair() failed');

        # Update the state.
        self.oCv.acquire();
        rc = None;
        if not self.fConnectCanceled:
            self.oSocket       = oSocket;
            self.oWakeupW      = oWakeupW;
            self.oWakeupR      = oWakeupR;
            self.fIsConnecting = True;
            self.oCv.release();

            # Try connect.
            if oWakeupR is None:
                oWakeupR = oSocket; # Avoid select failure.
            rc = self._connectAsClient(oSocket, oWakeupR, cMsTimeout);
            oSocket = None;

            # Update the state and cleanup on failure/cancel.
            self.oCv.acquire();
            if rc is True  and  self.fConnectCanceled:
                rc = False;
            self.fIsConnecting = False;

        if rc is not True:
            if self.oSocket is not None:
                self.oSocket.close();
                self.oSocket = None;
            self._closeWakeupSockets();
        self.oCv.release();

        reporter.log2('TransportTcp::connect: returning %s' % (rc,));
        return rc;

    def disconnect(self, fQuiet = False):
        if self.oSocket is not None:
            self.abReadAhead = array.array('B');

            # Try a shutting down the socket gracefully (draining it).
            try:
                self.oSocket.shutdown(socket.SHUT_WR);
            except:
                if not fQuiet:
                    reporter.error('shutdown(SHUT_WR)');
            try:
                self.oSocket.setblocking(0);    # just in case it's not set.
                sData = "1";
                while sData:
                    sData = self.oSocket.recv(16384);
            except:
                pass;

            # Close it.
            self.oCv.acquire();
            try:    self.oSocket.setblocking(1);
            except: pass;
            self.oSocket.close();
            self.oSocket = None;
        else:
            self.oCv.acquire();
        self._closeWakeupSockets();
        self.oCv.release();

    def sendBytes(self, abBuf, cMsTimeout):
        if self.oSocket is None:
            reporter.error('TransportTcp.sendBytes: No connection.');
            return False;

        # Try send it all.
        try:
            cbSent = self.oSocket.send(abBuf);
            if cbSent == len(abBuf):
                return True;
        except Exception as oXcpt:
            if not self.__isWouldBlockXcpt(oXcpt):
                reporter.errorXcpt('TranportTcp.sendBytes: %s bytes' % (len(abBuf)));
                return False;
            cbSent = 0;

        # Do a timed send.
        msStart = base.timestampMilli();
        while True:
            cMsElapsed = base.timestampMilli() - msStart;
            if cMsElapsed > cMsTimeout:
                reporter.error('TranportTcp.sendBytes: %s bytes timed out (1)' % (len(abBuf)));
                break;

            # wait.
            try:
                ttRc = select.select([], [self.oSocket], [self.oSocket], (cMsTimeout - cMsElapsed) / 1000.0);
                if ttRc[2] and not ttRc[1]:
                    reporter.error('TranportTcp.sendBytes: select returned with exception');
                    break;
                if not ttRc[1]:
                    reporter.error('TranportTcp.sendBytes: %s bytes timed out (2)' % (len(abBuf)));
                    break;
            except:
                reporter.errorXcpt('TranportTcp.sendBytes: select failed');
                break;

            # Try send more.
            try:
                cbSent += self.oSocket.send(abBuf[cbSent:]);
                if cbSent == len(abBuf):
                    return True;
            except Exception as oXcpt:
                if not self.__isWouldBlockXcpt(oXcpt):
                    reporter.errorXcpt('TranportTcp.sendBytes: %s bytes' % (len(abBuf)));
                    break;

        return False;

    def __returnReadAheadBytes(self, cb):
        """ Internal worker for recvBytes. """
        assert(len(self.abReadAhead) >= cb);
        abRet = self.abReadAhead[:cb];
        self.abReadAhead = self.abReadAhead[cb:];
        return abRet;

    def recvBytes(self, cb, cMsTimeout, fNoDataOk):
        if self.oSocket is None:
            reporter.error('TransportTcp.recvBytes(%s,%s): No connection.' % (cb, cMsTimeout));
            return None;

        # Try read in some more data without bothering with timeout handling first.
        if len(self.abReadAhead) < cb:
            try:
                abBuf = self.oSocket.recv(cb - len(self.abReadAhead));
                if abBuf:
                    self.abReadAhead.extend(array.array('B', abBuf));
            except Exception as oXcpt:
                if not self.__isWouldBlockXcpt(oXcpt):
                    reporter.errorXcpt('TranportTcp.recvBytes: 0/%s bytes' % (cb,));
                    return None;

        if len(self.abReadAhead) >= cb:
            return self.__returnReadAheadBytes(cb);

        # Timeout loop.
        msStart = base.timestampMilli();
        while True:
            cMsElapsed = base.timestampMilli() - msStart;
            if cMsElapsed > cMsTimeout:
                if not fNoDataOk or self.abReadAhead:
                    reporter.error('TranportTcp.recvBytes: %s/%s bytes timed out (1)' % (len(self.abReadAhead), cb));
                break;

            # Wait.
            try:
                ttRc = select.select([self.oSocket], [], [self.oSocket], (cMsTimeout - cMsElapsed) / 1000.0);
                if ttRc[2] and not ttRc[0]:
                    reporter.error('TranportTcp.recvBytes: select returned with exception');
                    break;
                if not ttRc[0]:
                    if not fNoDataOk or self.abReadAhead:
                        reporter.error('TranportTcp.recvBytes: %s/%s bytes timed out (2) fNoDataOk=%s'
                                       % (len(self.abReadAhead), cb, fNoDataOk));
                    break;
            except:
                reporter.errorXcpt('TranportTcp.recvBytes: select failed');
                break;

            # Try read more.
            try:
                abBuf = self.oSocket.recv(cb - len(self.abReadAhead));
                if not abBuf:
                    reporter.error('TranportTcp.recvBytes: %s/%s bytes (%s) - connection has been shut down'
                                   % (len(self.abReadAhead), cb, fNoDataOk));
                    self.disconnect();
                    return None;

                self.abReadAhead.extend(array.array('B', abBuf));

            except Exception as oXcpt:
                reporter.log('recv => exception %s' % (oXcpt,));
                if not self.__isWouldBlockXcpt(oXcpt):
                    if not fNoDataOk  or  not self.__isConnectionReset(oXcpt)  or  self.abReadAhead:
                        reporter.errorXcpt('TranportTcp.recvBytes: %s/%s bytes (%s)' % (len(self.abReadAhead), cb, fNoDataOk));
                    break;

            # Done?
            if len(self.abReadAhead) >= cb:
                return self.__returnReadAheadBytes(cb);

        #reporter.log('recv => None len(self.abReadAhead) -> %d' % (len(self.abReadAhead), ));
        return None;

    def isConnectionOk(self):
        if self.oSocket is None:
            return False;
        try:
            ttRc = select.select([], [], [self.oSocket], 0.0);
            if ttRc[2]:
                return False;

            self.oSocket.send(array.array('B')); # send zero bytes.
        except:
            return False;
        return True;

    def isRecvPending(self, cMsTimeout = 0):
        try:
            ttRc = select.select([self.oSocket], [], [], cMsTimeout / 1000.0);
            if not ttRc[0]:
                return False;
        except:
            pass;
        return True;


class UsbGadget(object):
    """
    USB Gadget control class using the USBT Test Service to talk to the external
    board behaving like a USB device.
    """

    def __init__(self):
        self.oUtsSession    = None;
        self.sImpersonation = g_ksGadgetImpersonationInvalid;
        self.idGadget       = None;
        self.iBusId         = None;
        self.iDevId         = None;
        self.iUsbIpPort     = None;

    def clearImpersonation(self):
        """
        Removes the current impersonation of the gadget.
        """
        fRc = True;

        if self.idGadget is not None:
            fRc = self.oUtsSession.syncGadgetDestroy(self.idGadget);
            self.idGadget = None;
            self.iBusId   = None;
            self.iDevId   = None;

        return fRc;

    def disconnectUsb(self):
        """
        Disconnects the USB gadget from the host. (USB connection not network
        connection used for control)
        """
        return self.oUtsSession.syncGadgetDisconnect(self.idGadget);

    def connectUsb(self):
        """
        Connect the USB gadget to the host.
        """
        return self.oUtsSession.syncGadgetConnect(self.idGadget);

    def impersonate(self, sImpersonation, fSuperSpeed = False):
        """
        Impersonate a given device.
        """

        # Clear any previous impersonation
        self.clearImpersonation();
        self.sImpersonation = sImpersonation;

        fRc = False;
        if sImpersonation == g_ksGadgetImpersonationTest:
            lstCfg = [];
            if fSuperSpeed is True:
                lstCfg.append( ('Gadget/SuperSpeed', g_kiGadgetCfgTypeBool, 'true') );
            fDone = self.oUtsSession.syncGadgetCreate(g_kiGadgetTypeTest, g_kiGadgetAccessUsbIp, lstCfg);
            if fDone is True and self.oUtsSession.isSuccess():
                # Get the gadget ID.
                _, _, abPayload = self.oUtsSession.getLastReply();

                fRc = True;
                self.idGadget = getU32(abPayload, 16);
                self.iBusId   = getU32(abPayload, 20);
                self.iDevId   = getU32(abPayload, 24);
        else:
            reporter.log('Invalid or unsupported impersonation');

        return fRc;

    def getUsbIpPort(self):
        """
        Returns the port the USB/IP server is listening on if requested,
        None if USB/IP is not supported.
        """
        return self.iUsbIpPort;

    def getGadgetBusAndDevId(self):
        """
        Returns the bus ad device ID of the gadget as a tuple.
        """
        return (self.iBusId, self.iDevId);

    def connectTo(self, cMsTimeout, sHostname, uPort = None, fUsbIpSupport = True, cMsIdleFudge = 0, fTryConnect = False):
        """
        Connects to the specified target device.
        Returns True on Success.
        Returns False otherwise.
        """
        fRc = True;

        # @todo
        if fUsbIpSupport is False:
            return False;

        reporter.log2('openTcpSession(%s, %s, %s, %s)' % \
                      (cMsTimeout, sHostname, uPort, cMsIdleFudge));
        try:
            oTransport = TransportTcp(sHostname, uPort);
            self.oUtsSession = Session(oTransport, cMsTimeout, cMsIdleFudge, fTryConnect);

            if self.oUtsSession is not None:
                fDone = self.oUtsSession.waitForTask(30*1000);
                reporter.log('connect: waitForTask -> %s, result %s' % (fDone, self.oUtsSession.getResult()));
                if fDone is True and self.oUtsSession.isSuccess():
                    # Parse the reply.
                    _, _, abPayload = self.oUtsSession.getLastReply();

                    if getU32(abPayload, 20) is g_kiGadgetAccessUsbIp:
                        fRc = True;
                        self.iUsbIpPort = getU32(abPayload, 24);
                    else:
                        reporter.log('Gadget doesn\'t support access over USB/IP despite being requested');
                        fRc = False;
                else:
                    fRc = False;
            else:
                fRc = False;
        except:
            reporter.errorXcpt(None, 15);
            return False;

        return fRc;

    def disconnectFrom(self):
        """
        Disconnects from the target device.
        """
        fRc = True;

        self.clearImpersonation();
        if self.oUtsSession is not None:
            fRc = self.oUtsSession.syncDisconnect();

        return fRc;
