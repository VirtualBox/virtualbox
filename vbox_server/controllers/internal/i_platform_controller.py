"""VBox REST API

Copyright (c) 2024-2025 Oracle and/or its affiliates.
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

from vbox_server.global_settings import *
from vbox_server.utils.vbox_utils import *
# from vbox_server.utils.restapi_objects_functions import *
from vbox_server.utils.object_conversion import *
from vbox_server.utils.enum_conversion import *

from vbox_server.models.error import Error  # noqa: E501
from vbox_server.models.platform_properties_obj_wrapper_response import PlatformPropertiesObjWrapperResponse  # noqa: E501


# def i_virtualbox_getplatformproperties(select=None, architecture=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::getPlatformProperties

#     :param select: The object attributes separated by comma
#     :type select: str
#     :param architecture: For the possible values of enumeration look into #/definitions/PlatformArchitecture
#     :type architecture: str

#     :rtype: PlatformPropertiesObjWrapperResponse
#     """

#     oError = None
#     httpCode = HTTPStatus.OK

#     vbox_utils_commonChecks()

#     oPlatformPropertiesResponse = PlatformPropertiesObjWrapperResponse()

#     try:
#         oVBox = ctx['vb']
#         vBoxPlatformArchitecture = swagger_to_vbox_platformarchitecture(architecture)
#         oPlatformProperties = oVBox.getPlatformProperties(vBoxPlatformArchitecture)
#         oPlatformPropertiesResponse.properties = i_fill_platformproperties(oPlatformProperties, select)
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oPlatformPropertiesResponse)
#     return response, httpCode
