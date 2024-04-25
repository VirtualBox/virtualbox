# pylint: disable=invalid-name
# pylint: disable=consider-using-f-string
# pylint: disable=line-too-long
# pylint: disable=undefined-variable
import logging
from http import HTTPStatus
from flask import jsonify

from vbox_server.global_settings import *
from vbox_server.utils.vbox_utils import *
from vbox_server.utils.restapi_objects_functions import *
from vbox_server.models.machine import Machine  # noqa: E501
from vbox_server.models.virtual_box_response import VirtualBoxResponse  # noqa: E501
from vbox_server.models.error import Error  # noqa: E501


def i_list_machines(fAll=False, select=None, groups=None):  # noqa: E501
    """List VirtualBox machines.

    Returns an array of Machine objects registered within this VirtualBox instance. # noqa: E501

    :param select: Allows filtering so only the requested attributes are returned
    :type select: str
    :param groups: Allows filtering so only the machines from the requested groups are returned
    :type groups: str
    :param fAll: return the list of all machines registered in VirtualBox 
    :type fAll: bool

    :rtype: List[Machine]
    """
    oError = None
    httpCode = HTTPStatus.OK

    vbox_utils_commonChecks()

    try:
        # olVBoxMachines = ctx['vb'].getMachines()
        oVBox = ctx['vb']
        olVBoxMachines = ctx['global'].getArray(oVBox, 'Machines')
    except Exception as e:
        logging.info ('couldn\'t get the VMs list')
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))
        return jsonify(oError), httpCode

    logging.info('Total number of machines is %d' % (len(olVBoxMachines)))

    olPassedGroupsList = list()
    if groups is not None and len(groups) > 0:
        if select is not None:
            select = select + ',groups'

        olPassedGroupsList = groups.split(',')
        logging.info(olPassedGroupsList)

    outlMachines = list()
    lIntersect = list()
    for item in olVBoxMachines:
        oMachine = Machine()
        if len(olPassedGroupsList) > 0:
            olMachineGroups = ctx['global'].getArray(item,'groups')
            logging.info(olMachineGroups)
            lIntersect = set(olPassedGroupsList).intersection(olMachineGroups)
        else:
            logging.info('The result may be an empty list because the list of the passed groups is empty')

        if len(lIntersect) > 0:               
            try:
                oMachine.accessible = item.accessible
                if oMachine.accessible:
                    oMachine = i_fill_machine(item, select)
                    outlMachines.append(oMachine)
            except Exception as e:
                httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
                oError = Error(httpCode, str(e))
                break
        elif fAll == True and len(olPassedGroupsList) == 0:
            try:
                oMachine.accessible = item.accessible
                if oMachine.accessible:
                    oMachine = i_fill_machine(item, select)
                    outlMachines.append(oMachine)
            except Exception as e:
                httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
                oError = Error(httpCode, str(e))
                break

    logging.info('Total number of found machines is %d' % (len(outlMachines)))
    response = jsonify(oError if oError is not None else outlMachines)
    return response, httpCode


def i_synthetic_getserver(select=None):  # noqa: E501
    """
    Call interface method IVirtualBox::syntheticGetServer

    :param select: The object attributes separated by comma
    :type select: str

    :rtype: VirtualBoxResponse
    """

    oError = None
    httpCode = 200 #(OK)

    vbox_utils_commonChecks()

    try:
        oVBox = ctx['vb']
    except Exception as e:
        logging.info ('couldn\'t get the VirtualBox object')
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))
        return jsonify(oError), httpCode

    oVirtualBoxResponse = VirtualBoxResponse()
    try:
        oVirtualBoxResponse.server = i_fill_virtual_box(oVBox, select)
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oVirtualBoxResponse)
    return response, httpCode


def i_virtualbox_getmachinesbygroups(select=None, groups=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getMachinesByGroups

    :param select: The object attributes separated by comma
    :type select: str
    :param groups: 
    :type groups: str

    :rtype: MachineArrayResponse
    """

    if groups is None or len(groups) == 0:
        return i_list_machines(True, select, groups)

    return i_list_machines(False, select, groups)


def i_virtualbox_checkfirmwarepresent(platformArchitecture=None, firmwareType=None, version=None):  # noqa: E501
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


def i_virtualbox_composemachinefilename(name=None, group=None, createFlags=None, baseFolder=None):  # noqa: E501
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


def i_virtualbox_createunattendedinstaller():  # noqa: E501
    """
    Call interface method IVirtualBox::createUnattendedInstaller


    :rtype: UnattendedResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_virtualbox_getextradata(key=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getExtraData

    :param key: 
    :type key: str

    :rtype: MediumGetpropertyResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_virtualbox_getextradatakeys():  # noqa: E501
    """
    Call interface method IVirtualBox::getExtraDataKeys


    :rtype: VirtualboxGetextradatakeysResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_virtualbox_getguestosdescsbysubtype(OSSubtype=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getGuestOSDescsBySubtype

    :param OSSubtype: 
    :type OSSubtype: str

    :rtype: VirtualboxGetguestosdescsbysubtypeResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_virtualbox_getguestossubtypesbyfamilyid(family=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getGuestOSSubtypesByFamilyId

    :param family: 
    :type family: str

    :rtype: VirtualboxGetguestossubtypesbyfamilyidResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_virtualbox_getguestostype(select=None, id=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getGuestOSType

    :param select: The object attributes separated by comma
    :type select: str
    :param id: 
    :type id: str

    :rtype: GuestOSTypeResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_virtualbox_getmachinestates(machines=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getMachineStates

    :param machines: Put here an ID of requested IMachine VirtualBox object
    :type machines: List[str]

    :rtype: MachineStateArrayResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_virtualbox_openmedium(oVirtualBoxOpenMediumRequestBody):  # noqa: E501
    """
    Call interface method IVirtualBox::openMedium

    :param oVirtualBoxOpenMediumRequestBody: 
    :type oVirtualBoxOpenMediumRequestBody: dict | bytes

    :rtype: MediumResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_virtualbox_setextradata(oVirtualBoxSetExtraDataRequestBody):  # noqa: E501
    """
    Call interface method IVirtualBox::setExtraData

    :param oVirtualBoxSetExtraDataRequestBody: 
    :type oVirtualBoxSetExtraDataRequestBody: dict | bytes

    :rtype: None
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED
