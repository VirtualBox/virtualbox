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
from vbox_server.utils.vbox_utils import *
from vbox_server.utils.decorators import *

from vbox_server.models.dhcp_config_obj_wrapper_response import DHCPConfigObjWrapperResponse  # noqa: E501
from vbox_server.models.dhcp_server_obj_wrapper_response import DHCPServerObjWrapperResponse  # noqa: E501
from vbox_server.models.dhcp_server_set_configuration_request_body import DHCPServerSetConfigurationRequestBody  # noqa: E501
from vbox_server.models.dhcp_server_start_request_body import DHCPServerStartRequestBody  # noqa: E501

@dhcpserverDecorator
def i_dhcpserver_getconfig(oVBoxObj, select=None, scope=None, name=None, slot=None, mayAdd=None):  # noqa: E501
    """
    Call interface method IDHCPServer::getConfig

    :param serverid: The Id of server
    :type serverid: str
    :param select: The object attributes separated by comma
    :type select: str
    :param scope: For the possible values of enumeration look into #/definitions/DHCPConfigScope
    :type scope: str
    :param name: 
    :type name: str
    :param slot: 
    :type slot: int
    :param mayAdd: 
    :type mayAdd: bool

    :rtype: DHCPConfigResponse
    """

    oError = None
    httpCode = HTTPStatus.OK

    oDHCPConfigResponse = DHCPConfigObjWrapperResponse()

    vbox_utils_commonChecks()

    oVBoxDHCPServer = oVBoxObj

    try:
        if scope == "GLOBAL": name = ""
        oVBoxDHCPConfig = oVBoxDHCPServer.getConfig(swagger_to_vbox_dhcpconfigscope(scope), name, slot, mayAdd)
        oDHCPConfigResponse.dhcpconfig = i_fill_dhcpconfig(oVBoxDHCPConfig, select)
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oDHCPConfigResponse)
    return response, httpCode


# def i_dhcpserver_restart(serverid):  # noqa: E501
#     """
#     Call interface method IDHCPServer::restart

#     :param serverid: The Id of server
#     :type serverid: str

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


@dhcpserverDecorator
def i_dhcpserver_setconfiguration(oVBoxObj, oDHCPServerSetConfigurationRequestBody: DHCPServerSetConfigurationRequestBody):  # noqa: E501
    """
    Call interface method IDHCPServer::setConfiguration

    :param serverid: The Id of server
    :type serverid: str
    :param oDHCPServerSetConfigurationRequestBody: 
    :type oDHCPServerSetConfigurationRequestBody: dict | bytes

    :rtype: None
    """

    oError = None
    httpCode = HTTPStatus.OK
    ipAddressFrom = oDHCPServerSetConfigurationRequestBody.FromIPAddress
    ipAddressTo = oDHCPServerSetConfigurationRequestBody.ToIPAddress
    ipAddress = oDHCPServerSetConfigurationRequestBody.IPAddress
    netMask = oDHCPServerSetConfigurationRequestBody.networkMask

    vbox_utils_commonChecks()

    oVBoxDHCPServer = oVBoxObj

    try:
        oVBoxDHCPServer.setConfiguration(ipAddress, netMask, ipAddressFrom, ipAddressTo)
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else f"Configuration for DHCP server using the internal network name '{oVBoxDHCPServer.networkName}' was set successfully")
    return response, httpCode


@dhcpserverDecorator
def i_dhcpserver_start(oVBoxObj, oDHCPServerStartRequestBody: DHCPServerStartRequestBody):  # noqa: E501
    """
    Call interface method IDHCPServer::start

    :param serverid: The Id of server
    :type serverid: str
    :param oDHCPServerStartRequestBody: 
    :type oDHCPServerStartRequestBody: dict | bytes

    :rtype: None
    """

    oError = None
    httpCode = HTTPStatus.OK
    strTrunkName = oDHCPServerStartRequestBody.trunkName
    strTrunktype = oDHCPServerStartRequestBody.trunkType

    vbox_utils_commonChecks()

    oVBoxDHCPServer = oVBoxObj

    try:
        oVBoxDHCPServer.start(strTrunkName, strTrunktype)
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else f"DHCP server using the internal network name '{oVBoxDHCPServer.networkName}' was started successfully")
    return response, httpCode


# def i_dhcpserver_stop(serverid):  # noqa: E501
#     """
#     Call interface method IDHCPServer::stop

#     :param serverid: The Id of server
#     :type serverid: str

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_virtualbox_createdhcpserver(name=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::createDHCPServer

#     :param name: 
#     :type name: str

#     :rtype: DHCPServerResponse
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_virtualbox_finddhcpserverbynetworkname(select=None, name=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::findDHCPServerByNetworkName

#     :param select: The object attributes separated by comma
#     :type select: str
#     :param name: 
#     :type name: str

#     :rtype: DHCPServerResponse
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_virtualbox_removedhcpserver(server=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::removeDHCPServer

#     :param server: Put here an ID of requested IDHCPServer VirtualBox object
#     :type server: str

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED
