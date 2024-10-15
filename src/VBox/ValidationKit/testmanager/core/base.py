# -*- coding: utf-8 -*-
# $Id: base.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
# pylint: disable=too-many-lines

"""
Test Manager Core - Base Class(es).
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
import copy;
import datetime;
import json;
import re;
import socket;
import sys;
import uuid;
import unittest;

# Validation Kit imports.
from common import utils;

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int      # pylint: disable=redefined-builtin,invalid-name


class TMExceptionBase(Exception):
    """
    For exceptions raised by any TestManager component.
    """
    pass;                               # pylint: disable=unnecessary-pass


class TMTooManyRows(TMExceptionBase):
    """
    Too many rows in the result.
    Used by ModelLogicBase decendants.
    """
    pass;                               # pylint: disable=unnecessary-pass


class TMRowNotFound(TMExceptionBase):
    """
    Database row not found.
    Used by ModelLogicBase decendants.
    """
    pass;                               # pylint: disable=unnecessary-pass


class TMRowAlreadyExists(TMExceptionBase):
    """
    Database row already exists (typically raised by addEntry).
    Used by ModelLogicBase decendants.
    """
    pass;                               # pylint: disable=unnecessary-pass


class TMInvalidData(TMExceptionBase):
    """
    Data validation failed.
    Used by ModelLogicBase decendants.
    """
    pass;                               # pylint: disable=unnecessary-pass


class TMRowInUse(TMExceptionBase):
    """
    Database row is in use and cannot be deleted.
    Used by ModelLogicBase decendants.
    """
    pass;                               # pylint: disable=unnecessary-pass


class TMInFligthCollision(TMExceptionBase):
    """
    Database update failed because someone else had already made changes to
    the data there.
    Used by ModelLogicBase decendants.
    """
    pass;                               # pylint: disable=unnecessary-pass


class ModelBase(object): # pylint: disable=too-few-public-methods
    """
    Something all classes in the logical model inherits from.

    Not sure if 'logical model' is the right term here.
    Will see if it has any purpose later on...
    """

    def __init__(self):
        pass;


class ModelDataBase(ModelBase): # pylint: disable=too-few-public-methods
    """
    Something all classes in the data classes in the logical model inherits from.
    """

    ## Child classes can use this to list array attributes which should use
    # an empty array ([]) instead of None as database NULL value.
    kasAltArrayNull = [];

    ## validate
    ## @{
    ksValidateFor_Add           = 'add';
    ksValidateFor_AddForeignId  = 'add-foreign-id';
    ksValidateFor_Edit          = 'edit';
    ksValidateFor_Other         = 'other';
    ## @}


    ## List of internal attributes which should be ignored by
    ## getDataAttributes and related machinery
    kasInternalAttributes       = [];

    def __init__(self):
        ModelBase.__init__(self);


    #
    # Standard methods implemented by combining python magic and hungarian prefixes.
    #

    def getDataAttributes(self):
        """
        Returns a list of data attributes.
        """
        asRet   = [];
        asAttrs = dir(self);
        for sAttr in asAttrs:
            if sAttr[0] == '_' or sAttr[0] == 'k':
                continue;
            if sAttr in self.kasInternalAttributes:
                continue;
            oValue = getattr(self, sAttr);
            if callable(oValue):
                continue;
            asRet.append(sAttr);
        return asRet;

    def initFromOther(self, oOther):
        """
        Initialize this object with the values from another instance (child
        class instance is accepted).

        This serves as a kind of copy constructor.

        Returns self.  May raise exception if the type of other object differs
        or is damaged.
        """
        for sAttr in self.getDataAttributes():
            setattr(self, sAttr, getattr(oOther, sAttr));
        return self;

    @staticmethod
    def getHungarianPrefix(sName):
        """
        Returns the hungarian prefix of the given name.
        """
        for i, _ in enumerate(sName):
            if sName[i] not in ['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
                                'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z']:
                assert re.search('^[A-Z][a-zA-Z0-9]*$', sName[i:]) is not None;
                return sName[:i];
        return sName;

    def getAttributeParamNullValues(self, sAttr):
        """
        Returns a list of parameter NULL values, with the preferred one being
        the first element.

        Child classes can override this to handle one or more attributes specially.
        """
        sPrefix = self.getHungarianPrefix(sAttr);
        if sPrefix in ['id', 'uid', 'i', 'off', 'pct']:
            return [-1, '', '-1',];
        if sPrefix in ['l', 'c',]:
            return [long(-1), '', '-1',];
        if sPrefix == 'f':
            return ['',];
        if sPrefix in ['enm', 'ip', 's', 'ts', 'uuid']:
            return ['',];
        if sPrefix in ['ai', 'aid', 'al', 'as']:
            return [[], '', None]; ## @todo ??
        if sPrefix == 'bm':
            return ['', [],]; ## @todo bitmaps.
        raise TMExceptionBase('Unable to classify "%s" (prefix %s)' % (sAttr, sPrefix));

    def isAttributeNull(self, sAttr, oValue):
        """
        Checks if the specified attribute value indicates NULL.
        Return True/False.

        Note! This isn't entirely kosher actually.
        """
        if oValue is None:
            return True;
        aoNilValues = self.getAttributeParamNullValues(sAttr);
        return oValue in aoNilValues;

    def _convertAttributeFromParamNull(self, sAttr, oValue):
        """
        Converts an attribute from parameter NULL to database NULL value.
        Returns the new attribute value.
        """
        aoNullValues = self.getAttributeParamNullValues(sAttr);
        if oValue in aoNullValues:
            oValue = None if sAttr not in self.kasAltArrayNull else [];
        #
        # Perform deep conversion on ModelDataBase object and lists of them.
        #
        elif isinstance(oValue, list) and oValue and isinstance(oValue[0], ModelDataBase):
            oValue = copy.copy(oValue);
            for i, oSubValue in enumerate(oValue):
                assert isinstance(oSubValue, ModelDataBase);
                oValue[i] = copy.copy(oSubValue);
                oValue[i].convertFromParamNull();

        elif isinstance(oValue, ModelDataBase):
            oValue = copy.copy(oValue);
            oValue.convertFromParamNull();

        return oValue;

    def convertFromParamNull(self):
        """
        Converts from parameter NULL values to database NULL values (None).
        Returns self.
        """
        for sAttr in self.getDataAttributes():
            oValue = getattr(self, sAttr);
            oNewValue = self._convertAttributeFromParamNull(sAttr, oValue);
            if oValue != oNewValue:
                setattr(self, sAttr, oNewValue);
        return self;

    def _convertAttributeToParamNull(self, sAttr, oValue):
        """
        Converts an attribute from database NULL to a sepcial value we can pass
        thru parameter list.
        Returns the new attribute value.
        """
        if oValue is None:
            oValue = self.getAttributeParamNullValues(sAttr)[0];
        #
        # Perform deep conversion on ModelDataBase object and lists of them.
        #
        elif isinstance(oValue, list) and oValue and isinstance(oValue[0], ModelDataBase):
            oValue = copy.copy(oValue);
            for i, oSubValue in enumerate(oValue):
                assert isinstance(oSubValue, ModelDataBase);
                oValue[i] = copy.copy(oSubValue);
                oValue[i].convertToParamNull();

        elif isinstance(oValue, ModelDataBase):
            oValue = copy.copy(oValue);
            oValue.convertToParamNull();

        return oValue;

    def convertToParamNull(self):
        """
        Converts from database NULL values (None) to special values we can
        pass thru parameters list.
        Returns self.
        """
        for sAttr in self.getDataAttributes():
            oValue = getattr(self, sAttr);
            oNewValue = self._convertAttributeToParamNull(sAttr, oValue);
            if oValue != oNewValue:
                setattr(self, sAttr, oNewValue);
        return self;

    def _validateAndConvertAttribute(self, sAttr, sParam, oValue, aoNilValues, fAllowNull, oDb):
        """
        Validates and convert one attribute.
        Returns the converted value.

        Child classes can override this to handle one or more attributes specially.
        Note! oDb can be None.
        """
        sPrefix = self.getHungarianPrefix(sAttr);

        if sPrefix in ['id', 'uid']:
            (oNewValue, sError) = self.validateInt( oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull);
        elif sPrefix in ['i', 'off', 'pct']:
            (oNewValue, sError) = self.validateInt( oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull,
                                                    iMin = getattr(self, 'kiMin_' + sAttr, 0),
                                                    iMax = getattr(self, 'kiMax_' + sAttr, 0x7ffffffe));
        elif sPrefix in ['l', 'c']:
            (oNewValue, sError) = self.validateLong(oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull,
                                                    lMin = getattr(self, 'klMin_' + sAttr, 0),
                                                    lMax = getattr(self, 'klMax_' + sAttr, None));
        elif sPrefix == 'f':
            if not oValue and not fAllowNull: oValue = '0'; # HACK ALERT! Checkboxes are only added when checked.
            (oNewValue, sError) = self.validateBool(oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull);
        elif sPrefix == 'ts':
            (oNewValue, sError) = self.validateTs(  oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull);
        elif sPrefix == 'ip':
            (oNewValue, sError) = self.validateIp(  oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull);
        elif sPrefix == 'uuid':
            (oNewValue, sError) = self.validateUuid(oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull);
        elif sPrefix == 'enm':
            (oNewValue, sError) = self.validateWord(oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull,
                                                    asValid = getattr(self, 'kasValidValues_' + sAttr)); # The list is required.
        elif sPrefix == 's':
            (oNewValue, sError) = self.validateStr( oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull,
                                                    cchMin = getattr(self, 'kcchMin_' + sAttr, 0),
                                                    cchMax = getattr(self, 'kcchMax_' + sAttr, 4096),
                                                    fAllowUnicodeSymbols = getattr(self, 'kfAllowUnicode_' + sAttr, False) );
        ## @todo al.
        elif sPrefix == 'aid':
            (oNewValue, sError) = self.validateListOfInts(oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull,
                                                          iMin = 1, iMax = 0x7ffffffe);
        elif sPrefix == 'as':
            (oNewValue, sError) = self.validateListOfStr(oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull,
                                                         asValidValues = getattr(self, 'kasValidValues_' + sAttr, None),
                                                         cchMin = getattr(self, 'kcchMin_' + sAttr, 0 if fAllowNull else 1),
                                                         cchMax = getattr(self, 'kcchMax_' + sAttr, 4096));

        elif sPrefix == 'bm':
            ## @todo figure out bitfields.
            (oNewValue, sError) = self.validateListOfStr(oValue, aoNilValues = aoNilValues, fAllowNull = fAllowNull);
        else:
            raise TMExceptionBase('Unable to classify "%s" (prefix %s)' % (sAttr, sPrefix));

        _ = sParam; _ = oDb;
        return (oNewValue, sError);

    def _validateAndConvertWorker(self, asAllowNullAttributes, oDb, enmValidateFor = ksValidateFor_Other):
        """
        Worker for implementing validateAndConvert().
        """
        dErrors = {};
        for sAttr in self.getDataAttributes():
            oValue      = getattr(self, sAttr);
            sParam      = getattr(self, 'ksParam_' + sAttr);
            aoNilValues = self.getAttributeParamNullValues(sAttr);
            aoNilValues.append(None);

            (oNewValue, sError) = self._validateAndConvertAttribute(sAttr, sParam, oValue, aoNilValues,
                                                                    sAttr in asAllowNullAttributes, oDb);
            if oValue != oNewValue:
                setattr(self, sAttr, oNewValue);
            if sError is not None:
                dErrors[sParam] = sError;

        # Check the NULL requirements of the primary ID(s) for the 'add' and 'edit' actions.
        if enmValidateFor in (ModelDataBase.ksValidateFor_Add,
                              ModelDataBase.ksValidateFor_AddForeignId,
                              ModelDataBase.ksValidateFor_Edit,):
            fMustBeNull = enmValidateFor == ModelDataBase.ksValidateFor_Add;
            sAttr = getattr(self, 'ksIdAttr', None);
            if sAttr is not None:
                oValue = getattr(self, sAttr);
                if self.isAttributeNull(sAttr, oValue) != fMustBeNull:
                    sParam = getattr(self, 'ksParam_' + sAttr);
                    sErrMsg = 'Must be NULL!' if fMustBeNull else 'Must not be NULL!'
                    if sParam in dErrors:
                        dErrors[sParam] += ' ' + sErrMsg;
                    else:
                        dErrors[sParam]  = sErrMsg;

        return dErrors;

    def validateAndConvert(self, oDb, enmValidateFor = ksValidateFor_Other):
        """
        Validates the input and converts valid fields to their right type.
        Returns a dictionary with per field reports, only invalid fields will
        be returned, so an empty dictionary means that the data is valid.

        The dictionary keys are ksParam_*.

        Child classes can override _validateAndConvertAttribute to handle
        selected fields specially.  There are also a few class variables that
        can be used to advice the validation: kcchMin_sAttr, kcchMax_sAttr,
            kiMin_iAttr, kiMax_iAttr, klMin_lAttr, klMax_lAttr,
            kasValidValues_enmAttr, and kasAllowNullAttributes.
        """
        return self._validateAndConvertWorker(getattr(self, 'kasAllowNullAttributes', []), oDb,
                                              enmValidateFor = enmValidateFor);

    def validateAndConvertEx(self, asAllowNullAttributes, oDb, enmValidateFor = ksValidateFor_Other):
        """
        Same as validateAndConvert but with custom allow-null list.
        """
        return self._validateAndConvertWorker(asAllowNullAttributes, oDb, enmValidateFor = enmValidateFor);

    def convertParamToAttribute(self, sAttr, sParam, oValue, oDisp, fStrict):
        """
        Calculate the attribute value when initialized from a parameter.

        Returns the new value, with parameter NULL values. Raises exception on
        invalid parameter value.

        Child classes can override to do special parameter conversion jobs.
        """
        sPrefix       = self.getHungarianPrefix(sAttr);
        asValidValues = getattr(self, 'kasValidValues_' + sAttr, None);
        fAllowNull    = sAttr in getattr(self, 'kasAllowNullAttributes', []);
        if fStrict:
            if sPrefix == 'f':
                # HACK ALERT! Checkboxes are only present when checked, so we always have to provide a default.
                oNewValue = oDisp.getStringParam(sParam, asValidValues, '0');
            elif sPrefix[0] == 'a':
                # HACK ALERT! Lists are not present if empty.
                oNewValue = oDisp.getListOfStrParams(sParam, []);
            else:
                oNewValue = oDisp.getStringParam(sParam, asValidValues, None, fAllowNull = fAllowNull);
        else:
            if sPrefix[0] == 'a':
                oNewValue = oDisp.getListOfStrParams(sParam, []);
            else:
                assert oValue is not None, 'sAttr=%s' % (sAttr,);
                oNewValue = oDisp.getStringParam(sParam, asValidValues, oValue, fAllowNull = fAllowNull);
        return oNewValue;

    def initFromParams(self, oDisp, fStrict = True):
        """
        Initialize the object from parameters.
        The input is not validated at all, except that all parameters must be
        present when fStrict is True.

        Returns self. Raises exception on invalid parameter value.

        Note! The returned object has parameter NULL values, not database ones!
        """

        self.convertToParamNull()
        for sAttr in self.getDataAttributes():
            oValue    = getattr(self, sAttr);
            oNewValue = self.convertParamToAttribute(sAttr, getattr(self, 'ksParam_' + sAttr), oValue, oDisp, fStrict);
            if oNewValue != oValue:
                setattr(self, sAttr, oNewValue);
        return self;

    def areAttributeValuesEqual(self, sAttr, sPrefix, oValue1, oValue2):
        """
        Called to compare two attribute values and python thinks differs.

        Returns True/False.

        Child classes can override this to do special compares of things like arrays.
        """
        # Just in case someone uses it directly.
        if oValue1 == oValue2:
            return True;

        #
        # Timestamps can be both string (param) and object (db)
        # depending on the data source.  Compare string values to make
        # sure we're doing the right thing here.
        #
        if sPrefix == 'ts':
            return str(oValue1) == str(oValue2);

        #
        # Some generic code handling ModelDataBase children.
        #
        if isinstance(oValue1, list) and isinstance(oValue2, list):
            if len(oValue1) == len(oValue2):
                for i, _ in enumerate(oValue1):
                    if   not isinstance(oValue1[i], ModelDataBase) \
                      or type(oValue1) is not type(oValue2):
                        return False;
                    if not oValue1[i].isEqual(oValue2[i]):
                        return False;
                return True;

        elif  isinstance(oValue1, ModelDataBase) \
          and type(oValue1) is type(oValue2):
            return oValue1[i].isEqual(oValue2[i]);

        _ = sAttr;
        return False;

    def isEqual(self, oOther):
        """ Compares two instances. """
        for sAttr in self.getDataAttributes():
            if getattr(self, sAttr) != getattr(oOther, sAttr):
                # Delegate the final decision to an overridable method.
                if not self.areAttributeValuesEqual(sAttr, self.getHungarianPrefix(sAttr),
                                                    getattr(self, sAttr), getattr(oOther, sAttr)):
                    return False;
        return True;

    def isEqualEx(self, oOther, asExcludeAttrs):
        """ Compares two instances, omitting the given attributes. """
        for sAttr in self.getDataAttributes():
            if    sAttr not in asExcludeAttrs \
              and getattr(self, sAttr) != getattr(oOther, sAttr):
                # Delegate the final decision to an overridable method.
                if not self.areAttributeValuesEqual(sAttr, self.getHungarianPrefix(sAttr),
                                                    getattr(self, sAttr), getattr(oOther, sAttr)):
                    return False;
        return True;

    def reinitToNull(self):
        """
        Reinitializes the object to (database) NULL values.
        Returns self.
        """
        for sAttr in self.getDataAttributes():
            setattr(self, sAttr, None);
        return self;

    def toString(self):
        """
        Stringifies the object.
        Returns string representation.
        """

        sMembers = '';
        for sAttr in self.getDataAttributes():
            oValue = getattr(self, sAttr);
            sMembers += ', %s=%s' % (sAttr, oValue);

        oClass = type(self);
        if sMembers == '':
            return '<%s>' % (oClass.__name__);
        return '<%s: %s>' % (oClass.__name__, sMembers[2:]);

    def __str__(self):
        return self.toString();



    #
    # New validation helpers.
    #
    # These all return (oValue, sError), where sError is None when the value
    # is valid and an error message when not.  On success and in case of
    # range errors, oValue is converted into the requested type.
    #

    @staticmethod
    def validateInt(sValue, iMin = 0, iMax = 0x7ffffffe, aoNilValues = tuple([-1, None, '']), fAllowNull = True):
        """ Validates an integer field. """
        if sValue in aoNilValues:
            if fAllowNull:
                return (None if sValue is None else aoNilValues[0], None);
            return (sValue, 'Mandatory.');

        try:
            if utils.isString(sValue):
                iValue = int(sValue, 0);
            else:
                iValue = int(sValue);
        except:
            return (sValue, 'Not an integer');

        if iValue in aoNilValues:
            return (aoNilValues[0], None if fAllowNull else 'Mandatory.');

        if iValue < iMin:
            return (iValue, 'Value too small (min %d)' % (iMin,));
        if iValue > iMax:
            return (iValue, 'Value too high (max %d)' % (iMax,));
        return (iValue, None);

    @staticmethod
    def validateLong(sValue, lMin = 0, lMax = None, aoNilValues = tuple([long(-1), None, '']), fAllowNull = True):
        """ Validates an long integer field. """
        if sValue in aoNilValues:
            if fAllowNull:
                return (None if sValue is None else aoNilValues[0], None);
            return (sValue, 'Mandatory.');
        try:
            if utils.isString(sValue):
                lValue = long(sValue, 0);
            else:
                lValue = long(sValue);
        except:
            return (sValue, 'Not a long integer');

        if lValue in aoNilValues:
            return (aoNilValues[0], None if fAllowNull else 'Mandatory.');

        if lMin is not None and lValue < lMin:
            return (lValue, 'Value too small (min %d)' % (lMin,));
        if lMax is not None and lValue > lMax:
            return (lValue, 'Value too high (max %d)' % (lMax,));
        return (lValue, None);

    kdTimestampRegex = {
        len('2012-10-08 01:54:06'):           r'(\d{4})-([01]\d)-([0123]\d)[ Tt]([012]\d):[0-5]\d:([0-6]\d)$',
        len('2012-10-08 01:54:06.00'):        r'(\d{4})-([01]\d)-([0123]\d)[ Tt]([012]\d):[0-5]\d:([0-6]\d).\d{2}$',
        len('2012-10-08 01:54:06.000'):       r'(\d{4})-([01]\d)-([0123]\d)[ Tt]([012]\d):[0-5]\d:([0-6]\d).\d{3}$',
        len('999999-12-31 00:00:00.00'):      r'(\d{6})-([01]\d)-([0123]\d)[ Tt]([012]\d):[0-5]\d:([0-6]\d).\d{2}$',
        len('9999-12-31 23:59:59.999999'):    r'(\d{4})-([01]\d)-([0123]\d)[ Tt]([012]\d):[0-5]\d:([0-6]\d).\d{6}$',
        len('9999-12-31T23:59:59.999999999'): r'(\d{4})-([01]\d)-([0123]\d)[ Tt]([012]\d):[0-5]\d:([0-6]\d).\d{9}$',
    };

    @staticmethod
    def validateTs(sValue, aoNilValues = tuple([None, '']), fAllowNull = True, fRelative = False):
        """ Validates a timestamp field. """
        if sValue in aoNilValues:
            return (sValue, None if fAllowNull else 'Mandatory.');
        if not utils.isString(sValue):
            return (sValue, None);

        # Validate and strip off the timezone stuff.
        if sValue[-1] in 'Zz':
            sStripped = sValue[:-1];
            sValue = sStripped + 'Z';
        elif len(sValue) >= 19 + 3:
            oRes = re.match(r'^.*[+-](\d\d):(\d\d)$', sValue);
            if oRes is not None:
                if int(oRes.group(1)) > 12 or int(oRes.group(2)) >= 60:
                    return (sValue, 'Invalid timezone offset.');
                sStripped = sValue[:-6];
            else:
                sStripped = sValue;
        else:
            sStripped = sValue;

        # Used the stripped value length to find regular expression for validating and parsing the timestamp.
        sError = None;
        sRegExp = ModelDataBase.kdTimestampRegex.get(len(sStripped), None);
        if sRegExp:
            oRes = re.match(sRegExp, sStripped);
            if oRes is not None:
                iYear  = int(oRes.group(1));
                if iYear % 4 == 0 and (iYear % 100 != 0  or iYear % 400 == 0):
                    acDaysOfMonth = [31, 29, 31,  30, 31, 30,  31, 31, 30,  31, 30, 31];
                else:
                    acDaysOfMonth = [31, 28, 31,  30, 31, 30,  31, 31, 30,  31, 30, 31];
                iMonth = int(oRes.group(2));
                iDay   = int(oRes.group(3));
                iHour  = int(oRes.group(4));
                iSec   = int(oRes.group(5));
                if iMonth > 12 or (iMonth <= 0 and not fRelative):
                    sError = 'Invalid timestamp month.';
                elif iDay > acDaysOfMonth[iMonth - 1]:
                    sError = 'Invalid timestamp day-of-month (%02d has %d days).' % (iMonth, acDaysOfMonth[iMonth - 1]);
                elif iHour > 23:
                    sError = 'Invalid timestamp hour.'
                elif iSec >= 61:
                    sError = 'Invalid timestamp second.'
                elif iSec >= 60:
                    sError = 'Invalid timestamp: no leap seconds, please.'
            else:
                sError = 'Invalid timestamp (validation regexp: %s).' % (sRegExp,);
        else:
            sError = 'Invalid timestamp length.';
        return (sValue, sError);

    @staticmethod
    def validateIp(sValue, aoNilValues = tuple([None, '']), fAllowNull = True):
        """ Validates an IP address field. """
        if sValue in aoNilValues:
            return (sValue, None if fAllowNull else 'Mandatory.');

        if sValue == '::1':
            return (sValue, None);

        try:
            socket.inet_pton(socket.AF_INET, sValue); # pylint: disable=no-member
        except:
            try:
                socket.inet_pton(socket.AF_INET6, sValue); # pylint: disable=no-member
            except:
                return (sValue, 'Not a valid IP address.');

        return (sValue, None);

    @staticmethod
    def validateBool(sValue, aoNilValues = tuple([None, '']), fAllowNull = True):
        """ Validates a boolean field. """
        if sValue in aoNilValues:
            return (sValue, None if fAllowNull else 'Mandatory.');

        if sValue in ('True', 'true', '1', True):
            return (True, None);
        if sValue in ('False', 'false', '0', False):
            return (False, None);
        return (sValue, 'Invalid boolean value.');

    @staticmethod
    def validateUuid(sValue, aoNilValues = tuple([None, '']), fAllowNull = True):
        """ Validates an UUID field. """
        if sValue in aoNilValues:
            return (sValue, None if fAllowNull else 'Mandatory.');

        try:
            sValue = str(uuid.UUID(sValue));
        except:
            return (sValue, 'Invalid UUID value.');
        return (sValue, None);

    @staticmethod
    def validateWord(sValue, cchMin = 1, cchMax = 64, asValid = None, aoNilValues = tuple([None, '']), fAllowNull = True):
        """ Validates a word field. """
        if sValue in aoNilValues:
            return (sValue, None if fAllowNull else 'Mandatory.');

        if re.search('[^a-zA-Z0-9_-]', sValue) is not None:
            sError = 'Single word ([a-zA-Z0-9_-]), please.';
        elif cchMin is not None and len(sValue) < cchMin:
            sError = 'Too short, min %s chars' % (cchMin,);
        elif cchMax is not None and len(sValue) > cchMax:
            sError = 'Too long, max %s chars' % (cchMax,);
        elif asValid is not None and sValue not in asValid:
            sError = 'Invalid value "%s", must be one of: %s' % (sValue, asValid);
        else:
            sError = None;
        return (sValue, sError);

    @staticmethod
    def validateStr(sValue, cchMin = 0, cchMax = 4096, aoNilValues = tuple([None, '']), fAllowNull = True,
                    fAllowUnicodeSymbols = False):
        """ Validates a string field. """
        if sValue in aoNilValues:
            return (sValue, None if fAllowNull else 'Mandatory.');

        if cchMin is not None and len(sValue) < cchMin:
            sError = 'Too short, min %s chars' % (cchMin,);
        elif cchMax is not None and len(sValue) > cchMax:
            sError = 'Too long, max %s chars' % (cchMax,);
        elif fAllowUnicodeSymbols is False and utils.hasNonAsciiCharacters(sValue):
            sError = 'Non-ascii characters not allowed'
        else:
            sError = None;
        return (sValue, sError);

    @staticmethod
    def validateEmail(sValue, aoNilValues = tuple([None, '']), fAllowNull = True):
        """ Validates a email field."""
        if sValue in aoNilValues:
            return (sValue, None if fAllowNull else 'Mandatory.');

        if re.match(r'.+@.+\..+', sValue) is None:
            return (sValue,'Invalid e-mail format.');
        return (sValue, None);

    @staticmethod
    def validateListOfSomething(asValues, aoNilValues = tuple([[], None]), fAllowNull = True):
        """ Validate a list of some uniform values. Returns a copy of the list (if list it is). """
        if asValues in aoNilValues  or  (not asValues and not fAllowNull):
            return (asValues, None if fAllowNull else 'Mandatory.')

        if not isinstance(asValues, list):
            return (asValues, 'Invalid data type (%s).' % (type(asValues),));

        asValues = list(asValues); # copy the list.
        if asValues:
            oType = type(asValues[0]);
            for i in range(1, len(asValues)):
                if type(asValues[i]) is not oType: # pylint: disable=unidiomatic-typecheck
                    return (asValues, 'Invalid entry data type ([0]=%s vs [%d]=%s).' % (oType, i, type(asValues[i])) );

        return (asValues, None);

    @staticmethod
    def validateListOfStr(asValues, cchMin = None, cchMax = None, asValidValues = None,
                          aoNilValues = tuple([[], None]), fAllowNull = True):
        """ Validates a list of text items."""
        (asValues, sError) = ModelDataBase.validateListOfSomething(asValues, aoNilValues, fAllowNull);

        if sError is None  and  asValues not in aoNilValues  and  asValues:
            if not utils.isString(asValues[0]):
                return (asValues, 'Invalid item data type.');

            if not fAllowNull and cchMin is None:
                cchMin = 1;

            for sValue in asValues:
                if asValidValues is not None  and  sValue not in asValidValues:
                    sThisErr = 'Invalid value "%s".' % (sValue,);
                elif cchMin is not None  and  len(sValue) < cchMin:
                    sThisErr = 'Value "%s" is too short, min length is %u chars.' % (sValue, cchMin);
                elif cchMax is not None  and  len(sValue) > cchMax:
                    sThisErr = 'Value "%s" is too long, max length is %u chars.' % (sValue, cchMax);
                else:
                    continue;

                if sError is None:
                    sError = sThisErr;
                else:
                    sError += ' ' + sThisErr;

        return (asValues, sError);

    @staticmethod
    def validateListOfInts(asValues, iMin = 0, iMax = 0x7ffffffe, aoNilValues = tuple([[], None]), fAllowNull = True):
        """ Validates a list of integer items."""
        (asValues, sError) = ModelDataBase.validateListOfSomething(asValues, aoNilValues, fAllowNull);

        if sError is None  and  asValues not in aoNilValues  and  asValues:
            for i, _ in enumerate(asValues):
                sValue = asValues[i];

                sThisErr = '';
                try:
                    iValue = int(sValue);
                except:
                    sThisErr = 'Invalid integer value "%s".' % (sValue,);
                else:
                    asValues[i] = iValue;
                    if iValue < iMin:
                        sThisErr = 'Value %d is too small (min %d)' % (iValue, iMin,);
                    elif iValue > iMax:
                        sThisErr = 'Value %d is too high (max %d)' % (iValue, iMax,);
                    else:
                        continue;

                if sError is None:
                    sError = sThisErr;
                else:
                    sError += ' ' + sThisErr;

        return (asValues, sError);



    #
    # Old validation helpers.
    #

    @staticmethod
    def _validateInt(dErrors, sName, sValue, iMin = 0, iMax = 0x7ffffffe, aoNilValues = tuple([-1, None, ''])):
        """ Validates an integer field. """
        (sValue, sError) = ModelDataBase.validateInt(sValue, iMin, iMax, aoNilValues, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateIntNN(dErrors, sName, sValue, iMin = 0, iMax = 0x7ffffffe, aoNilValues = tuple([-1, None, ''])):
        """ Validates an integer field, not null. """
        (sValue, sError) = ModelDataBase.validateInt(sValue, iMin, iMax, aoNilValues, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateLong(dErrors, sName, sValue, lMin = 0, lMax = None, aoNilValues = tuple([long(-1), None, ''])):
        """ Validates an long integer field. """
        (sValue, sError) = ModelDataBase.validateLong(sValue, lMin, lMax, aoNilValues, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateLongNN(dErrors, sName, sValue, lMin = 0, lMax = None, aoNilValues = tuple([long(-1), None, ''])):
        """ Validates an long integer field, not null. """
        (sValue, sError) = ModelDataBase.validateLong(sValue, lMin, lMax, aoNilValues, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateTs(dErrors, sName, sValue):
        """ Validates a timestamp field. """
        (sValue, sError) = ModelDataBase.validateTs(sValue, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateTsNN(dErrors, sName, sValue):
        """ Validates a timestamp field, not null. """
        (sValue, sError) = ModelDataBase.validateTs(sValue, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateIp(dErrors, sName, sValue):
        """ Validates an IP address field. """
        (sValue, sError) = ModelDataBase.validateIp(sValue, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateIpNN(dErrors, sName, sValue):
        """ Validates an IP address field, not null. """
        (sValue, sError) = ModelDataBase.validateIp(sValue, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateBool(dErrors, sName, sValue):
        """ Validates a boolean field. """
        (sValue, sError) = ModelDataBase.validateBool(sValue, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateBoolNN(dErrors, sName, sValue):
        """ Validates a boolean field, not null. """
        (sValue, sError) = ModelDataBase.validateBool(sValue, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateUuid(dErrors, sName, sValue):
        """ Validates an UUID field. """
        (sValue, sError) = ModelDataBase.validateUuid(sValue, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateUuidNN(dErrors, sName, sValue):
        """ Validates an UUID field, not null. """
        (sValue, sError) = ModelDataBase.validateUuid(sValue, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateWord(dErrors, sName, sValue, cchMin = 1, cchMax = 64, asValid = None):
        """ Validates a word field. """
        (sValue, sError) = ModelDataBase.validateWord(sValue, cchMin, cchMax, asValid, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateWordNN(dErrors, sName, sValue, cchMin = 1, cchMax = 64, asValid = None):
        """ Validates a boolean field, not null. """
        (sValue, sError) = ModelDataBase.validateWord(sValue, cchMin, cchMax, asValid, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateStr(dErrors, sName, sValue, cchMin = 0, cchMax = 4096):
        """ Validates a string field. """
        (sValue, sError) = ModelDataBase.validateStr(sValue, cchMin, cchMax, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateStrNN(dErrors, sName, sValue, cchMin = 0, cchMax = 4096):
        """ Validates a string field, not null. """
        (sValue, sError) = ModelDataBase.validateStr(sValue, cchMin, cchMax, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateEmail(dErrors, sName, sValue):
        """ Validates a email field."""
        (sValue, sError) = ModelDataBase.validateEmail(sValue, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateEmailNN(dErrors, sName, sValue):
        """ Validates a email field."""
        (sValue, sError) = ModelDataBase.validateEmail(sValue, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateListOfStr(dErrors, sName, asValues, asValidValues = None):
        """ Validates a list of text items."""
        (sValue, sError) = ModelDataBase.validateListOfStr(asValues, asValidValues = asValidValues, fAllowNull = True);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    @staticmethod
    def _validateListOfStrNN(dErrors, sName, asValues, asValidValues = None):
        """ Validates a list of text items, not null and len >= 1."""
        (sValue, sError) = ModelDataBase.validateListOfStr(asValues, asValidValues = asValidValues, fAllowNull = False);
        if sError is not None:
            dErrors[sName] = sError;
        return sValue;

    #
    # Various helpers.
    #

    @staticmethod
    def formatSimpleNowAndPeriod(oDb, tsNow = None, sPeriodBack = None,
                                 sTablePrefix = '', sExpCol = 'tsExpire', sEffCol = 'tsEffective'):
        """
        Formats a set of tsNow and sPeriodBack arguments for a standard testmanager
        table.

        If sPeriodBack is given, the query is effective for the period
        (tsNow - sPeriodBack) thru (tsNow).

        If tsNow isn't given, it defaults to current time.

        Returns the final portion of a WHERE query (start with AND) and maybe an
        ORDER BY and LIMIT bit if sPeriodBack is given.
        """
        if tsNow is not None:
            if sPeriodBack is not None:
                sRet = oDb.formatBindArgs('  AND  ' + sTablePrefix + sExpCol + ' > (%s::timestamp - %s::interval)\n'
                                          '  AND  tsEffective <= %s\n'
                                          'ORDER BY ' + sTablePrefix + sExpCol + ' DESC\n'
                                          'LIMIT 1\n'
                                          , ( tsNow, sPeriodBack, tsNow));
            else:
                sRet = oDb.formatBindArgs('  AND  ' + sTablePrefix + sExpCol + '     > %s\n'
                                          '  AND  ' + sTablePrefix + sEffCol + ' <= %s\n'
                                          , ( tsNow, tsNow, ));
        else:
            if sPeriodBack is not None:
                sRet = oDb.formatBindArgs('  AND  ' + sTablePrefix + sExpCol + '     > (CURRENT_TIMESTAMP - %s::interval)\n'
                                          '  AND  ' + sTablePrefix + sEffCol + ' <= CURRENT_TIMESTAMP\n'
                                          'ORDER BY ' + sTablePrefix + sExpCol + ' DESC\n'
                                          'LIMIT 1\n'
                                          , ( sPeriodBack, ));
            else:
                sRet = '  AND  ' + sTablePrefix + sExpCol + '     = \'infinity\'::timestamp\n';
        return sRet;

    @staticmethod
    def formatSimpleNowAndPeriodQuery(oDb, sQuery, aBindArgs, tsNow = None, sPeriodBack = None,
                                      sTablePrefix = '', sExpCol = 'tsExpire', sEffCol = 'tsEffective'):
        """
        Formats a simple query for a standard testmanager table with optional
        tsNow and sPeriodBack arguments.

        The sQuery and sBindArgs are passed along to oDb.formatBindArgs to form
        the first part of the query.  Must end with an open WHERE statement as
        we'll be adding the time part starting with 'AND something...'.

        See formatSimpleNowAndPeriod for tsNow and sPeriodBack description.

        Returns the final portion of a WHERE query (start with AND) and maybe an
        ORDER BY and LIMIT bit if sPeriodBack is given.

        """
        return oDb.formatBindArgs(sQuery, aBindArgs) \
             + ModelDataBase.formatSimpleNowAndPeriod(oDb, tsNow, sPeriodBack, sTablePrefix, sExpCol, sEffCol);


    #
    # JSON
    #

    @staticmethod
    def stringToJson(sString):
        """ Converts a string to a JSON value string. """
        if not utils.isString(sString):
            sString = utils.toUnicode(sString);
            if not utils.isString(sString):
                sString = str(sString);
        return json.dumps(sString);

    @staticmethod
    def dictToJson(dDict, dOptions = None):
        """ Converts a dictionary to a JSON string. """
        sJson = u'{ ';
        for i, oKey in enumerate(dDict):
            if i > 0:
                sJson += ', ';
            sJson += '%s: %s' % (ModelDataBase.stringToJson(oKey),
                                 ModelDataBase.genericToJson(dDict[oKey], dOptions));
        return sJson + ' }';

    @staticmethod
    def listToJson(aoList, dOptions = None):
        """ Converts list of something to a JSON string. """
        sJson = u'[ ';
        for i, oValue in enumerate(aoList):
            if i > 0:
                sJson += u', ';
            sJson += ModelDataBase.genericToJson(oValue, dOptions);
        return sJson + u' ]';

    @staticmethod
    def datetimeToJson(oDateTime):
        """ Converts a datetime instance to a JSON string. """
        return '"%s"' % (oDateTime,);


    @staticmethod
    def genericToJson(oValue, dOptions = None):
        """ Converts a generic object to a JSON string. """
        if isinstance(oValue, ModelDataBase):
            return oValue.toJson();
        if isinstance(oValue, dict):
            return ModelDataBase.dictToJson(oValue, dOptions);
        if isinstance(oValue, (list, tuple, set, frozenset)):
            return ModelDataBase.listToJson(oValue, dOptions);
        if isinstance(oValue, datetime.datetime):
            return ModelDataBase.datetimeToJson(oValue)
        return json.dumps(oValue);

    def attribValueToJson(self, sAttr, oValue, dOptions = None):
        """
        Converts the attribute value to JSON.
        Returns JSON (string).
        """
        _ = sAttr;
        return self.genericToJson(oValue, dOptions);

    def toJson(self, dOptions = None):
        """
        Converts the object to JSON.
        Returns JSON (string).
        """
        sJson = u'{ ';
        for iAttr, sAttr in enumerate(self.getDataAttributes()):
            oValue = getattr(self, sAttr);
            if iAttr > 0:
                sJson += ', ';
            sJson += u'"%s": ' % (sAttr,);
            sJson += self.attribValueToJson(sAttr, oValue, dOptions);
        return sJson + u' }';


    #
    # Sub-classes.
    #

    class DispWrapper(object):
        """Proxy object."""
        def __init__(self, oDisp, sAttrFmt):
            self.oDisp    = oDisp;
            self.sAttrFmt = sAttrFmt;
        def getStringParam(self, sName, asValidValues = None, sDefault = None, fAllowNull = False):
            """See WuiDispatcherBase.getStringParam."""
            return self.oDisp.getStringParam(self.sAttrFmt % (sName,), asValidValues, sDefault, fAllowNull = fAllowNull);
        def getListOfStrParams(self, sName, asDefaults = None):
            """See WuiDispatcherBase.getListOfStrParams."""
            return self.oDisp.getListOfStrParams(self.sAttrFmt % (sName,), asDefaults);
        def getListOfIntParams(self, sName, iMin = None, iMax = None, aiDefaults = None):
            """See WuiDispatcherBase.getListOfIntParams."""
            return self.oDisp.getListOfIntParams(self.sAttrFmt % (sName,), iMin, iMax, aiDefaults);




# pylint: disable=no-member,missing-docstring,too-few-public-methods
class ModelDataBaseTestCase(unittest.TestCase):
    """
    Base testcase for ModelDataBase decendants.
    Derive from this and override setUp.
    """

    def setUp(self):
        """
        Override this! Don't call super!
        The subclasses are expected to set aoSamples to an array of instance
        samples.  The first entry must be a default object, the subsequent ones
        are optional and their contents freely choosen.
        """
        self.aoSamples = [ModelDataBase(),];

    def testEquality(self):
        for oSample in self.aoSamples:
            self.assertEqual(oSample.isEqual(copy.copy(oSample)), True);
            self.assertIsNotNone(oSample.isEqual(self.aoSamples[0]));

    def testNullConversion(self):
        if not self.aoSamples[0].getDataAttributes():
            return;
        for oSample in self.aoSamples:
            oCopy = copy.copy(oSample);
            self.assertEqual(oCopy.convertToParamNull(), oCopy);
            self.assertEqual(oCopy.isEqual(oSample), False);
            self.assertEqual(oCopy.convertFromParamNull(), oCopy);
            self.assertEqual(oCopy.isEqual(oSample), True, '\ngot     : %s\nexpected: %s' % (oCopy, oSample,));

            oCopy = copy.copy(oSample);
            self.assertEqual(oCopy.convertToParamNull(), oCopy);
            oCopy2 = copy.copy(oCopy);
            self.assertEqual(oCopy.convertToParamNull(), oCopy);
            self.assertEqual(oCopy.isEqual(oCopy2), True);
            self.assertEqual(oCopy.convertToParamNull(), oCopy);
            self.assertEqual(oCopy.isEqual(oCopy2), True);

            oCopy = copy.copy(oSample);
            self.assertEqual(oCopy.convertFromParamNull(), oCopy);
            oCopy2 = copy.copy(oCopy);
            self.assertEqual(oCopy.convertFromParamNull(), oCopy);
            self.assertEqual(oCopy.isEqual(oCopy2), True);
            self.assertEqual(oCopy.convertFromParamNull(), oCopy);
            self.assertEqual(oCopy.isEqual(oCopy2), True);

    def testReinitToNull(self):
        oFirst = copy.copy(self.aoSamples[0]);
        self.assertEqual(oFirst.reinitToNull(), oFirst);
        for oSample in self.aoSamples:
            oCopy = copy.copy(oSample);
            self.assertEqual(oCopy.reinitToNull(), oCopy);
            self.assertEqual(oCopy.isEqual(oFirst), True);

    def testValidateAndConvert(self):
        for oSample in self.aoSamples:
            oCopy = copy.copy(oSample);
            oCopy.convertToParamNull();
            dError1 = oCopy.validateAndConvert(None);

            oCopy2  = copy.copy(oCopy);
            self.assertEqual(oCopy.validateAndConvert(None), dError1);
            self.assertEqual(oCopy.isEqual(oCopy2), True);

    def testInitFromParams(self):
        class DummyDisp(object):
            def getStringParam(self, sName, asValidValues = None, sDefault = None, fAllowNull = False):
                _ = sName; _ = asValidValues; _ = fAllowNull;
                return sDefault;
            def getListOfStrParams(self, sName, asDefaults = None):
                _ = sName;
                return asDefaults;
            def getListOfIntParams(self, sName, iMin = None, iMax = None, aiDefaults = None):
                _ = sName; _ = iMin; _ = iMax;
                return aiDefaults;

        for oSample in self.aoSamples:
            oCopy = copy.copy(oSample);
            self.assertEqual(oCopy.initFromParams(DummyDisp(), fStrict = False), oCopy);

    def testToString(self):
        for oSample in self.aoSamples:
            self.assertIsNotNone(oSample.toString());


class FilterCriterionValueAndDescription(object):
    """
    A filter criterion value and its description.
    """

    def __init__(self, oValue, sDesc, cTimes = None, sHover = None, fIrrelevant = False):
        self.oValue      = oValue;      ##< Typically the ID of something in the database.
        self.sDesc       = sDesc;       ##< What to display.
        self.cTimes      = cTimes;      ##< Number of times the value occurs in the result set. None if not given.
        self.sHover      = sHover;      ##< Optional hover/title string.
        self.fIrrelevant = fIrrelevant; ##< Irrelevant filter option, only present because it's selected
        self.aoSubs      = [];          ##< References to FilterCriterion.oSub.aoPossible.


class FilterCriterion(object):
    """
    A filter criterion.
    """

    ## @name The state.
    ## @{
    ksState_NotSelected = 'not-selected';
    ksState_Selected    = 'selected';
    ## @}

    ## @name The kind of filtering.
    ## @{
    ## 'Element of' by default, 'not an element of' when fInverted is False.
    ksKind_ElementOfOrNot = 'element-of-or-not';
    ## The criterion is a special one and cannot be inverted.
    ksKind_Special        = 'special';
    ## @}

    ## @name The value type.
    ## @{
    ksType_UInt    = 'uint';     ##< unsigned integer value.
    ksType_UIntNil = 'uint-nil'; ##< unsigned integer value, with nil.
    ksType_String  = 'string';   ##< string value.
    ksType_Ranges  = 'ranges';   ##< List of (unsigned) integer ranges.
    ## @}

    def __init__(self, sName, sVarNm = None, sType = ksType_UInt, # pylint: disable=too-many-arguments
                 sState = ksState_NotSelected, sKind = ksKind_ElementOfOrNot,
                 sTable = None, sColumn = None, asTables = None, oSub = None):
        assert len(sVarNm) == 2;    # required by wuimain.py for filtering.
        self.sName      = sName;
        self.sState     = sState;
        self.sType      = sType;
        self.sKind      = sKind;
        self.sVarNm     = sVarNm;
        self.aoSelected = [];       ##< User input from sVarNm. Single value, type according to sType.
        self.sInvVarNm  = 'i' + sVarNm if sKind == self.ksKind_ElementOfOrNot else None;
        self.fInverted  = False;    ##< User input from sInvVarNm. Inverts the operation (-> not an element of).
        self.aoPossible = [];       ##< type: list[FilterCriterionValueAndDescription]
        assert (sTable is None and asTables is None) or ((sTable is not None) != (asTables is not None)), \
               '%s %s' % (sTable, asTables);
        self.asTables   = [sTable,] if sTable is not None else asTables;
        assert sColumn is None or len(self.asTables) == 1, '%s %s' % (self.asTables, sColumn);
        self.sColumn    = sColumn;  ##< Normally only applicable if one table.
        self.fExpanded  = None;     ##< Tristate (None, False, True)
        self.oSub       = oSub;     ##< type: FilterCriterion


class ModelFilterBase(ModelBase):
    """
    Base class for filters.

    Filters are used to narrow down data that is displayed in a list or
    report.  This class differs a little from ModelDataBase in that it is not
    tied to a database table, but one or more database queries that are
    typically rather complicated.

    The filter object has two roles:

      1. It is used by a ModelLogicBase descendant to store the available
         filtering options for data begin displayed.

      2. It decodes and stores the filtering options submitted by the user so
         a ModeLogicBase descendant can use it to construct WHERE statements.

    The ModelFilterBase class is related to the ModelDataBase class in that it
    decodes user parameters and stores data, however it is not a descendant.

    Note! In order to reduce URL lengths, we use very very brief parameter
          names for the filters.
    """

    def __init__(self):
        ModelBase.__init__(self);
        self.aCriteria = [] # type: list[FilterCriterion]

    def _initFromParamsWorker(self, oDisp, oCriterion): # (,FilterCriterion)
        """ Worker for initFromParams. """
        if oCriterion.sType == FilterCriterion.ksType_UInt:
            oCriterion.aoSelected = oDisp.getListOfIntParams(oCriterion.sVarNm, iMin = 0, aiDefaults = []);
        elif oCriterion.sType == FilterCriterion.ksType_UIntNil:
            oCriterion.aoSelected = oDisp.getListOfIntParams(oCriterion.sVarNm, iMin = -1, aiDefaults = []);
        elif oCriterion.sType == FilterCriterion.ksType_String:
            oCriterion.aoSelected = oDisp.getListOfStrParams(oCriterion.sVarNm, asDefaults = []);
            if len(oCriterion.aoSelected) > 100:
                raise TMExceptionBase('Variable %s has %u value, max allowed is 100!'
                                      % (oCriterion.sVarNm, len(oCriterion.aoSelected)));
            for sValue in oCriterion.aoSelected:
                if   len(sValue) > 64 \
                  or '\'' in sValue \
                  or sValue[-1] == '\\':
                    raise TMExceptionBase('Variable %s has an illegal value "%s"!' % (oCriterion.sVarNm, sValue));
        elif oCriterion.sType == FilterCriterion.ksType_Ranges:
            def convertRangeNumber(sValue):
                """ Helper """
                sValue = sValue.strip();
                if sValue and sValue not in ('inf', 'Inf', 'INf', 'INF', 'InF', 'iNf', 'iNF', 'inF',):
                    try:    return int(sValue);
                    except: pass;
                return None;

            for sRange in oDisp.getStringParam(oCriterion.sVarNm, sDefault = '').split(','):
                sRange = sRange.strip();
                if sRange and sRange != '-' and any(ch.isdigit() for ch in sRange):
                    asValues = sRange.split('-');
                    if len(asValues) == 1:
                        asValues = [asValues[0], asValues[0]];
                    elif len(asValues) > 2:
                        asValues = [asValues[0], asValues[-1]];
                    tTuple = (convertRangeNumber(asValues[0]), convertRangeNumber(asValues[1]));
                    if tTuple[0] is not None and tTuple[1] is not None and tTuple[0] > tTuple[1]:
                        tTuple = (tTuple[1], tTuple[0]);
                    oCriterion.aoSelected.append(tTuple);
        else:
            assert False;
        if oCriterion.aoSelected:
            oCriterion.sState = FilterCriterion.ksState_Selected;
        else:
            oCriterion.sState = FilterCriterion.ksState_NotSelected;

        if oCriterion.sKind == FilterCriterion.ksKind_ElementOfOrNot:
            oCriterion.fInverted = oDisp.getBoolParam(oCriterion.sInvVarNm, fDefault = False);

        if oCriterion.oSub is not None:
            self._initFromParamsWorker(oDisp, oCriterion.oSub);
        return;

    def initFromParams(self, oDisp): # type: (WuiDispatcherBase) -> self
        """
        Initialize the object from parameters.

        Returns self. Raises exception on invalid parameter value.
        """

        for oCriterion in self.aCriteria:
            self._initFromParamsWorker(oDisp, oCriterion);
        return self;

    def strainParameters(self, dParams, aAdditionalParams = None):
        """ Filters just the parameters relevant to this filter, returning a copy. """

        # Collect the parameter names.
        dWanted = {};
        for oCrit in self.aCriteria:
            dWanted[oCrit.sVarNm] = 1;
            if oCrit.sInvVarNm:
                dWanted[oCrit.sInvVarNm] = 1;

        # Add additional stuff.
        if aAdditionalParams:
            for sParam in aAdditionalParams:
                dWanted[sParam] = 1;

        # To the straining.
        dRet = {};
        for sKey in dParams:
            if sKey in dWanted:
                dRet[sKey] = dParams[sKey];
        return dRet;


class ModelLogicBase(ModelBase): # pylint: disable=too-few-public-methods
    """
    Something all classes in the logic classes the logical model inherits from.
    """

    def __init__(self, oDb):
        ModelBase.__init__(self);

        #
        # Note! Do not create a connection here if None, we need to DB share
        #       connection with all other logic objects so we can perform half
        #       complex transactions involving several logic objects.
        #
        self._oDb = oDb;

    def getDbConnection(self):
        """
        Gets the database connection.
        This should only be used for instantiating other ModelLogicBase children.
        """
        return self._oDb;

    def _dbRowsToModelDataList(self, oModelDataType, aaoRows = None):
        """
        Helper for conerting a simple fetch into a list of ModelDataType python objects.

        If aaoRows is None, we'll fetchAll from the database ourselves.

        The oModelDataType must be a class derived from ModelDataBase and implement
        the initFormDbRow method.

        Returns a list of oModelDataType instances.
        """
        assert issubclass(oModelDataType, ModelDataBase);
        aoRet = [];
        if aaoRows is None:
            aaoRows = self._oDb.fetchAll();
        for aoRow in aaoRows:
            aoRet.append(oModelDataType().initFromDbRow(aoRow));
        return aoRet;



class AttributeChangeEntry(object): # pylint: disable=too-few-public-methods
    """
    Data class representing the changes made to one attribute.
    """

    def __init__(self, sAttr, oNewRaw, oOldRaw, sNewText, sOldText):
        self.sAttr          = sAttr;
        self.oNewRaw        = oNewRaw;
        self.oOldRaw        = oOldRaw;
        self.sNewText       = sNewText;
        self.sOldText       = sOldText;

class AttributeChangeEntryPre(AttributeChangeEntry): # pylint: disable=too-few-public-methods
    """
    AttributeChangeEntry for preformatted values.
    """

    def __init__(self, sAttr, oNewRaw, oOldRaw, sNewText, sOldText):
        AttributeChangeEntry.__init__(self, sAttr, oNewRaw, oOldRaw, sNewText, sOldText);

class ChangeLogEntry(object): # pylint: disable=too-few-public-methods
    """
    A change log entry returned by the fetchChangeLog method typically
    implemented by ModelLogicBase child classes.
    """

    def __init__(self, uidAuthor, sAuthor, tsEffective, tsExpire, oNewRaw, oOldRaw, aoChanges):
        self.uidAuthor      = uidAuthor;
        self.sAuthor        = sAuthor;
        self.tsEffective    = tsEffective;
        self.tsExpire       = tsExpire;
        self.oNewRaw        = oNewRaw;
        self.oOldRaw        = oOldRaw;      # Note! NULL for the last entry.
        self.aoChanges      = aoChanges;

