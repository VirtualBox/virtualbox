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

from vbox_server.models.appliance_obj_wrapper_response import ApplianceObjWrapperResponse  # noqa: E501
from vbox_server.models.error import Error  # noqa: E501
from vbox_server.models.progress_obj_wrapper_response import ProgressObjWrapperResponse  # noqa: E501
from vbox_server import util


def i_appliance_addpasswords(applianceid, oApplianceAddPasswordsRequestBody):  # noqa: E501
    """
    Call interface method IAppliance::addPasswords

    :param applianceid: The Id of appliance
    :type applianceid: str
    :param oApplianceAddPasswordsRequestBody: 
    :type oApplianceAddPasswordsRequestBody: dict | bytes

    :rtype: None
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_appliance_createvirtualsystemdescriptions(applianceid, requested=None):  # noqa: E501
#     """
#     Call interface method IAppliance::createVirtualSystemDescriptions

#     :param applianceid: The Id of appliance
#     :type applianceid: str
#     :param requested: 
#     :type requested: int

#     :rtype: ApplianceCreatevirtualsystemdescriptionsResponse
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_appliance_getmediumidsforpasswordid(applianceid, passwordId=None):  # noqa: E501
    """
    Call interface method IAppliance::getMediumIdsForPasswordId

    :param applianceid: The Id of appliance
    :type applianceid: str
    :param passwordId: 
    :type passwordId: str

    :rtype: ApplianceGetmediumidsforpasswordidResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_appliance_getpasswordids(applianceid):  # noqa: E501
    """
    Call interface method IAppliance::getPasswordIds

    :param applianceid: The Id of appliance
    :type applianceid: str

    :rtype: ApplianceGetmediumidsforpasswordidResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_appliance_getwarnings(applianceid):  # noqa: E501
    """
    Call interface method IAppliance::getWarnings

    :param applianceid: The Id of appliance
    :type applianceid: str

    :rtype: ApplianceGetwarningsResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_appliance_importmachines(applianceid, options=None):  # noqa: E501
    """
    Call interface method IAppliance::importMachines

    :param applianceid: The Id of appliance
    :type applianceid: str
    :param options: For the possible values of enumeration look into #/definitions/ImportOptions
    :type options: List[str]

    :rtype: ProgressResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_appliance_interpret(applianceid):  # noqa: E501
#     """
#     Call interface method IAppliance::interpret

#     :param applianceid: The Id of appliance
#     :type applianceid: str

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_appliance_read(applianceid, file=None):  # noqa: E501
    """
    Call interface method IAppliance::read

    :param applianceid: The Id of appliance
    :type applianceid: str
    :param file: 
    :type file: str

    :rtype: ProgressResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_appliance_write(applianceid, oApplianceWriteRequestBody):  # noqa: E501
    """
    Call interface method IAppliance::write

    :param applianceid: The Id of appliance
    :type applianceid: str
    :param oApplianceWriteRequestBody: 
    :type oApplianceWriteRequestBody: dict | bytes

    :rtype: ProgressResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_virtualbox_createappliance():  # noqa: E501
#     """
#     Call interface method IVirtualBox::createAppliance


#     :rtype: ApplianceResponse
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED
