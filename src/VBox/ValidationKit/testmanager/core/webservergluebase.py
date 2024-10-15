# -*- coding: utf-8 -*-
# $Id: webservergluebase.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

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
import cgitb;   # pylint: disable=deprecated-module ## @todo these will be retired in python 3.13!
import codecs;
import os
import sys

# Validation Kit imports.
from common         import webutils, utils;
from testmanager    import config;


class WebServerGlueException(Exception):
    """
    For exceptions raised by glue code.
    """
    pass;                               # pylint: disable=unnecessary-pass


class WebServerGlueBase(object):
    """
    Web server interface abstraction and some HTML utils.
    """

    ## Enables more debug output.
    kfDebugInfoEnabled = True;

    ## The maximum number of characters to cache.
    kcchMaxCached = 65536;

    ## Special getUserName return value.
    ksUnknownUser = 'Unknown User';

    ## HTTP status codes and their messages.
    kdStatusMsgs = {
        100: 'Continue',
        101: 'Switching Protocols',
        102: 'Processing',
        103: 'Early Hints',
        200: 'OK',
        201: 'Created',
        202: 'Accepted',
        203: 'Non-Authoritative Information',
        204: 'No Content',
        205: 'Reset Content',
        206: 'Partial Content',
        207: 'Multi-Status',
        208: 'Already Reported',
        226: 'IM Used',
        300: 'Multiple Choices',
        301: 'Moved Permantently',
        302: 'Found',
        303: 'See Other',
        304: 'Not Modified',
        305: 'Use Proxy',
        306: 'Switch Proxy',
        307: 'Temporary Redirect',
        308: 'Permanent Redirect',
        400: 'Bad Request',
        401: 'Unauthorized',
        402: 'Payment Required',
        403: 'Forbidden',
        404: 'Not Found',
        405: 'Method Not Allowed',
        406: 'Not Acceptable',
        407: 'Proxy Authentication Required',
        408: 'Request Timeout',
        409: 'Conflict',
        410: 'Gone',
        411: 'Length Required',
        412: 'Precondition Failed',
        413: 'Payload Too Large',
        414: 'URI Too Long',
        415: 'Unsupported Media Type',
        416: 'Range Not Satisfiable',
        417: 'Expectation Failed',
        418: 'I\'m a teapot',
        421: 'Misdirection Request',
        422: 'Unprocessable Entity',
        423: 'Locked',
        424: 'Failed Dependency',
        425: 'Too Early',
        426: 'Upgrade Required',
        428: 'Precondition Required',
        429: 'Too Many Requests',
        431: 'Request Header Fields Too Large',
        451: 'Unavailable For Legal Reasons',
        500: 'Internal Server Error',
        501: 'Not Implemented',
        502: 'Bad Gateway',
        503: 'Service Unavailable',
        504: 'Gateway Timeout',
        505: 'HTTP Version Not Supported',
        506: 'Variant Also Negotiates',
        507: 'Insufficient Storage',
        508: 'Loop Detected',
        510: 'Not Extended',
        511: 'Network Authentication Required',
    };


    def __init__(self, sValidationKitDir, fHtmlDebugOutput = True):
        self._sValidationKitDir    = sValidationKitDir;

        # Debug
        self.tsStart           = utils.timestampNano();
        self._fHtmlDebugOutput = fHtmlDebugOutput; # For trace
        self._oDbgFile         = sys.stderr;
        if config.g_ksSrvGlueDebugLogDst is not None and config.g_kfSrvGlueDebug is True:
            self._oDbgFile = open(config.g_ksSrvGlueDebugLogDst, 'a');  # pylint: disable=consider-using-with,unspecified-encoding
            if config.g_kfSrvGlueCgiDumpArgs:
                self._oDbgFile.write('Arguments: %s\nEnvironment:\n' % (sys.argv,));
            if config.g_kfSrvGlueCgiDumpEnv:
                for sVar in sorted(os.environ):
                    self._oDbgFile.write('  %s=\'%s\' \\\n' % (sVar, os.environ[sVar],));

        self._afnDebugInfo     = [];

        # HTTP header.
        self._fHeaderWrittenOut = False;
        self._dHeaderFields = \
        { \
            'Content-Type': 'text/html; charset=utf-8',
        };

        # Body.
        self._sBodyType = None;
        self._dParams = {};
        self._sHtmlBody = '';
        self._cchCached = 0;
        self._cchBodyWrittenOut = 0;

        # Output.
        if sys.version_info[0] >= 3:
            self.oOutputRaw = sys.stdout.detach();  # pylint: disable=no-member
            sys.stdout = None; # Prevents flush_std_files() from complaining on stderr during sys.exit().
        else:
            self.oOutputRaw = sys.stdout;
        self.oOutputText = codecs.getwriter('utf-8')(self.oOutputRaw);


    #
    # Get stuff.
    #

    def getParameters(self):
        """
        Returns a dictionary with the query parameters.

        The parameter name is the key, the values are given as lists.  If a
        parameter is given more than once, the value is appended to the
        existing dictionary entry.
        """
        return {};

    def getClientAddr(self):
        """
        Returns the client address, as a string.
        """
        raise WebServerGlueException('getClientAddr is not implemented');

    def getMethod(self):
        """
        Gets the HTTP request method.
        """
        return 'POST';

    def getLoginName(self):
        """
        Gets login name provided by Apache.
        Returns kUnknownUser if not logged on.
        """
        return WebServerGlueBase.ksUnknownUser;

    def getUrlScheme(self):
        """
        Gets scheme name (aka. access protocol) from request URL, i.e. 'http' or 'https'.
        See also urlparse.scheme.
        """
        return 'http';

    def getUrlNetLoc(self):
        """
        Gets the network location (server host name / ip) from the request URL.
        See also urlparse.netloc.
        """
        raise WebServerGlueException('getUrlNetLoc is not implemented');

    def getUrlPath(self):
        """
        Gets the hirarchical path (relative to server) from the request URL.
        See also urlparse.path.
        Note! This includes the leading slash.
        """
        raise WebServerGlueException('getUrlPath is not implemented');

    def getUrlBasePath(self):
        """
        Gets the hirarchical base path (relative to server) from the request URL.
        Note! This includes both a leading an trailing slash.
        """
        sPath = self.getUrlPath();      # virtual method # pylint: disable=assignment-from-no-return
        iLastSlash = sPath.rfind('/');
        if iLastSlash >= 0:
            sPath = sPath[:iLastSlash];
            sPath = sPath.rstrip('/');
        return sPath + '/';

    def getUrl(self):
        """
        Gets the URL being accessed, sans parameters.
        For instance this will return, "http://localhost/testmanager/admin.cgi"
        when "http://localhost/testmanager/admin.cgi?blah=blah" is being access.
        """
        return '%s://%s%s' % (self.getUrlScheme(), self.getUrlNetLoc(), self.getUrlPath());

    def getBaseUrl(self):
        """
        Gets the base URL (with trailing slash).
        For instance this will return, "http://localhost/testmanager/" when
        "http://localhost/testmanager/admin.cgi?blah=blah" is being access.
        """
        return '%s://%s%s' % (self.getUrlScheme(), self.getUrlNetLoc(), self.getUrlBasePath());

    def getUserAgent(self):
        """
        Gets the User-Agent field of the HTTP header, returning empty string
        if not present.
        """
        return '';

    def getContentType(self):
        """
        Gets the Content-Type field of the HTTP header, parsed into a type
        string and a dictionary.
        """
        return ('text/html', {});

    def getContentLength(self):
        """
        Gets the content length.
        Returns int.
        """
        return 0;

    def getBodyIoStream(self):
        """
        Returns file object for reading the HTML body.
        """
        raise WebServerGlueException('getUrlPath is not implemented');

    def getBodyIoStreamBinary(self):
        """
        Returns file object for reading the binary HTML body.
        """
        raise WebServerGlueException('getBodyIoStreamBinary is not implemented');

    #
    # Output stuff.
    #

    def _writeHeader(self, sHeaderLine):
        """
        Worker function which child classes can override.
        """
        sys.stderr.write('_writeHeader: cch=%s "%s..."\n' % (len(sHeaderLine), sHeaderLine[0:10],))
        self.oOutputText.write(sHeaderLine);
        return True;

    def flushHeader(self):
        """
        Flushes the HTTP header.
        """
        if self._fHeaderWrittenOut is False:
            for sKey, sValue in self._dHeaderFields.items():
                self._writeHeader('%s: %s\n' % (sKey, sValue,));
            self._fHeaderWrittenOut = True;
            self._writeHeader('\n'); # End of header indicator.
        return None;

    def setHeaderField(self, sField, sValue):
        """
        Sets a header field.
        """
        assert self._fHeaderWrittenOut is False;
        self._dHeaderFields[sField] = sValue;
        return True;

    def setRedirect(self, sLocation, iCode = 302):
        """
        Sets up redirection of the page.
        Raises an exception if called too late.
        """
        if self._fHeaderWrittenOut is True:
            raise WebServerGlueException('setRedirect called after the header was written');
        if iCode != 302:
            raise WebServerGlueException('Redirection code %d is not supported' % (iCode,));

        self.setHeaderField('Location', sLocation);
        self.setHeaderField('Status', '302 Found');
        return True;

    def setStatus(self, iStatus, sMsg = None):
        """ Sets the status code. """
        if not sMsg:
            sMsg = self.kdStatusMsgs[iStatus];
        return self.setHeaderField('Status', '%u %s' % (iStatus, sMsg));

    def setContentType(self, sType):
        """ Sets the content type header field. """
        return self.setHeaderField('Content-Type', sType);

    def _writeWorker(self, sChunkOfHtml):
        """
        Worker function which child classes can override.
        """
        sys.stderr.write('_writeWorker: cch=%s "%s..."\n' % (len(sChunkOfHtml), sChunkOfHtml[0:10],))
        self.oOutputText.write(sChunkOfHtml);
        return True;

    def write(self, sChunkOfHtml):
        """
        Writes chunk of HTML, making sure the HTTP header is flushed first.
        """
        if self._sBodyType is None:
            self._sBodyType = 'html';
        elif self._sBodyType != 'html':
            raise WebServerGlueException('Cannot use writeParameter when body type is "%s"' % (self._sBodyType, ));

        self._sHtmlBody += sChunkOfHtml;
        self._cchCached += len(sChunkOfHtml);

        if self._cchCached > self.kcchMaxCached:
            self.flush();
        return True;

    def writeRaw(self, abChunk):
        """
        Writes a raw chunk the document. Can be binary or any encoding.
        No caching.
        """
        if self._sBodyType is None:
            self._sBodyType = 'raw';
        elif self._sBodyType != 'raw':
            raise WebServerGlueException('Cannot use writeRaw when body type is "%s"' % (self._sBodyType, ));

        self.flushHeader();
        if self._cchCached > 0:
            self.flush();

        sys.stderr.write('writeRaw: cb=%s\n' % (len(abChunk),))
        self.oOutputRaw.write(abChunk);
        return True;

    def writeParams(self, dParams):
        """
        Writes one or more reply parameters in a form style response. The names
        and values in dParams are unencoded, this method takes care of that.

        Note! This automatically changes the content type to
        'application/x-www-form-urlencoded', if the header hasn't been flushed
        already.
        """
        if self._sBodyType is None:
            if not self._fHeaderWrittenOut:
                self.setHeaderField('Content-Type', 'application/x-www-form-urlencoded; charset=utf-8');
            elif self._dHeaderFields['Content-Type'] != 'application/x-www-form-urlencoded; charset=utf-8':
                raise WebServerGlueException('Cannot use writeParams when content-type is "%s"' % \
                                             (self._dHeaderFields['Content-Type'],));
            self._sBodyType = 'form';

        elif self._sBodyType != 'form':
            raise WebServerGlueException('Cannot use writeParams when body type is "%s"' % (self._sBodyType, ));

        for sKey in dParams:
            sValue = str(dParams[sKey]);
            self._dParams[sKey] = sValue;
            self._cchCached += len(sKey) + len(sValue);

        if self._cchCached > self.kcchMaxCached:
            self.flush();

        return True;

    def flush(self):
        """
        Flush the output.
        """
        self.flushHeader();

        if self._sBodyType == 'form':
            sBody = webutils.encodeUrlParams(self._dParams);
            self._writeWorker(sBody);

            self._dParams = {};
            self._cchBodyWrittenOut += self._cchCached;

        elif self._sBodyType == 'html':
            self._writeWorker(self._sHtmlBody);

            self._sHtmlBody = '';
            self._cchBodyWrittenOut += self._cchCached;

        self._cchCached = 0;
        return None;

    #
    # Paths.
    #

    def pathTmWebUI(self):
        """
        Gets the path to the TM 'webui' directory.
        """
        return os.path.join(self._sValidationKitDir, 'testmanager', 'webui');

    #
    # Error stuff & Debugging.
    #

    def errorLog(self, sError, aXcptInfo, sLogFile):
        """
        Writes the error to a log file.
        """
        # Easy solution for log file size: Only one report.
        try:    os.unlink(sLogFile);
        except: pass;

        # Try write the log file.
        fRc    = True;
        fSaved = self._fHtmlDebugOutput;

        try:
            with open(sLogFile, 'w') as oFile: # pylint: disable=unspecified-encoding
                oFile.write(sError + '\n\n');
                if aXcptInfo[0] is not None:
                    oFile.write(' B a c k t r a c e\n');
                    oFile.write('===================\n');
                    oFile.write(cgitb.text(aXcptInfo, 5));
                    oFile.write('\n\n');

                oFile.write(' D e b u g   I n f o\n');
                oFile.write('=====================\n\n');
                self._fHtmlDebugOutput = False;
                self.debugDumpStuff(oFile.write);
        except:
            fRc = False;

        self._fHtmlDebugOutput = fSaved;
        return fRc;

    def errorPage(self, sError, aXcptInfo, sLogFile = None):
        """
        Displays a page with an error message.
        """
        if sLogFile is not None:
            self.errorLog(sError, aXcptInfo, sLogFile);

        # Reset buffering, hoping that nothing was flushed yet.
        self._sBodyType = None;
        self._sHtmlBody = '';
        self._cchCached = 0;
        if not self._fHeaderWrittenOut:
            if self._fHtmlDebugOutput:
                self.setHeaderField('Content-Type', 'text/html; charset=utf-8');
            else:
                self.setHeaderField('Content-Type', 'text/plain; charset=utf-8');

        # Write the error page.
        if self._fHtmlDebugOutput:
            self.write('<html><head><title>Test Manage Error</title></head>\n' +
                       '<body><h1>Test Manager Error:</h1>\n' +
                       '<p>' + sError + '</p>\n');
        else:
            self.write(' Test Manage Error\n'
                       '===================\n'
                       '\n'
                       '' + sError + '\n\n');

        if aXcptInfo[0] is not None:
            if self._fHtmlDebugOutput:
                self.write('<h1>Backtrace:</h1>\n');
                self.write(cgitb.html(aXcptInfo, 5));
            else:
                self.write('Backtrace\n'
                           '---------\n'
                           '\n');
                self.write(cgitb.text(aXcptInfo, 5));
                self.write('\n\n');

        if self.kfDebugInfoEnabled:
            if self._fHtmlDebugOutput:
                self.write('<h1>Debug Info:</h1>\n');
            else:
                self.write('Debug Info\n'
                           '----------\n'
                           '\n');
            self.debugDumpStuff();

            for fn in self._afnDebugInfo:
                try:
                    fn(self, self._fHtmlDebugOutput);
                except Exception as oXcpt:
                    self.write('\nDebug info callback %s raised exception: %s\n' % (fn, oXcpt));

        if self._fHtmlDebugOutput:
            self.write('</body></html>');

        self.flush();

    def debugInfoPage(self, fnWrite = None):
        """
        Dumps useful debug info.
        """
        if fnWrite is None:
            fnWrite = self.write;

        fnWrite('<html><head><title>Test Manage Debug Info</title></head>\n<body>\n');
        self.debugDumpStuff(fnWrite = fnWrite);
        fnWrite('</body></html>');
        self.flush();

    def debugDumpDict(self, sName, dDict, fSorted = True, fnWrite = None):
        """
        Dumps dictionary.
        """
        if fnWrite is None:
            fnWrite = self.write;

        asKeys = list(dDict.keys());
        if fSorted:
            asKeys.sort();

        if self._fHtmlDebugOutput:
            fnWrite('<h2>%s</h2>\n'
                    '<table border="1"><tr><th>name</th><th>value</th></tr>\n' % (sName,));
            for sKey in asKeys:
                fnWrite('  <tr><td>' + webutils.escapeElem(sKey) + '</td><td>' \
                        + webutils.escapeElem(str(dDict.get(sKey))) \
                        + '</td></tr>\n');
            fnWrite('</table>\n');
        else:
            for i in range(len(sName) - 1):
                fnWrite('%s ' % (sName[i],));
            fnWrite('%s\n\n' % (sName[-1],));

            fnWrite('%28s  Value\n' % ('Name',));
            fnWrite('------------------------------------------------------------------------\n');
            for sKey in asKeys:
                fnWrite('%28s: %s\n' % (sKey, dDict.get(sKey),));
            fnWrite('\n');

        return True;

    def debugDumpList(self, sName, aoStuff, fnWrite = None):
        """
        Dumps array.
        """
        if fnWrite is None:
            fnWrite = self.write;

        if self._fHtmlDebugOutput:
            fnWrite('<h2>%s</h2>\n'
                    '<table border="1"><tr><th>index</th><th>value</th></tr>\n' % (sName,));
            for i, oStuff in enumerate(aoStuff):
                fnWrite('  <tr><td>' + str(i) + '</td><td>' + webutils.escapeElem(str(oStuff)) + '</td></tr>\n');
            fnWrite('</table>\n');
        else:
            for ch in sName[:-1]:
                fnWrite('%s ' % (ch,));
            fnWrite('%s\n\n' % (sName[-1],));

            fnWrite('Index  Value\n');
            fnWrite('------------------------------------------------------------------------\n');
            for i, oStuff in enumerate(aoStuff):
                fnWrite('%5u  %s\n' % (i, str(oStuff)));
            fnWrite('\n');

        return True;

    def debugDumpParameters(self, fnWrite):
        """ Dumps request parameters. """
        if fnWrite is None:
            fnWrite = self.write;

        try:
            dParams = self.getParameters();
            return self.debugDumpDict('Parameters', dParams);
        except Exception as oXcpt:
            if self._fHtmlDebugOutput:
                fnWrite('<p>Exception %s while retriving parameters.</p>\n' % (oXcpt,))
            else:
                fnWrite('Exception %s while retriving parameters.\n' % (oXcpt,))
            return False;

    def debugDumpEnv(self, fnWrite = None):
        """ Dumps os.environ. """
        return self.debugDumpDict('Environment (os.environ)', os.environ, fnWrite = fnWrite);

    def debugDumpArgv(self, fnWrite = None):
        """ Dumps sys.argv. """
        return self.debugDumpList('Arguments (sys.argv)', sys.argv, fnWrite = fnWrite);

    def debugDumpPython(self, fnWrite = None):
        """
        Dump python info.
        """
        dInfo = {};
        dInfo['sys.version']                = sys.version;
        dInfo['sys.hexversion']             = sys.hexversion;
        dInfo['sys.api_version']            = sys.api_version;
        if hasattr(sys, 'subversion'):
            dInfo['sys.subversion']         = sys.subversion;   # pylint: disable=no-member
        dInfo['sys.platform']               = sys.platform;
        dInfo['sys.executable']             = sys.executable;
        dInfo['sys.copyright']              = sys.copyright;
        dInfo['sys.byteorder']              = sys.byteorder;
        dInfo['sys.exec_prefix']            = sys.exec_prefix;
        dInfo['sys.prefix']                 = sys.prefix;
        dInfo['sys.path']                   = sys.path;
        dInfo['sys.builtin_module_names']   = sys.builtin_module_names;
        dInfo['sys.flags']                  = sys.flags;

        return self.debugDumpDict('Python Info', dInfo, fnWrite = fnWrite);


    def debugDumpStuff(self, fnWrite = None):
        """
        Dumps stuff to the error page and debug info page.
        Should be extended by child classes when possible.
        """
        self.debugDumpParameters(fnWrite);
        self.debugDumpEnv(fnWrite);
        self.debugDumpArgv(fnWrite);
        self.debugDumpPython(fnWrite);
        return True;

    def dprint(self, sMessage):
        """
        Prints to debug log (usually apache error log).
        """
        if config.g_kfSrvGlueDebug is True:
            if config.g_kfSrvGlueDebugTS is False:
                self._oDbgFile.write(sMessage);
                if not sMessage.endswith('\n'):
                    self._oDbgFile.write('\n');
            else:
                tsNow = utils.timestampMilli();
                tsReq = tsNow - (self.tsStart / 1000000);
                iPid  = os.getpid();
                for sLine in sMessage.split('\n'):
                    self._oDbgFile.write('%s/%03u,pid=%04x: %s\n' % (tsNow, tsReq, iPid, sLine,));

        return True;

    def registerDebugInfoCallback(self, fnDebugInfo):
        """
        Registers a debug info method for calling when the error page is shown.

        The fnDebugInfo function takes two parameters.  The first is this
        object, the second is a boolean indicating html (True) or text (False)
        output.  The return value is ignored.
        """
        if self.kfDebugInfoEnabled:
            self._afnDebugInfo.append(fnDebugInfo);
        return True;

    def unregisterDebugInfoCallback(self, fnDebugInfo):
        """
        Unregisters a debug info method previously registered by
        registerDebugInfoCallback.
        """
        if self.kfDebugInfoEnabled:
            try:    self._afnDebugInfo.remove(fnDebugInfo);
            except: pass;
        return True;

