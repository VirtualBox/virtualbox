"""VBox REST API

Copyright (c) 2025 Oracle and/or its affiliates.
Licensed under the Universal Permissive License v 1.0 as shown at https://oss.oracle.com/licenses/upl

SPDX-License-Identifier: UPL-1.0
"""

# pylint: disable=invalid-name
# pylint: disable=consider-using-f-string
# pylint: disable=line-too-long
# pylint: disable=undefined-variable
import os
import platform
import logging
from http import HTTPStatus
from flask import jsonify

if os.name == 'nt' or platform.system() == 'Windows':
    from pywintypes import com_error as COMException
else:
    from xpcom import COMException

from typing import List, Dict  # noqa: F401

from vbox_server.global_settings import *
from vbox_server.utils.vbox_utils import *
from vbox_server.utils.enum_conversion import *
from vbox_server.utils.object_conversion import *
from vbox_server.models.machine import Machine  # noqa: E501

from vbox_server.models.error import Error  # noqa: E501
from vbox_server.models.machine_state_enum_array_wrapper_response import MachineStateEnumArrayWrapperResponse  # noqa: E501
from vbox_server.models.medium_obj_wrapper_response import MediumObjWrapperResponse  # noqa: E501
from vbox_server.models.virtual_box_check_firmware_present_response import VirtualBoxCheckFirmwarePresentResponse  # noqa: E501
from vbox_server.models.virtual_box_compose_machine_filename_response import VirtualBoxComposeMachineFilenameResponse  # noqa: E501
from vbox_server.models.virtual_box_get_extra_data_keys_response import VirtualBoxGetExtraDataKeysResponse  # noqa: E501
from vbox_server.models.virtual_box_get_tracked_object_ids_response import VirtualBoxGetTrackedObjectIdsResponse  # noqa: E501
from vbox_server.models.virtual_box_get_tracked_object_response import VirtualBoxGetTrackedObjectResponse  # noqa: E501
from vbox_server.models.virtual_box_obj_wrapper_response import VirtualBoxObjWrapperResponse  # noqa: E501


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

    oVirtualBoxResponse = VirtualBoxObjWrapperResponse()
    try:
        oVirtualBoxResponse.server = i_fill_virtualbox(oVBox, select)
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

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK
    vBoxPlatformArchitecture = swagger_to_vbox_platformarchitecture(platformArchitecture)
    vBoxFirmwareType = swagger_to_vbox_firmwaretype(firmwareType)
    if vBoxFirmwareType is None:
        return "The requested firmware type " + str(firmwareType) + " wasn't found", HTTPStatus.NOT_FOUND
    
    if version is None: version=''

    oVirtualboxCheckfirmwarepresentResponse = VirtualBoxCheckFirmwarePresentResponse()
    try:
        oVBox = ctx['vb']
        bRes, sFile, sUrl = oVBox.checkFirmwarePresent(vBoxPlatformArchitecture, vBoxFirmwareType, version)
        if bRes == True:
            logging.info('Successfully get the information about firmware')
            logging.info('The command result is ' + str(bRes))
            oVirtualboxCheckfirmwarepresentResponse.url = sUrl
            oVirtualboxCheckfirmwarepresentResponse.file = sFile
            oVirtualboxCheckfirmwarepresentResponse.result = bRes
        else:
            logging.info('Something is wrong with the passed data or some values are empty ')

    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oVirtualboxCheckfirmwarepresentResponse)
    return response, httpCode


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

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oVirtualboxComposemachinefilenameResponse = VirtualBoxComposeMachineFilenameResponse()
    try:
        oVBox = ctx['vb']
        sFullSettingFilePath = oVBox.composeMachineFilename(name, group, createFlags, baseFolder)
        if sFullSettingFilePath!='':
            logging.info('Successfully get the full path of the settings file name')
            logging.info('The command result is ' + sFullSettingFilePath)
            oVirtualboxComposemachinefilenameResponse.file = sFullSettingFilePath
        else:
            logging.info('Weird! The full path of the settings file name is empty')

    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oVirtualboxComposemachinefilenameResponse)
    return response, httpCode


# def i_virtualbox_createunattendedinstaller():  # noqa: E501
#     """
#     Call interface method IVirtualBox::createUnattendedInstaller


#     :rtype: UnattendedResponse
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_virtualbox_getextradata(key=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::getExtraData

#     :param key: 
#     :type key: str

#     :rtype: MediumGetpropertyResponse
#     """

#     vbox_utils_commonChecks()

#     oError = None
#     httpCode = HTTPStatus.OK

#     try:
#         oVBox = ctx['vb']
#         res = oVBox.getExtraData(key)
#         if res!='':
#             logging.info('Successfully get the value of VirtualBox extra data ' + key)
#             logging.info('The command result is ' + res)
#         else:
#             logging.info('Unknown extra data or the value is empty ')

#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else res)
#     return response, httpCode


def i_virtualbox_getextradatakeys():  # noqa: E501
    """
    Call interface method IVirtualBox::getExtraDataKeys


    :rtype: VirtualboxGetextradatakeysResponse
    """

    vbox_utils_commonChecks()

    oVM = None
    oError = None
    httpCode = HTTPStatus.OK

    oVirtualboxGetextradatakeysResponse = VirtualBoxGetExtraDataKeysResponse()
    try:
        oVBox = ctx['vb']
        olKeys = oVBox.getExtraDataKeys()
        keys = []
        for item in olKeys:
            logging.info(item)
            keys.append(item)

        oVirtualboxGetextradatakeysResponse.keys = keys
        logging.info('Successfully get the list of VirtualBox extra keys')

    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oVirtualboxGetextradatakeysResponse)
    return response, httpCode


def i_virtualbox_getmachinestates(machines=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getMachineStates

    :param machines: Put here an ID of requested IMachine VirtualBox object
    :type machines: List[str]

    :rtype: MachineStateArrayResponse
    """

    oError = None
    httpCode = HTTPStatus.OK

    vbox_utils_commonChecks()

    lMachine = []

    for vmid in machines:
        # reset oError to None after this call
        oVM, oError = vbox_utils_find_machine(vmid)
        if oVM is None:
            httpCode = HTTPStatus.PRECONDITION_FAILED
            oError = Error(httpCode, "Machine with the id %vmid hasn\'t registered in VirtualBox" % (vmid))
            return jsonify(oError), httpCode
        else:
            oError = None
            lMachine.append(oVM)

    oMachineStateArrayResponse = MachineStateEnumArrayWrapperResponse()
    try:
        oVBox = ctx['vb']
        oVMStateList = oVBox.getMachineStates(lMachine)
        lMachineState = []
        for state in oVMStateList:
            sState = vbox_to_swagger_machinestate(state)
            lMachineState.append(sState)

        oMachineStateArrayResponse.states = lMachineState
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oMachineStateArrayResponse)
    return response, httpCode


def i_virtualbox_openmedium(oVirtualBoxOpenMediumRequestBody):  # noqa: E501
    """
    Call interface method IVirtualBox::openMedium

    :param oVirtualBoxOpenMediumRequestBody: 
    :type oVirtualBoxOpenMediumRequestBody: dict | bytes

    :rtype: MediumResponse
    """

    oError = None
    httpCode = 200 #(OK)

    vbox_utils_commonChecks()

    location = oVirtualBoxOpenMediumRequestBody.location

    oMediumResponse = MediumObjWrapperResponse()

    accessMode = swagger_to_vbox_accessmode(oVirtualBoxOpenMediumRequestBody.accessMode)
    if accessMode is None:
        return "Unknown access mode " + str(accessMode), HTTPStatus.NOT_FOUND

    deviceType = swagger_to_vbox_devicetype(oVirtualBoxOpenMediumRequestBody.deviceType)
    if deviceType is None or deviceType == ctx['const'].DeviceType_Network:
        return "Unknown or unsupported device type " + str(deviceType), HTTPStatus.NOT_FOUND

    try:
        oVBox = ctx['vb']
        oMedium = oVBox.openMedium(location, deviceType, accessMode, False)
        oMediumResponse.medium = i_fill_medium(oMedium)
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oMediumResponse)
    return response, httpCode


def i_virtualbox_setextradata(oVirtualBoxSetExtraDataRequestBody):  # noqa: E501
    """
    Call interface method IVirtualBox::setExtraData

    :param oVirtualBoxSetExtraDataRequestBody: 
    :type oVirtualBoxSetExtraDataRequestBody: dict | bytes

    :rtype: None
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


from datetime import datetime
def i_virtualbox_gettrackedobject(trObjId=None):  # noqa: E501
    """
    Call interface method IVirtualBox::getTrackedObject

    :param trObjId:
    :type trObjId: str

    :rtype: VirtualboxGettrackedobjectResponse
    """
    oError = None
    httpCode = HTTPStatus.OK

    vbox_utils_commonChecks()

    try:
        oVBox = ctx['vb']
        oVBoxMgr = ctx['global']

    except Exception as e:
        logging.info ('couldn\'t get the VirtualBox object')
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))
        return jsonify(oError), httpCode

    o = VirtualBoxGetTrackedObjectResponse()
    try:
        oIUnknown, enmState, creationTime, deletionTime = oVBox.getTrackedObject(trObjId)
        o.state = vbox_to_swagger_trackedobjectstate(enmState)
        o.creationTime = creationTime/1000
        if (deletionTime != 0):
            o.deletionTime = deletionTime/1000
        else:
            o.deletionTime = 0

        logging.info('state ' + o.state)
        logging.info('creation time ' + str(datetime.fromtimestamp(o.creationTime)))
        logging.info('deletion time ' + str(datetime.fromtimestamp(o.deletionTime)))

        if oIUnknown is None:
            httpCode = HTTPStatus.NOT_FOUND
            oError = Error(httpCode, str("Can\'t find the object with Id " + trObjId + ' on the server'))
            return jsonify(oError), httpCode
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str("Can\'t get object with Id " + trObjId + ' from the server'))
        return jsonify(oError), httpCode

    alInterfaceName = ['IProgress', 'ISession', 'IMedium', 'IMachine']
    oRes = None
    obj = None

    for i in alInterfaceName:
        try:
            obj = oVBoxMgr.queryInterface(oIUnknown, i)

            # Windows hack, on Windows queryInterface() ALWAYS returns an object
            # if there is a way to convert one interface to another one.
            if platform.system() == "Windows":
                if oIUnknown.CLSID != obj.CLSID:
                    continue

        except COMException as e:
            # This branch for XPCOM (Linux).
            # queryInterface() on Linux throws an exception if the inquired interface wasn't found.
            # The returned value is negative. In instance, negative -2147467262 is equal to positive 0x80004002.
            # The conversion to unsigned integer is required for comparison with 0x80004002.
            # The error 0x80004002 is "'Interface not supported (NS_ERROR_NO_INTERFACE)'".
            from ctypes import c_uint32
            comErrorHex = c_uint32(e.args[0]).value
            if comErrorHex == 0x80004002:
                continue

            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))
            break

        except Exception as e:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))
            break

        try:
            if obj is not None:
                if i == 'IProgress':
                    oRes = i_fill_progress(obj)
                elif i == 'ISession':
                    oRes = i_fill_session(obj)
                elif i == 'IMedium':
                    oRes = i_fill_medium(obj)
                elif i == 'IMachine':
                    oRes = i_fill_machine(obj)
        except Exception as e:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))
            break

        if oRes: break

    if oRes: o.p_iface = oRes

    response = jsonify(oError if oError is not None else o)
    return response, httpCode


def i_virtualbox_gettrackedobjectids(name=None):
    """
    Call interface method IVirtualBox::getTrackedObjectIds

    :param name: 
    :type name: str

    :rtype: VirtualboxGettrackedobjectidsResponse
    """

    oError = None
    httpCode = HTTPStatus.OK

    vbox_utils_commonChecks()

    try:
        oVBox = ctx['vb']

    except Exception as e:
        logging.info ('couldn\'t get the VirtualBox object')
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))
        return jsonify(oError), httpCode

    oVirtualboxGettrackedobjectidsResponse = VirtualBoxGetTrackedObjectIdsResponse()
    try:
        if name and len(name) != 0:
            oObjIdList = oVBox.getTrackedObjectIds(name)
            if len(oObjIdList) != 0:
                oVirtualboxGettrackedobjectidsResponse.objIdsList = list()
                for i in oObjIdList:
                    oVirtualboxGettrackedobjectidsResponse.objIdsList.append(i)
            else:
                httpCode = HTTPStatus.NOT_FOUND
                oError = Error(httpCode, 'Unknown interface or no objects were found for the passed interface name')
        else:
            httpCode = HTTPStatus.BAD_REQUEST
            oError = Error(httpCode, 'The passed interface name string is Null or empty')

    except COMException as e:
        httpCode = HTTPStatus.NOT_FOUND
        from ctypes import c_uint32
        comErrorHex = c_uint32(e.args[0]).value
        if platform.system() == "Windows":
            logging.info ('COM error 0x%X' % (comErrorHex))
        else:
            logging.info ('XPCOM error %X' % (comErrorHex))
        oError = Error(httpCode, str(e))

    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        e.msg = e.msg + ' At least, check that VirtualBox is running.'
        oError = Error(httpCode, str(e))
    response = jsonify(oError if oError is not None else oVirtualboxGettrackedobjectidsResponse)
    return response, httpCode