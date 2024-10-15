# -*- coding: utf-8 -*-
# $Id: txsclient.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=too-many-lines

"""
Test eXecution Service Client.
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
import array;
import errno;
import os;
import select;
import socket;
import sys;
import threading;
import time;
import zlib;
import uuid;

# Validation Kit imports.
from common             import utils;
from testdriver         import base;
from testdriver         import reporter;
from testdriver.base    import TdTaskBase;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int;     # pylint: disable=redefined-builtin,invalid-name

#
# Helpers for decoding data received from the TXS.
# These are used both the Session and Transport classes.
#

def getU32(abData, off):
    """Get a U32 field."""
    return abData[off] \
         + abData[off + 1] * 256 \
         + abData[off + 2] * 65536 \
         + abData[off + 3] * 16777216;

def getSZ(abData, off, sDefault = None):
    """
    Get a zero-terminated string field.
    Returns sDefault if the string is invalid.
    """
    cchStr = getSZLen(abData, off);
    if cchStr >= 0:
        abStr = abData[off:(off + cchStr)];
        try:
            if sys.version_info < (3, 9, 0):
                # Removed since Python 3.9.
                sStr = abStr.tostring(); # pylint: disable=no-member
            else:
                sStr = abStr.tobytes();
            return sStr.decode('utf_8');
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
# Helper for encoding data sent to the TXS.
#

def u32ToByteArray(u32):
    """Encodes the u32 value as a little endian byte (B) array."""
    return array.array('B',
                       (  u32              % 256,
                         (u32 // 256)      % 256,
                         (u32 // 65536)    % 256,
                         (u32 // 16777216) % 256) );

def escapeString(sString):
    """
    Does $ escaping of the string so TXS doesn't try do variable expansion.
    """
    return sString.replace('$', '$$');



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
        Quietly attempts to connect to the TXS.

        Returns True on success.
        Returns False on retryable errors (no logging).
        Returns None on fatal errors with details in the log.

        Override this method, don't call super.
        """
        _ = cMsTimeout;
        return False;

    def disconnect(self, fQuiet = False):
        """
        Disconnect from the TXS.

        Returns True.

        Override this method, don't call super.
        """
        _ = fQuiet;
        return True;

    def sendBytes(self, abBuf, cMsTimeout):
        """
        Sends the bytes in the buffer abBuf to the TXS.

        Returns True on success.
        Returns False on failure and error details in the log.

        Override this method, don't call super.

        Remarks: len(abBuf) is always a multiple of 16.
        """
        _ = abBuf; _ = cMsTimeout;
        return False;

    def recvBytes(self, cb, cMsTimeout, fNoDataOk):
        """
        Receive cb number of bytes from the TXS.

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

        reporter.log2('sendMsgInt: op=%s len=%d timeout=%d' % (sOpcode, len(abMsg), cMsTimeout));
        return self.sendBytes(abMsg, cMsTimeout);

    def recvMsg(self, cMsTimeout, fNoDataOk = False):
        """
        Receives a message from the TXS.

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

        if sys.version_info < (3, 9, 0):
            # Removed since Python 3.9.
            sOpcode = abHdr[8:16].tostring(); # pylint: disable=no-member
        else:
            sOpcode = abHdr[8:16].tobytes();
        sOpcode = sOpcode.decode('ascii');

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
                    if sys.version_info[0] >= 3:
                        abPayload.extend(o.encode('utf_8'));
                    else:
                        # the primitive approach...
                        sUtf8 = o.encode('utf_8');
                        for ch in sUtf8:
                            abPayload.append(ord(ch))
                    abPayload.append(0);
                elif isinstance(o, (long, int)):
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
    A Test eXecution Service (TXS) client session.
    """

    def __init__(self, oTransport, cMsTimeout, cMsIdleFudge, fTryConnect = False, fnProcessEvents = None):
        """
        Construct a TXS session.

        This starts by connecting to the TXS and will enter the signalled state
        when connected or the timeout has been reached.
        """
        TdTaskBase.__init__(self, utils.getCallerName(), fnProcessEvents);
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

        if not self.startTask(cMsTimeout, False, "connecting", self.taskConnect, (cMsIdleFudge,)):
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
            reporter.maybeErr(not fIgnoreErrors, 'txsclient.Session.startTask: failed to cancel previous task.');
            return False;

        # Change status and make sure we're the
        self.lockTask();
        if self.sStatus != "":
            self.unlockTask();
            reporter.maybeErr(not fIgnoreErrors, 'txsclient.Session.startTask: race.');
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
        self.oThread        = threading.Thread(target=self.taskThread, args=(), name='TXS-%s' % (sStatus));
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

        reporter.log('txsclient: cancelling "%s"...' % (self.sStatus));
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

    def taskThread(self):
        """
        The task thread function.
        This does some housekeeping activities around the real task method call.
        """
        if not self.isCancelled():
            try:
                fnTask = self.fnTask;
                oTaskRc = fnTask(*self.aTaskArgs);
            except:
                reporter.fatalXcpt('taskThread', 15);
                oTaskRc = None;
        else:
            reporter.log('taskThread: cancelled already');

        self.lockTask();

        reporter.log('taskThread: signalling task with status "%s", oTaskRc=%s' % (self.sStatus, oTaskRc));
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
        Receives an ACK or error response from the TXS.

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
        return (sOpcode, getSZ(abPayload, 0, sOpcode));

    def recvAckLogged(self, sCommand, fNoDataOk = False):
        """
        Wrapper for recvAck and logging.
        Returns True on success (ACK).
        Returns False on time, transport error and errors signalled by TXS.
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
        Receives a TRUE/FALSE response from the TXS.
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
        reporter.maybeErr(self.fErr, 'recvAckLogged: %s response was %s: %s' % (sCommand, sOpcode, getSZ(abPayload, 0, sOpcode)));
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
            reporter.maybeErr(self.fErr, 'asyncToSync: waitForTask (timeout %d) failed...' % (self.cMsTimeout,));
            self.cancelTask();
            #reporter.log2('asyncToSync(%s): returns False (#2)' % (fnAsync, rc));
            return False;

        rc = self.getResult();
        #reporter.log2('asyncToSync(%s): returns %s' % (fnAsync, rc));
        return rc;

    #
    # Connection tasks.
    #

    def taskConnect(self, cMsIdleFudge):
        """Tries to connect to the TXS"""
        while not self.isCancelled():
            reporter.log2('taskConnect: connecting ...');
            rc = self.oTransport.connect(self.getMsLeft(500));
            if rc is True:
                reporter.log('taskConnect: succeeded');
                return self.taskGreet(cMsIdleFudge);
            if rc is None:
                reporter.log2('taskConnect: unable to connect');
                return None;
            if self.hasTimedOut():
                reporter.log2('taskConnect: timed out');
                if not self.fTryConnect:
                    reporter.maybeErr(self.fErr, 'taskConnect: timed out');
                return False;
            time.sleep(self.getMsLeft(1, 1000) / 1000.0);
        if not self.fTryConnect:
            reporter.maybeErr(self.fErr, 'taskConnect: cancelled');
        return False;

    def taskGreet(self, cMsIdleFudge):
        """Greets the TXS"""
        rc = self.sendMsg("HOWDY", ());
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
        """Says goodbye to the TXS"""
        rc = self.sendMsg("BYE");
        if rc is True:
            rc = self.recvAckLogged("BYE");
        self.oTransport.disconnect();
        return rc;

    def taskVer(self):
        """Requests version information from TXS"""
        rc = self.sendMsg("VER");
        if rc is True:
            rc = False;
            cbMsg, sOpcode, abPayload = self.recvReply();
            if cbMsg is not None:
                sOpcode = sOpcode.strip();
                if sOpcode == "ACK VER":
                    sVer = getSZ(abPayload, 0);
                    if sVer is not None:
                        rc = sVer;
                elif sOpcode == "UNKNOWN":
                    reporter.log(self.fErr, 'taskVer got a UNKNOWN, txs server is probably too old, just ignore')
                else:
                    reporter.maybeErr(self.fErr, 'taskVer got a bad reply: %s' % (sOpcode,));
            else:
                reporter.maybeErr(self.fErr, 'taskVer got 3xNone from recvReply.');
        return rc;

    def taskUuid(self):
        """Gets the TXS UUID"""
        rc = self.sendMsg("UUID");
        if rc is True:
            rc = False;
            cbMsg, sOpcode, abPayload = self.recvReply();
            if cbMsg is not None:
                sOpcode = sOpcode.strip()
                if sOpcode == "ACK UUID":
                    sUuid = getSZ(abPayload, 0);
                    if sUuid is not None:
                        sUuid = '{%s}' % (sUuid,)
                        try:
                            _ = uuid.UUID(sUuid);
                            rc = sUuid;
                        except:
                            reporter.errorXcpt('taskUuid got an invalid UUID string %s' % (sUuid,));
                    else:
                        reporter.maybeErr(self.fErr, 'taskUuid did not get a UUID string.');
                else:
                    reporter.maybeErr(self.fErr, 'taskUuid got a bad reply: %s' % (sOpcode,));
            else:
                reporter.maybeErr(self.fErr, 'taskUuid got 3xNone from recvReply.');
        return rc;

    #
    # Process task
    # pylint: disable=missing-docstring
    #

    def taskExecEx(self, sExecName, fFlags, asArgs, asAddEnv, oStdIn, oStdOut, oStdErr, oTestPipe, sAsUser): # pylint: disable=too-many-arguments,too-many-locals,too-many-statements,line-too-long
        # Construct the payload.
        aoPayload = [long(fFlags), '%s' % (sExecName), long(len(asArgs))];
        for sArg in asArgs:
            aoPayload.append('%s' % (sArg));
        aoPayload.append(long(len(asAddEnv)));
        for sPutEnv in asAddEnv:
            aoPayload.append('%s' % (sPutEnv));
        for o in (oStdIn, oStdOut, oStdErr, oTestPipe):
            if utils.isString(o):
                aoPayload.append(o);
            elif o is not None:
                aoPayload.append('|');
                o.uTxsClientCrc32 = zlib.crc32(b'');
            else:
                aoPayload.append('');
        aoPayload.append('%s' % (sAsUser));
        aoPayload.append(long(self.cMsTimeout));

        # Kick of the EXEC command.
        rc = self.sendMsg('EXEC', aoPayload)
        if rc is True:
            rc = self.recvAckLogged('EXEC');
        if rc is True:
            # Loop till the process completes, feed input to the TXS and
            # receive output from it.
            sFailure                  = "";
            msPendingInputReply       = None;
            cbMsg, sOpcode, abPayload = (None, None, None);
            while True:
                # Pending input?
                if     msPendingInputReply is None \
                   and oStdIn is not None \
                   and not utils.isString(oStdIn):
                    try:
                        sInput = oStdIn.read(65536);
                    except:
                        reporter.errorXcpt('read standard in');
                        sFailure = 'exception reading stdin';
                        rc = None;
                        break;
                    if sInput:
                        # Convert to a byte array before handing it of to sendMsg or the string
                        # will get some zero termination added breaking the CRC (and injecting
                        # unwanted bytes).
                        abInput = array.array('B', sInput.encode('utf-8'));
                        oStdIn.uTxsClientCrc32 = zlib.crc32(abInput, oStdIn.uTxsClientCrc32);
                        rc = self.sendMsg('STDIN', (long(oStdIn.uTxsClientCrc32 & 0xffffffff), abInput));
                        if rc is not True:
                            sFailure = 'sendMsg failure';
                            break;
                        msPendingInputReply = base.timestampMilli();
                        continue;

                    rc = self.sendMsg('STDINEOS');
                    oStdIn = None;
                    if rc is not True:
                        sFailure = 'sendMsg failure';
                        break;
                    msPendingInputReply = base.timestampMilli();

                # Wait for input (500 ms timeout).
                if cbMsg is None:
                    cbMsg, sOpcode, abPayload = self.recvReply(cMsTimeout=500, fNoDataOk=True);
                    if cbMsg is None:
                        # Check for time out before restarting the loop.
                        # Note! Only doing timeout checking here does mean that
                        #       the TXS may prevent us from timing out by
                        #       flooding us with data.  This is unlikely though.
                        if    self.hasTimedOut() \
                         and (   msPendingInputReply is None \
                              or base.timestampMilli() - msPendingInputReply > 30000):
                            reporter.maybeErr(self.fErr, 'taskExecEx: timed out');
                            sFailure = 'timeout';
                            rc = None;
                            break;
                        # Check that the connection is OK.
                        if not self.oTransport.isConnectionOk():
                            self.oTransport.disconnect();
                            sFailure = 'disconnected';
                            rc = False;
                            break;
                        continue;

                # Handle the response.
                sOpcode = sOpcode.rstrip();
                if sOpcode == 'STDOUT':
                    oOut = oStdOut;
                elif sOpcode == 'STDERR':
                    oOut = oStdErr;
                elif sOpcode == 'TESTPIPE':
                    oOut = oTestPipe;
                else:
                    oOut = None;
                if oOut is not None:
                    # Output from the process.
                    if len(abPayload) < 4:
                        sFailure = 'malformed output packet (%s, %u bytes)' % (sOpcode, cbMsg);
                        reporter.maybeErr(self.fErr, 'taskExecEx: %s' % (sFailure));
                        rc = None;
                        break;
                    uStreamCrc32 = getU32(abPayload, 0);
                    oOut.uTxsClientCrc32 = zlib.crc32(abPayload[4:], oOut.uTxsClientCrc32);
                    if uStreamCrc32 != (oOut.uTxsClientCrc32 & 0xffffffff):
                        sFailure = 'crc error - mine=%#x their=%#x (%s, %u bytes: %s)' \
                            % (oOut.uTxsClientCrc32 & 0xffffffff, uStreamCrc32, sOpcode, cbMsg,
                               ' '.join(['%02x' % (b,) for b in abPayload]),);
                        reporter.maybeErr(self.fErr, 'taskExecEx: %s' % (sFailure));
                        rc = None;
                        break;
                    try:
                        oOut.write(abPayload[4:]);
                    except:
                        sFailure = 'exception writing %s' % (sOpcode);
                        reporter.errorXcpt('taskExecEx: %s' % (sFailure));
                        rc = None;
                        break;
                elif sOpcode == 'STDINIGN' and msPendingInputReply is not None:
                    # Standard input is ignored.  Ignore this condition for now.
                    msPendingInputReply = None;
                    reporter.log('taskExecEx: Standard input is ignored... why?');
                    del oStdIn.uTxsClientCrc32;
                    oStdIn = '/dev/null';
                elif sOpcode in ('STDINMEM', 'STDINBAD', 'STDINCRC',) and msPendingInputReply is not None:
                    # TXS STDIN error, abort.
                    # TODO: STDINMEM - consider undoing the previous stdin read and try resubmitt it.
                    msPendingInputReply = None;
                    sFailure = 'TXS is out of memory for std input buffering';
                    reporter.maybeErr(self.fErr, 'taskExecEx: %s' % (sFailure));
                    rc = None;
                    break;
                elif sOpcode == 'ACK' and msPendingInputReply is not None:
                    msPendingInputReply = None;
                elif sOpcode.startswith('PROC '):
                    # Process status message, handle it outside the loop.
                    rc = True;
                    break;
                else:
                    sFailure = 'Unexpected opcode %s' % (sOpcode);
                    reporter.maybeErr(self.fErr, 'taskExecEx: %s' % (sFailure));
                    rc = None;
                    break;
                # Clear the message.
                cbMsg, sOpcode, abPayload = (None, None, None);

            # If we sent an STDIN packet and didn't get a reply yet, we'll give
            # TXS some 5 seconds to reply to this.  If we don't wait here we'll
            # get screwed later on if we mix it up with the reply to some other
            # command.  Hackish.
            if msPendingInputReply is not None:
                cbMsg2, sOpcode2, abPayload2 = self.oTransport.recvMsg(5000);
                if cbMsg2 is not None:
                    reporter.log('taskExecEx: Out of order STDIN, got reply: %s, %s, %s [ignored]'
                                 % (cbMsg2, sOpcode2, abPayload2));
                    msPendingInputReply = None;
                else:
                    reporter.maybeErr(self.fErr, 'taskExecEx: Pending STDIN, no reply after 5 secs!');
                    self.fScrewedUpMsgState = True;

            # Parse the exit status (True), abort (None) or do nothing (False).
            if rc is True:
                if sOpcode == 'PROC OK':
                    pass;
                else:
                    rc = False;
                    # Do proper parsing some other day if needed:
                    #   PROC TOK, PROC TOA, PROC DWN, PROC DOO,
                    #   PROC NOK + rc, PROC SIG + sig, PROC ABD, FAILED.
                    if sOpcode == 'PROC DOO':
                        reporter.log('taskExecEx: PROC DOO[FUS]: %s' % (abPayload,));
                    elif sOpcode.startswith('PROC NOK'):
                        reporter.log('taskExecEx: PROC NOK: rcExit=%s' % (abPayload,));
                    elif abPayload and sOpcode.startswith('PROC '):
                        reporter.log('taskExecEx: %s payload=%s' % (sOpcode, abPayload,));

            else:
                if rc is None:
                    # Abort it.
                    reporter.log('taskExecEx: sending ABORT...');
                    rc = self.sendMsg('ABORT');
                    while rc is True:
                        cbMsg, sOpcode, abPayload = self.oTransport.recvMsg(30000);
                        if cbMsg is None:
                            reporter.maybeErr(self.fErr, 'taskExecEx: Pending ABORT, no reply after 30 secs!')
                            self.fScrewedUpMsgState = True;
                            break;
                        if sOpcode.startswith('PROC '):
                            reporter.log('taskExecEx: ABORT reply: %s, %s, %s [ignored]' % (cbMsg, sOpcode, abPayload));
                            break;
                        reporter.log('taskExecEx: ABORT in process, ignoring reply: %s, %s, %s' % (cbMsg, sOpcode, abPayload));
                        # Check that the connection is OK before looping.
                        if not self.oTransport.isConnectionOk():
                            self.oTransport.disconnect();
                            break;

                # Fake response with the reason why we quit.
                if sFailure is not None:
                    self.t3oReply = (0, 'EXECFAIL', sFailure);
                rc = None;
        else:
            rc = None;

        # Cleanup.
        for o in (oStdIn, oStdOut, oStdErr, oTestPipe):
            if o is not None and not utils.isString(o):
                del o.uTxsClientCrc32;      # pylint: disable=maybe-no-member
                # Make sure all files are closed
                o.close();                  # pylint: disable=maybe-no-member
        reporter.log('taskExecEx: returns %s' % (rc));
        return rc;

    #
    # Admin tasks
    #

    def hlpRebootShutdownWaitForAck(self, sCmd):
        """Wait for reboot/shutodwn ACK."""
        rc = self.recvAckLogged(sCmd);
        if rc is True:
            # poll a little while for server to disconnect.
            uMsStart = base.timestampMilli();
            while self.oTransport.isConnectionOk() \
              and base.timestampMilli() - uMsStart >= 5000:
                if self.oTransport.isRecvPending(min(500, self.getMsLeft())):
                    break;
            self.oTransport.disconnect();
        return rc;

    def taskReboot(self):
        rc = self.sendMsg('REBOOT');
        if rc is True:
            rc = self.hlpRebootShutdownWaitForAck('REBOOT');
        return rc;

    def taskShutdown(self):
        rc = self.sendMsg('SHUTDOWN');
        if rc is True:
            rc = self.hlpRebootShutdownWaitForAck('SHUTDOWN');
        return rc;

    #
    # CD/DVD control tasks.
    #

    ## TODO

    #
    # File system tasks
    #

    def taskMkDir(self, sRemoteDir, fMode):
        rc = self.sendMsg('MKDIR', (fMode, sRemoteDir));
        if rc is True:
            rc = self.recvAckLogged('MKDIR');
        return rc;

    def taskMkDirPath(self, sRemoteDir, fMode):
        rc = self.sendMsg('MKDRPATH', (fMode, sRemoteDir));
        if rc is True:
            rc = self.recvAckLogged('MKDRPATH');
        return rc;

    def taskMkSymlink(self, sLinkTarget, sLink):
        rc = self.sendMsg('MKSYMLNK', (sLinkTarget, sLink));
        if rc is True:
            rc = self.recvAckLogged('MKSYMLNK');
        return rc;

    def taskRmDir(self, sRemoteDir):
        rc = self.sendMsg('RMDIR', (sRemoteDir,));
        if rc is True:
            rc = self.recvAckLogged('RMDIR');
        return rc;

    def taskRmFile(self, sRemoteFile):
        rc = self.sendMsg('RMFILE', (sRemoteFile,));
        if rc is True:
            rc = self.recvAckLogged('RMFILE');
        return rc;

    def taskRmSymlink(self, sRemoteSymlink):
        rc = self.sendMsg('RMSYMLNK', (sRemoteSymlink,));
        if rc is True:
            rc = self.recvAckLogged('RMSYMLNK');
        return rc;

    def taskRmTree(self, sRemoteTree):
        rc = self.sendMsg('RMTREE', (sRemoteTree,));
        if rc is True:
            rc = self.recvAckLogged('RMTREE');
        return rc;

    def taskChMod(self, sRemotePath, fMode):
        rc = self.sendMsg('CHMOD', (int(fMode), sRemotePath,));
        if rc is True:
            rc = self.recvAckLogged('CHMOD');
        return rc;

    def taskChOwn(self, sRemotePath, idUser, idGroup):
        rc = self.sendMsg('CHOWN', (int(idUser), int(idGroup), sRemotePath,));
        if rc is True:
            rc = self.recvAckLogged('CHOWN');
        return rc;

    def taskIsDir(self, sRemoteDir):
        rc = self.sendMsg('ISDIR', (sRemoteDir,));
        if rc is True:
            rc = self.recvTrueFalse('ISDIR');
        return rc;

    def taskIsFile(self, sRemoteFile):
        rc = self.sendMsg('ISFILE', (sRemoteFile,));
        if rc is True:
            rc = self.recvTrueFalse('ISFILE');
        return rc;

    def taskIsSymlink(self, sRemoteSymlink):
        rc = self.sendMsg('ISSYMLNK', (sRemoteSymlink,));
        if rc is True:
            rc = self.recvTrueFalse('ISSYMLNK');
        return rc;

    #def "STAT    "
    #def "LSTAT   "
    #def "LIST    "

    def taskCopyFile(self, sSrcFile, sDstFile, fMode, fFallbackOkay):
        """ Copies a file within the remote from source to destination. """
        _ = fFallbackOkay; # Not used yet.
        # Note: If fMode is set to 0, it's up to the target OS' implementation with
        #       what a file mode the destination file gets created (i.e. via umask).
        rc = self.sendMsg('CPFILE', (int(fMode), sSrcFile, sDstFile,));
        if rc is True:
            rc = self.recvAckLogged('CPFILE');
        return rc;

    def taskUploadFile(self, sLocalFile, sRemoteFile, fMode, fFallbackOkay):
        #
        # Open the local file (make sure it exist before bothering TXS) and
        # tell TXS that we want to upload a file.
        #
        try:
            oLocalFile = utils.openNoInherit(sLocalFile, 'rb');
        except:
            reporter.errorXcpt('taskUpload: failed to open "%s"' % (sLocalFile));
            return False;

        # Common cause with taskUploadStr
        rc = self.taskUploadCommon(oLocalFile, sRemoteFile, fMode, fFallbackOkay);

        # Cleanup.
        oLocalFile.close();
        return rc;

    def taskUploadString(self, sContent, sRemoteFile, fMode, fFallbackOkay):
        # Wrap sContent in a file like class.
        class InStringFile(object): # pylint: disable=too-few-public-methods
            def __init__(self, sContent):
                self.sContent = sContent;
                self.off      = 0;

            def read(self, cbMax):
                cbLeft = len(self.sContent) - self.off;
                if cbLeft == 0:
                    return "";
                if cbLeft <= cbMax:
                    sRet = self.sContent[self.off:(self.off + cbLeft)];
                else:
                    sRet = self.sContent[self.off:(self.off + cbMax)];
                self.off = self.off + len(sRet);
                return sRet;

        oLocalString = InStringFile(sContent);
        return self.taskUploadCommon(oLocalString, sRemoteFile, fMode, fFallbackOkay);

    def taskUploadCommon(self, oLocalFile, sRemoteFile, fMode, fFallbackOkay):
        """Common worker used by taskUploadFile and taskUploadString."""
        #
        # Command + ACK.
        #
        # Only used the new PUT2FILE command if we've got a non-zero mode mask.
        # Fall back on the old command if the new one is not known by the TXS.
        #
        if fMode == 0:
            rc = self.sendMsg('PUT FILE', (sRemoteFile,));
            if rc is True:
                rc = self.recvAckLogged('PUT FILE');
        else:
            rc = self.sendMsg('PUT2FILE', (fMode, sRemoteFile));
            if rc is True:
                rc = self.recvAck();
                if rc is False:
                    reporter.maybeErr(self.fErr, 'recvAckLogged: PUT2FILE transport error');
                elif rc is not True:
                    if rc[0] == 'UNKNOWN' and fFallbackOkay:
                        # Fallback:
                        rc = self.sendMsg('PUT FILE', (sRemoteFile,));
                        if rc is True:
                            rc = self.recvAckLogged('PUT FILE');
                    else:
                        reporter.maybeErr(self.fErr, 'recvAckLogged: PUT2FILE response was %s: %s' % (rc[0], rc[1],));
                        rc = False;
        if rc is True:
            #
            # Push data packets until eof.
            #
            uMyCrc32 = zlib.crc32(b'');
            while True:
                # Read up to 64 KB of data.
                try:
                    sRaw = oLocalFile.read(65536);
                except:
                    rc = None;
                    break;

                # Convert to array - this is silly!
                abBuf = array.array('B');
                if utils.isString(sRaw):
                    for ch in sRaw:
                        abBuf.append(ord(ch));
                else:
                    abBuf.extend(sRaw);
                sRaw = None;

                # Update the file stream CRC and send it off.
                uMyCrc32 = zlib.crc32(abBuf, uMyCrc32);
                if not abBuf:
                    rc = self.sendMsg('DATA EOF', (long(uMyCrc32 & 0xffffffff), ));
                else:
                    rc = self.sendMsg('DATA    ', (long(uMyCrc32 & 0xffffffff), abBuf));
                if rc is False:
                    break;

                # Wait for the reply.
                rc = self.recvAck();
                if rc is not True:
                    if rc is False:
                        reporter.maybeErr(self.fErr, 'taskUpload: transport error waiting for ACK');
                    else:
                        reporter.maybeErr(self.fErr, 'taskUpload: DATA response was %s: %s' % (rc[0], rc[1]));
                        rc = False;
                    break;

                # EOF?
                if not abBuf:
                    break;

            # Send ABORT on ACK and I/O errors.
            if rc is None:
                rc = self.sendMsg('ABORT');
                if rc is True:
                    self.recvAckLogged('ABORT');
                rc = False;
        return rc;

    def taskDownloadFile(self, sRemoteFile, sLocalFile):
        try:
            oLocalFile = utils.openNoInherit(sLocalFile, 'wb');
        except:
            reporter.errorXcpt('taskDownload: failed to open "%s"' % (sLocalFile));
            return False;

        rc = self.taskDownloadCommon(sRemoteFile, oLocalFile);

        oLocalFile.close();
        if rc is False:
            try:
                os.remove(sLocalFile);
            except:
                reporter.errorXcpt();
        return rc;

    def taskDownloadString(self, sRemoteFile, sEncoding = 'utf-8', fIgnoreEncodingErrors = True):
        # Wrap sContent in a file like class.
        class OutStringFile(object): # pylint: disable=too-few-public-methods
            def __init__(self):
                self.asContent = [];

            def write(self, sBuf):
                self.asContent.append(sBuf);
                return None;

        oLocalString = OutStringFile();
        rc = self.taskDownloadCommon(sRemoteFile, oLocalString);
        if rc is True:
            rc = '';
            for sBuf in oLocalString.asContent:
                if hasattr(sBuf, 'decode'):
                    rc += sBuf.decode(sEncoding, 'ignore' if fIgnoreEncodingErrors else 'strict');
                else:
                    rc += sBuf;
        return rc;

    def taskDownloadCommon(self, sRemoteFile, oLocalFile):
        """Common worker for taskDownloadFile and taskDownloadString."""
        rc = self.sendMsg('GET FILE', (sRemoteFile,))
        if rc is True:
            #
            # Process data packets until eof.
            #
            uMyCrc32 = zlib.crc32(b'');
            while rc is True:
                cbMsg, sOpcode, abPayload = self.recvReply();
                if cbMsg is None:
                    reporter.maybeErr(self.fErr, 'taskDownload got 3xNone from recvReply.');
                    rc = None;
                    break;

                # Validate.
                sOpcode = sOpcode.rstrip();
                if sOpcode not in ('DATA', 'DATA EOF',):
                    reporter.maybeErr(self.fErr, 'taskDownload got a error reply: opcode="%s" details="%s"'
                                      % (sOpcode, getSZ(abPayload, 0, "None")));
                    rc = False;
                    break;
                if sOpcode == 'DATA' and len(abPayload) < 4:
                    reporter.maybeErr(self.fErr, 'taskDownload got a bad DATA packet: len=%u' % (len(abPayload)));
                    rc = None;
                    break;
                if sOpcode == 'DATA EOF' and len(abPayload) != 4:
                    reporter.maybeErr(self.fErr, 'taskDownload got a bad EOF packet: len=%u' % (len(abPayload)));
                    rc = None;
                    break;

                # Check the CRC (common for both packets).
                uCrc32 = getU32(abPayload, 0);
                if sOpcode == 'DATA':
                    uMyCrc32 = zlib.crc32(abPayload[4:], uMyCrc32);
                if uCrc32 != (uMyCrc32 & 0xffffffff):
                    reporter.maybeErr(self.fErr, 'taskDownload got a bad CRC: mycrc=%s remotecrc=%s'
                                      % (hex(uMyCrc32), hex(uCrc32)));
                    rc = None;
                    break;
                if sOpcode == 'DATA EOF':
                    rc = self.sendMsg('ACK');
                    break;

                # Finally, push the data to the file.
                try:
                    if sys.version_info < (3, 9, 0):
                        # Removed since Python 3.9.
                        abData = abPayload[4:].tostring();
                    else:
                        abData = abPayload[4:].tobytes();
                    oLocalFile.write(abData);
                except:
                    reporter.errorXcpt('I/O error writing to "%s"' % (sRemoteFile));
                    rc = None;
                    break;
                rc = self.sendMsg('ACK');

            # Send NACK on validation and I/O errors.
            if rc is None:
                rc = self.sendMsg('NACK');
                rc = False;
        return rc;

    def taskPackFile(self, sRemoteFile, sRemoteSource):
        rc = self.sendMsg('PKFILE', (sRemoteFile, sRemoteSource));
        if rc is True:
            rc = self.recvAckLogged('PKFILE');
        return rc;

    def taskUnpackFile(self, sRemoteFile, sRemoteDir):
        rc = self.sendMsg('UNPKFILE', (sRemoteFile, sRemoteDir));
        if rc is True:
            rc = self.recvAckLogged('UNPKFILE');
        return rc;

    def taskExpandString(self, sString):
        rc = self.sendMsg('EXP STR ', (sString,));
        if rc is True:
            rc = False;
            cbMsg, sOpcode, abPayload = self.recvReply();
            if cbMsg is not None:
                sOpcode = sOpcode.strip();
                if sOpcode == "STRING":
                    sStringExp = getSZ(abPayload, 0);
                    if sStringExp is not None:
                        rc = sStringExp;
                else: # Also handles SHORTSTR reply (not enough space to store result).
                    reporter.maybeErr(self.fErr, 'taskExpandString got a bad reply: %s' % (sOpcode,));
            else:
                reporter.maybeErr(self.fErr, 'taskExpandString got 3xNone from recvReply.');
        return rc;

    # pylint: enable=missing-docstring


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

    def asyncVer(self, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a task for getting the TXS version information.

        Returns True on success, False on failure (logged).

        The task returns the version string on success and False on failure.
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "ver", self.taskVer);

    def syncVer(self, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncVer, cMsTimeout, fIgnoreErrors);

    def asyncUuid(self, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a task for getting the TXS UUID.

        Returns True on success, False on failure (logged).

        The task returns UUID string (in {}) on success and False on failure.
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "uuid", self.taskUuid);

    def syncUuid(self, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncUuid, cMsTimeout, fIgnoreErrors);

    #
    # Public methods - execution.
    #

    def asyncExecEx(self, sExecName, asArgs = (), asAddEnv = (), # pylint: disable=too-many-arguments
                    oStdIn = None, oStdOut = None, oStdErr = None, oTestPipe = None,
                    sAsUser = "", cMsTimeout = 3600000, fIgnoreErrors = False):
        """
        Initiates a exec process task.

        Returns True on success, False on failure (logged).

        The task returns True if the process exited normally with status code 0.
        The task returns None if on failure prior to executing the process, and
        False if the process exited with a different status or in an abnormal
        manner.  Both None and False are logged of course and further info can
        also be obtained by getLastReply().

        The oStdIn, oStdOut, oStdErr and oTestPipe specifiy how to deal with
        these streams.  If None, no special action is taken and the output goes
        to where ever the TXS sends its output, and ditto for input.
            - To send to / read from the bitbucket, pass '/dev/null'.
            - To redirect to/from a file, just specify the remote filename.
            - To append to a file use '>>' followed by the remote filename.
            - To pipe the stream to/from the TXS, specify a file like
              object.  For StdIn a non-blocking read() method is required. For
              the other a write() method is required.  Watch out for deadlock
              conditions between StdIn and StdOut/StdErr/TestPipe piping.
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "exec", self.taskExecEx,
                              (sExecName, long(0), asArgs, asAddEnv, oStdIn,
                               oStdOut, oStdErr, oTestPipe, sAsUser));

    def syncExecEx(self, sExecName, asArgs = (), asAddEnv = (), # pylint: disable=too-many-arguments
                   oStdIn = '/dev/null', oStdOut = '/dev/null',
                   oStdErr = '/dev/null', oTestPipe = '/dev/null',
                   sAsUser = '', cMsTimeout = 3600000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncExecEx, sExecName, asArgs, asAddEnv, oStdIn, oStdOut, \
                                                  oStdErr, oTestPipe, sAsUser, cMsTimeout, fIgnoreErrors);

    def asyncExec(self, sExecName, asArgs = (), asAddEnv = (), sAsUser = "", fWithTestPipe = True, sPrefix = '', \
                  cMsTimeout = 3600000, fIgnoreErrors = False):
        """
        Initiates a exec process test task.

        Returns True on success, False on failure (logged).

        The task returns True if the process exited normally with status code 0.
        The task returns None if on failure prior to executing the process, and
        False if the process exited with a different status or in an abnormal
        manner.  Both None and False are logged of course and further info can
        also be obtained by getLastReply().

        Standard in is taken from /dev/null.  While both standard output and
        standard error goes directly to reporter.log().  The testpipe is piped
        to reporter.xxxx.
        """

        sStdIn    = '/dev/null';
        oStdOut   = reporter.FileWrapper('%sstdout' % sPrefix);
        oStdErr   = reporter.FileWrapper('%sstderr' % sPrefix);
        if fWithTestPipe:   oTestPipe = reporter.FileWrapperTestPipe();
        else:               oTestPipe = '/dev/null'; # pylint: disable=redefined-variable-type

        return self.startTask(cMsTimeout, fIgnoreErrors, "exec", self.taskExecEx,
                              (sExecName, long(0), asArgs, asAddEnv, sStdIn, oStdOut, oStdErr, oTestPipe, sAsUser));

    def syncExec(self, sExecName, asArgs = (), asAddEnv = (), sAsUser = '', fWithTestPipe = True, sPrefix = '',
                 cMsTimeout = 3600000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncExec, sExecName, asArgs, asAddEnv, sAsUser, fWithTestPipe, sPrefix, \
            cMsTimeout, fIgnoreErrors);

    #
    # Public methods - system
    #

    def asyncReboot(self, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a reboot task.

        Returns True on success, False on failure (logged).

        The task returns True on success, False on failure (logged).  The
        session will be disconnected on successful task completion.
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "reboot", self.taskReboot, ());

    def syncReboot(self, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncReboot, cMsTimeout, fIgnoreErrors);

    def asyncShutdown(self, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a shutdown task.

        Returns True on success, False on failure (logged).

        The task returns True on success, False on failure (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "shutdown", self.taskShutdown, ());

    def syncShutdown(self, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncShutdown, cMsTimeout, fIgnoreErrors);


    #
    # Public methods - file system
    #

    def asyncMkDir(self, sRemoteDir, fMode = 0o700, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a mkdir task.

        Returns True on success, False on failure (logged).

        The task returns True on success, False on failure (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "mkDir", self.taskMkDir, (sRemoteDir, long(fMode)));

    def syncMkDir(self, sRemoteDir, fMode = 0o700, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncMkDir, sRemoteDir, long(fMode), cMsTimeout, fIgnoreErrors);

    def asyncMkDirPath(self, sRemoteDir, fMode = 0o700, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a mkdir -p task.

        Returns True on success, False on failure (logged).

        The task returns True on success, False on failure (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "mkDirPath", self.taskMkDirPath, (sRemoteDir, long(fMode)));

    def syncMkDirPath(self, sRemoteDir, fMode = 0o700, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncMkDirPath, sRemoteDir, long(fMode), cMsTimeout, fIgnoreErrors);

    def asyncMkSymlink(self, sLinkTarget, sLink, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a symlink task.

        Returns True on success, False on failure (logged).

        The task returns True on success, False on failure (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "mkSymlink", self.taskMkSymlink, (sLinkTarget, sLink));

    def syncMkSymlink(self, sLinkTarget, sLink, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncMkSymlink, sLinkTarget, sLink, cMsTimeout, fIgnoreErrors);

    def asyncRmDir(self, sRemoteDir, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a rmdir task.

        Returns True on success, False on failure (logged).

        The task returns True on success, False on failure (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "rmDir", self.taskRmDir, (sRemoteDir,));

    def syncRmDir(self, sRemoteDir, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncRmDir, sRemoteDir, cMsTimeout, fIgnoreErrors);

    def asyncRmFile(self, sRemoteFile, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a rmfile task.

        Returns True on success, False on failure (logged).

        The task returns True on success, False on failure (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "rmFile", self.taskRmFile, (sRemoteFile,));

    def syncRmFile(self, sRemoteFile, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncRmFile, sRemoteFile, cMsTimeout, fIgnoreErrors);

    def asyncRmSymlink(self, sRemoteSymlink, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a rmsymlink task.

        Returns True on success, False on failure (logged).

        The task returns True on success, False on failure (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "rmSymlink", self.taskRmSymlink, (sRemoteSymlink,));

    def syncRmSymlink(self, sRemoteSymlink, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncRmSymlink, sRemoteSymlink, cMsTimeout, fIgnoreErrors);

    def asyncRmTree(self, sRemoteTree, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a rmtree task.

        Returns True on success, False on failure (logged).

        The task returns True on success, False on failure (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "rmTree", self.taskRmTree, (sRemoteTree,));

    def syncRmTree(self, sRemoteTree, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncRmTree, sRemoteTree, cMsTimeout, fIgnoreErrors);

    def asyncChMod(self, sRemotePath, fMode, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a chmod task.

        Returns True on success, False on failure (logged).

        The task returns True on success, False on failure (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "chMod", self.taskChMod, (sRemotePath, fMode));

    def syncChMod(self, sRemotePath, fMode, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncChMod, sRemotePath, fMode, cMsTimeout, fIgnoreErrors);

    def asyncChOwn(self, sRemotePath, idUser, idGroup, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a chown task.

        Returns True on success, False on failure (logged).

        The task returns True on success, False on failure (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "chOwn", self.taskChOwn, (sRemotePath, idUser, idGroup));

    def syncChOwn(self, sRemotePath, idUser, idGroup, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncChMod, sRemotePath, idUser, idGroup, cMsTimeout, fIgnoreErrors);

    def asyncIsDir(self, sRemoteDir, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a is-dir query task.

        Returns True on success, False on failure (logged).

        The task returns True if it's a directory, False if it isn't, and
        None on error (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "isDir", self.taskIsDir, (sRemoteDir,));

    def syncIsDir(self, sRemoteDir, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncIsDir, sRemoteDir, cMsTimeout, fIgnoreErrors);

    def asyncIsFile(self, sRemoteFile, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a is-file query task.

        Returns True on success, False on failure (logged).

        The task returns True if it's a file, False if it isn't, and None on
        error (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "isFile", self.taskIsFile, (sRemoteFile,));

    def syncIsFile(self, sRemoteFile, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncIsFile, sRemoteFile, cMsTimeout, fIgnoreErrors);

    def asyncIsSymlink(self, sRemoteSymlink, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a is-symbolic-link query task.

        Returns True on success, False on failure (logged).

        The task returns True if it's a symbolic linke, False if it isn't, and
        None on error (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "isSymlink", self.taskIsSymlink, (sRemoteSymlink,));

    def syncIsSymlink(self, sRemoteSymlink, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncIsSymlink, sRemoteSymlink, cMsTimeout, fIgnoreErrors);

    #def "STAT    "
    #def "LSTAT   "
    #def "LIST    "

    @staticmethod
    def calcFileXferTimeout(cbFile):
        """
        Calculates a reasonable timeout for an upload/download given the file size.

        Returns timeout in milliseconds.
        """
        return 30000 + cbFile / 32; # 32 KiB/s (picked out of thin air)

    @staticmethod
    def calcUploadTimeout(sLocalFile):
        """
        Calculates a reasonable timeout for an upload given the file (will stat it).

        Returns timeout in milliseconds.
        """
        try:    cbFile = os.path.getsize(sLocalFile);
        except: cbFile = 1024*1024;
        return Session.calcFileXferTimeout(cbFile);

    def asyncCopyFile(self, sSrcFile, sDstFile,
                      fMode = 0, fFallbackOkay = True, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a file copying task on the remote.

        Returns True on success, False on failure (logged).

        The task returns True on success, False on failure (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "cpfile",
                              self.taskCopyFile, (sSrcFile, sDstFile, fMode, fFallbackOkay));

    def syncCopyFile(self, sSrcFile, sDstFile, fMode = 0, cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncCopyFile, sSrcFile, sDstFile, fMode, cMsTimeout, fIgnoreErrors);

    def asyncUploadFile(self, sLocalFile, sRemoteFile,
                        fMode = 0, fFallbackOkay = True, cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a download query task.

        Returns True on success, False on failure (logged).

        The task returns True on success, False on failure (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "upload",
                              self.taskUploadFile, (sLocalFile, sRemoteFile, fMode, fFallbackOkay));

    def syncUploadFile(self, sLocalFile, sRemoteFile, fMode = 0, fFallbackOkay = True, cMsTimeout = 0, fIgnoreErrors = False):
        """Synchronous version."""
        if cMsTimeout <= 0:
            cMsTimeout = self.calcUploadTimeout(sLocalFile);
        return self.asyncToSync(self.asyncUploadFile, sLocalFile, sRemoteFile, fMode, fFallbackOkay, cMsTimeout, fIgnoreErrors);

    def asyncUploadString(self, sContent, sRemoteFile,
                          fMode = 0, fFallbackOkay = True, cMsTimeout = 0, fIgnoreErrors = False):
        """
        Initiates a upload string task.

        Returns True on success, False on failure (logged).

        The task returns True on success, False on failure (logged).
        """
        if cMsTimeout <= 0:
            cMsTimeout = self.calcFileXferTimeout(len(sContent));
        return self.startTask(cMsTimeout, fIgnoreErrors, "uploadString",
                              self.taskUploadString, (sContent, sRemoteFile, fMode, fFallbackOkay));

    def syncUploadString(self, sContent, sRemoteFile, fMode = 0, fFallbackOkay = True, cMsTimeout = 0, fIgnoreErrors = False):
        """Synchronous version."""
        if cMsTimeout <= 0:
            cMsTimeout = self.calcFileXferTimeout(len(sContent));
        return self.asyncToSync(self.asyncUploadString, sContent, sRemoteFile, fMode, fFallbackOkay, cMsTimeout, fIgnoreErrors);

    def asyncDownloadFile(self, sRemoteFile, sLocalFile, cMsTimeout = 120000, fIgnoreErrors = False):
        """
        Initiates a download file task.

        Returns True on success, False on failure (logged).

        The task returns True on success, False on failure (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "downloadFile", self.taskDownloadFile, (sRemoteFile, sLocalFile));

    def syncDownloadFile(self, sRemoteFile, sLocalFile, cMsTimeout = 120000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncDownloadFile, sRemoteFile, sLocalFile, cMsTimeout, fIgnoreErrors);

    def asyncDownloadString(self, sRemoteFile, sEncoding = 'utf-8', fIgnoreEncodingErrors = True,
                            cMsTimeout = 30000, fIgnoreErrors = False):
        """
        Initiates a download string task.

        Returns True on success, False on failure (logged).

        The task returns a byte string on success, False on failure (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "downloadString",
                              self.taskDownloadString, (sRemoteFile, sEncoding, fIgnoreEncodingErrors));

    def syncDownloadString(self, sRemoteFile, sEncoding = 'utf-8', fIgnoreEncodingErrors = True,
                           cMsTimeout = 30000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncDownloadString, sRemoteFile, sEncoding, fIgnoreEncodingErrors,
                                cMsTimeout, fIgnoreErrors);

    def asyncPackFile(self, sRemoteFile, sRemoteSource, cMsTimeout = 120000, fIgnoreErrors = False):
        """
        Initiates a packing file/directory task.

        Returns True on success, False on failure (logged).

        The task returns True on success, False on failure (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "packFile", self.taskPackFile,
                              (sRemoteFile, sRemoteSource));

    def syncPackFile(self, sRemoteFile, sRemoteSource, cMsTimeout = 120000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncPackFile, sRemoteFile, sRemoteSource, cMsTimeout, fIgnoreErrors);

    def asyncUnpackFile(self, sRemoteFile, sRemoteDir, cMsTimeout = 120000, fIgnoreErrors = False):
        """
        Initiates a unpack file task.

        Returns True on success, False on failure (logged).

        The task returns True on success, False on failure (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "unpackFile", self.taskUnpackFile,
                              (sRemoteFile, sRemoteDir));

    def syncUnpackFile(self, sRemoteFile, sRemoteDir, cMsTimeout = 120000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncUnpackFile, sRemoteFile, sRemoteDir, cMsTimeout, fIgnoreErrors);

    def asyncExpandString(self, sString, cMsTimeout = 120000, fIgnoreErrors = False):
        """
        Initiates an expand string task.

        Returns expanded string on success, False on failure (logged).

        The task returns True on success, False on failure (logged).
        """
        return self.startTask(cMsTimeout, fIgnoreErrors, "expandString",
                              self.taskExpandString, (sString,));

    def syncExpandString(self, sString, cMsTimeout = 120000, fIgnoreErrors = False):
        """Synchronous version."""
        return self.asyncToSync(self.asyncExpandString, sString, cMsTimeout, fIgnoreErrors);


class TransportTcp(TransportBase):
    """
    TCP transport layer for the TXS client session class.
    """

    def __init__(self, sHostname, uPort, fReversedSetup):
        """
        Save the parameters. The session will call us back to make the
        connection later on its worker thread.
        """
        TransportBase.__init__(self, utils.getCallerName());
        self.sHostname        = sHostname;
        self.fReversedSetup   = fReversedSetup;
        self.uPort            = uPort if uPort is not None else 5042 if fReversedSetup is False else 5048;
        self.oSocket          = None;
        self.oWakeupW         = None;
        self.oWakeupR         = None;
        self.fConnectCanceled = False;
        self.fIsConnecting    = False;
        self.oCv              = threading.Condition();
        self.abReadAhead      = array.array('B');

    def toString(self):
        return '<%s sHostname=%s, fReversedSetup=%s, uPort=%s, oSocket=%s,'\
               ' fConnectCanceled=%s, fIsConnecting=%s, oCv=%s, abReadAhead=%s>' \
             % (TransportBase.toString(self), self.sHostname, self.fReversedSetup, self.uPort, self.oSocket,
                self.fConnectCanceled, self.fIsConnecting, self.oCv, self.abReadAhead);

    def __isInProgressXcpt(self, oXcpt):
        """ In progress exception? """
        try:
            if isinstance(oXcpt, socket.error):
                try:
                    if oXcpt.errno == errno.EINPROGRESS:
                        return True;
                except: pass;
                # Windows?
                try:
                    if oXcpt.errno == errno.EWOULDBLOCK:
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
                    if oXcpt.errno == errno.EWOULDBLOCK:
                        return True;
                except: pass;
                try:
                    if oXcpt.errno == errno.EAGAIN:
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
                    if oXcpt.errno == errno.ECONNRESET:
                        return True;
                except: pass;
                try:
                    if oXcpt.errno == errno.ENETRESET:
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
                try:    oWakeupW.send(b'cancelled!\n');
                except: reporter.logXcpt();
                try:    oWakeupW.shutdown(socket.SHUT_WR);
                except: reporter.logXcpt();
                oWakeupW.close();
        self.oCv.release();

    def _connectAsServer(self, oSocket, oWakeupR, cMsTimeout):
        """ Connects to the TXS server as server, i.e. the reversed setup. """
        assert(self.fReversedSetup);

        reporter.log2('TransportTcp::_connectAsServer: oSocket=%s, cMsTimeout=%u' % (oSocket, cMsTimeout));

        # Workaround for bind() failure...
        try:
            oSocket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1);
        except:
            reporter.errorXcpt('socket.listen(1) failed');
            return None;

        # Bind the socket and make it listen.
        try:
            oSocket.bind((self.sHostname, self.uPort));
        except:
            reporter.errorXcpt('socket.bind((%s,%s)) failed' % (self.sHostname, self.uPort));
            return None;
        try:
            oSocket.listen(1);
        except:
            reporter.errorXcpt('socket.listen(1) failed');
            return None;

        # Accept connections.
        oClientSocket = None;
        tClientAddr   = None;
        try:
            (oClientSocket, tClientAddr) = oSocket.accept();
        except socket.error as e:
            if not self.__isInProgressXcpt(e):
                raise;

            # Do the actual waiting.
            reporter.log2('TransportTcp::accept: operation in progress (%s)...' % (e,));
            try:
                select.select([oSocket, oWakeupR], [], [oSocket, oWakeupR], cMsTimeout / 1000.0);
            except socket.error as oXctp:
                if oXctp.errno != errno.EBADF  or  not self.fConnectCanceled:
                    raise;
                reporter.log('socket.select() on accept was canceled');
                return None;
            except:
                reporter.logXcpt('socket.select() on accept');

            # Try accept again.
            try:
                (oClientSocket, tClientAddr) = oSocket.accept();
            except socket.error as oXcpt:
                if not self.__isInProgressXcpt(e):
                    if oXcpt.errno != errno.EBADF  or  not self.fConnectCanceled:
                        raise;
                    reporter.log('socket.accept() was canceled');
                    return None;
                reporter.log('socket.accept() timed out');
                return False;
            except:
                reporter.errorXcpt('socket.accept() failed');
                return None;
        except:
            reporter.errorXcpt('socket.accept() failed');
            return None;

        # Store the connected socket and throw away the server socket.
        self.oCv.acquire();
        if not self.fConnectCanceled:
            self.oSocket.close();
            self.oSocket = oClientSocket;
            self.sHostname = "%s:%s" % (tClientAddr[0], tClientAddr[1]);
        self.oCv.release();
        return True;

    def _connectAsClient(self, oSocket, oWakeupR, cMsTimeout):
        """ Connects to the TXS server as client. """
        assert(not self.fReversedSetup);

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
            if self.fReversedSetup:
                rc = self._connectAsServer(oSocket, oWakeupR, cMsTimeout);
            else:
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


def openTcpSession(cMsTimeout, sHostname, uPort = None, fReversedSetup = False, cMsIdleFudge = 0, fnProcessEvents = None):
    """
    Opens a connection to a Test Execution Service via TCP, given its name.

    The optional fnProcessEvents callback should be set to vbox.processPendingEvents
    or similar.
    """
    reporter.log2('openTcpSession(%s, %s, %s, %s, %s)' %
                  (cMsTimeout, sHostname, uPort, fReversedSetup, cMsIdleFudge));
    try:
        oTransport = TransportTcp(sHostname, uPort, fReversedSetup);
        oSession = Session(oTransport, cMsTimeout, cMsIdleFudge, fnProcessEvents = fnProcessEvents);
    except:
        reporter.errorXcpt(None, 15);
        return None;
    return oSession;


def tryOpenTcpSession(cMsTimeout, sHostname, uPort = None, fReversedSetup = False, cMsIdleFudge = 0, fnProcessEvents = None):
    """
    Tries to open a connection to a Test Execution Service via TCP, given its name.

    This differs from openTcpSession in that it won't log a connection failure
    as an error.
    """
    try:
        oTransport = TransportTcp(sHostname, uPort, fReversedSetup);
        oSession = Session(oTransport, cMsTimeout, cMsIdleFudge, fTryConnect = True, fnProcessEvents = fnProcessEvents);
    except:
        reporter.errorXcpt(None, 15);
        return None;
    return oSession;
