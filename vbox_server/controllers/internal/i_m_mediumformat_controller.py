"""VBox REST API

Copyright (c) 2025 Oracle and/or its affiliates.
Licensed under the Universal Permissive License v 1.0 as shown at https://oss.oracle.com/licenses/upl

SPDX-License-Identifier: UPL-1.0
"""

# pylint: disable=invalid-name
# pylint: disable=consider-using-f-string
# pylint: disable=line-too-long
# pylint: disable=undefined-variable
import logging
from http import HTTPStatus
from flask import jsonify
import connexion
import six

from vbox_server.global_settings import *
from vbox_server.utils.vbox_utils import *
from vbox_server.utils.object_conversion import *
from vbox_server.utils.enum_conversion import *
from vbox_server.models.error import Error
from vbox_server.models.medium_format_describe_file_extensions_response import MediumFormatDescribeFileExtensionsResponse  # noqa: E501
from vbox_server.models.medium_format_describe_properties_response import MediumFormatDescribePropertiesResponse  # noqa: E501


########################### Medium extension: VirtualBox-Swagger vice versa conversion ###########################
def swagger_to_vbox_medim_extension(inVal: str):
    out = None
    supportedExtensions = ("VMDK",
                           "VDI",
                           "VHD",
                           "Parallels",
                           "DMG",
                           "QED",
                           "QCOW",
                           "VHDX",
                           "CUE",
                           "VBoxIsoMaker",
                           "RAW",
                           "iSCSI")

    for item in supportedExtensions:
        if inVal.upper() == item.upper():
            out = item
            break

    return out


def i_wrapper_mediumformat_describefileextensions(formatid):
    return _mediumformat_describefileextensions(formatid)


def _mediumformat_describefileextensions(formatid):  # noqa: E501
    """
    Call interface method IMediumFormat::describeFileExtensions

    :param formatid: Identifier of format
    :type name: str

    :rtype: MediumformatDescribefileextensionsResponse
    """

    vbox_utils_commonChecks()

    httpCode = HTTPStatus.OK
    oError = None

    oMediumformatDescribefileextensionsResponse = MediumFormatDescribeFileExtensionsResponse()

    oVBox = ctx['vb']
    try:
        oVBoxSystemProperties = oVBox.systemProperties
        if oVBoxSystemProperties: 
            oVBoxMediumFormats = ctx['global'].getArray(oVBoxSystemProperties,'mediumFormats')
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

        response = jsonify(oError)
        return response, httpCode

    fFound = False
    if oVBoxMediumFormats is not None:
        try:
            for item in oVBoxMediumFormats:
                 # Weird, because the description says:
                 # The format identifier is a non-@c null non-empty ASCII string. Note that
                 # this string is case-insensitive. This means that, for example, all of
                 # the following strings:
                 # <pre>
                 # "VDI"
                 # "vdi"
                 # "VdI"</pre>
                 # refer to the same medium format.
                 # The valid ids for 29.05.2025 are:
                 #      VMDK, VDI, VHD, Parallels, DMG, QED, QCOW, VHDX, CUE, VBoxIsoMaker, RAW, iSCSI

                if item.id == swagger_to_vbox_medim_extension(formatid):
                    fFound = True
                    olFileExt, olDeviceType = item.describeFileExtensions()
                    for c, item in enumerate(olFileExt):
                        logging.info(item)
                        oMediumformatDescribefileextensionsResponse.extensions = item
                        oMediumformatDescribefileextensionsResponse.types = vbox_to_swagger_devicetype(olDeviceType[c])
                    break

            if fFound == False:
                httpCode = HTTPStatus.NOT_FOUND
                oError = Error(httpCode, "The passed medium format wasn't found. It seems VirtualBox may not support it.")
            else:
                logging.info('Successfull call of i_mediumformat_describefileextensions')

        except Exception as e:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oMediumformatDescribefileextensionsResponse)
    return response, httpCode


def i_wrapper_mediumformat_describeproperties(formatid):
    return _mediumformat_describeproperties(formatid)


def _mediumformat_describeproperties(formatid):  # noqa: E501
    """
    Call interface method IMediumFormat::describeProperties

    :param formatid: Identifier of format
    :type name: str

    :rtype: MediumformatDescribepropertiesResponse
    """

    vbox_utils_commonChecks()

    httpCode = HTTPStatus.OK
    oError = None

    oMediumformatDescribepropertiesResponse = MediumFormatDescribePropertiesResponse([],[],[],[],[])

    oVBox = ctx['vb']
    try:
        oVBoxSystemProperties = oVBox.systemProperties
        if oVBoxSystemProperties: 
            oVBoxMediumFormats = ctx['global'].getArray(oVBoxSystemProperties,'mediumFormats')
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

        response = jsonify(oError)
        return response, httpCode

    fFound = False
    if oVBoxMediumFormats is not None:
        try:
            for item in oVBoxMediumFormats:
                if item.id == swagger_to_vbox_medim_extension(formatid):
                    fFound = True
                    olNames, olDesc, olType, olFlag, olDefault = item.describeProperties()
                    for c, item in enumerate(olNames):
                        logging.info(item)
                        oMediumformatDescribepropertiesResponse.names.append(item)
                        oMediumformatDescribepropertiesResponse.descriptions.append(olDesc[c])
                        oMediumformatDescribepropertiesResponse.types.append(vbox_to_swagger_datatype(olType[c]))
                        oMediumformatDescribepropertiesResponse.flags.append(olFlag[c])
                        oMediumformatDescribepropertiesResponse.defaults.append(olDefault[c])
                    break

            if fFound == False:
                httpCode = HTTPStatus.NOT_FOUND
                oError = Error(httpCode, "The passed medium format wasn't found. It seems VirtualBox may not support it.")
            else:
                logging.info('Successfull call of i_mediumformat_describeproperties')

        except Exception as e:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oMediumformatDescribepropertiesResponse)
    return response, httpCode
