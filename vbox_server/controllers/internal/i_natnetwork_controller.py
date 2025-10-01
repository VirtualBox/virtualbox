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
from vbox_server.utils.decorators import *

from vbox_server.models.error import Error  # noqa: E501
from vbox_server.models.nat_network_add_local_mapping_request_body import NATNetworkAddLocalMappingRequestBody  # noqa: E501
from vbox_server.models.nat_network_add_port_forward_rule_request_body import NATNetworkAddPortForwardRuleRequestBody  # noqa: E501
from vbox_server.models.nat_network_remove_port_forward_rule_request_body import NATNetworkRemovePortForwardRuleRequestBody  # noqa: E501
from vbox_server.models.nat_network_obj_wrapper_response import NATNetworkObjWrapperResponse  # noqa: E501
from vbox_server import util


@natnetworkDecorator
def i_natnetwork_addlocalmapping(oVBoxObj, oNATNetworkAddLocalMappingRequestBody: NATNetworkAddLocalMappingRequestBody):  # noqa: E501
    """
    Call interface method INATNetwork::addLocalMapping

    :param networkid: The Id of network
    :type networkid: str
    :param oNATNetworkAddLocalMappingRequestBody: 
    :type oNATNetworkAddLocalMappingRequestBody: dict | bytes

    :rtype: None
    """
    
    vbox_utils_commonChecks()

    hostId = oNATNetworkAddLocalMappingRequestBody.hostid
    offset = oNATNetworkAddLocalMappingRequestBody.offset

    oError = None
    httpCode = HTTPStatus.OK
    oCurrNATNetwork = oVBoxObj

    try:
        oCurrNATNetwork.addLocalMapping(hostId, offset)
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else f"The local mapping for NAT network '{oCurrNATNetwork.networkName}' was successfully added")
    return response, httpCode


@natnetworkDecorator
def i_natnetwork_addportforwardrule(oVBoxObj, oNATNetworkAddPortForwardRuleRequestBody: NATNetworkAddPortForwardRuleRequestBody):  # noqa: E501
    """
    Call interface method INATNetwork::addPortForwardRule

    :param networkid: The Id of network
    :type networkid: str
    :param oNATNetworkAddPortForwardRuleRequestBody: 
    :type oNATNetworkAddPortForwardRuleRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    fIPv6 = oNATNetworkAddPortForwardRuleRequestBody.isIpv6
    guestIP = oNATNetworkAddPortForwardRuleRequestBody.guestIP
    guestPort= oNATNetworkAddPortForwardRuleRequestBody.guestPort
    hostIP = oNATNetworkAddPortForwardRuleRequestBody.hostIP
    hostPort = oNATNetworkAddPortForwardRuleRequestBody.hostPort
    proto = swagger_to_vbox_natprotocol(oNATNetworkAddPortForwardRuleRequestBody.proto)
    ruleName = oNATNetworkAddPortForwardRuleRequestBody.ruleName

    oError = None
    httpCode = HTTPStatus.OK
    oCurrNATNetwork = oVBoxObj

    try:
        oCurrNATNetwork.addPortForwardRule(fIPv6, ruleName, proto, hostIP, hostPort, guestIP, guestPort)
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else f"The rule '{ruleName}' for NAT network '{oCurrNATNetwork.networkName}' was successfully added")
    return response, httpCode


@natnetworkDecorator
def i_natnetwork_removeportforwardrule(oVBoxObj, oNATNetworkRemovePortForwardRuleRequestBody: NATNetworkRemovePortForwardRuleRequestBody):  # noqa: E501
    """
    Call interface method INATNetwork::removePortForwardRule

    :param networkid: The Id of network
    :type networkid: str
    :param oNATNetworkRemovePortForwardRuleRequestBody: 
    :type oNATNetworkRemovePortForwardRuleRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    ruleName = oNATNetworkRemovePortForwardRuleRequestBody.ruleName
    fIPv6 = oNATNetworkRemovePortForwardRuleRequestBody.isIpv6

    oError = None
    httpCode = HTTPStatus.OK
    oCurrNATNetwork = oVBoxObj


    try:
        oCurrNATNetwork.removePortForwardRule(fIPv6, ruleName)
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else f"The rule '{ruleName}' for NAT network '{oCurrNATNetwork.networkName}' was successfully removed")
    return response, httpCode


# def i_natnetwork_start(networkid):  # noqa: E501
#     """
#     Call interface method INATNetwork::start

#     :param networkid: The Id of network
#     :type networkid: str

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_natnetwork_stop(networkid):  # noqa: E501
#     """
#     Call interface method INATNetwork::stop

#     :param networkid: The Id of network
#     :type networkid: str

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_virtualbox_createnatnetwork(networkName=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::createNATNetwork

#     :param networkName: 
#     :type networkName: str

#     :rtype: NATNetworkResponse
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_virtualbox_findnatnetworkbyname(select=None, networkName=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::findNATNetworkByName

#     :param select: The object attributes separated by comma
#     :type select: str
#     :param networkName: 
#     :type networkName: str

#     :rtype: NATNetworkResponse
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_virtualbox_removenatnetwork(network=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::removeNATNetwork

#     :param network: Put here an ID of requested INATNetwork VirtualBox object
#     :type network: str

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED
