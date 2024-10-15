# -*- coding: utf-8 -*-
# $Id: db.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager - Database Interface.
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
import datetime;
import os;
import sys;
import psycopg2;                            # pylint: disable=import-error
import psycopg2.extensions;                 # pylint: disable=import-error

# Validation Kit imports.
from common                             import utils, webutils;
from testmanager                        import config;

# Fix psycho unicode handling in psycopg2 with python 2.x.
if sys.version_info[0] < 3:
    psycopg2.extensions.register_type(psycopg2.extensions.UNICODE);
    psycopg2.extensions.register_type(psycopg2.extensions.UNICODEARRAY);
else:
    unicode = str;  # pylint: disable=redefined-builtin,invalid-name



def isDbTimestampInfinity(tsValue):
    """
    Checks if tsValue is an infinity timestamp.
    """
    ## @todo improve this test...
    return tsValue.year >= 9999;

def isDbTimestamp(oValue):
    """
    Checks if oValue is a DB timestamp object.
    """
    if isinstance(oValue, datetime.datetime):
        return True;
    if utils.isString(oValue):
        ## @todo detect strings as well.
        return False;
    return getattr(oValue, 'pydatetime', None) is not None;

def dbTimestampToDatetime(oValue):
    """
    Converts a database timestamp to a datetime instance.
    """
    if isinstance(oValue, datetime.datetime):
        return oValue;
    if utils.isString(oValue):
        return utils.parseIsoTimestamp(oValue);
    return oValue.pydatetime();

def dbTimestampToZuluDatetime(oValue):
    """
    Converts a database timestamp to a zulu datetime instance.
    """
    tsValue = dbTimestampToDatetime(oValue);

    class UTC(datetime.tzinfo):
        """UTC TZ Info Class"""
        def utcoffset(self, _):
            return datetime.timedelta(0);
        def tzname(self, _):
            return "UTC";
        def dst(self, _):
            return datetime.timedelta(0);
    if tsValue.tzinfo is not None:
        tsValue = tsValue.astimezone(UTC());
    else:
        tsValue = tsValue.replace(tzinfo=UTC());
    return tsValue;

def dbTimestampPythonNow():
    """
    Gets the current python timestamp in a database compatible way.
    """
    return dbTimestampToZuluDatetime(datetime.datetime.utcnow());

def dbOneTickIntervalString():
    """
    Returns the interval string for one tick.

    Mogrify the return value into the SQL:
        "... %s::INTERVAL ..."
    or
        "INTERVAL %s"
    The completed SQL will contain the necessary ticks.
    """
    return '1 microsecond';

def dbTimestampMinusOneTick(oValue):
    """
    Returns a new timestamp that's one tick before the given one.
    """
    oValue = dbTimestampToZuluDatetime(oValue);
    return oValue - datetime.timedelta(microseconds = 1);

def dbTimestampPlusOneTick(oValue):
    """
    Returns a new timestamp that's one tick after the given one.
    """
    oValue = dbTimestampToZuluDatetime(oValue);
    return oValue + datetime.timedelta(microseconds = 1);

def isDbInterval(oValue):
    """
    Checks if oValue is a DB interval object.
    """
    if isinstance(oValue, datetime.timedelta):
        return True;
    return False;


class TMDatabaseIntegrityException(Exception):
    """
    Herolds a database integrity error up the callstack.

    Do NOT use directly, only thru TMDatabaseConnection.integrityException.
    Otherwise, we won't be able to log the issue.
    """
    pass;                               # pylint: disable=unnecessary-pass


class TMDatabaseCursor(object):
    """ Cursor wrapper class. """

    def __init__(self, oDb, oCursor):
        self._oDb     = oDb;
        self._oCursor = oCursor;

    def execute(self, sOperation, aoArgs = None):
        """ See TMDatabaseConnection.execute()"""
        return self._oDb.executeInternal(self._oCursor, sOperation, aoArgs, utils.getCallerName());

    def callProc(self, sProcedure, aoArgs = None):
        """ See TMDatabaseConnection.callProc()"""
        return self._oDb.callProcInternal(self._oCursor, sProcedure, aoArgs, utils.getCallerName());

    def insertList(self, sInsertSql, aoList, fnEntryFmt):
        """ See TMDatabaseConnection.insertList. """
        return self._oDb.insertListInternal(self._oCursor, sInsertSql, aoList, fnEntryFmt, utils.getCallerName());

    def fetchOne(self):
        """Wrapper around Psycopg2.cursor.fetchone."""
        return self._oCursor.fetchone();

    def fetchMany(self, cRows = None):
        """Wrapper around Psycopg2.cursor.fetchmany."""
        return self._oCursor.fetchmany(cRows if cRows is not None else self._oCursor.arraysize);

    def fetchAll(self):
        """Wrapper around Psycopg2.cursor.fetchall."""
        return self._oCursor.fetchall();

    def getRowCount(self):
        """Wrapper around Psycopg2.cursor.rowcount."""
        return self._oCursor.rowcount;

    def formatBindArgs(self, sStatement, aoArgs):
        """Wrapper around Psycopg2.cursor.mogrify."""
        oRet = self._oCursor.mogrify(sStatement, aoArgs);
        if sys.version_info[0] >= 3 and not isinstance(oRet, str):
            oRet = oRet.decode('utf-8');
        return oRet;

    def copyExpert(self, sSqlCopyStmt, oFile, cbBuf = 8192):
        """ See TMDatabaseConnection.copyExpert()"""
        return self._oCursor.copy_expert(sSqlCopyStmt, oFile, cbBuf);

    @staticmethod
    def isTsInfinity(tsValue):
        """ Checks if tsValue is an infinity timestamp. """
        return isDbTimestampInfinity(tsValue);


class TMDatabaseConnection(object):
    """
    Test Manager Database Access class.

    This class contains no logic, just raw access abstraction and utilities,
    as well as some debug help and some statistics.
    """

    def __init__(self, fnDPrint = None, oSrvGlue = None):
        """
        Database connection wrapper.
        The fnDPrint is for debug logging of all database activity.

        Raises an exception on failure.
        """

        sAppName = '%s-%s' % (os.getpid(), os.path.basename(sys.argv[0]),)
        if len(sAppName) >= 64:
            sAppName = sAppName[:64];
        os.environ['PGAPPNAME'] = sAppName;

        dArgs = \
        { \
            'database': config.g_ksDatabaseName,
            'user':     config.g_ksDatabaseUser,
            'password': config.g_ksDatabasePassword,
        #    'application_name': sAppName, - Darn stale debian! :/
        };
        if config.g_ksDatabaseAddress is not None:
            dArgs['host'] = config.g_ksDatabaseAddress;
        if config.g_ksDatabasePort is not None:
            dArgs['port'] = config.g_ksDatabasePort;
        self._oConn             = psycopg2.connect(**dArgs); # pylint: disable=star-args
        self._oConn.set_client_encoding('UTF-8');
        self._oCursor           = self._oConn.cursor();
        self._oExplainConn      = None;
        self._oExplainCursor    = None;
        if config.g_kfWebUiSqlTraceExplain and config.g_kfWebUiSqlTrace:
            self._oExplainConn  = psycopg2.connect(**dArgs); # pylint: disable=star-args
            self._oExplainConn.set_client_encoding('UTF-8');
            self._oExplainCursor = self._oExplainConn.cursor();
        self._fTransaction      = False;
        self._tsCurrent         = None;
        self._tsCurrentMinusOne = None;

        assert self.isAutoCommitting() is False;

        # Debug and introspection.
        self._fnDPrint              = fnDPrint;
        self._aoTraceBack           = [];

        # Exception class handles.
        self.oXcptError         = psycopg2.Error;

        if oSrvGlue is not None:
            oSrvGlue.registerDebugInfoCallback(self.debugInfoCallback);

        # Object caches (used by database logic classes).
        self.ddCaches = {};

    def isAutoCommitting(self):
        """ Work around missing autocommit attribute in older versions."""
        return getattr(self._oConn, 'autocommit', False);

    def close(self):
        """
        Closes the connection and renders all cursors useless.
        """
        if self._oCursor is not None:
            self._oCursor.close();
            self._oCursor = None;

        if self._oConn is not None:
            self._oConn.close();
            self._oConn = None;

        if self._oExplainCursor is not None:
            self._oExplainCursor.close();
            self._oExplainCursor = None;

        if self._oExplainConn is not None:
            self._oExplainConn.close();
            self._oExplainConn = None;


    def _startedTransaction(self):
        """
        Called to work the _fTransaction and related variables when starting
        a transaction.
        """
        self._fTransaction      = True;
        self._tsCurrent         = None;
        self._tsCurrentMinusOne = None;
        return None;

    def _endedTransaction(self):
        """
        Called to work the _fTransaction and related variables when ending
        a transaction.
        """
        self._fTransaction      = False;
        self._tsCurrent         = None;
        self._tsCurrentMinusOne = None;
        return None;

    def begin(self):
        """
        Currently just for marking where a transaction starts in the code.
        """
        assert self._oConn is not None;
        assert self.isAutoCommitting() is False;
        self._aoTraceBack.append([utils.timestampNano(), 'START TRANSACTION', 0, 0, utils.getCallerName(), None]);
        self._startedTransaction();
        return True;

    def commit(self, sCallerName = None):
        """ Wrapper around Psycopg2.connection.commit."""
        assert self._fTransaction is True;

        nsStart = utils.timestampNano();
        oRc = self._oConn.commit();
        cNsElapsed = utils.timestampNano() - nsStart;

        if sCallerName is None:
            sCallerName = utils.getCallerName();
        self._aoTraceBack.append([nsStart, 'COMMIT', cNsElapsed, 0, sCallerName, None]);
        self._endedTransaction();
        return oRc;

    def maybeCommit(self, fCommit):
        """
        Commits if fCommit is True.
        Returns True if committed, False if not.
        """
        if fCommit is True:
            self.commit(utils.getCallerName());
            return True;
        return False;

    def rollback(self):
        """ Wrapper around Psycopg2.connection.rollback."""
        nsStart = utils.timestampNano();
        oRc = self._oConn.rollback();
        cNsElapsed = utils.timestampNano() - nsStart;

        self._aoTraceBack.append([nsStart, 'ROLLBACK', cNsElapsed, 0, utils.getCallerName(), None]);
        self._endedTransaction();
        return oRc;

    #
    # Internal cursor workers.
    #

    def executeInternal(self, oCursor, sOperation, aoArgs, sCallerName):
        """
        Execute a query or command.

        Mostly a wrapper around the psycopg2 cursor method with the same name,
        but collect data for traceback.
        """
        if aoArgs is not None:
            sBound = oCursor.mogrify(unicode(sOperation), aoArgs);
        elif sOperation.find('%') < 0:
            sBound = oCursor.mogrify(unicode(sOperation), []);
        else:
            sBound = unicode(sOperation);

        if sys.version_info[0] >= 3 and not isinstance(sBound, str):
            sBound = sBound.decode('utf-8'); # pylint: disable=redefined-variable-type

        aasExplain = None;
        if self._oExplainCursor is not None and not sBound.startswith('DROP'):
            try:
                if config.g_kfWebUiSqlTraceExplainTiming:
                    self._oExplainCursor.execute('EXPLAIN (ANALYZE, BUFFERS, COSTS, VERBOSE, TIMING) ' + sBound);
                else:
                    self._oExplainCursor.execute('EXPLAIN (ANALYZE, BUFFERS, COSTS, VERBOSE) ' + sBound);
            except Exception as oXcpt:
                aasExplain = [ ['Explain exception: '], [str(oXcpt)]];
                try:    self._oExplainConn.rollback();
                except: pass;
            else:
                aasExplain = self._oExplainCursor.fetchall();

        nsStart = utils.timestampNano();
        try:
            oRc = oCursor.execute(sBound);
        except Exception as oXcpt:
            cNsElapsed = utils.timestampNano() - nsStart;
            self._aoTraceBack.append([nsStart, 'oXcpt=%s; Statement: %s' % (oXcpt, sBound), cNsElapsed, 0, sCallerName, None]);
            if self._fnDPrint is not None:
                self._fnDPrint('db::execute %u ns, caller %s: oXcpt=%s; Statement: %s'
                               % (cNsElapsed, sCallerName, oXcpt, sBound));
            raise;
        cNsElapsed = utils.timestampNano() - nsStart;

        if self._fTransaction is False and not self.isAutoCommitting(): # Even SELECTs starts transactions with psycopg2, see FAQ.
            self._aoTraceBack.append([nsStart, '[START TRANSACTION]', 0, 0, sCallerName, None]);
            self._startedTransaction();
        self._aoTraceBack.append([nsStart, sBound, cNsElapsed, oCursor.rowcount, sCallerName, aasExplain]);
        if self._fnDPrint is not None:
            self._fnDPrint('db::execute %u ns, caller %s: "\n%s"' % (cNsElapsed, sCallerName, sBound));
        if self.isAutoCommitting():
            self._aoTraceBack.append([nsStart, '[AUTO COMMIT]', 0, 0, sCallerName, None]);

        return oRc;

    def callProcInternal(self, oCursor, sProcedure, aoArgs, sCallerName):
        """
        Call a stored procedure.

        Mostly a wrapper around the psycopg2 cursor method 'callproc', but
        collect data for traceback.
        """
        if aoArgs is None:
            aoArgs = [];

        nsStart = utils.timestampNano();
        try:
            oRc = oCursor.callproc(sProcedure, aoArgs);
        except Exception as oXcpt:
            cNsElapsed = utils.timestampNano() - nsStart;
            self._aoTraceBack.append([nsStart, 'oXcpt=%s; Calling: %s(%s)' % (oXcpt, sProcedure, aoArgs),
                                      cNsElapsed, 0, sCallerName, None]);
            if self._fnDPrint is not None:
                self._fnDPrint('db::callproc %u ns, caller %s: oXcpt=%s; Calling: %s(%s)'
                               % (cNsElapsed, sCallerName, oXcpt, sProcedure, aoArgs));
            raise;
        cNsElapsed = utils.timestampNano() - nsStart;

        if self._fTransaction is False and not self.isAutoCommitting(): # Even SELECTs starts transactions with psycopg2, see FAQ.
            self._aoTraceBack.append([nsStart, '[START TRANSACTION]', 0, 0, sCallerName, None]);
            self._startedTransaction();
        self._aoTraceBack.append([nsStart, '%s(%s)' % (sProcedure, aoArgs), cNsElapsed, oCursor.rowcount, sCallerName, None]);
        if self._fnDPrint is not None:
            self._fnDPrint('db::callproc %u ns, caller %s: "%s(%s)"' % (cNsElapsed, sCallerName, sProcedure, aoArgs));
        if self.isAutoCommitting():
            self._aoTraceBack.append([nsStart, '[AUTO COMMIT]', 0, 0, sCallerName, sCallerName, None]);

        return oRc;

    def insertListInternal(self, oCursor, sInsertSql, aoList, fnEntryFmt, sCallerName):
        """
        Optimizes the insertion of a list of values.
        """
        oRc = None;
        asValues = [];
        for aoEntry in aoList:
            asValues.append(fnEntryFmt(aoEntry));
            if len(asValues) > 256:
                oRc = self.executeInternal(oCursor, sInsertSql + 'VALUES' + ', '.join(asValues), None, sCallerName);
                asValues = [];
        if asValues:
            oRc = self.executeInternal(oCursor, sInsertSql + 'VALUES' + ', '.join(asValues), None, sCallerName);
        return oRc

    def _fetchOne(self, oCursor):
        """Wrapper around Psycopg2.cursor.fetchone."""
        oRow = oCursor.fetchone()
        if self._fnDPrint is not None:
            self._fnDPrint('db:fetchOne returns: %s' % (oRow,));
        return oRow;

    def _fetchMany(self, oCursor, cRows):
        """Wrapper around Psycopg2.cursor.fetchmany."""
        return oCursor.fetchmany(cRows if cRows is not None else oCursor.arraysize);

    def _fetchAll(self, oCursor):
        """Wrapper around Psycopg2.cursor.fetchall."""
        return oCursor.fetchall()

    def _getRowCountWorker(self, oCursor):
        """Wrapper around Psycopg2.cursor.rowcount."""
        return oCursor.rowcount;


    #
    # Default cursor access.
    #

    def execute(self, sOperation, aoArgs = None):
        """
        Execute a query or command.

        Mostly a wrapper around the psycopg2 cursor method with the same name,
        but collect data for traceback.
        """
        return self.executeInternal(self._oCursor, sOperation, aoArgs, utils.getCallerName());

    def callProc(self, sProcedure, aoArgs = None):
        """
        Call a stored procedure.

        Mostly a wrapper around the psycopg2 cursor method 'callproc', but
        collect data for traceback.
        """
        return self.callProcInternal(self._oCursor, sProcedure, aoArgs, utils.getCallerName());

    def insertList(self, sInsertSql, aoList, fnEntryFmt):
        """
        Optimizes the insertion of a list of values.
        """
        return self.insertListInternal(self._oCursor, sInsertSql, aoList, fnEntryFmt, utils.getCallerName());

    def fetchOne(self):
        """Wrapper around Psycopg2.cursor.fetchone."""
        return self._oCursor.fetchone();

    def fetchMany(self, cRows = None):
        """Wrapper around Psycopg2.cursor.fetchmany."""
        return self._oCursor.fetchmany(cRows if cRows is not None else self._oCursor.arraysize);

    def fetchAll(self):
        """Wrapper around Psycopg2.cursor.fetchall."""
        return self._oCursor.fetchall();

    def getRowCount(self):
        """Wrapper around Psycopg2.cursor.rowcount."""
        return self._oCursor.rowcount;

    def formatBindArgs(self, sStatement, aoArgs):
        """Wrapper around Psycopg2.cursor.mogrify."""
        oRet = self._oCursor.mogrify(sStatement, aoArgs);
        if sys.version_info[0] >= 3 and not isinstance(oRet, str):
            oRet = oRet.decode('utf-8');
        return oRet;

    def copyExpert(self, sSqlCopyStmt, oFile, cbBuf = 8192):
        """ Wrapper around Psycopg2.cursor.copy_expert. """
        return self._oCursor.copy_expert(sSqlCopyStmt, oFile, cbBuf);

    def getCurrentTimestamps(self):
        """
        Returns the current timestamp and the current timestamp minus one tick.
        This will start a transaction if necessary.
        """
        if self._tsCurrent is None:
            self.execute('SELECT CURRENT_TIMESTAMP, CURRENT_TIMESTAMP - INTERVAL \'1 microsecond\'');
            (self._tsCurrent, self._tsCurrentMinusOne) = self.fetchOne();
        return (self._tsCurrent, self._tsCurrentMinusOne);

    def getCurrentTimestamp(self):
        """
        Returns the current timestamp.
        This will start a transaction if necessary.
        """
        if self._tsCurrent is None:
            self.getCurrentTimestamps();
        return self._tsCurrent;

    def getCurrentTimestampMinusOne(self):
        """
        Returns the current timestamp minus one tick.
        This will start a transaction if necessary.
        """
        if self._tsCurrentMinusOne is None:
            self.getCurrentTimestamps();
        return self._tsCurrentMinusOne;


    #
    # Additional cursors.
    #
    def openCursor(self):
        """
        Opens a new cursor (TMDatabaseCursor).
        """
        oCursor = self._oConn.cursor();
        return TMDatabaseCursor(self, oCursor);

    #
    # Cache support.
    #
    def getCache(self, sType):
        """ Returns the cache dictionary for this data type. """
        dRet = self.ddCaches.get(sType, None);
        if dRet is None:
            dRet = {};
            self.ddCaches[sType] = dRet;
        return dRet;


    #
    # Utilities.
    #

    @staticmethod
    def isTsInfinity(tsValue):
        """ Checks if tsValue is an infinity timestamp. """
        return isDbTimestampInfinity(tsValue);

    #
    # Error stuff.
    #
    def integrityException(self, sMessage):
        """
        Database integrity reporter and exception factory.
        Returns an TMDatabaseIntegrityException which the caller can raise.
        """
        ## @todo Create a new database connection and log the issue in the SystemLog table.
        ##       Alternatively, rollback whatever is going on and do it using the current one.
        return TMDatabaseIntegrityException(sMessage);


    #
    # Debugging.
    #

    def dprint(self, sText):
        """
        Debug output.
        """
        if not self._fnDPrint:
            return False;
        self._fnDPrint(sText);
        return True;

    def debugHtmlReport(self, tsStart = 0):
        """
        Used to get a SQL activity dump as HTML, usually for WuiBase._sDebug.
        """
        cNsElapsed = 0;
        for aEntry in self._aoTraceBack:
            cNsElapsed += aEntry[2];

        sDebug = '<h3>SQL Debug Log (total time %s ns):</h3>\n' \
                 '<table class="tmsqltable">\n' \
                 ' <tr>\n' \
                 '  <th>No.</th>\n' \
                 '  <th>Timestamp (ns)</th>\n' \
                 '  <th>Elapsed (ns)</th>\n' \
                 '  <th>Rows Returned</th>\n' \
                 '  <th>Command</th>\n' \
                 '  <th>Caller</th>\n' \
                 ' </tr>\n' \
               % (utils.formatNumber(cNsElapsed, '&nbsp;'),);

        iEntry = 0;
        for aEntry in self._aoTraceBack:
            iEntry += 1;
            sDebug += ' <tr>\n' \
                      '  <td>%s</td>\n' \
                      '  <td>%s</td>\n' \
                      '  <td>%s</td>\n' \
                      '  <td>%s</td>\n' \
                      '  <td><pre>%s</pre></td>\n' \
                      '  <td>%s</td>\n' \
                      ' </tr>\n' \
                    % (iEntry,
                       utils.formatNumber(aEntry[0] - tsStart, '&nbsp;'),
                       utils.formatNumber(aEntry[2], '&nbsp;'),
                       utils.formatNumber(aEntry[3], '&nbsp;'),
                       webutils.escapeElem(aEntry[1]),
                       webutils.escapeElem(aEntry[4]),
                    );
            if aEntry[5] is not None:
                sDebug += ' <tr>\n' \
                          '  <td colspan="6"><pre style="white-space: pre-wrap;">%s</pre></td>\n' \
                          ' </tr>\n' \
                            % (webutils.escapeElem('\n'.join([aoRow[0] for aoRow in aEntry[5]])),);

        sDebug += '</table>';
        return sDebug;

    def debugTextReport(self, tsStart = 0):
        """
        Used to get a SQL activity dump as text.
        """
        cNsElapsed = 0;
        for aEntry in self._aoTraceBack:
            cNsElapsed += aEntry[2];

        sHdr = 'SQL Debug Log (total time %s ns)' % (utils.formatNumber(cNsElapsed),);
        sDebug = sHdr + '\n' + '-' * len(sHdr) + '\n';

        iEntry = 0;
        for aEntry in self._aoTraceBack:
            iEntry += 1;
            sHdr = 'Query #%s  Timestamp: %s ns  Elapsed: %s ns  Rows: %s  Caller: %s' \
                % ( iEntry,
                    utils.formatNumber(aEntry[0] - tsStart),
                    utils.formatNumber(aEntry[2]),
                    utils.formatNumber(aEntry[3]),
                    aEntry[4], );
            sDebug += '\n' + sHdr + '\n' + '-' * len(sHdr) + '\n';

            sDebug += aEntry[1];
            if sDebug[-1] != '\n':
                sDebug += '\n';

            if aEntry[5] is not None:
                sDebug += 'Explain:\n' \
                          '  %s\n' \
                        % ( '\n'.join([aoRow[0] for aoRow in aEntry[5]]),);

        return sDebug;

    def debugInfoCallback(self, oGlue, fHtml):
        """ Called back by the glue code on error. """
        oGlue.write('\n');
        if not fHtml:   oGlue.write(self.debugTextReport());
        else:           oGlue.write(self.debugHtmlReport());
        oGlue.write('\n');
        return True;

    def debugEnableExplain(self):
        """ Enabled explain. """
        if self._oExplainConn is None:
            dArgs = \
            { \
                'database': config.g_ksDatabaseName,
                'user':     config.g_ksDatabaseUser,
                'password': config.g_ksDatabasePassword,
            #    'application_name': sAppName, - Darn stale debian! :/
            };
            if config.g_ksDatabaseAddress is not None:
                dArgs['host'] = config.g_ksDatabaseAddress;
            if config.g_ksDatabasePort is not None:
                dArgs['port'] = config.g_ksDatabasePort;
            self._oExplainConn  = psycopg2.connect(**dArgs); # pylint: disable=star-args
            self._oExplainCursor = self._oExplainConn.cursor();
        return True;

    def debugDisableExplain(self):
        """ Disables explain. """
        self._oExplainCursor = None;
        self._oExplainConn   = None
        return True;

    def debugIsExplainEnabled(self):
        """ Check if explaining of SQL statements is enabled. """
        return self._oExplainConn is not None;

