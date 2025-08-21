"""VBox REST API

Copyright (c) 2025 Oracle and/or its affiliates.
Licensed under the Universal Permissive License v 1.0 as shown at https://oss.oracle.com/licenses/upl

SPDX-License-Identifier: UPL-1.0
"""

import connexion
import six

from vbox_server.models.virtual_box_open_medium_request_body import VirtualBoxOpenMediumRequestBody
from vbox_server.models.virtual_box_set_extra_data_request_body import VirtualBoxSetExtraDataRequestBody

from vbox_server.controllers.internal.i_server_controller import *


def synthetic_getserver(select=None):  # noqa: E501
    """
    Call interface method ISynthetic::getServer

    :param select: The object attributes separated by comma
    :type select: str

    :rtype: VirtualBoxResponse
    """
    return i_synthetic_getserver(select)


def virtualbox_checkfirmwarepresent(platformArchitecture=None, firmwareType=None, version=None):  # noqa: E501
    """
    Call interface method IVirtualBox::checkFirmwarePresent

    :param platformArchitecture: For the possible values of enumeration look into #/definitions/PlatformArchitecture
    :type platformArchitecture: str
    :param firmwareType: For the possible values of enumeration look into #/definitions/FirmwareType
    :type firmwareType: str
    :param version: 
    :type version: str

    :rtype: VirtualboxCheckfirmwarepresentResponse
    """
    return i_virtualbox_checkfirmwarepresent(platformArchitecture, firmwareType, version)


def virtualbox_composemachinefilename(name=None, group=None, createFlags=None, baseFolder=None):  # noqa: E501
    """
    Call interface method IVirtualBox::composeMachineFilename

    :param name: 
    :type name: str
    :param group: 
    :type group: str
    :param createFlags: 
    :type createFlags: str
    :param baseFolder: 
    :type baseFolder: str

    :rtype: VirtualboxComposemachinefilenameResponse
    """
    return i_virtualbox_composemachinefilename(name, group, createFlags, baseFolder)


def virtualbox_createunattendedinstaller():  # noqa: E501
    """
    Call interface method IVirtualBox::createUnattendedInstaller


    :rtype: UnattendedResponse
    """
    return i_virtualbox_createunattendedinstaller()


def virtualbox_getextradata(key=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getExtraData

    :param key: 
    :type key: str

    :rtype: MediumGetpropertyResponse
    """
    return i_virtualbox_getextradata(key)


def virtualbox_getextradatakeys():  # noqa: E501
    """
    Call interface method IVirtualBox::getExtraDataKeys


    :rtype: VirtualboxGetextradatakeysResponse
    """
    return i_virtualbox_getextradatakeys()


def virtualbox_getguestosdescsbysubtype(OSSubtype=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getGuestOSDescsBySubtype

    :param OSSubtype: 
    :type OSSubtype: str

    :rtype: VirtualboxGetguestosdescsbysubtypeResponse
    """
    return i_virtualbox_getguestosdescsbysubtype(OSSubtype)


def virtualbox_getguestossubtypesbyfamilyid(family=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getGuestOSSubtypesByFamilyId

    :param family: 
    :type family: str

    :rtype: VirtualboxGetguestossubtypesbyfamilyidResponse
    """
    return i_virtualbox_getguestossubtypesbyfamilyid(family)


def virtualbox_getguestostype(select=None, id=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getGuestOSType

    :param select: The object attributes separated by comma
    :type select: str
    :param id: 
    :type id: str

    :rtype: GuestOSTypeResponse
    """
    return i_virtualbox_getguestostype(select, id)


def virtualbox_getmachinesbygroups(select=None, groups=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getMachinesByGroups

    :param select: The object attributes separated by comma
    :type select: str
    :param groups: 
    :type groups: str

    :rtype: MachineArrayResponse
    """
    return i_virtualbox_getmachinesbygroups(select, groups)


def virtualbox_getmachinestates(machines=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getMachineStates

    :param machines: Put here an ID of requested IMachine VirtualBox object
    :type machines: List[str]

    :rtype: MachineStateArrayResponse
    """
    return i_virtualbox_getmachinestates(machines)


def virtualbox_gettrackedobject(trObjId=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getTrackedObject

    :param trObjId: 
    :type trObjId: str

    :rtype: VirtualboxGettrackedobjectResponse
    """
    return i_virtualbox_gettrackedobject(trObjId)


def virtualbox_gettrackedobjectids(name=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getTrackedObjectIds

    :param name: 
    :type name: str

    :rtype: VirtualboxGettrackedobjectidsResponse
    """
    return i_virtualbox_gettrackedobjectids(name)


def virtualbox_openmedium(oVirtualBoxOpenMediumRequestBody):  # noqa: E501
    """
    Call interface method IVirtualBox::openMedium

    :param oVirtualBoxOpenMediumRequestBody: 
    :type oVirtualBoxOpenMediumRequestBody: dict | bytes

    :rtype: MediumResponse
    """
    if connexion.request.is_json:
        oVirtualBoxOpenMediumRequestBody = VirtualBoxOpenMediumRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_virtualbox_openmedium(oVirtualBoxOpenMediumRequestBody)


def virtualbox_setextradata(oVirtualBoxSetExtraDataRequestBody):  # noqa: E501
    """
    Call interface method IVirtualBox::setExtraData

    :param oVirtualBoxSetExtraDataRequestBody: 
    :type oVirtualBoxSetExtraDataRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oVirtualBoxSetExtraDataRequestBody = VirtualBoxSetExtraDataRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_virtualbox_setextradata(oVirtualBoxSetExtraDataRequestBody)
