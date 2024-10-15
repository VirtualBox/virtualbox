# -*- coding: utf-8 -*-
# $Id: restdispatcher.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
Test Manager Core - REST cgi handler.
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

# Validation Kit imports.
#from common                             import constants;
from common                             import utils;
from testmanager                        import config;
#from testmanager.core                   import coreconsts;
from testmanager.core.db                import TMDatabaseConnection;
from testmanager.core.base              import TMExceptionBase, ModelDataBase;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int;     # pylint: disable=redefined-builtin,invalid-name


#
# Exceptions
#

class RestDispException(TMExceptionBase):
    """
    Exception class for the REST dispatcher.
    """
    def __init__(self, sMsg, iStatus):
        TMExceptionBase.__init__(self, sMsg);
        self.iStatus = iStatus;

# 400
class RestDispException400(RestDispException):
    """ A 400 error """
    def __init__(self, sMsg):
        RestDispException.__init__(self, sMsg, 400);

class RestUnknownParameters(RestDispException400):
    """ Unknown parameter(s). """
    pass;                               # pylint: disable=unnecessary-pass

# 404
class RestDispException404(RestDispException):
    """ A 404 error """
    def __init__(self, sMsg):
        RestDispException.__init__(self, sMsg, 404);

class RestBadPathException(RestDispException404):
    """ We've got a bad path. """
    pass;                               # pylint: disable=unnecessary-pass

class RestBadParameter(RestDispException404):
    """ Bad parameter. """
    pass;                               # pylint: disable=unnecessary-pass

class RestMissingParameter(RestDispException404):
    """ Missing parameter. """
    pass;                               # pylint: disable=unnecessary-pass



class RestMain(object): # pylint: disable=too-few-public-methods
    """
    REST main dispatcher class.
    """

    ksParam_sPath = 'sPath';


    def __init__(self, oSrvGlue):
        self._oSrvGlue          = oSrvGlue;
        self._oDb               = TMDatabaseConnection(oSrvGlue.dprint);
        self._iFirstHandlerPath = 0;
        self._iNextHandlerPath  = 0;
        self._sPath             = None; # _getStandardParams / dispatchRequest sets this later on.
        self._asPath            = None; # _getStandardParams / dispatchRequest sets this later on.
        self._sMethod           = None; # _getStandardParams / dispatchRequest sets this later on.
        self._dParams           = None; # _getStandardParams / dispatchRequest sets this later on.
        self._asCheckedParams   = [];
        self._dGetTree          = {
            'vcs': {
                'changelog': self._handleVcsChangelog_Get,
                'bugreferences': self._handleVcsBugReferences_Get,
            },
        };
        self._dMethodTrees      = {
            'GET': self._dGetTree,
        }

    #
    # Helpers.
    #

    def _getStringParam(self, sName, asValidValues = None, fStrip = False, sDefValue = None):
        """
        Gets a string parameter (stripped).

        Raises exception if not found and no default is provided, or if the
        value isn't found in asValidValues.
        """
        if sName not in self._dParams:
            if sDefValue is None:
                raise RestMissingParameter('%s parameter %s is missing' % (self._sPath, sName));
            return sDefValue;
        sValue = self._dParams[sName];
        if isinstance(sValue, list):
            if len(sValue) == 1:
                sValue = sValue[0];
            else:
                raise RestBadParameter('%s parameter %s value is not a string but list: %s'
                                       % (self._sPath, sName, sValue));
        if fStrip:
            sValue = sValue.strip();

        if sName not in self._asCheckedParams:
            self._asCheckedParams.append(sName);

        if asValidValues is not None and sValue not in asValidValues:
            raise RestBadParameter('%s parameter %s value "%s" not in %s '
                                   % (self._sPath, sName, sValue, asValidValues));
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
            raise RestBadParameter('%s parameter %s value "%s" cannot be convert to an integer'
                                   % (self._sPath, sName, sValue));

        if   (iMin is not None and iValue < iMin) \
          or (iMax is not None and iValue > iMax):
            raise RestBadParameter('%s parameter %s value %d is out of range [%s..%s]'
                                   % (self._sPath, sName, iValue, iMin, iMax));
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
            raise RestBadParameter('%s parameter %s value "%s" cannot be convert to an integer (%s)'
                                   % (self._sPath, sName, sValue, oXcpt));

        if   (lMin is not None and lValue < lMin) \
          or (lMax is not None and lValue > lMax):
            raise RestBadParameter('%s parameter %s value %d is out of range [%s..%s]'
                                   % (self._sPath, sName, lValue, lMin, lMax));
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
            raise RestUnknownParameters('Unknown parameters: ' + sUnknownParams);

        return True;

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

    def _getNextPathElementString(self, sName, oDefault = None):
        """
        Gets the next handler specific path element.
        Returns unprocessed string.
        Throws exception
        """
        i = self._iNextHandlerPath;
        if i < len(self._asPath):
            self._iNextHandlerPath = i + 1;
            return self._asPath[i];
        if oDefault is None:
            raise RestBadPathException('Requires a "%s" element after "%s"' % (sName, self._sPath,));
        return oDefault;

    def _getNextPathElementInt(self, sName, iDefault = None, iMin = None, iMax = None):
        """
        Gets the next handle specific path element as an integer.
        Returns integer value.
        Throws exception if not found or not a valid integer.
        """
        sValue = self._getNextPathElementString(sName, oDefault = iDefault);
        try:
            iValue = int(sValue);
        except:
            raise RestBadPathException('Not an integer "%s" (%s)' % (sValue, sName,));
        if iMin is not None and iValue < iMin:
            raise RestBadPathException('Integer "%s" value (%s) is too small, min %s' % (sValue, sName, iMin));
        if iMax is not None and iValue > iMax:
            raise RestBadPathException('Integer "%s" value (%s) is too large, max %s' % (sValue, sName, iMax));
        return iValue;

    def _getNextPathElementLong(self, sName, iDefault = None, iMin = None, iMax = None):
        """
        Gets the next handle specific path element as a long integer.
        Returns integer value.
        Throws exception if not found or not a valid integer.
        """
        sValue = self._getNextPathElementString(sName, oDefault = iDefault);
        try:
            iValue = long(sValue);
        except:
            raise RestBadPathException('Not an integer "%s" (%s)' % (sValue, sName,));
        if iMin is not None and iValue < iMin:
            raise RestBadPathException('Integer "%s" value (%s) is too small, min %s' % (sValue, sName, iMin));
        if iMax is not None and iValue > iMax:
            raise RestBadPathException('Integer "%s" value (%s) is too large, max %s' % (sValue, sName, iMax));
        return iValue;

    def _checkNoMorePathElements(self):
        """
        Checks that there are no more path elements.
        Throws exception if there are.
        """
        i = self._iNextHandlerPath;
        if i < len(self._asPath):
            raise RestBadPathException('Unknown subpath "%s" below "%s"' %
                                       ('/'.join(self._asPath[i:]), '/'.join(self._asPath[:i]),));
        return True;

    def _doneParsingArguments(self):
        """
        Checks that there are no more path elements or unhandled parameters.
        Throws exception if there are.
        """
        self._checkNoMorePathElements();
        self._checkForUnknownParameters();
        return True;

    def _dataArrayToJsonReply(self, aoData, sName = 'aoData', dExtraFields = None, iStatus = 200):
        """
        Converts aoData into an array objects
        return True.
        """
        self._oSrvGlue.setContentType('application/json');
        self._oSrvGlue.setStatus(iStatus);
        self._oSrvGlue.write(u'{\n');
        if dExtraFields:
            for sKey in dExtraFields:
                self._oSrvGlue.write(u'  "%s": %s,\n' % (sKey, ModelDataBase.genericToJson(dExtraFields[sKey]),));
        self._oSrvGlue.write(u'  "c%s": %u,\n' % (sName[2:],len(aoData),));
        self._oSrvGlue.write(u'  "%s": [\n' % (sName,));
        for i, oData in enumerate(aoData):
            if i > 0:
                self._oSrvGlue.write(u',\n');
            self._oSrvGlue.write(ModelDataBase.genericToJson(oData));
        self._oSrvGlue.write(u'  ]\n');
        ## @todo if config.g_kfWebUiSqlDebug:
        self._oSrvGlue.write(u'}\n');
        self._oSrvGlue.flush();
        return True;


    #
    # Handlers.
    #

    def _handleVcsChangelog_Get(self):
        """ GET /vcs/changelog/{sRepository}/{iStartRev}[/{cEntriesBack}] """
        # Parse arguments
        sRepository  = self._getNextPathElementString('sRepository');
        iStartRev    = self._getNextPathElementInt('iStartRev', iMin = 0);
        cEntriesBack = self._getNextPathElementInt('cEntriesBack', iDefault = 32, iMin = 0, iMax = 8192);
        self._checkNoMorePathElements();
        self._checkForUnknownParameters();

        # Execute it.
        from testmanager.core.vcsrevisions import VcsRevisionLogic;
        oLogic = VcsRevisionLogic(self._oDb);
        return self._dataArrayToJsonReply(oLogic.fetchTimeline(sRepository, iStartRev, cEntriesBack), 'aoCommits',
                                          { 'sTracChangesetUrlFmt':
                                            config.g_ksTracChangsetUrlFmt.replace('%(sRepository)s', sRepository), } );

    def _handleVcsBugReferences_Get(self):
        """ GET /vcs/bugreferences/{sTrackerId}/{lBugId} """
        # Parse arguments
        sTrackerId   = self._getNextPathElementString('sTrackerId');
        lBugId       = self._getNextPathElementLong('lBugId', iMin = 0);
        self._checkNoMorePathElements();
        self._checkForUnknownParameters();

        # Execute it.
        from testmanager.core.vcsbugreference import VcsBugReferenceLogic;
        oLogic = VcsBugReferenceLogic(self._oDb);
        oLogic.fetchForBug(sTrackerId, lBugId)
        return self._dataArrayToJsonReply(oLogic.fetchForBug(sTrackerId, lBugId), 'aoCommits',
                                          { 'sTracChangesetUrlFmt': config.g_ksTracChangsetUrlFmt, } );


    #
    # Dispatching.
    #

    def _dispatchRequestCommon(self):
        """
        Dispatches the incoming request after have gotten the path and parameters.

        Will raise RestDispException on failure.
        """

        #
        # Split up the path.
        #
        asPath = self._sPath.split('/');
        self._asPath = asPath;

        #
        # Get the method and the corresponding handler tree.
        #
        try:
            sMethod = self._oSrvGlue.getMethod();
        except Exception as oXcpt:
            raise RestDispException('Error retriving request method: %s' % (oXcpt,), 400);
        self._sMethod = sMethod;

        try:
            dTree = self._dMethodTrees[sMethod];
        except KeyError:
            raise RestDispException('Unsupported method %s' % (sMethod,), 405);

        #
        # Walk the path till we find a handler for it.
        #
        iPath = 0;
        while iPath < len(asPath):
            try:
                oTreeOrHandler = dTree[asPath[iPath]];
            except KeyError:
                raise RestBadPathException('Path element #%u "%s" not found (path="%s")' % (iPath, asPath[iPath], self._sPath));
            iPath += 1;
            if isinstance(oTreeOrHandler, dict):
                dTree = oTreeOrHandler;
            else:
                #
                # Call the handler.
                #
                self._iFirstHandlerPath = iPath;
                self._iNextHandlerPath  = iPath;
                return oTreeOrHandler();

        raise RestBadPathException('Empty path (%s)' % (self._sPath,));

    def dispatchRequest(self):
        """
        Dispatches the incoming request where the path is given as an argument.

        Will raise RestDispException on failure.
        """

        #
        # Get the parameters.
        #
        try:
            dParams = self._oSrvGlue.getParameters();
        except Exception as oXcpt:
            raise RestDispException('Error retriving parameters: %s' % (oXcpt,), 500);
        self._dParams = dParams;

        #
        # Get the path parameter.
        #
        if self.ksParam_sPath not in dParams:
            raise RestDispException('No "%s" parameter in request (params: %s)' % (self.ksParam_sPath, dParams,), 500);
        self._sPath = self._getStringParam(self.ksParam_sPath);
        assert utils.isString(self._sPath);

        return self._dispatchRequestCommon();

