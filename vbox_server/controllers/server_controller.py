import connexion
import six

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
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


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
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def virtualbox_createunattendedinstaller():  # noqa: E501
    """
    Call interface method IVirtualBox::createUnattendedInstaller


    :rtype: UnattendedResponse
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def virtualbox_getextradata(key=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getExtraData

    :param key: 
    :type key: str

    :rtype: MediumGetpropertyResponse
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def virtualbox_getextradatakeys():  # noqa: E501
    """
    Call interface method IVirtualBox::getExtraDataKeys


    :rtype: VirtualboxGetextradatakeysResponse
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def virtualbox_getguestosdescsbysubtype(OSSubtype=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getGuestOSDescsBySubtype

    :param OSSubtype: 
    :type OSSubtype: str

    :rtype: VirtualboxGetguestosdescsbysubtypeResponse
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def virtualbox_getguestossubtypesbyfamilyid(family=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getGuestOSSubtypesByFamilyId

    :param family: 
    :type family: str

    :rtype: VirtualboxGetguestossubtypesbyfamilyidResponse
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def virtualbox_getguestostype(select=None, id=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getGuestOSType

    :param select: The object attributes separated by comma
    :type select: str
    :param id: 
    :type id: str

    :rtype: GuestOSTypeResponse
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


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
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def virtualbox_openmedium(oVirtualBoxOpenMediumRequestBody):  # noqa: E501
    """
    Call interface method IVirtualBox::openMedium

    :param oVirtualBoxOpenMediumRequestBody: 
    :type oVirtualBoxOpenMediumRequestBody: dict | bytes

    :rtype: MediumResponse
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def virtualbox_setextradata(oVirtualBoxSetExtraDataRequestBody):  # noqa: E501
    """
    Call interface method IVirtualBox::setExtraData

    :param oVirtualBoxSetExtraDataRequestBody: 
    :type oVirtualBoxSetExtraDataRequestBody: dict | bytes

    :rtype: None
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED
