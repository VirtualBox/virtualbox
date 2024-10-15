# -*- coding: utf-8 -*-
# $Id: testboxconnection.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
TestBox Script - HTTP Connection Handling.
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
import sys;
if sys.version_info[0] >= 3:
    import http.client as httplib;                          # pylint: disable=import-error,no-name-in-module
    import urllib.parse as urlparse;                        # pylint: disable=import-error,no-name-in-module
    from urllib.parse import urlencode as urllib_urlencode; # pylint: disable=import-error,no-name-in-module
else:
    import httplib;                                         # pylint: disable=import-error,no-name-in-module
    import urlparse;                                        # pylint: disable=import-error,no-name-in-module
    from urllib import urlencode as urllib_urlencode;       # pylint: disable=import-error,no-name-in-module

# Validation Kit imports.
from common import constants
from common import utils
import testboxcommons



class TestBoxResponse(object):
    """
    Response object return by TestBoxConnection.request().
    """
    def __init__(self, oResponse):
        """
        Convert the HTTPResponse to a dictionary, raising TestBoxException on
        malformed response.
        """
        if oResponse is not None:
            # Read the whole response (so we can log it).
            sBody = oResponse.read();
            sBody = sBody.decode('utf-8');

            # Check the content type.
            sContentType = oResponse.getheader('Content-Type');
            if sContentType is None  or  sContentType != 'application/x-www-form-urlencoded; charset=utf-8':
                testboxcommons.log('SERVER RESPONSE: Content-Type: %s' % (sContentType,));
                testboxcommons.log('SERVER RESPONSE: %s' % (sBody.rstrip(),))
                raise testboxcommons.TestBoxException('Invalid server response type: "%s"' % (sContentType,));

            # Parse the body (this should be the exact reverse of what
            # TestBoxConnection.postRequestRaw).
            ##testboxcommons.log2('SERVER RESPONSE: "%s"' % (sBody,))
            self._dResponse = urlparse.parse_qs(sBody, strict_parsing=True);

            # Convert the dictionary from 'field:values' to 'field:value'. Fail
            # if a field has more than one value (i.e. given more than once).
            for sField in self._dResponse:
                if len(self._dResponse[sField]) != 1:
                    raise testboxcommons.TestBoxException('The field "%s" appears more than once in the server response' \
                                                          % (sField,));
                self._dResponse[sField] = self._dResponse[sField][0]
        else:
            # Special case, dummy response object.
            self._dResponse = {};
        # Done.

    def getStringChecked(self, sField):
        """
        Check if specified field is present in server response and returns it as string.
        If not present, a fitting exception will be raised.
        """
        if not sField in self._dResponse:
            raise testboxcommons.TestBoxException('Required data (' + str(sField) + ') was not found in server response');
        return str(self._dResponse[sField]).strip();

    def getIntChecked(self, sField, iMin = None, iMax = None):
        """
        Check if specified field is present in server response and returns it as integer.
        If not present, a fitting exception will be raised.

        The iMin and iMax values are inclusive.
        """
        if not sField in self._dResponse:
            raise testboxcommons.TestBoxException('Required data (' + str(sField) + ') was not found in server response')
        try:
            iValue = int(self._dResponse[sField]);
        except:
            raise testboxcommons.TestBoxException('Malformed integer field %s: "%s"' % (sField, self._dResponse[sField]));

        if   (iMin is not None and iValue < iMin) \
          or (iMax is not None and iValue > iMax):
            raise testboxcommons.TestBoxException('Value (%d) of field %s is out of range [%s..%s]' \
                                                  % (iValue, sField, iMin, iMax));
        return iValue;

    def checkParameterCount(self, cExpected):
        """
        Checks the parameter count, raise TestBoxException if it doesn't meet
        the expectations.
        """
        if len(self._dResponse) != cExpected:
            raise testboxcommons.TestBoxException('Expected %d parameters, server sent %d' % (cExpected, len(self._dResponse)));
        return True;

    def toString(self):
        """
        Convers the response to a string (for debugging purposes).
        """
        return str(self._dResponse);


class TestBoxConnection(object):
    """
    Wrapper around HTTPConnection.
    """

    def __init__(self, sTestManagerUrl, sTestBoxId, sTestBoxUuid, fLongTimeout = False):
        """
        Constructor.
        """
        self._oConn             = None;
        self._oParsedUrl        = urlparse.urlparse(sTestManagerUrl);
        self._sTestBoxId        = sTestBoxId;
        self._sTestBoxUuid      = sTestBoxUuid;

        #
        # Connect to it - may raise exception on failure.
        # When connecting we're using a 15 second timeout, we increase it later.
        #
        if self._oParsedUrl.scheme == 'https': # pylint: disable=no-member
            fnCtor = httplib.HTTPSConnection;
        else:
            fnCtor = httplib.HTTPConnection;
        if     sys.version_info[0] >= 3 \
           or (sys.version_info[0] == 2 and sys.version_info[1] >= 6):

            self._oConn = fnCtor(self._oParsedUrl.hostname, timeout=15);
        else:
            self._oConn = fnCtor(self._oParsedUrl.hostname);

        if self._oConn.sock is None:
            self._oConn.connect();

        #
        # Increase the timeout for the non-connect operations.
        #
        try:
            self._oConn.sock.settimeout(5*60 if fLongTimeout else 1 * 60);
        except:
            pass;

        ##testboxcommons.log2('hostname=%s timeout=%u' % (self._oParsedUrl.hostname, self._oConn.sock.gettimeout()));

    def __del__(self):
        """ Makes sure the connection is really closed on destruction """
        self.close()

    def close(self):
        """ Closes the connection """
        if self._oConn is not None:
            self._oConn.close();
            self._oConn = None;

    def postRequestRaw(self, sAction, dParams):
        """
        Posts a request to the test manager and gets the response.  The dParams
        argument is a dictionary of unencoded key-value pairs (will be
        modified).
        Raises exception on failure.
        """
        dHeader = \
        {
            'Content-Type':     'application/x-www-form-urlencoded; charset=utf-8',
            'User-Agent':       'TestBoxScript/%s.0 (%s, %s)' % (__version__, utils.getHostOs(), utils.getHostArch()),
            'Accept':           'text/plain,application/x-www-form-urlencoded',
            'Accept-Encoding':  'identity',
            'Cache-Control':    'max-age=0',
            'Connection':       'keep-alive',
        };
        sServerPath = '/%s/testboxdisp.py' % (self._oParsedUrl.path.strip('/'),); # pylint: disable=no-member
        dParams[constants.tbreq.ALL_PARAM_ACTION] = sAction;
        sBody = urllib_urlencode(dParams);
        ##testboxcommons.log2('sServerPath=%s' % (sServerPath,));
        try:
            self._oConn.request('POST', sServerPath, sBody, dHeader);
            oResponse = self._oConn.getresponse();
            oResponse2 = TestBoxResponse(oResponse);
        except:
            testboxcommons.log2Xcpt();
            raise
        return oResponse2;

    def postRequest(self, sAction, dParams = None):
        """
        Posts a request to the test manager, prepending the testbox ID and
        UUID to the arguments, and gets the response. The dParams argument is a
        is a dictionary of unencoded key-value pairs (will be modified).
        Raises exception on failure.
        """
        if dParams is None:
            dParams = {};
        dParams[constants.tbreq.ALL_PARAM_TESTBOX_ID]   = self._sTestBoxId;
        dParams[constants.tbreq.ALL_PARAM_TESTBOX_UUID] = self._sTestBoxUuid;
        return self.postRequestRaw(sAction, dParams);

    def sendReply(self, sReplyAction, sCmdName):
        """
        Sends a reply to a test manager command.
        Raises exception on failure.
        """
        return self.postRequest(sReplyAction, { constants.tbreq.COMMAND_ACK_PARAM_CMD_NAME: sCmdName });

    def sendReplyAndClose(self, sReplyAction, sCmdName):
        """
        Sends a reply to a test manager command and closes the connection.
        Raises exception on failure.
        """
        self.sendReply(sReplyAction, sCmdName);
        self.close();
        return True;

    def sendAckAndClose(self, sCmdName):
        """
        Acks a command and closes the connection to the test manager.
        Raises exception on failure.
        """
        return self.sendReplyAndClose(constants.tbreq.COMMAND_ACK, sCmdName);

    def sendAck(self, sCmdName):
        """
        Acks a command.
        Raises exception on failure.
        """
        return self.sendReply(constants.tbreq.COMMAND_ACK, sCmdName);

    @staticmethod
    def sendSignOn(sTestManagerUrl, dParams):
        """
        Sends a sign-on request to the server, returns the response (TestBoxResponse).
        No exceptions will be raised.
        """
        oConnection = None;
        try:
            oConnection = TestBoxConnection(sTestManagerUrl, None, None);
            return oConnection.postRequestRaw(constants.tbreq.SIGNON, dParams);
        except:
            testboxcommons.log2Xcpt();
            if oConnection is not None: # Be kind to apache.
                try:    oConnection.close();
                except: pass;

        return TestBoxResponse(None);

    @staticmethod
    def requestCommandWithConnection(sTestManagerUrl, sTestBoxId, sTestBoxUuid, fBusy):
        """
        Queries the test manager for a command and returns its respons + an open
        connection for acking/nack the command (and maybe more).

        No exceptions will be raised.  On failure (None, None) will be returned.
        """
        oConnection = None;
        try:
            oConnection = TestBoxConnection(sTestManagerUrl, sTestBoxId, sTestBoxUuid, fLongTimeout = not fBusy);
            if fBusy:
                oResponse = oConnection.postRequest(constants.tbreq.REQUEST_COMMAND_BUSY);
            else:
                oResponse = oConnection.postRequest(constants.tbreq.REQUEST_COMMAND_IDLE);
            return (oResponse, oConnection);
        except:
            testboxcommons.log2Xcpt();
            if oConnection is not None: # Be kind to apache.
                try:    oConnection.close();
                except: pass;
        return (None, None);

    def isConnected(self):
        """
        Checks if we are still connected.
        """
        return self._oConn is not None;
