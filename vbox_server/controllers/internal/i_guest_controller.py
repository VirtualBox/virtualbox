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
# from vbox_server.utils.restapi_objects_functions import *
from vbox_server.utils.enum_conversion import *
from vbox_server.utils.object_conversion import *


from vbox_server.models.error import Error  # noqa: E501
from vbox_server.models.guest_os_type_obj_wrapper_response import GuestOSTypeObjWrapperResponse  # noqa: E501
from vbox_server.models.virtual_box_get_guest_os_descs_by_subtype_response import VirtualBoxGetGuestOSDescsBySubtypeResponse  # noqa: E501
from vbox_server.models.virtual_box_get_guest_os_subtypes_by_family_id_response import VirtualBoxGetGuestOSSubtypesByFamilyIdResponse  # noqa: E501


def i_guest_findsession(sessionName=None):  # noqa: E501
    """
    Call interface method IGuest::findSession

    :param sessionName: 
    :type sessionName: str

    :rtype: GuestFindsessionResponse
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_guest_getadditionsstatus(level=None):  # noqa: E501
#     """
#     Call interface method IGuest::getAdditionsStatus

#     :param level: For the possible values of enumeration look into #/definitions/AdditionsRunLevelType
#     :type level: str

#     :rtype: GuestGetadditionsstatusResponse
#     """
#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_guest_getfacilitystatus(facility=None):  # noqa: E501
    """
    Call interface method IGuest::getFacilityStatus

    :param facility: For the possible values of enumeration look into #/definitions/AdditionsFacilityType
    :type facility: str

    :rtype: GuestGetfacilitystatusResponse
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_virtualbox_getguestosdescsbysubtype(OSSubtype=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::getGuestOSDescsBySubtype

#     :param OSSubtype: 
#     :type OSSubtype: str

#     :rtype: VirtualboxGetguestosdescsbysubtypeResponse
#     """

#     oError = None
#     httpCode = HTTPStatus.OK

#     vbox_utils_commonChecks()
#     oVirtualboxGetguestosdescsbysubtypeResponse = VirtualBoxGetGuestOSDescsBySubtypeResponse([])

#     try:
#         oVBox = ctx['vb']
#         olVBoxGuestOSDesc = oVBox.getGuestOSDescsBySubtype(OSSubtype)
#         for item in olVBoxGuestOSDesc:
#             oVirtualboxGetguestosdescsbysubtypeResponse.GuestOSDescs.append(item)
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oVirtualboxGetguestosdescsbysubtypeResponse)
#     return response, httpCode


# def i_virtualbox_getguestossubtypesbyfamilyid(family=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::getGuestOSSubtypesByFamilyId

#     :param family: 
#     :type family: str

#     :rtype: VirtualBoxGetGuestOSSubtypesByFamilyIdResponse
#     """

#     oError = None
#     httpCode = HTTPStatus.OK

#     vbox_utils_commonChecks()

#     oVirtualboxGetguestossubtypesbyfamilyidResponse = VirtualBoxGetGuestOSSubtypesByFamilyIdResponse([])

#     try:
#         oVBox = ctx['vb']
#         olVBoxGuestOSSubtype = oVBox.getGuestOSSubtypesByFamilyId(family)
#         for item in olVBoxGuestOSSubtype:
#             oVirtualboxGetguestossubtypesbyfamilyidResponse.OSsubtypes.append(item)
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oVirtualboxGetguestossubtypesbyfamilyidResponse)
#     return response, httpCode


# def i_virtualbox_getguestostype(select=None, id=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::getGuestOSType

#     :param select: The object attributes separated by comma
#     :type select: str
#     :param id: 
#     :type id: str

#     :rtype: GuestOSTypeObjWrapperResponse
#     """

#     oError = None
#     httpCode = HTTPStatus.OK

#     vbox_utils_commonChecks()

#     oGuestOSTypeResponse = GuestOSTypeObjWrapperResponse()

#     try:
#         oVBox = ctx['vb']
#         oVBoxGuestOSType = oVBox.getGuestOSType(id)
#         oGuestOSTypeResponse = i_fill_guestostype(oVBoxGuestOSType, select)
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oGuestOSTypeResponse)
#     return response, httpCode
