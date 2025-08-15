"""VBox REST API

Copyright (c) 2024-2025 Oracle and/or its affiliates.
Licensed under the Universal Permissive License v 1.0 as shown at https://oss.oracle.com/licenses/upl

SPDX-License-Identifier: UPL-1.0
"""

# pylint: disable=invalid-name
# pylint: disable=consider-using-f-string
# pylint: disable=line-too-long
# pylint: disable=undefined-variable
import sys
import platform
import connexion
import logging
import time
# import base64
from http import HTTPStatus
from flask import jsonify
# from flask.json import loads
from connexion.lifecycle import ConnexionResponse
from werkzeug.datastructures import Headers

from vbox_server.global_settings import *
from vbox_server.utils.vbox_utils import *
from vbox_server.utils.enum_conversion import *
from vbox_server.utils.object_conversion import *
from vbox_server.utils.vbox_utils import vbox_utils_tryLockMachine as tryLockMachine
from vbox_server.utils.vbox_utils import vbox_utils_unlockAndDeleteSession as unlockAndDeleteSession
from vbox_server.utils.vbox_utils import vbox_utils_unlockSession as unlockSession
from vbox_server.utils.decorators import consoleDecorator
from vbox_server.utils.decorators import sessionDecorator
from vbox_server.utils.decorators import open_exclusive_session as openExclusiveSession
from vbox_server.utils.decorators import open_session as openSession

############################ Implemented or used ############################
from vbox_server.models.error import Error  # noqa: E501
from vbox_server.models.console_attach_usb_device_request_body import ConsoleAttachUSBDeviceRequestBody  # noqa: E501
from vbox_server.models.console_create_shared_folder_request_body import ConsoleCreateSharedFolderRequestBody  # noqa: E501
from vbox_server.models.machine_add_storage_controller_request_body import MachineAddStorageControllerRequestBody  # noqa: E501
from vbox_server.models.machine_add_usb_controller_request_body import MachineAddUSBControllerRequestBody  # noqa: E501
from vbox_server.models.machine_attach_device_request_body import MachineAttachDeviceRequestBody  # noqa: E501
from vbox_server.models.machine_attach_device_without_medium_request_body import MachineAttachDeviceWithoutMediumRequestBody  # noqa: E501
from vbox_server.models.machine_clone_to_request_body import MachineCloneToRequestBody  # noqa: E501
from vbox_server.models.machine_create_shared_folder_request_body import MachineCreateSharedFolderRequestBody  # noqa: E501
from vbox_server.models.machine_detach_device_request_body import MachineDetachDeviceRequestBody  # noqa: E501
from vbox_server.models.machine_enumerate_guest_properties_response import MachineEnumerateGuestPropertiesResponse  # noqa: E501
from vbox_server.models.machine_get_cpu_status_response import MachineGetCPUStatusResponse  # noqa: E501
from vbox_server.models.machine_get_guest_property_response import MachineGetGuestPropertyResponse  # noqa: E501
from vbox_server.models.machine_get_guest_property_timestamp_response import MachineGetGuestPropertyTimestampResponse  # noqa: E501
from vbox_server.models.machine_get_usb_controller_count_by_type_response import MachineGetUSBControllerCountByTypeResponse  # noqa: E501
from vbox_server.models.machine_launch_vm_process_request_body import MachineLaunchVMProcessRequestBody  # noqa: E501
from vbox_server.models.machine_mount_medium_request_body import MachineMountMediumRequestBody  # noqa: E501
from vbox_server.models.machine_move_to_request_body import MachineMoveToRequestBody  # noqa: E501
from vbox_server.models.machine_non_rotational_device_request_body import MachineNonRotationalDeviceRequestBody  # noqa: E501
from vbox_server.models.machine_obj_wrapper_response import MachineObjWrapperResponse  # noqa: E501
from vbox_server.models.machine_passthrough_device_request_body import MachinePassthroughDeviceRequestBody  # noqa: E501
from vbox_server.models.machine_query_log_filename_response import MachineQueryLogFilenameResponse  # noqa: E501
from vbox_server.models.machine_read_log_response import MachineReadLogResponse  # noqa: E501
from vbox_server.models.machine_set_auto_discard_for_device_request_body import MachineSetAutoDiscardForDeviceRequestBody  # noqa: E501
from vbox_server.models.machine_set_bandwidth_group_for_device_request_body import MachineSetBandwidthGroupForDeviceRequestBody  # noqa: E501
from vbox_server.models.machine_set_boot_order_request_body import MachineSetBootOrderRequestBody  # noqa: E501
from vbox_server.models.machine_set_extra_data_request_body import MachineSetExtraDataRequestBody  # noqa: E501
from vbox_server.models.machine_set_guest_property_request_body import MachineSetGuestPropertyRequestBody  # noqa: E501
from vbox_server.models.machine_set_guest_property_value_request_body import MachineSetGuestPropertyValueRequestBody  # noqa: E501
from vbox_server.models.machine_set_hot_pluggable_for_device_request_body import MachineSetHotPluggableForDeviceRequestBody  # noqa: E501
from vbox_server.models.machine_set_no_bandwidth_group_for_device_request_body import MachineSetNoBandwidthGroupForDeviceRequestBody  # noqa: E501
from vbox_server.models.machine_set_storage_controller_bootable_request_body import MachineSetStorageControllerBootableRequestBody  # noqa: E501
from vbox_server.models.machine_take_snapshot_request_body import MachineTakeSnapshotRequestBody  # noqa: E501
from vbox_server.models.machine_take_snapshot_response import MachineTakeSnapshotResponse  # noqa: E501
from vbox_server.models.machine_temporary_eject_device_request_body import MachineTemporaryEjectDeviceRequestBody  # noqa: E501
from vbox_server.models.machine_unmount_medium_request_body import MachineUnmountMediumRequestBody  # noqa: E501
from vbox_server.models.medium_attachment_obj_array_wrapper_response import MediumAttachmentObjArrayWrapperResponse  # noqa: E501
from vbox_server.models.medium_attachment_obj_wrapper_response import MediumAttachmentObjWrapperResponse  # noqa: E501
from vbox_server.models.medium_get_property_response import MediumGetPropertyResponse  # noqa: E501
from vbox_server.models.medium_obj_array_wrapper_response import MediumObjArrayWrapperResponse  # noqa: E501
from vbox_server.models.medium_obj_wrapper_response import MediumObjWrapperResponse  # noqa: E501
from vbox_server.models.network_adapter_obj_wrapper_response import NetworkAdapterObjWrapperResponse  # noqa: E501
from vbox_server.models.parallel_port_obj_wrapper_response import ParallelPortObjWrapperResponse  # noqa: E501
from vbox_server.models.paravirt_provider_enum_wrapper_response import ParavirtProviderEnumWrapperResponse  # noqa: E501
from vbox_server.models.platform_arm_get_cpu_property_response import PlatformARMGetCPUPropertyResponse  # noqa: E501
from vbox_server.models.platform_arm_set_cpu_property_request_body import PlatformARMSetCPUPropertyRequestBody  # noqa: E501
from vbox_server.models.platform_x86_set_cpu_property_request_body import PlatformX86SetCPUPropertyRequestBody  # noqa: E501
from vbox_server.models.platform_x86_set_hw_virt_ex_property_request_body import PlatformX86SetHWVirtExPropertyRequestBody  # noqa: E501
from vbox_server.models.progress_obj_wrapper_response import ProgressObjWrapperResponse  # noqa: E501
from vbox_server.models.serial_port_obj_wrapper_response import SerialPortObjWrapperResponse  # noqa: E501
from vbox_server.models.snapshot_obj_wrapper_response import SnapshotObjWrapperResponse  # noqa: E501
from vbox_server.models.storage_controller_obj_wrapper_response import StorageControllerObjWrapperResponse  # noqa: E501
from vbox_server.models.usb_controller_obj_wrapper_response import USBControllerObjWrapperResponse  # noqa: E501
from vbox_server.models.usb_device_obj_wrapper_response import USBDeviceObjWrapperResponse  # noqa: E501
from vbox_server.models.virtual_box_create_machine_request_body import VirtualBoxCreateMachineRequestBody  # noqa: E501
from vbox_server.models.virtual_box_get_extra_data_keys_response import VirtualBoxGetExtraDataKeysResponse  # noqa: E501


# Set logging level for module
logging.getLogger().setLevel(logging.INFO)

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int    # pylint: disable=redefined-builtin,invalid-name
    xrange = range; # pylint: disable=redefined-builtin,invalid-name


@consoleDecorator
def i_machine_action(oVBoxObj, action):  # noqa: E501
    """machine_action

    Performs one of the following power actions on the specified machine: 
    - STOP - Machine is being normally stopped powering it off, or after the guest OS has initiated a shutdown sequence. 
    - SAVE - Machine is saving its execution state to a file. 
    - RESTORE - Execution state of the machine is being restored from a file after powering it on from the saved execution state. 
    - PAUSE - Machine is being paused.

    :param machineId: The Id of the machine.
    :type machineId: str
    :param action: The action to perform on the machine.
    :type action: str

    :rtype: Machine
    """

    httpCode = HTTPStatus.OK

    vbox_utils_commonChecks()

    oConsole = oVBoxObj
    
    logging.info('Passed machine Id is ' + oConsole.machine.id)
    logging.info('Passed action type is ' + action)

    oError = Error()

    vbox_utils_logVmInfo(oConsole.machine)

    oProgress = None

    ops = { 'PAUSE':            lambda: oConsole.pause(),
            'RESTORE':          lambda: oConsole.resume(),
            'STOP':             lambda: oConsole.powerDown(),
            'ACPIPOWERBUTTON':  lambda: oConsole.powerButton(),
            'STARTANDPAUSE':    lambda: oConsole.powerUpPaused(),
            'ACPISLEEP':        lambda: oConsole.sleepButton(),#doesn't work#
            'RESET':            lambda: oConsole.reset(),
            # 'SAVE':             lambda: oCurrMachine.saveState(),
            }

    try:
        ops[action]()
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    data = {
        'progress id': oProgress.id if (oProgress is not None and action=='STOP') else 'Null',
    }

    response = jsonify(oError if oError is not None else data)

    print(response)
    return response, httpCode


# def i_console_pause(vmid):  # noqa: E501
#       """
#       Call interface method IConsole::pause

#       :param vmid: The Id of vm
#       :type vmid: str

#       :rtype: None
#       """
#       return i_machine_action(vmid, "PAUSE")


# def i_console_powerbutton(vmid):  # noqa: E501
#       """
#       Call interface method IConsole::powerButton

#       :param vmid: The Id of vm
#       :type vmid: str

#       :rtype: None
#       """
#       return i_machine_action(vmid, "ACPIPOWERBUTTON")


def i_console_powerdown(vmid):  # noqa: E501
    """
    Call interface method IConsole::powerDown

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: ProgressResponse
    """
    return i_machine_action(vmid, "STOP")


def i_console_powerup(vmid):  # noqa: E501
    """
    Call interface method IConsole::powerUp

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: ProgressResponse
    """
    return i_machine_action(vmid, "POWERUP")


def i_console_poweruppaused(vmid):  # noqa: E501
    """
    Call interface method IConsole::powerUpPaused

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: ProgressResponse
    """
    return i_machine_action(vmid, "STARTANDPAUSE")


# def i_console_reset(vmid):  # noqa: E501
#     """
#     Call interface method IConsole::reset

#     :param vmid: The Id of vm
#     :type vmid: str

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_console_resume(vmid):  # noqa: E501
#     """
#     Call interface method IConsole::resume

#     :param vmid: The Id of vm
#     :type vmid: str

#     :rtype: None
#     """
#     return i_machine_action(vmid, "RESTORE")


# def i_console_sleepbutton(vmid):  # noqa: E501
#     """
#     Call interface method IConsole::sleepButton

#     :param vmid: The Id of vm
#     :type vmid: str

#     :rtype: None
#     """
#     return i_machine_action(vmid, "ACPISLEEP")


def i_machine_deleteconfig(vmid, media=None):
    """
    Call interface method IMachine::deleteConfig

    :param vmid: The Id of vm
    :type vmid: str
    :param media: Put here an ID of requested IMedium VirtualBox object
    :type media: List[str]
    """

    oVM, oError = vbox_utils_find_machine(vmid)
    if oVM is None:
        logging.info (oError)
        return jsonify(oError), HTTPStatus.NOT_FOUND
    
    oError = None
    httpCode = HTTPStatus.OK

    vbox_utils_commonChecks()

    logging.info('Passed machine Id is ' + vmid)
    logging.info("Try to remove the machine " + oVM.name + " (UUID " + oVM.id + ")")

    oCurrMachine = oVM
    oVBoxMediumList = []

    try:
        olVBoxMediumAttachments = ctx['global'].getArray(oCurrMachine, 'mediumAttachments')
    except Exception as e:
        logging.info("Can't delete VM '%s': %s" % (oCurrMachine.name, str(e)))
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    oProgress = None

    if oError is None:
        try:
            logging.info ('Nobody locks the machine. Try to get the lock back for ' + oVM.id)
            oVBoxMediumList = oVM.unregister(ctx['const'].CleanupMode_DetachAllReturnHardDisksOnly)
            oProgress = oVM.deleteConfig(oVBoxMediumList)
        except Exception as e:
            logging.info("Can't delete VM '%s': %s" % (oVM.name, str(e)))
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR            
            oError = Error(httpCode, str(e))

    data = {
        'progress id': oProgress.id if oProgress is not None else 'Null',
    }

    response = jsonify(oError if oError is not None else data)

    return response, httpCode


# def i_machine_getbootorder(vmid, position=None):  # noqa: E501
#     """
#     Call interface method IMachine::getBootOrder

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param position: 
#     :type position: int

#     :rtype: DeviceTypeResponse
#     """

#     vbox_utils_commonChecks()

#     httpCode = HTTPStatus.OK

#     logging.info('Passed machine Id is ' + vmid)

#     oVM, oError = vbox_utils_find_machine(vmid)
#     if oVM is None:
#         return jsonify(oError), HTTPStatus.NOT_FOUND
#     else:
#         #set to None
#         oError = None

#     oDeviceTypeResponse = DeviceTypeResponse()
#     try:
#         oVBoxMediumdeviceType = oVM.getBootOrder(position)
#         oDeviceTypeResponse.device = ctx[ 'global'].getEnumValueName('DeviceType', oVBoxMediumdeviceType)
#         logging.info('The command result is ' + str(oDeviceTypeResponse.device))            
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oDeviceTypeResponse)
#     return response, httpCode


# def i_machine_getextradata(vmid, key=None):  # noqa: E501
#     """
#     Call interface method IMachine::getExtraData

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param key: 
#     :type key: str

#     :rtype: MediumGetpropertyResponse
#     """

#     vbox_utils_commonChecks()

#     httpCode = HTTPStatus.OK

#     logging.info('Passed machine Id is ' + vmid)

#     oVM, oError = vbox_utils_find_machine(vmid)
#     if oVM is None:
#         return jsonify(oError), HTTPStatus.NOT_FOUND
#     else:
#         #set to None
#         oError = None

#     oMediumGetpropertyResponse = MediumGetpropertyResponse()

#     try:
#         oMediumGetpropertyResponse.value = oVM.getExtraData(key)
#         if oMediumGetpropertyResponse.value!='':
#             logging.info("Successfully get the value '" + oMediumGetpropertyResponse.value + "' of VM extra data key '" + key + "'")
#         else:
#             logging.info("Unknown extra data key '" + key + "' or the value is empty ")
#             httpCode = HTTPStatus.NOT_FOUND
#             oError = Error(httpCode, "Unknown extra data key '" + key + "' or the value is empty ")

#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oMediumGetpropertyResponse)
#     return response, httpCode


def i_machine_getextradatakeys(vmid):  # noqa: E501
    """
    Call interface method IMachine::getExtraDataKeys

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: VirtualboxGetextradatakeysResponse
    """

    vbox_utils_commonChecks()

    httpCode = HTTPStatus.OK

    logging.info('Passed machine Id is ' + vmid)

    oVM, oError = vbox_utils_find_machine(vmid)
    if oVM is None:
        return jsonify(oError), HTTPStatus.NOT_FOUND
    else:
        #set to None
        oError = None

    oVirtualboxGetextradatakeysResponse = VirtualBoxGetExtraDataKeysResponse()
    if oVM is not None:
        try:
            olKeys = oVM.getExtraDataKeys()
            keys = []
            for item in olKeys:
                logging.info(item)
                keys.append(item)

            oVirtualboxGetextradatakeysResponse.keys = keys

            logging.info('Successfully get the list of extra keys')

        except Exception as e:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oVirtualboxGetextradatakeysResponse)
    return response, httpCode


def i_machine_getguestproperty(vmid, name=None):  # noqa: E501
    """
    Call interface method IMachine::getGuestProperty

    :param vmid: The Id of vm
    :type vmid: str
    :param name: 
    :type name: str

    :rtype: MachineGetguestpropertyResponse
    """

    vbox_utils_commonChecks()

    httpCode = HTTPStatus.OK

    logging.info('Passed machine Id is ' + vmid)

    oVM, oError = vbox_utils_find_machine(vmid)
    if oVM is None:
        return jsonify(oError), HTTPStatus.NOT_FOUND
    else:
        #set to None
        oError = None

    oMachineGetguestpropertyResponse = MachineGetGuestPropertyResponse()
    if oVM is not None:
        try:
            [v,t,f] = oVM.getGuestProperty(name)
            oMachineGetguestpropertyResponse.value = v
            oMachineGetguestpropertyResponse.timestamp = t
            oMachineGetguestpropertyResponse.flags = f
            logging.info('The command result is ' + str(oMachineGetguestpropertyResponse))
            logging.info('Successfully get the value ' + v + ' of VM guest property ' + name)
        except Exception as e:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oMachineGetguestpropertyResponse)
    return response, httpCode


def i_machine_launchvmprocess(vmid, oMachineLaunchVMProcessRequestBody: MachineLaunchVMProcessRequestBody):  # noqa: E501
    """
    Call interface method IMachine::launchVMProcess

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineLaunchVMProcessRequestBody:
    :type oMachineLaunchVMProcessRequestBody: dict | bytes

    :rtype: ProgressResponse
    """
    o = oMachineLaunchVMProcessRequestBody
    print (o)
    httpCode = HTTPStatus.OK
    oSessionId = o.session
    name = o.name
    environmentChanges = o.environmentChanges

    vbox_utils_commonChecks()

    print ('Passed machine Id is ' + vmid)

    oVM, oError = vbox_utils_find_machine(vmid)
    if oVM is None:
        return jsonify(oError), HTTPStatus.NOT_FOUND
    else:
        #set to None
        oError = None

    vbox_utils_logVmInfo(oVM)
    
    #todo: uuid conversion and check should be here
    if oSessionId is None or oSessionId=="None" or oSessionId=="":
        oSession = ctx['global'].getSessionObject()

    oProgress = None
    oError = None

    try:  
        logging.info ('Trying to call oVM.launchVMProcess()')
        oProgress = oVM.launchVMProcess(oSession, name, environmentChanges)

        if oProgress is not None:
            logging.info ('Progress Id is ' + oProgress.id)

            # with session observer there is no need to wait progress completion, but we wait so far
            oProgress.waitForCompletion(-1)

    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR  
        oError = Error(httpCode, str(e))  

    logging.info ('Session name is ' + oSession.name)# returns GUI/Qt always
    logging.info ('Session state is ' + ctx['global'].getEnumValueName('SessionState', oSession.state))

    if oSession is not None:
        resProgress = Progress(oProgress.id)
        # Try to unlock session because we don't need that here
        try:
            if oSession.state == ctx['const'].SessionState_Locked:
                # logging.info (" BEFORE oSession.unlockMachine(): %s" % (ctx['global'].getEnumValueName('SessionState', oVM.sessionState),))
                # logging.info ('Session state is ' + ctx['global'].getEnumValueName('SessionState', oSession.state))
                oSession.unlockMachine()
                # logging.info (" AFTER oSession.unlockMachine(): %s" % (ctx['global'].getEnumValueName('SessionState', oVM.sessionState),))
                # logging.info ('Session state is ' + ctx['global'].getEnumValueName('SessionState', oSession.state))

                # Add Progress Id and Session object into the tracking lists
                ctx['tracker'][oProgress.id] = oSession
                ctx['vms'][oVM.id] = oProgress.id

                oSession = None
        except:
            # logging.info('Exception trying unlock session')
            try:    fIgnore = oSession.state == ctx['const'].SessionState_Unlocked
            except: fIgnore = False
        
            if fIgnore:
                oSession  = None
            else:
                logging.warning ('ISession::unlockMachine failed on %s' % (oSession))

    response = jsonify(oError if oError is not None else resProgress)

    return response, httpCode


# def i_machine_querylogfilename(vmid, idx=None):  # noqa: E501
#     """
#     Call interface method IMachine::queryLogFilename

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param idx: 
#     :type idx: int

#     :rtype: MachineQuerylogfilenameResponse
#     """

#     httpCode = HTTPStatus.OK

#     oVM, oError = vbox_utils_find_machine(vmid)
#     if oVM is None:
#         return jsonify(oError), HTTPStatus.NOT_FOUND
#     else:
#         #set to None
#         oError = None

#     oMachineQuerylogfilenameResponse = MachineQueryLogFilenameResponse()
#     # oMachineQuerylogfilenameResponse = MachineQuerylogfilenameResponse()

#     try:
#         if idx is None: idx = 0
#         oMachineQuerylogfilenameResponse.filename = oVM.queryLogFilename(idx)

#     except Exception as e:
#         logging.info("Can't find VM's log file '%d': %s" % (idx, str(e)))
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oMachineQuerylogfilenameResponse)
#     return response, httpCode


def i_machine_readlog(vmid, idx=None, offset=None, size=None):  # noqa: E501
    """
    Call interface method IMachine::readLog

    :param vmid: The Id of vm
    :type vmid: str
    :param idx:
    :type idx: int
    :param offset:
    :type offset: int
    :param size:
    :type size: int

    :rtype: MachineReadlogResponse
    """

    httpCode = HTTPStatus.OK

    oVM, oError = vbox_utils_find_machine(vmid)
    if oVM is None:
        return jsonify(oError), HTTPStatus.NOT_FOUND
    else:
        #set to None
        oError = None

    oMachineReadlogResponse = MachineReadLogResponse()
    try:
        if (idx is not None and idx < 0) \
            or (offset is not None and offset < 0) \
            or (size is not None and size <= 0):
            raise ValueError

    except Exception as e:
        logging.info("One of the passed parameters has an inappropriate value")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, "One of the passed parameters has an inappropriate value")

    if oError is not None:
        return jsonify(oError), httpCode

    try:
        if idx is None:
            idx = 0
        if offset is None:
            offset = 0
        if size is None:
            size = 4096
        data = oVM.readLog(idx, offset, size)

        oMachineReadlogResponse.data = data
        if platform.system() == "Windows":
            oMachineReadlogResponse.data = data.tobytes()

    except Exception as e:
        logging.info("Can't read VM's log file '%d': %s" % (idx, str(e)))
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is not None:
        response = jsonify(oError)
    else:
        contentType = 'application/octet-stream'
        if offset!=0 and size!=0:
            if oMachineReadlogResponse.data != 0:
                httpCode = HTTPStatus.PARTIAL_CONTENT
                contentType = 'multipart/byteranges'
            else:
                httpCode = HTTPStatus.OK
                contentType = 'application/octet-stream'

        response = oMachineReadlogResponse.data
        contentRange = 'bytes ' + str(offset) + '-' + str(offset+size)
        h = Headers()
        h.add('Content-Range', contentRange)
        return ConnexionResponse(
            status_code=httpCode,
            content_type=contentType,
            body=response,
            headers=h
            )

    return response, httpCode


@sessionDecorator
def i_machine_createsharedfolder(oVBoxObj, oMachineCreateSharedFolderRequestBody: MachineCreateSharedFolderRequestBody):  # noqa: E501
    """
    Call interface method IMachine::createSharedFolder

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineCreateSharedFolderRequestBody: 
    :type oMachineCreateSharedFolderRequestBody: dict | bytes

    :rtype: None
    """

    oCurrMachine = oVBoxObj

    o = oMachineCreateSharedFolderRequestBody
    name = o.name
    hostPath = o.hostPath
    fWritable = o.writable
    fAutomount = o.automount
    autoMountPoint = o.autoMountPoint

    logging.info("Try to create the shared folder " + name + " for machine " + oCurrMachine.name + " (UUID " + oCurrMachine.id + ")")

    oError = None
    httpCode = HTTPStatus.OK

    for sf in ctx['global'].getArray(oCurrMachine, 'sharedFolders'):
        if sf.name == name:
            logging.info("The shared folder with the name %s exists" % (name))
            httpCode = HTTPStatus.PRECONDITION_FAILED
            oError = Error(httpCode, "The shared folder with the name %s exists" % (name))
            break

    if oError is None:
        try:
            # No return result check
            oCurrMachine.createSharedFolder(name, hostPath, fWritable, fAutomount, autoMountPoint)
            logging.info("Created the shared folder %s" % (name))

            #Don't forget to save
            oCurrMachine.saveSettings()

        except Exception as e:
            logging.info("Exception during creation the shared folder %s" % (name))
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else "Successfully created the shared folder")
    return response, httpCode


# @sessionDecorator
# def i_machine_removesharedfolder(vmid, name=None, *var_args_tuple):  # noqa: E501
#     """
#     Call interface method IMachine::removeSharedFolder

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param name: 
#     :type name: str

#     :rtype: None
#     """

#     oVM = var_args_tuple[0]
#     oSession = var_args_tuple[1]
#     oCurrMachine = oSession.machine
#     oError = None
#     httpCode = HTTPStatus.OK

#     logging.info("Try to remove the shared folder " + name + " for machine " + oVM.name + " (UUID " + oVM.id + ")")

#     fFound = False
#     for sf in ctx['global'].getArray(oVM, 'sharedFolders'):
#         if sf.name == name:
#             try:
#                 fFound = True
#                 # No return result check.
#                 # removeSharedFolder returns None instead of the result S_OK.
#                 oCurrMachine.removeSharedFolder(name)
#                 logging.info("Removed the shared folder %s" % (name))

#                 #Don't forget to save
#                 oCurrMachine.saveSettings()
#                 break

#             except Exception as e:
#                 logging.info("Exception during removing the shared folder %s" % (name))
#                 httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#                 oError = Error(httpCode, str(e))

#     if fFound is False and oError is None:
#         logging.info("The shared folder with the name %s doesn\'t exists" % (name))
#         httpCode = HTTPStatus.NOT_FOUND
#         oError = Error(httpCode, "The shared folder with the name %s doesn\'t exists" % (name))

#     response = jsonify(oError if oError is not None else "Successfully removed the shared folder")
#     return response, httpCode


# @sessionDecorator
# def i_machine_savesettings(vmid, *var_args_tuple):  # noqa: E501
#     """
#     Call interface method IMachine::saveSettings

#     :param vmid: The Id of vm
#     :type vmid: str

#     :rtype: None
#     """

#     vbox_utils_commonChecks()

#     oError = None
#     httpCode = HTTPStatus.OK

#     logging.info('Passed machine Id is ' + vmid)

#     oSession = var_args_tuple[1]
#     oCurrMachine = oSession.machine

#     if oCurrMachine is not None:
#         try:
#             oCurrMachine.saveSettings()
#         except Exception as e:
#             httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#             oError = Error(httpCode, str(e))

#     if oError is not None:
#         return jsonify(oError), httpCode

#     return "Machine's settings has been successfully saved " + "(uuid " + vmid + ")"


def i_machine_savestate(vmid):  # noqa: E501
    """
    Call interface method IMachine::saveState

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: ProgressResponse
    """
    return i_machine_action(vmid, 'SAVE')


@sessionDecorator
def i_machine_setbootorder(oVBoxObj, oMachineSetBootOrderRequestBody: MachineSetBootOrderRequestBody):  # noqa: E501
    """
    Call interface method IMachine::setBootOrder

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineSetBootOrderRequestBody:
    :type oMachineSetBootOrderRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)
    
    o = oMachineSetBootOrderRequestBody
    device = o.device
    position = o.position

    try:
        device = swagger_to_vbox_devicetype(o.device)
        if device is None:
            return "The requested device " + str(o.device) + " is not supported for booting", HTTPStatus.PRECONDITION_FAILED

        oCurrMachine.setBootOrder(position, device)
        oCurrMachine.saveSettings()
        logging.info('Set boot order [%d] for device %s' % (position, str(device)))
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is not None:
        return jsonify(oError), httpCode

    return 'Set boot order [' + str(position) + '] for device ' + str(o.device) + ' has been done'


@sessionDecorator
def i_machine_setextradata(oVBoxObj, oMachineSetExtraDataRequestBody: MachineSetExtraDataRequestBody):  # noqa: E501
    """
    Call interface method IMachine::setExtraData

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineSetExtraDataRequestBody:
    :type oMachineSetExtraDataRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)
    
    o = oMachineSetExtraDataRequestBody

    try:
        oCurrMachine.setExtraData(o.key, o.value)
        oCurrMachine.saveSettings()
        logging.info("Successfully set VM extra data key " + "'" + o.key + "'" + " to value " + "'" + o.value + "'")

    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is not None:
        return jsonify(oError), httpCode

    return "Successfully set VM extra data key " + "'" + o.key + "'" + " to value " + "'" + o.value + "'"


@sessionDecorator
def i_machine_setguestproperty(oVBoxObj, oMachineSetGuestPropertyRequestBody: MachineSetGuestPropertyRequestBody):  # noqa: E501
    """
    Call interface method IMachine::setGuestProperty

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineSetGuestPropertyRequestBody:
    :type oMachineSetGuestPropertyRequestBody: dict | bytes

    :rtype: None
    """

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)

    o = oMachineSetGuestPropertyRequestBody
    sProperty = o._property
    sValue = o.value
    sFlags = o.flags

    try:
        oCurrMachine.setGuestProperty(sProperty, sValue, sFlags)
        oCurrMachine.saveSettings()
        logging.info("Successfully set VM guest property " + "'" + sProperty + "'" + " to value " + "'" + sValue + "'")
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is not None:
        return jsonify(oError), httpCode

    return "Successfully set VM guest property " + "'" + sProperty + "'" + " to value " + "'" + sValue + "'"


def i_machine_unregister(vmid, cleanupMode=None):  # noqa: E501
    """
    Call interface method IMachine::unregister

    :param vmid: The Id of vm
    :type vmid: str
    :param cleanupMode: For the possible values of enumeration look into #/definitions/CleanupMode
    :type cleanupMode: str

    :rtype: MediumArrayResponse
    """

    vbox_utils_commonChecks()

    httpCode = HTTPStatus.OK

    print ('Passed machine Id is ' + vmid)
    if cleanupMode is not None: logging.info ('Passed cleanupMode is ' + cleanupMode)
    else: cleanupMode = 'FULL'

    oVM, oError = vbox_utils_find_machine(vmid)
    if oVM is None:
        return jsonify(oError), HTTPStatus.NOT_FOUND
    else:
        #set to None
        oError = None

    oMediumList = list[Medium]()
    oMediumArrayResponse = None

    vbox_utils_logVmInfo(oVM)

    try:
        vBoxCleanupMode = swagger_to_vbox_cleanupmode(cleanupMode)
        if vBoxCleanupMode is None:
            return "The requested cleanup mode " + str(cleanupMode) + " wasn't found", HTTPStatus.NOT_FOUND

        oVBoxMediumList, oError = __machine_unregister(oVM, cleanupMode)
        if oVBoxMediumList:
            for item in oVBoxMediumList:
                oMediumList.append(i_fill_medium(item))

            oMediumArrayResponse = MediumObjArrayWrapperResponse()
            oMediumArrayResponse.media = oMediumList
            logging.info ('VM ' + vmid + ' was successfully unregistered')
        else:
            logging.info ('Can\'t unregister VM ' + vmid)

    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oMediumArrayResponse if oMediumArrayResponse is not None else oError)

    return response, httpCode


def __machine_unregister(oVM, cleanupMode: CleanupMode):
    oError = None
    try:
        olDisks = oVM.unregister(cleanupMode)
    except Exception as e:
        olDisks = None
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    return olDisks, oError


def i_virtualbox_createmachine(oVirtualBoxCreateMachineRequestBody: VirtualBoxCreateMachineRequestBody):  # noqa: E501
    """
    Call interface method IVirtualBox::createMachine

    :param oVirtualBoxCreateMachineRequestBody: 
    :type oVirtualBoxCreateMachineRequestBody: dict | bytes

    :rtype: MachineResponse
    """

    vbox_utils_commonChecks()

    httpCode = HTTPStatus.OK

    o = oVirtualBoxCreateMachineRequestBody
    print(o)
    name = o.name
    osTypeId = o.osTypeId
    groups = o.groups
    flags = o.flags
    settingsFile = o.settingsFile# check or ignore?

    platform = swagger_to_vbox_platformarchitecture(o.platform)
    logging.info('The passed PlatformArchitecture is ' + str(platform))

    cipher = o.cipher
    passwordId = o.passwordId
    password = o.password

    oVM, oError = vbox_utils_find_machine(name)
    if oVM is not None:
        httpCode = HTTPStatus.PRECONDITION_FAILED
        oError = Error(httpCode, "Machine with the name %s has already registered in VirtualBox" % (name))
        return jsonify(oError), httpCode
    else:
        #set to None
        oError = None

    try:
        ctx['vb'].getGuestOSType(osTypeId)
    except Exception as e:
        httpCode = HTTPStatus.PRECONDITION_FAILED
        oError = Error(httpCode, str(e))
        return jsonify(oError), httpCode 

    oVBox = ctx['vb']

    try:
        oVM = oVBox.createMachine(settingsFile, name, platform, groups, osTypeId, flags, cipher, passwordId, password)
        oVM.saveSettings()
        logging.info("created machine with UUID", str(oVM.id))
        oVBox.registerMachine(oVM)
        logging.info("registered machine with UUID", str(oVM.id))
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))
        return jsonify(oError), httpCode

    if oVM is not None:
        vbox_utils_logVmInfo(oVM)

        oMachine = None
        try:
            oMachine = i_fill_machine(oVM)
        except Exception as e:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))
            return jsonify(oError), httpCode

    response = jsonify(oMachine if oMachine is not None else oError)
    return response, httpCode


# def i_virtualbox_findmachine(vmid, select=None, nameOrId=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::findMachine

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param select: The object attributes separated by comma
#     :type select: str
#     :param nameOrId: 
#     :type nameOrId: str

#     :rtype: MachineResponse
#     """

#     vbox_utils_commonChecks()

#     httpCode = HTTPStatus.OK

#     logging.info ('Passed machine Id is ' + vmid)

#     oVM, oError = vbox_utils_find_machine(vmid)
#     if oVM is None:
#         return jsonify(oError), HTTPStatus.NOT_FOUND
#     else:
#         #set to None
#         oError = None

#     vbox_utils_logVmInfo(oVM)

#     oMachineResponse = MachineObjWrapperResponse()
#     # oMachineResponse = MachineResponse()

#     try:
#         oMachineResponse.machine = i_fill_machine(oVM, select)
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oMachineResponse)

#     return response, httpCode


@sessionDecorator
def i_machine_attachdevice(oVBoxObj, oMachineAttachDeviceRequestBody: MachineAttachDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::attachDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineAttachDeviceRequestBody:
    :type oMachineAttachDeviceRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)

    o = oMachineAttachDeviceRequestBody
    name = o.name
    port = o.controllerPort
    slot = o.device
    devType = swagger_to_vbox_devicetype(o.type)
    if devType is None:
        return "The requested type " + str(o.type) + " is not supported", HTTPStatus.NOT_FOUND
    mediumPath = o.medium

    machineState = oCurrMachine.state

    if machineState != ctx['const'].MachineState_PoweredOff and \
        machineState != ctx['const'].MachineState_Aborted and \
        machineState != ctx['const'].MachineState_AbortedSaved and \
        machineState != ctx['const'].MachineState_Saved:
            httpCode = HTTPStatus.PRECONDITION_FAILED
            oError = Error(httpCode, "Machine must be in one of the states - PoweredOff, Aborted, AbortedSaved, Saved")
            return jsonify(oError), httpCode

    oVBoxMedium = None
    try:
        if mediumPath is not None or len(mediumPath)>0:
            oVBoxMedium = ctx['vb'].openMedium(mediumPath, ctx['global'].constants.DeviceType_HardDisk, ctx['global'].constants.AccessMode_ReadWrite, False)
            if oVBoxMedium is not None:
                oCurrMachine.attachDevice(name, port, slot, devType, oVBoxMedium)
            else:
                httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
                oError = Error(httpCode, "Something went wrong during opening the medium " + str(mediumPath))
        else:
            oCurrMachine.attachDevice(name, port, slot, devType, None)

        oCurrMachine.saveSettings()

    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        logging.info("Exception during attaching the device to the controller " + name +
        " (port " + str(port) + "; slot " + str(slot) + ")")
        oError = Error(httpCode, str(e))

    if oError is not None:
        response = jsonify(oError)
    else:
        response = "Successfully attached the device to the controller " + name + \
        " (port " + str(port) + "; slot " + str(slot) + ")."
        if oVBoxMedium is not None:
            response = response + " The device is " + mediumPath

    return response, httpCode


@sessionDecorator
def i_machine_detachdevice(oVBoxObj, oMachineDetachDeviceRequestBody: MachineDetachDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::detachDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineDetachDeviceRequestBody:
    :type oMachineDetachDeviceRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)

    o = oMachineDetachDeviceRequestBody
    name = o.name
    port = o.controllerPort
    slot = o.device

    machineState = oCurrMachine.state
    if machineState != ctx['const'].MachineState_PoweredOff and \
        machineState != ctx['const'].MachineState_Aborted and \
        machineState != ctx['const'].MachineState_AbortedSaved and \
        machineState != ctx['const'].MachineState_Saved:
            httpCode = HTTPStatus.PRECONDITION_FAILED
            oError = Error(httpCode, "Machine must be in one of the states - PoweredOff, Aborted, AbortedSaved, Saved")
            return jsonify(oError), httpCode

    oMediumAttachment = None
    try:
        oResponse, httpCode = i_machine_getmediumattachment(oCurrMachine.id, None, name, port, slot)

        if oResponse.is_json is True: logging.info(oResponse.get_json())

        if httpCode == HTTPStatus.OK:
            logging.info("Try to detach device from the machine " + oCurrMachine.name + " (UUID " + oCurrMachine.id + ")")
            oMediumAttachment = MediumAttachment.from_dict(oResponse.get_json())
            oCurrMachine.detachDevice(name, port, slot)
            oCurrMachine.saveSettings()
        else:
            oError = oResponse

    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        logging.info("Exception during detaching the device from the controller " + name +
         " (port " + str(port) + "; slot " + str(slot) + ")")
        oError = Error(httpCode, str(e))

    if oError is not None:
        response = jsonify(oError)
    else:
        response = "Successfuly detached the device from the controller " + name + \
        " (port " + str(port) + "; slot " + str(slot) + ")."
        if oMediumAttachment is not None:
            response = response + " The device has uuid " + oMediumAttachment.medium

    return response, httpCode


def i_machine_getmediumattachment(vmid, select=None, name=None, controllerPort=None, device=None):
    """
    Call interface method IMachine::getMediumAttachment

    :param vmid: The Id of vm
    :type vmid: str
    :param select: The object attributes separated by comma
    :type select: str
    :param name: 
    :type name: str
    :param controllerPort: controller port
    :type controllerPort: int
    :param device: device number
    :type device: int

    :rtype: MediumAttachmentResponse
    """

    httpCode = HTTPStatus.OK

    vbox_utils_commonChecks()

    oVM, oError = vbox_utils_find_machine(vmid)
    if oVM is None:
        return jsonify(oError), HTTPStatus.NOT_FOUND
    else:
        #set to None
        oError = None

    oMediumAttachmentResponse = MediumAttachmentObjWrapperResponse()
    try:
        oVBoxMedium = oVM.getMediumAttachment(name, controllerPort, device)
        oMediumAttachmentResponse.attachment = i_fill_mediumattachment(oVBoxMedium, select)
    except Exception as e:
        logging.info("Can't get medium attachment for VM '%s': %s" % (oVM.name, str(e)))
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oMediumAttachmentResponse)
    return response, httpCode


@sessionDecorator
def i_machine_mountmedium(oVBoxObj, oMachineMountMediumRequestBody: MachineMountMediumRequestBody):  # noqa: E501
    """
    Call interface method IMachine::mountMedium

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineMountMediumRequestBody:
    :type oMachineMountMediumRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK
    
    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)

    o = oMachineMountMediumRequestBody
    print(o)
    name = o.name
    controllerPort = o.controllerPort
    device = o.device
    force = o.force
    mediumId = o.medium

    oFoundMedium = None
    try:
        lTypes = {'DVDImages', 'floppyImages'}
        oVBoxVirtualBox = ctx['vb']

        for disktype in lTypes:
            olDisks = ctx['global'].getArray(oVBoxVirtualBox, disktype)
            for item in olDisks:
                o = i_fill_partial_medium(item, 'id')
                if o.id == mediumId:
                    oFoundMedium = item
                    logging.info('Found medium with id ' + mediumId)
                    break

            if oFoundMedium is not None:
                break

    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        logging.info('Exception during finding the passed medium with uuid ' + mediumId)
        oError = Error(httpCode, 'Exception during finding the passed medium with uuid ' + mediumId)
        return jsonify(oError), httpCode

    if oFoundMedium is None:
        httpCode = HTTPStatus.NOT_FOUND
        oError = Error(httpCode, 'The passed medium with uuid ' + mediumId + ' wasn\'t found among DVD or floppy images')
        return jsonify(oError), httpCode

    try:
        oCurrMachine.mountMedium(name, controllerPort, device, oFoundMedium, force)
        oCurrMachine.saveSettings()
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is not None:
        return jsonify(oError), httpCode

    return 'The passed medium with uuid ' + mediumId +' has been successfully mounted', httpCode


@sessionDecorator
def i_machine_unmountmedium(oVBoxObj, oMachineUnmountMediumRequestBody: MachineUnmountMediumRequestBody):  # noqa: E501
    """
    Call interface method IMachine::unmountMedium

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineUnmountMediumRequestBody:
    :type oMachineUnmountMediumRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    o = oMachineUnmountMediumRequestBody
    print(o)
    name = o.name
    controllerPort = o.controllerPort
    device = o.device
    force = o.force

    oMedium = None
    found = False
    
    oCurrMachine = oVBoxObj

    if oCurrMachine is not None:
        try:
            oVBoxMedium = oCurrMachine.getMedium(name, controllerPort, device)
            oMedium = i_fill_medium(oVBoxMedium)
            if oMedium.deviceType == 'HardDisk':
                raise 'Wrong medium device type. Must be DVD or Floppy.'
            logging.info('Successfully found the medium on the controller ' + name + ' on port ' + str(controllerPort) + ' on device ' + str(device))
            found = True

        except Exception as e:
            logging.info('Exception during finding the medium on the controller ' + name + ' on port ' + str(controllerPort) + ' on device ' + str(device))
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))
            return jsonify(oError), httpCode

    if found != True:
        httpCode = HTTPStatus.NOT_FOUND
        logging.info("Couldn\'t find the medium on the controller " + name + ' on port ' + str(controllerPort) + ' on device ' + str(device))
        oError = Error(httpCode, "Couldn\'t find the medium on the controller " + name + ' on port ' + str(controllerPort) + ' on device ' + str(device))
        return jsonify(oError), httpCode

    try:
        oCurrMachine.unmountMedium(name, controllerPort, device, force)

    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is not None:
        return jsonify(oError), httpCode

    return 'The medium on the controller ' + name + ' on port ' + str(controllerPort) + \
            ' on device ' + str(device) + ' has been successfully unmounted', httpCode


@openExclusiveSession
def i_machine_moveto(vmid, oMachineMoveToRequestBody: MachineMoveToRequestBody, *var_args_tuple):  # noqa: E501
    """
    Call interface method IMachine::moveTo

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineMoveToRequestBody:
    :type oMachineMoveToRequestBody: dict | bytes

    :rtype: ProgressResponse
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    logging.info('Passed machine Id is ' + vmid)

    o = oMachineMoveToRequestBody
    sLocation = o.folder
    sType = o.type

    oVM = var_args_tuple[0]
    oSession = var_args_tuple[1]
    oCurrMachine = oSession.machine
    machineState = oSession.machine.state

    if machineState != ctx['const'].MachineState_PoweredOff and \
        machineState != ctx['const'].MachineState_Aborted and \
        machineState != ctx['const'].MachineState_AbortedSaved and \
        machineState != ctx['const'].MachineState_Saved:
            httpCode = HTTPStatus.PRECONDITION_FAILED
            oError = Error(httpCode, "Machine must be in one of the states - PoweredOff, Aborted, AbortedSaved, Saved")
            return jsonify(oError), httpCode

    oProgressResponse = ProgressObjWrapperResponse()

    try:
        oVBoxProgress = oCurrMachine.moveTo(sLocation, sType)

        if oVBoxProgress is not None:
            oProgressResponse.progress = i_fill_progress(oVBoxProgress)
            logging.info('The moving machine has been successfully started')

            # todo: Add Progress Id and Session object into the tracking lists
            ctx['tracker'][oProgressResponse.progress.id] = oSession
            ctx['vms'][oSession.machine.id] = oProgressResponse.progress.id
        else:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, "Something wrong with the Progress object")

    except Exception as e:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oProgressResponse)

    return response, httpCode


def i_machine_cloneto(vmid, oMachineCloneToRequestBody: MachineCloneToRequestBody):
    """
    Call interface method IMachine::cloneTo

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineCloneToRequestBody: 
    :type oMachineCloneToRequestBody: dict | bytes

    :rtype: ProgressResponse
    """

    oSourceMachine, oError = vbox_utils_find_machine(vmid)
    if oSourceMachine is None:
        logging.info (oError)
        return jsonify(oError), HTTPStatus.NOT_FOUND
    else:
        #set to None
        oError = None

    vbox_utils_commonChecks()
    vbox_utils_logVmInfo(oSourceMachine)

    httpCode = HTTPStatus.OK

    logging.info('Passed source machine Id is ' + vmid)

    o = oMachineCloneToRequestBody

    mode = swagger_to_vbox_clonemode(o.mode)
    if mode is None:
        return "The requested type " + str(o.mode) + " is not supported", HTTPStatus.NOT_FOUND

    options = list() # List[CloneOptions]
    for item in o.options:
        if item == "LINK":
            options.append(ctx['const'].CloneOptions_Link)
        elif item == "KEEPALLMACS":
            options.append(ctx['const'].CloneOptions_KeepAllMACs)
        elif item == "KEEPNATMACS":
            options.append(ctx['const'].CloneOptions_KeepNATMACs)
        elif item == "KEEPDISKNAMES":
            options.append(ctx['const'].CloneOptions_KeepDiskNames)
        elif item == "KEEPHWUUIDS":
            options.append(ctx['const'].CloneOptions_KeepHwUUIDs)

    logging.info('Passed target machine Id is ' + o.target)

    oTargetMachine, oError = vbox_utils_find_machine(o.target)
    if oTargetMachine is None:
        logging.info (oError)
        return jsonify(oError), HTTPStatus.NOT_FOUND
    else:
        #set to None
        oError = None

    vbox_utils_logVmInfo(oTargetMachine)

    try:
        # Produces an exception with the flag CleanupMode_UnregisterOnly if there are some disks attached to this VM
        # It's suitable for us
        olDisks = oTargetMachine.unregister(ctx['const'].CleanupMode_UnregisterOnly)
    except Exception as e:
        logging.info("Can't unregister VM '%s': %s" % (oTargetMachine.name, str(e)))
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))
        return jsonify(oError), httpCode

    vbox_utils_logVmInfo(oTargetMachine)
    targetMachineState = oTargetMachine.state

    if targetMachineState != ctx['const'].MachineState_PoweredOff and \
        targetMachineState != ctx['const'].MachineState_Aborted and \
        targetMachineState != ctx['const'].MachineState_AbortedSaved and \
        targetMachineState != ctx['const'].MachineState_Saved:
            httpCode = HTTPStatus.PRECONDITION_FAILED
            oError = Error(httpCode, "Target machine must be in one of the states - PoweredOff, Aborted, AbortedSaved, Saved")
            return jsonify(oError), httpCode

    sourceMachineState = oSourceMachine.state
    if sourceMachineState != ctx['const'].MachineState_PoweredOff and \
        sourceMachineState != ctx['const'].MachineState_Aborted and \
        sourceMachineState != ctx['const'].MachineState_AbortedSaved and \
        sourceMachineState != ctx['const'].MachineState_Saved:
            httpCode = HTTPStatus.PRECONDITION_FAILED
            oError = Error(httpCode, "Source machine must be in one of the states - PoweredOff, Aborted, AbortedSaved, Saved")
            return jsonify(oError), httpCode

    oProgressResponse = ProgressObjWrapperResponse()

    try:
        oVBoxProgress = oSourceMachine.cloneTo(oTargetMachine, mode, options)

        if oVBoxProgress is not None:
            oProgressResponse.progress = i_fill_progress(oVBoxProgress)
            logging.info('The machine cloning has been successfully started')

            ctx['vb'].registerMachine(oTargetMachine)
            logging.info("registered machine with UUID " + oTargetMachine.id)
        else:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, "Something wrong with the Progress object")

    except Exception as e:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oProgressResponse)
    print(response)
    return response, httpCode


@consoleDecorator
def i_console_attachusbdevice(oVBoxObj, oConsoleAttachUSBDeviceRequestBody: ConsoleAttachUSBDeviceRequestBody):  # noqa: E501
    """
    Call interface method IConsole::attachUSBDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oConsoleAttachUSBDeviceRequestBody: 
    :type oConsoleAttachUSBDeviceRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    httpCode = HTTPStatus.OK
    oError = None

    logging.info('Passed machine Id is ' + oVBoxObj.machine.id)

    oConsole = oVBoxObj

    o = oConsoleAttachUSBDeviceRequestBody
    try:
        logging.info("Try to attach USB device to the machine " + oVBoxObj.machine.name + " (UUID " + oVBoxObj.machine.id + ")")

        ol_usb_devices = ctx['global'].getArray(oConsole,'USBDevices')
        for item in ol_usb_devices:
            oUsb = i_fill_usbdevice(item)

        if o.captureFilename is None or o.captureFilename == '':
            oConsole.attachUSBDevice(o.id, '')
        else:
            #Get VBox asertion on debug build with non-empty name
            oConsole.attachUSBDevice(o.id, o.captureFilename)

        response = jsonify("Successfully attached USB device with id " + o.id + " to the machine " + oVBoxObj.machine.name)

    except Exception as e:
        logging.info("Exception during attaching USB device to the machine " + oVBoxObj.machine.name + " (UUID " + oVBoxObj.machine.id + ")")
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is not None:
        response = jsonify(oError)
        return response, httpCode

    return response, httpCode


# @sessionDecorator
# def i_console_detachusbdevice(vmid, id=None, *var_args_tuple):  # noqa: E501
#     """
#     Call interface method IConsole::detachUSBDevice

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param id: 
#     :type id: str

#     :rtype: USBDeviceResponse
#     """

#     vbox_utils_commonChecks()

#     oError = None
#     httpCode = HTTPStatus.OK

#     logging.info('Passed machine Id is ' + vmid)

#     oVM = var_args_tuple[0]
#     oSession = var_args_tuple[1]
#     oConsole = oSession.console

#     oUSBDeviceResponse = USBDeviceObjWrapperResponse()
#     # oUSBDeviceResponse = USBDeviceResponse()
#     try:
#         if id is not None and id != '':
#             logging.info("Try to detach USB device from the machine " + oVM.name + " (UUID " + oVM.id + ")")
#             oUsbDev = oConsole.detachUSBDevice(id)
#             o = i_fill_usbdevice(oUsbDev)
#             # o = i_fill_usb_device(oUsbDev)
#             oUSBDeviceResponse.device = o
#         else:
#             httpCode = HTTPStatus.NOT_FOUND
#             oError = Error(httpCode, "The passed USB id is empty or hasn't been passed at all")
#             return jsonify(oError), httpCode

#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         logging.info("Exception during detaching USB device with id " + id)
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oUSBDeviceResponse)

#     return response, httpCode


# @sessionDecorator
# def i_console_findusbdevicebyid(vmid, select=None, id=None, *var_args_tuple):  # noqa: E501
#     """
#     Call interface method IConsole::findUSBDeviceById

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param select: The object attributes separated by comma
#     :type select: str
#     :param id: 
#     :type id: str

#     :rtype: USBDeviceResponse
#     """

#     vbox_utils_commonChecks()

#     # oVM = var_args_tuple[0]
#     oError = None
#     httpCode = HTTPStatus.OK

#     logging.info('Passed machine Id is ' + vmid)

#     oSession = var_args_tuple[1]
#     oConsole = oSession.console

#     oUSBDeviceResponse = USBDeviceObjWrapperResponse()
#     # oUSBDeviceResponse = USBDeviceResponse()
#     try:
#         if id is not None and id!='':
#             logging.info("Try to find USB device by id " + id)
#             oUsbDev = oConsole.findUSBDeviceById(id)
#             o = i_fill_usbdevice(oUsbDev, select)
#             # o = i_fill_usb_device(oUsbDev, select)
#             oUSBDeviceResponse.device = o
#         else:
#             httpCode = HTTPStatus.PRECONDITION_FAILED
#             oError = Error(httpCode, "The passed USB id is empty or hasn't been passed at all")
#             return jsonify(oError), httpCode

#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         logging.info("Exception during finding USB device by id " + id)
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oUSBDeviceResponse)

#     return response, httpCode


# @sessionDecorator
# def i_console_findusbdevicebyaddress(vmid, select=None, name=None, *var_args_tuple):  # noqa: E501
#     """
#     Call interface method IConsole::findUSBDeviceByAddress

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param select: The object attributes separated by comma
#     :type select: str
#     :param name: 
#     :type name: str

#     :rtype: USBDeviceResponse
#     """

#     vbox_utils_commonChecks()

#     oVM = var_args_tuple[0]
#     oError = None
#     httpCode = HTTPStatus.OK

#     logging.info('Passed machine Id is ' + vmid)

#     oSession = var_args_tuple[1]
#     oConsole = oSession.console

#     oUSBDeviceResponse = USBDeviceObjWrapperResponse()
#     # oUSBDeviceResponse = USBDeviceResponse()
#     try:
#         if name is not None and name!='':
#             logging.info("Try to find USB device by address " + name)
#             oUsbDev = oConsole.findUSBDeviceByAddress(name)
#             o = i_fill_usbdevice(oUsbDev, select)
#             # o = i_fill_usb_device(oUsbDev, select)
#             oUSBDeviceResponse.device = o
#         else:
#             httpCode = HTTPStatus.PRECONDITION_FAILED
#             oError = Error(httpCode, "The passed USB name is empty or hasn't been passed at all")
#             return jsonify(oError), httpCode

#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         logging.info("Exception during finding USB device with name " + name)
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oUSBDeviceResponse)

#     return response, httpCode


# def i_machine_getparallelport(vmid, select=None, slot=None):  # noqa: E501
#     """
#     Call interface method IMachine::getParallelPort

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param select: The object attributes separated by comma
#     :type select: str
#     :param slot: 
#     :type slot: int

#     :rtype: ParallelPortResponse
#     """

#     vbox_utils_commonChecks()

#     httpCode = HTTPStatus.OK

#     logging.info('Passed machine Id is ' + vmid)

#     oVM, oError = vbox_utils_find_machine(vmid)
#     if oVM is None:
#         return jsonify(oError), HTTPStatus.NOT_FOUND
#     else:
#         #set to None
#         oError = None

#     oParallelPortResponse = ParallelPortObjWrapperResponse()
#     # oParallelPortResponse = ParallelPortResponse()

#     try:
#         oVBoxParallelPort = oVM.getParallelPort(slot)
#         oParallelPortResponse.port = i_fill_parallelport(oVBoxParallelPort, select)
#         # oParallelPortResponse.port = i_fill_parallel_port(oVBoxParallelPort, select)
#         logging.info('Successfully get the parallel port')
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oParallelPortResponse)
#     return response, httpCode


# def i_machine_getserialport(vmid, select=None, slot=None):  # noqa: E501
#     """
#     Call interface method IMachine::getSerialPort

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param select: The object attributes separated by comma
#     :type select: str
#     :param slot: 
#     :type slot: int

#     :rtype: SerialPortResponse
#     """

#     vbox_utils_commonChecks()

#     httpCode = HTTPStatus.OK

#     logging.info('Passed machine Id is ' + vmid)

#     oVM, oError = vbox_utils_find_machine(vmid)
#     if oVM is None:
#         return jsonify(oError), HTTPStatus.NOT_FOUND
#     else:
#         #set to None
#         oError = None

#     oSerialPortResponse = SerialPortObjWrapperResponse()
#     # oSerialPortResponse = SerialPortResponse()

#     try:
#         oVBoxSerialPort = oVM.getSerialPort(slot)
#         oSerialPortResponse.port = i_fill_serialport(oVBoxSerialPort, select)
#         # oSerialPortResponse.port = i_fill_serial_port(oVBoxSerialPort, select)
#         logging.info('Successfully get the serial port')
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oSerialPortResponse)
#     return response, httpCode


def i_machine_getmedium(vmid, select=None, name=None, controllerPort=None, device=None):  # noqa: E501
    """
    Call interface method IMachine::getMedium

    :param vmid: The Id of vm
    :type vmid: str
    :param select: The object attributes separated by comma
    :type select: str
    :param name: 
    :type name: str
    :param controllerPort: 
    :type controllerPort: int
    :param device: 
    :type device: int

    :rtype: MediumResponse
    """

    httpCode = HTTPStatus.OK

    vbox_utils_commonChecks()

    oVM, oError = vbox_utils_find_machine(vmid)
    if oVM is None:
        return jsonify(oError), HTTPStatus.NOT_FOUND
    else:
        #set to None
        oError = None

    fFound = False
    oMediumResponse = MediumObjWrapperResponse()
    try:
        oVBoxMedium = oVM.getMedium(name, controllerPort, device)
        oMediumResponse.medium = i_fill_medium(oVBoxMedium, select)
        fFound = True
    except Exception as e:
        logging.info(str(e))
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if fFound == False:
        httpCode = HTTPStatus.NOT_FOUND
        oError = Error(httpCode, str("The Medium wasn't found"))
        
    response = jsonify(oError if oError is not None else oMediumResponse)
    return response, httpCode


def i_machine_getmediumattachmentsofcontroller(vmid, select=None, name=None):  # noqa: E501
    """
    Call interface method IMachine::getMediumAttachmentsOfController

    :param vmid: The Id of vm
    :type vmid: str
    :param select: The object attributes separated by comma
    :type select: str
    :param name:
    :type name: str

    :rtype: MediumAttachmentArrayResponse
    """

    httpCode = HTTPStatus.OK

    vbox_utils_commonChecks()

    logging.info('Passed machine Id is ' + vmid)
    logging.info('Passed name is ' + name)

    oVM, oError = vbox_utils_find_machine(vmid)
    if oVM is None:
        return jsonify(oError), HTTPStatus.NOT_FOUND
    else:
        #set to None
        oError = None

    oMediumAttachmentArrayResponse = MediumAttachmentObjArrayWrapperResponse()
    olMediumAttachment = list()
    try:
        oVmMediumAttch = oVM.getMediumAttachmentsOfController(name)
        for item in oVmMediumAttch:
            oMediumAttachment = i_fill_mediumattachment(item)
            olMediumAttachment.append(oMediumAttachment)

        oMediumAttachmentArrayResponse.mediumAttachments = olMediumAttachment

    except Exception as e:
        logging.info("Can't get medium attachments for VM '%s': %s" % (oVM.name, str(e)))
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oMediumAttachmentArrayResponse)
    return response, httpCode


# close the session is done inside session_observer.py in SessionObserver::run()
@openSession
def i_machine_takesnapshot(vmid, oMachineTakeSnapshotRequestBody: MachineTakeSnapshotRequestBody, *var_args_tuple):  # noqa: E501
    """
    Call interface method IMachine::takeSnapshot

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineTakeSnapshotRequestBody: 
    :type oMachineTakeSnapshotRequestBody: dict | bytes

    :rtype: MachineTakesnapshotResponse
    """

    httpCode = HTTPStatus.OK
    oError = None

    oVM = var_args_tuple[0]
    vbox_utils_logVmInfo(oVM)

    oSession = var_args_tuple[1]
    oProgress = None
    oCurrMachine = oSession.machine

    fPause = oMachineTakeSnapshotRequestBody.pause
    sName = oMachineTakeSnapshotRequestBody.name
    sDescription = oMachineTakeSnapshotRequestBody.description

    oMachineTakesnapshotResponse = MachineTakeSnapshotResponse()

    logging.info ('MachineState is ' +  ctx['global'].getEnumValueName('MachineState', oCurrMachine.state))
    
    if oCurrMachine.state is ctx['const'].MachineState_Running:
        sResponse = "REST API doesn't support snapshotting when machine is running"
        logging.info (sResponse)
        httpCode = HTTPStatus.PRECONDITION_FAILED
        oError = Error(httpCode, sResponse)

    if oError is None:
        try:
            oProgress, uuidSnapshot = oCurrMachine.takeSnapshot(sName, sDescription, fPause)
        except Exception as e:
            logging.info("Exception during taking the snapshot '%s' for the machine with UUID '%s'" % (sName, oCurrMachine.id))
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))

    if oError is None:
        try:
            # Add Progress Id and Session object into the tracking lists
            ctx['tracker'][oProgress.id] = oSession
            ctx['vms'][oVM.id] = oProgress.id
            oSession = None

            oMachineTakesnapshotResponse.id = uuidSnapshot
            oMachineTakesnapshotResponse.progress = i_fill_progress(oProgress)
        except Exception as e:
            logging.info("The action was successful. But an exception occurred while composing the response.")
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e) + " The action was successful. But an exception occurred while composing the response.")

    response = jsonify(oError if oError is not None else oMachineTakesnapshotResponse)
    return response, httpCode


# def i_machine_findsnapshot(vmid, select=None, nameOrId=None):  # noqa: E501
#     """
#     Call interface method IMachine::findSnapshot

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param select: The object attributes separated by comma
#     :type select: str
#     :param nameOrId: 
#     :type nameOrId: str

#     :rtype: SnapshotResponse
#     """

#     httpCode = HTTPStatus.OK

#     vbox_utils_commonChecks()

#     logging.info('Passed machine Id is ' + vmid)
#     if nameOrId is None:
#         nameOrId = ''
#     logging.info('Passed nameOrId is ' + nameOrId)

#     oVM, oError = vbox_utils_find_machine(vmid)
#     if oVM is None:
#         return jsonify(oError), HTTPStatus.NOT_FOUND
#     else:
#         #set to None
#         oError = None

#     oSnapshotResponse = SnapshotObjWrapperResponse()
#     # oSnapshotResponse = SnapshotResponse()
#     try:
#         oSnapshot = oVM.findSnapshot(nameOrId)
#         if oSnapshot is None:
#             httpCode = HTTPStatus.NOT_FOUND
#             oError = Error(httpCode, "Snapshot with the name or Id %s wasn\'t found" % (nameOrId))
#             return jsonify(oError), httpCode

#         oSnapshotResponse.snapshot = i_fill_snapshot(oSnapshot)
#     except Exception as e:
#         logging.info("Exception during finding the snapshot '%s' for VM '%s': %s" % (nameOrId, oVM.name, str(e)))
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oSnapshotResponse)
#     return response, httpCode


# close the session is done inside session_observer.py in SessionObserver::run()
@openSession
def i_machine_deletesnapshot(vmid, id=None, *var_args_tuple):  # noqa: E501
    """
    Call interface method IMachine::deleteSnapshot

    :param vmid: The Id of vm
    :type vmid: str
    :param id: 
    :type id: str

    :rtype: ProgressResponse
    """

    httpCode = HTTPStatus.OK
    oError = None

    oVM = var_args_tuple[0]
    vbox_utils_logVmInfo(oVM)

    oSession = var_args_tuple[1]
    oProgress = None
    oCurrMachine = oSession.machine

    oProgressResponse = ProgressObjWrapperResponse()

    try:
        oSnapshot = oCurrMachine.findSnapshot(id)
        if oSnapshot is None:
            httpCode = HTTPStatus.NOT_FOUND
            oError = Error(httpCode, "Snapshot with the name or Id %s wasn\'t found" % (id))
            return jsonify(oError), httpCode

        oProgress = oCurrMachine.deleteSnapshot(id)

    except Exception as e:
        logging.info("Exception during deleting the snapshot '%s' for VM '%s': %s" % (id, oVM.name, str(e)))
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is None:
        try:
            # Add Progress Id and Session object into the tracking lists
            ctx['tracker'][oProgress.id] = oSession
            ctx['vms'][oVM.id] = oProgress.id
            oSession = None

            oProgressResponse.progress = i_fill_progress(oProgress)
        except Exception as e:
            logging.info("The action was successful. But an exception occurred while composing the response.")
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e) + " The action was successful. But an exception occurred while composing the response.")

    response = jsonify(oError if oError is not None else oProgressResponse)
    return response, httpCode


# close the session is done inside session_observer.py in SessionObserver::run()
@openSession
def i_machine_restoresnapshot(vmid, snapshot=None, *var_args_tuple):  # noqa: E501
    """
    Call interface method IMachine::restoreSnapshot

    :param vmid: The Id of vm
    :type vmid: str
    :param snapshot: Put here an ID of requested ISnapshot VirtualBox object
    :type snapshot: str

    :rtype: ProgressResponse
    """

    httpCode = HTTPStatus.OK
    oError = None

    oVM = var_args_tuple[0]
    vbox_utils_logVmInfo(oVM)

    oSession = var_args_tuple[1]
    oProgress = None
    oCurrMachine = oSession.machine

    oProgressResponse = ProgressObjWrapperResponse()

    oSnapshot = None
    try:
        oSnapshot = oCurrMachine.findSnapshot(snapshot)
    except Exception as e:
        logging.info("Exception during finding the snapshot '%s' for VM '%s': %s" % (snapshot, oVM.name, str(e)))
        httpCode = HTTPStatus.NOT_FOUND
        oError = Error(httpCode, "Snapshot with the name or Id %s wasn\'t found" % (snapshot))
        return jsonify(oError), httpCode

    try:
        oProgress = oCurrMachine.restoreSnapshot(oSnapshot)
        # This delay is necessary because VirtualBox fails on debug build.
        # Apparently, some actions related to saving or changing the machine state are in progress
        # and take some time to complete.
        time.sleep(0.2)
    except Exception as e:
        logging.info("Exception during restoring the snapshot '%s' for VM '%s': %s" % (snapshot, oVM.name, str(e)))
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is None:
        try:
            # Add Progress Id and Session object into the tracking lists
            ctx['tracker'][oProgress.id] = oSession
            ctx['vms'][oVM.id] = oProgress.id
            oSession = None

            oProgressResponse.progress = i_fill_progress(oProgress)
        except Exception as e:
            logging.info("The action was successful. But an exception occurred while composing the response.")
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e) + " The action was successful. But an exception occurred while composing the response.")

    response = jsonify(oError if oError is not None else oProgressResponse)
    return response, httpCode


def i_machine_deletesnapshotandallchildren(vmid, id=None):  # noqa: E501
    """
    Call interface method IMachine::deleteSnapshotAndAllChildren

    :param vmid: The Id of vm
    :type vmid: str
    :param id: 
    :type id: str

    :rtype: ProgressResponse
    """

    # This API method is right now not implemented! See the official VirtualBox.xidl
    return "This API method is right now not implemented!", HTTPStatus.NOT_IMPLEMENTED


def i_machine_deletesnapshotrange(vmid, oMachineDeleteSnapshotRangeRequestBody):  # noqa: E501
    """
    Call interface method IMachine::deleteSnapshotRange

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineDeleteSnapshotRangeRequestBody:
    :type oMachineDeleteSnapshotRangeRequestBody: dict | bytes

    :rtype: ProgressResponse
    """

    # This API method is right now not implemented! See the official VirtualBox.xidl
    return "This API method is right now not implemented!", HTTPStatus.NOT_IMPLEMENTED


# def i_machine_getnetworkadapter(vmid, select=None, slot=None):  # noqa: E501
#     """
#     Call interface method IMachine::getNetworkAdapter

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param select: The object attributes separated by comma
#     :type select: str
#     :param slot:
#     :type slot: int

#     :rtype: NetworkAdapterResponse
#     """

#     vbox_utils_commonChecks()

#     httpCode = HTTPStatus.OK

#     logging.info('Passed machine Id is ' + vmid)

#     oVM, oError = vbox_utils_find_machine(vmid)
#     if oVM is None:
#         return jsonify(oError), HTTPStatus.NOT_FOUND
#     else:
#         #set to None
#         oError = None

#     oNetworkAdapterResponse = NetworkAdapterObjWrapperResponse()

#     try:
#         if slot is None or slot=="": slot = 0
#         oVBoxNetworkAdapter = oVM.getNetworkAdapter(slot)
#         oNetworkAdapterResponse.adapter = i_fill_networkadapter(oVBoxNetworkAdapter, select)
#         # oNetworkAdapterResponse.adapter = i_fill_network_adapter(oVBoxNetworkAdapter, select)
#         logging.info('Successfully get the network adapter')
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oNetworkAdapterResponse)
#     return response, httpCode


# def i_platformx86_getcpuproperty(vmid, property=None):  # noqa: E501
#     """
#     Call interface method IPlatformX86::getCPUProperty

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param property: For the possible values of enumeration look into #/definitions/CPUPropertyTypeX86
#     :type property: str

#     :rtype: Platformx86GetcpupropertyResponse
#     """

#     vbox_utils_commonChecks()


#     httpCode = HTTPStatus.OK

#     logging.info('Passed machine Id is ' + vmid)

#     oVM, oError = vbox_utils_find_machine(vmid)
#     if oVM is None:
#         return jsonify(oError), HTTPStatus.NOT_FOUND
#     else:
#         #set to None
#         oError = None

#     oPlatformx86GetcpupropertyResponse = PlatformARMGetCPUPropertyResponse()
#     # oPlatformx86GetcpupropertyResponse = Platformx86GetcpupropertyResponse()

#     try:
#         vBoxCPUProperty = swagger_to_vbox_cpupropertytypex86(property)
#         if vBoxCPUProperty is None:
#             return "The requested property " + str(property) + " wasn't found", HTTPStatus.NOT_FOUND

#         strArchtype = vbox_to_swagger_platformarchitecture(oVM.platform.architecture)
#         if strArchtype == "X86":
#             oPlatformX86 = oVM.platform.x86
#             oPlatformx86GetcpupropertyResponse.value = oPlatformX86.getCPUProperty(vBoxCPUProperty)
#             logging.info('The CPU property ' + property + ' is ' + str(oPlatformx86GetcpupropertyResponse.value))
#         else:
#             httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#             oError = Error(httpCode, "Virtual CPU isn't X86 architecture CPU")
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oPlatformx86GetcpupropertyResponse)
#     return response, httpCode


# def i_platformarm_getcpuproperty(vmid, property=None):  # noqa: E501
#     """
#     Call interface method IPlatformARM::getCPUProperty

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param property: For the possible values of enumeration look into #/definitions/CPUPropertyTypeARM
#     :type property: str

#     :rtype: PlatformarmGetcpupropertyResponse
#     """

#     vbox_utils_commonChecks()

#     httpCode = HTTPStatus.OK

#     logging.info('Passed machine Id is ' + vmid)

#     oVM, oError = vbox_utils_find_machine(vmid)
#     if oVM is None:
#         return jsonify(oError), HTTPStatus.NOT_FOUND
#     else:
#         #set to None
#         oError = None

#     oPlatformARMGetcpupropertyResponse = PlatformARMGetCPUPropertyResponse()
#     try:
#         vBoxCPUProperty = swagger_to_vbox_cpupropertytypearm(property)
#         if vBoxCPUProperty is None:
#             return "The requested property " + str(property) + " wasn't found", HTTPStatus.NOT_FOUND

#         strArchtype = vbox_to_swagger_platformarchitecture(oVM.platform.architecture)
#         if strArchtype == "ARM":
#             oPlatformARM = oVM.platform.arm
#             oPlatformARMGetcpupropertyResponse.value = oPlatformARM.getCPUProperty(vBoxCPUProperty)
#             logging.info('The CPU property ' + property + ' is ' + str(oPlatformARMGetcpupropertyResponse.value))
#         else:
#             httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#             oError = Error(httpCode, "Virtual CPU isn't ARM architecture CPU")
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oPlatformARMGetcpupropertyResponse)
#     return response, httpCode


# def i_platformx86_gethwvirtexproperty(vmid, property=None):  # noqa: E501
#     """
#     Call interface method IPlatformX86::getHWVirtExProperty

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param property: For the possible values of enumeration look into #/definitions/HWVirtExPropertyType
#     :type property: str

#     :rtype: Platformx86GetcpupropertyResponse
#     """

#     vbox_utils_commonChecks()

#     httpCode = HTTPStatus.OK

#     logging.info('Passed machine Id is ' + vmid)

#     oVM, oError = vbox_utils_find_machine(vmid)
#     if oVM is None:
#         return jsonify(oError), HTTPStatus.NOT_FOUND
#     else:
#         #set to None
#         oError = None

#     oPlatformx86GetcpupropertyResponse = PlatformARMGetCPUPropertyResponse()
#     try:
#         vBoxHWVirtExProperty = swagger_to_vbox_hwvirtexpropertytype(property)
#         if vBoxHWVirtExProperty is None:
#             return "The requested hardware property " + str(property) + " wasn't found", HTTPStatus.NOT_FOUND

#         strArchtype = vbox_to_swagger_platformarchitecture(oVM.platform.architecture)
#         if strArchtype == "X86":
#             oPlatformX86 = oVM.platform.x86
#             oPlatformx86GetcpupropertyResponse.value = oPlatformX86.getHWVirtExProperty(vBoxHWVirtExProperty)
#             logging.info('The property ' + property + ' is ' + str(oPlatformx86GetcpupropertyResponse.value))
#         else:
#             httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#             oError = Error(httpCode, "Virtual CPU isn't X86 architecture CPU")
#     except Exception as e:
#         logging.info('Exception during obtaining the specified hardware virtualization property ' + property)
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oPlatformx86GetcpupropertyResponse)
#     return response, httpCode


@sessionDecorator
def i_platformx86_sethwvirtexproperty(oVBoxObj, oPlatformX86SetHWVirtExPropertyRequestBody: PlatformX86SetHWVirtExPropertyRequestBody):  # noqa: E501
    """
    Call interface method IPlatformX86::setHWVirtExProperty

    :param vmid: The Id of vm
    :type vmid: str
    :param oPlatformX86SetHWVirtExPropertyRequestBody: 
    :type oPlatformX86SetHWVirtExPropertyRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)

    property = oPlatformX86SetHWVirtExPropertyRequestBody._property
    value = oPlatformX86SetHWVirtExPropertyRequestBody.value

    try:
        vBoxHWVirtExProperty = swagger_to_vbox_hwvirtexpropertytype(property)
        if vBoxHWVirtExProperty is None:
            return "The requested hardware property " + str(property) + " wasn't found", HTTPStatus.NOT_FOUND

        strArchtype = vbox_to_swagger_platformarchitecture(oCurrMachine.platform.architecture)
        if strArchtype == "X86":
            oPlatformX86 = oCurrMachine.platform.x86
            oPlatformX86.setHWVirtExProperty(vBoxHWVirtExProperty, value)
            oCurrMachine.saveSettings()
            logging.info("Successfully set VM hardware virtualization property "  + "'" \
                        + property  + "'" + " to value " + "'" + str(value) + "'")
        else:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, "Virtual CPU isn't X86 architecture CPU")
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is not None:
        return jsonify(oError), httpCode

    return "Successfully set VM hardware virtualization property "  + "'" \
        + property  + "'" + " to value " + "'" + str(value) + "'"


@sessionDecorator
def i_platformx86_setcpuproperty(oVBoxObj, oPlatformX86SetCPUPropertyRequestBody: PlatformX86SetCPUPropertyRequestBody):  # noqa: E501
    """
    Call interface method IPlatformX86::setCPUProperty

    :param vmid: The Id of vm
    :type vmid: str
    :param oPlatformX86SetCPUPropertyRequestBody: 
    :type oPlatformX86SetCPUPropertyRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)

    property = oPlatformX86SetCPUPropertyRequestBody._property
    value = oPlatformX86SetCPUPropertyRequestBody.value
    logging.info('The passed CPU property ' + property + ' is ' + str(value))

    try:
        vBoxCPUProperty = swagger_to_vbox_cpupropertytypex86(property)
        if vBoxCPUProperty is None:
            return "The requested property " + str(property) + " wasn't found", HTTPStatus.NOT_FOUND

        strArchtype = vbox_to_swagger_platformarchitecture(oCurrMachine.platform.architecture)
        if strArchtype == "X86":
            oPlatformX86 = oCurrMachine.platform.x86
            oPlatformX86.setCPUProperty(vBoxCPUProperty, value)
            oCurrMachine.saveSettings()
            logging.info('Successfully set the virtual CPU property ' + property + ' to ' + str(value))
        else:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, f"Virtual CPU isn't X86 architecture CPU")

    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is not None:
        return jsonify(oError), httpCode

    return 'Successfully set the virtual CPU property ' + property + ' to ' + str(value)


@sessionDecorator
def i_platformarm_setcpuproperty(oVBoxObj, oPlatformARMSetCPUPropertyRequestBody: PlatformARMSetCPUPropertyRequestBody):  # noqa: E501
    """
    Call interface method IPlatformARM::setCPUProperty

    :param vmid: The Id of vm
    :type vmid: str
    :param oPlatformARMSetCPUPropertyRequestBody: 
    :type oPlatformARMSetCPUPropertyRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)

    property = oPlatformARMSetCPUPropertyRequestBody._property
    value = oPlatformARMSetCPUPropertyRequestBody.value
    logging.info('The passed CPU property ' + property + ' is ' + str(value))

    try:
        vBoxCPUProperty = swagger_to_vbox_cpupropertytypearm(property)
        if vBoxCPUProperty is None:
            return "The requested property " + str(property) + " wasn't found", HTTPStatus.NOT_FOUND

        strArchtype = vbox_to_swagger_platformarchitecture(oCurrMachine.platform.architecture)
        if strArchtype == "ARM":
            oPlatformARM = oCurrMachine.platform.arm
            oPlatformARM.setCPUProperty(vBoxCPUProperty, value)
            oCurrMachine.saveSettings()
            logging.info('Successfully set the virtual CPU property ' + property + ' to ' + str(value))
        else:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, "Virtual CPU isn't ARM architecture CPU")

    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is not None:
        return jsonify(oError), httpCode

    return 'Successfully set the virtual CPU property ' + property + ' to ' + str(value)


# @sessionDecorator
# def i_machine_hotplugcpu(vmid, cpu=None, *var_args_tuple):  # noqa: E501
#     """
#     Call interface method IMachine::hotPlugCPU

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param cpu:
#     :type cpu: int

#     :rtype: None
#     """

#     vbox_utils_commonChecks()

#     oError = None
#     httpCode = HTTPStatus.OK

#     logging.info('Passed machine Id is ' + vmid)

#     oSession = var_args_tuple[1]
#     oCurrMachine = oSession.machine

#     try:
#         # No return value
#         oCurrMachine.hotPlugCPU(cpu)
#         oCurrMachine.saveSettings()
#         logging.info('Successfully plugged CPU ' + str(cpu))
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else 'Successfully plugged CPU ' + str(cpu))
#     return response, httpCode


# @sessionDecorator
# def i_machine_hotunplugcpu(vmid, cpu=None, *var_args_tuple):  # noqa: E501
#     """
#     Call interface method IMachine::hotUnplugCPU

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param cpu:
#     :type cpu: int

#     :rtype: None
#     """

#     vbox_utils_commonChecks()

#     oError = None
#     httpCode = HTTPStatus.OK

#     logging.info('Passed machine Id is ' + vmid)

#     oSession = var_args_tuple[1]
#     oCurrMachine = oSession.machine

#     try:
#         # No return value
#         oCurrMachine.hotUnplugCPU(cpu)
#         oCurrMachine.saveSettings()
#         logging.info('Successfully unplugged CPU ' + str(cpu))
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else 'Successfully unplugged CPU ' + str(cpu))
#     return response, httpCode


# def i_machine_getcpustatus(vmid, cpu=None):  # noqa: E501
#     """
#     Call interface method IMachine::getCPUStatus

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param cpu: 
#     :type cpu: int

#     :rtype: MachineGetcpustatusResponse
#     """

#     vbox_utils_commonChecks()

#     httpCode = HTTPStatus.OK

#     logging.info('Passed machine Id is ' + vmid)

#     oVM, oError = vbox_utils_find_machine(vmid)
#     if oVM is None:
#         return jsonify(oError), HTTPStatus.NOT_FOUND
#     else:
#         #set to None
#         oError = None

#     oMachineGetcpustatusResponse = MachineGetCPUStatusResponse()

#     try:
#         bStatus = oVM.getCPUStatus(cpu)
#         oMachineGetcpustatusResponse.attached = bStatus
#         logging.info('Successfully get the CPU status')
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oMachineGetcpustatusResponse)
#     return response, httpCode


def i_machine_enumerateguestproperties(vmid, patterns=None):  # noqa: E501
    """
    Call interface method IMachine::enumerateGuestProperties

    :param vmid: The Id of vm
    :type vmid: str
    :param patterns: 
    :type patterns: str

    :rtype: MachineEnumerateguestpropertiesResponse
    """

    vbox_utils_commonChecks()

    httpCode = HTTPStatus.OK

    logging.info('Passed machine Id is ' + vmid)

    oVM, oError = vbox_utils_find_machine(vmid)
    if oVM is None:
        return jsonify(oError), HTTPStatus.NOT_FOUND
    else:
        #set to None
        oError = None

    oMachineEnumerateguestpropertiesResponse = MachineEnumerateGuestPropertiesResponse()
    try:
        names, values, timestamps, flags = oVM.enumerateGuestProperties(patterns)

        oMachineEnumerateguestpropertiesResponse.names = names
        oMachineEnumerateguestpropertiesResponse.values = values
        oMachineEnumerateguestpropertiesResponse.timestamps = timestamps
        oMachineEnumerateguestpropertiesResponse.flags = flags

        logging.info('Successfully enumerated the guest properties')
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oMachineEnumerateguestpropertiesResponse)
    return response, httpCode


# @sessionDecorator
# def i_machine_deleteguestproperty(vmid, name=None, *var_args_tuple):  # noqa: E501
#     """
#     Call interface method IMachine::deleteGuestProperty

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param name: 
#     :type name: str

#     :rtype: None
#     """

#     vbox_utils_commonChecks()

#     oError = None
#     httpCode = HTTPStatus.OK

#     logging.info('Passed machine Id is ' + vmid)

#     oSession = var_args_tuple[1]
#     oCurrMachine = oSession.machine

#     try:
#         oCurrMachine.deleteGuestProperty(name)
#         oCurrMachine.saveSettings()
#         logging.info('Successfully deleted the guest property ' + name)
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else 'Successfully deleted the guest property ' + name)
#     return response, httpCode


@sessionDecorator
def i_machine_setguestpropertyvalue(oVBoxObj, oMachineSetGuestPropertyValueRequestBody: MachineSetGuestPropertyValueRequestBody):  # noqa: E501
    """
    Call interface method IMachine::setGuestPropertyValue

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineSetGuestPropertyValueRequestBody:
    :type oMachineSetGuestPropertyValueRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)

    o = oMachineSetGuestPropertyValueRequestBody

    sProperty = o._property
    sValue = o.value

    try:
        logging.info(o)
        logging.info(o._property)
        logging.info(o.value)
        oCurrMachine.setGuestPropertyValue(sProperty, sValue)
        oCurrMachine.saveSettings()
        logging.info("Successfully set VM guest property " + "'" + sProperty + "'" + " to value " + "'" + sValue + "'")
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is not None:
        return jsonify(oError), httpCode

    return "Successfully set VM guest property " + "'" + sProperty + "'" + " to value " + "'" + sValue + "'"


# def i_machine_getguestpropertytimestamp(vmid, property=None):  # noqa: E501
#     """
#     Call interface method IMachine::getGuestPropertyTimestamp

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param _property: 
#     :type _property: str

#     :rtype: MachineGetguestpropertytimestampResponse
#     """

#     vbox_utils_commonChecks()

#     httpCode = HTTPStatus.OK

#     logging.info('Passed machine Id is ' + vmid)

#     oVM, oError = vbox_utils_find_machine(vmid)
#     if oVM is None:
#         return jsonify(oError), HTTPStatus.NOT_FOUND
#     else:
#         #set to None
#         oError = None

#     oMachineGetguestpropertytimestampResponse = MachineGetGuestPropertyTimestampResponse()

#     try:
#         timestamp = oVM.getGuestPropertyTimestamp(property)
#         oMachineGetguestpropertytimestampResponse.value= timestamp

#         logging.info('Successfully get the timestamp of VM guest property ' + str(property))
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oMachineGetguestpropertytimestampResponse)
#     return response, httpCode


# def i_machine_getguestpropertyvalue(vmid, property=None):  # noqa: E501
#     """
#     Call interface method IMachine::getGuestPropertyValue

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param _property: 
#     :type _property: str

#     :rtype: MediumGetpropertyResponse
#     """

#     vbox_utils_commonChecks()

#     httpCode = HTTPStatus.OK

#     logging.info('Passed machine Id is ' + vmid)

#     oVM, oError = vbox_utils_find_machine(vmid)
#     if oVM is None:
#         return jsonify(oError), HTTPStatus.NOT_FOUND
#     else:
#         #set to None
#         oError = None

#     oMediumGetpropertyResponse = MediumGetPropertyResponse()

#     try:
#         value = oVM.getGuestPropertyValue(property)
#         oMediumGetpropertyResponse.value = value

#         logging.info('Successfully get the value of VM guest property ' + str(property))
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else oMediumGetpropertyResponse)
#     return response, httpCode


@sessionDecorator
def i_machine_addstoragecontroller(oVBoxObj, oMachineAddStorageControllerRequestBody: MachineAddStorageControllerRequestBody):  # noqa: E501
    """
    Call interface method IMachine::addStorageController

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineAddStorageControllerRequestBody:
    :type oMachineAddStorageControllerRequestBody: dict | bytes

    :rtype: StorageControllerResponse
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)

    o = oMachineAddStorageControllerRequestBody
    print(o)
    name = o.name

    oStorageControllerResponse = StorageControllerObjWrapperResponse()
    try:
        connectionType = swagger_to_vbox_storagebus(o.connectionType)
        if connectionType:
            oStorageController = oCurrMachine.addStorageController(name, connectionType)
            oCurrMachine.saveSettings()
        else:
            logging.info("The passed connection type %s isn't supported or unknown" % (o.connectionType))
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, "The passed connection type %s isn't supported or unknown" % (o.connectionType))
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is None:
        oStorageControllerResponse.controller = i_fill_storagecontroller(oStorageController)

    response = jsonify(oError if oError is not None else oStorageControllerResponse)
    return response, httpCode


# @sessionDecorator
# def i_machine_removestoragecontroller(vmid, name=None, *var_args_tuple):  # noqa: E501
#     """
#     Call interface method IMachine::removeStorageController

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param name:
#     :type name: str

#     :rtype: None
#     """

#     vbox_utils_commonChecks()

#     oError = None
#     httpCode = HTTPStatus.OK
#     oSession = var_args_tuple[1]

#     oCurrMachine = oSession.machine
#     try:
#         oCurrMachine.removeStorageController(name)
#         oCurrMachine.saveSettings()
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else "The controller named %s has been successfully removed" % (name))
#     return response, httpCode


# def i_machine_getstoragecontrollerbyname(vmid, select=None, name=None):  # noqa: E501
#     """
#     Call interface method IMachine::getStorageControllerByName

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param select: The object attributes separated by comma
#     :type select: str
#     :param name:
#     :type name: str

#     :rtype: StorageControllerResponse1
#     """

#     vbox_utils_commonChecks()
#     httpCode = HTTPStatus.OK

#     oVM, oError = vbox_utils_find_machine(vmid)
#     if oVM is None:
#         return jsonify(oError), HTTPStatus.NOT_FOUND
#     else:
#         #set to None
#         oError = None

#     oStorageControllerResponse = StorageControllerObjWrapperResponse()
#     try:
#         oStorageController = oVM.getStorageControllerByName(name)
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     if oError is None:
#         oStorageControllerResponse.controller = i_fill_storagecontroller(oStorageController, select)

#     response = jsonify(oError if oError is not None else oStorageControllerResponse)
#     return response, httpCode


def i_machine_getstoragecontrollerbyinstance(vmid, select=None, connectionType=None, instance=None):  # noqa: E501
    """
    Call interface method IMachine::getStorageControllerByInstance

    :param vmid: The Id of vm
    :type vmid: str
    :param select: The object attributes separated by comma
    :type select: str
    :param connectionType: For the possible values of enumeration look into #/definitions/StorageBus
    :type connectionType: str
    :param instance:
    :type instance: int

    :rtype: StorageControllerResponse
    """

    vbox_utils_commonChecks()
    httpCode = HTTPStatus.OK

    oVM, oError = vbox_utils_find_machine(vmid)
    if oVM is None:
        return jsonify(oError), HTTPStatus.NOT_FOUND
    else:
        #set to None
        oError = None

    oStorageControllerResponse = StorageControllerObjWrapperResponse()
    try:
        vBoxConnectionType = swagger_to_vbox_storagebus(connectionType)
        if vBoxConnectionType:
            oStorageController = oVM.getStorageControllerByInstance(vBoxConnectionType, instance)
        else:
            logging.info("The passed connection type %s isn't supported or unknown" % (connectionType))
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, "The passed connection type %s isn't supported or unknown" % (connectionType))
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is None:
        oStorageControllerResponse.controller = i_fill_storagecontroller(oStorageController, select)

    response = jsonify(oError if oError is not None else oStorageControllerResponse)
    return response, httpCode


@sessionDecorator
def i_machine_setstoragecontrollerbootable(oVBoxObj, oMachineSetStorageControllerBootableRequestBody: MachineSetStorageControllerBootableRequestBody):  # noqa: E501
    """
    Call interface method IMachine::setStorageControllerBootable

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineSetStorageControllerBootableRequestBody: 
    :type oMachineSetStorageControllerBootableRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)

    o = oMachineSetStorageControllerBootableRequestBody
    name = o.name
    fBootable = o.bootable

    try:
        oCurrMachine.setStorageControllerBootable(name, fBootable)
        oCurrMachine.saveSettings()
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    try:
        response = jsonify(oError if oError is not None 
                            else "The bootable flag for controller named %s has been successfully set to %s" % (name, fBootable))
    except Exception as e:
        if oError is None:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))
            response = jsonify("The action was successful. But an exception occurred while composing the response.")

    return response, httpCode


@sessionDecorator
def i_machine_attachdevicewithoutmedium(oVBoxObj, oMachineAttachDeviceWithoutMediumRequestBody: MachineAttachDeviceWithoutMediumRequestBody):  # noqa: E501
    """
    Call interface method IMachine::attachDeviceWithoutMedium

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineAttachDeviceWithoutMediumRequestBody: 
    :type oMachineAttachDeviceWithoutMediumRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)

    o = oMachineAttachDeviceWithoutMediumRequestBody
    print(o)
    name = o.name
    controllerPort = o.controllerPort
    device = o.device

    try:
        type = swagger_to_vbox_devicetype(o.type)

        if type != ctx['const'].DeviceType_Floppy and type != ctx['const'].DeviceType_DVD:
            logging.info("VBox REST API doesn't support the type %s for this operation" % (o.type))
            httpCode = HTTPStatus.PRECONDITION_FAILED
            oError = Error(httpCode, "VBox REST API doesn't support the type %s for this operation" % (o.type))
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is None:
        try:
            oCurrMachine.attachDeviceWithoutMedium(name, controllerPort, device, type)
            oCurrMachine.saveSettings()
        except Exception as e:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else "The device %s with the type %s has been successfully attached" % (o.name, o.type))
    return response, httpCode


@consoleDecorator
def i_console_createsharedfolder(oVBoxObj, oConsoleCreateSharedFolderRequestBody: ConsoleCreateSharedFolderRequestBody):  # noqa: E501
    """
    Call interface method IConsole::createSharedFolder

    :param vmid: The Id of vm
    :type vmid: str
    :param oConsoleCreateSharedFolderRequestBody: 
    :type oConsoleCreateSharedFolderRequestBody: dict | bytes

    :rtype: None
    """

    oConsole = oVBoxObj
    logging.info('Passed machine Id is ' + oConsole.machine.id)

    o = oConsoleCreateSharedFolderRequestBody
    name = o.name
    hostPath = o.hostPath
    fWritable = o.writable
    fAutomount = o.automount
    autoMountPoint = o.autoMountPoint

    logging.info("Try to create the shared folder " + name + " for machine " + oConsole.machine.name + " (UUID " + oConsole.machine.id + ")")

    oError = None
    httpCode = HTTPStatus.OK

    for sf in ctx['global'].getArray(oConsole.machine, 'sharedFolders'):
        if sf.name == name:
            logging.info("The shared folder with the name %s exists" % (name))
            httpCode = HTTPStatus.PRECONDITION_FAILED
            oError = Error(httpCode, "The shared folder with the name %s exists" % (name))
            break

    if oError is None:
        try:
            # No return result check
            oConsole.createSharedFolder(name, hostPath, fWritable, fAutomount, autoMountPoint)
            logging.info("Created the shared folder %s" % (name))

        except Exception as e:
            logging.info("Exception during creation the shared folder %s" % (name))
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else "Successfully created the shared folder")
    return response, httpCode


# @sessionDecorator
# def i_console_removesharedfolder(vmid, name=None, *var_args_tuple):  # noqa: E501
#     """
#     Call interface method IConsole::removeSharedFolder

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param name: 
#     :type name: str

#     :rtype: None
#     """

#     oVM = var_args_tuple[0]
#     oSession = var_args_tuple[1]
#     oCurrMachine = oSession.machine
#     oConsole = oSession.console

#     oError = None
#     httpCode = HTTPStatus.OK

#     logging.info("Try to remove the shared folder " + name + " for machine " + oVM.name + " (UUID " + oVM.id + ")")

#     try:
#         # No return result check.
#         # removeSharedFolder returns None instead of the result S_OK.
#         oConsole.removeSharedFolder(name)
#         logging.info("Removed the shared folder %s" % (name))

#         #Don't forget to save
#         oCurrMachine.saveSettings()
#     except Exception as e:
#         logging.info("Exception during removing the shared folder %s" % (name))
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else "Successfully removed the shared folder")
#     return response, httpCode


@sessionDecorator
def i_machine_addusbcontroller(oVBoxObj, oMachineAddUSBControllerRequestBody: MachineAddUSBControllerRequestBody):  # noqa: E501
    """
    Call interface method IMachine::addUSBController

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineAddUSBControllerRequestBody: 
    :type oMachineAddUSBControllerRequestBody: dict | bytes

    :rtype: USBControllerResponse
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)

    o = oMachineAddUSBControllerRequestBody
    print(o)
    name = o.name

    oUSBControllerResponse = USBControllerObjWrapperResponse()
    try:
        usbType = swagger_to_vbox_usbcontrollertype(o.type)
        if usbType:
            oUSBController = oCurrMachine.addUSBController(name, usbType)
            oCurrMachine.saveSettings()
        else:
            logging.info("The passed connection type %s isn't supported or unknown" % (o.type))
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, "The passed connection type %s isn't supported or unknown" % (o.type))
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is None:
        oUSBControllerResponse.controller = i_fill_storagecontroller(oUSBController)

    response = jsonify(oError if oError is not None else oUSBControllerResponse)
    return response, httpCode


# @sessionDecorator
# def i_machine_removeusbcontroller(vmid, name=None, *var_args_tuple):  # noqa: E501
#     """
#     Call interface method IMachine::removeUSBController

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param name: 
#     :type name: str

#     :rtype: None
#     """

#     vbox_utils_commonChecks()

#     oError = None
#     httpCode = HTTPStatus.OK
#     oSession = var_args_tuple[1]

#     oCurrMachine = oSession.machine
#     try:
#         oCurrMachine.removeUSBController(name)
#         oCurrMachine.saveSettings()
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else "The controller named %s has been successfully removed" % (name))
#     return response, httpCode


# def i_machine_getusbcontrollerbyname(vmid, select=None, name=None):  # noqa: E501
#     """
#     Call interface method IMachine::getUSBControllerByName

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param select: The object attributes separated by comma
#     :type select: str
#     :param name: 
#     :type name: str

#     :rtype: USBControllerResponse
#     """

#     vbox_utils_commonChecks()
#     httpCode = HTTPStatus.OK

#     oVM, oError = vbox_utils_find_machine(vmid)
#     if oVM is None:
#         return jsonify(oError), HTTPStatus.NOT_FOUND
#     else:
#         #set to None
#         oError = None

#     oUSBControllerResponse = USBControllerObjWrapperResponse()
#     try:
#         oUSBController = oVM.getUSBControllerByName(name)
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     if oError is None:
#         oUSBControllerResponse.controller = i_fill_usbcontroller(oUSBController, select)

#     response = jsonify(oError if oError is not None else oUSBControllerResponse)
#     return response, httpCode


# def i_machine_getusbcontrollercountbytype(vmid, type=None):  # noqa: E501
#     """
#     Call interface method IMachine::getUSBControllerCountByType

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param type: For the possible values of enumeration look into #/definitions/USBControllerType
#     :type type: str

#     :rtype: MachineGetusbcontrollercountbytypeResponse
#     """

#     vbox_utils_commonChecks()
#     httpCode = HTTPStatus.OK

#     oVM, oError = vbox_utils_find_machine(vmid)
#     if oVM is None:
#         return jsonify(oError), HTTPStatus.NOT_FOUND
#     else:
#         #set to None
#         oError = None

#     oMachineGetusbcontrollercountbytypeResponse = MachineGetUSBControllerCountByTypeResponse()
#     try:
#         cType = swagger_to_vbox_usbcontrollertype(type)
#         cNum = oVM.getUSBControllerCountByType(cType)
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     if oError is None:
#         oMachineGetusbcontrollercountbytypeResponse.controllers = cNum

#     response = jsonify(oError if oError is not None else oMachineGetusbcontrollercountbytypeResponse)
#     return response, httpCode


@sessionDecorator
def i_machine_setautodiscardfordevice(oVBoxObj, oMachineSetAutoDiscardForDeviceRequestBody: MachineSetAutoDiscardForDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::setAutoDiscardForDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineSetAutoDiscardForDeviceRequestBody:
    :type oMachineSetAutoDiscardForDeviceRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)

    o = oMachineSetAutoDiscardForDeviceRequestBody
    print(o)
    name = o.name
    controllerPort = o.controllerPort
    device = o.device
    fDiscard = o.discard

    try:
        oCurrMachine.setAutoDiscardForDevice(name, controllerPort, device, fDiscard)
        oCurrMachine.saveSettings()
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    try:
        response = jsonify(oError if oError is not None else "The discard device flag was set to " + str(fDiscard) + " successfully")
    except Exception as e:
        if oError is None:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))
            response = jsonify("The action was successful. But an exception occurred while composing the response.")

    return response, httpCode


@sessionDecorator
def i_machine_setnobandwidthgroupfordevice(oVBoxObj, oMachineSetNoBandwidthGroupForDeviceRequestBody: MachineSetNoBandwidthGroupForDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::setNoBandwidthGroupForDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineSetNoBandwidthGroupForDeviceRequestBody:
    :type oMachineSetNoBandwidthGroupForDeviceRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)

    o = oMachineSetNoBandwidthGroupForDeviceRequestBody
    print(o)
    name = o.name
    controllerPort = o.controllerPort
    device = o.device

    try:
        oCurrMachine.setNoBandwidthGroupForDevice(name, controllerPort, device)
        oCurrMachine.saveSettings()
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    try:
        response = jsonify(oError if oError is not None else "The no bandwidth group was set successfully")
    except Exception as e:
        if oError is None:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))
            response = jsonify("The action was successful. But an exception occurred while composing the response.")

    return response, httpCode


@sessionDecorator
def i_machine_setbandwidthgroupfordevice(oVBoxObj, oMachineSetBandwidthGroupForDeviceRequestBody: MachineSetBandwidthGroupForDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::setBandwidthGroupForDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineSetBandwidthGroupForDeviceRequestBody:
    :type oMachineSetBandwidthGroupForDeviceRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)

    o = oMachineSetBandwidthGroupForDeviceRequestBody
    print(o)
    name = o.name
    controllerPort = o.controllerPort
    device = o.device
    bandwidthGroup = o.bandwidthGroup
    # oVBoxBandwidthGroup = i_find_bandwidth_group_by_name(bandwidthGroup)
    # if oVBoxBandwidthGroup is None:
    #     oVBoxBandwidthGroup = ''

    # try:
    #     oCurrMachine.setBandwidthGroupForDevice(name, controllerPort, device, oVBoxBandwidthGroup)
    # except Exception as e:
    #     httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
    #     oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else "The setting bandwidth group for device aren't supported by REST API at moment")
    return response, httpCode


@sessionDecorator
def i_machine_sethotpluggablefordevice(oVBoxObj, oMachineSetHotPluggableForDeviceRequestBody: MachineSetHotPluggableForDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::setHotPluggableForDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineSetHotPluggableForDeviceRequestBody:
    :type oMachineSetHotPluggableForDeviceRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)

    o = oMachineSetHotPluggableForDeviceRequestBody
    print(o)
    name = o.name
    controllerPort = o.controllerPort
    device = o.device
    fhotPluggable = o.hotPluggable

    try:
        oCurrMachine.setHotPluggableForDevice(name, controllerPort, device, fhotPluggable)
        oCurrMachine.saveSettings()
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    try:
        response = jsonify(oError if oError is not None else "The hot-pluggable device flag was set to " + str(fhotPluggable) + " successfully")
    except Exception as e:
        if oError is None:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))
            response = jsonify("The action was successful. But an exception occurred while composing the response.")

    return response, httpCode


@sessionDecorator
def i_machine_passthroughdevice(oVBoxObj, oMachinePassthroughDeviceRequestBody: MachinePassthroughDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::passthroughDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachinePassthroughDeviceRequestBody:
    :type oMachinePassthroughDeviceRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)

    o = oMachinePassthroughDeviceRequestBody
    name = o.name
    controllerPort = o.controllerPort
    device = o.device
    fPassthrough = o.passthrough

    try:
        oCurrMachine.passthroughDevice(name, controllerPort, device, fPassthrough)
        oCurrMachine.saveSettings()
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    try:
        response = jsonify(oError if oError is not None else "The passthrough device flag was set to " + str(fPassthrough) + " successfully")
    except Exception as e:
        if oError is None:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))
            response = jsonify("The action was successful. But an exception occurred while composing the response.")

    return response, httpCode


@sessionDecorator
def i_machine_nonrotationaldevice(oVBoxObj, oMachineNonRotationalDeviceRequestBody: MachineNonRotationalDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::nonRotationalDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineNonRotationalDeviceRequestBody:
    :type oMachineNonRotationalDeviceRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)

    o = oMachineNonRotationalDeviceRequestBody
    name = o.name
    controllerPort = o.controllerPort
    device = o.device
    fNonRotational = o.nonRotational

    try:
        oCurrMachine.nonRotationalDevice(name, controllerPort, device, fNonRotational)
        oCurrMachine.saveSettings()
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    try:
        response = jsonify(oError if oError is not None
                            else "The non-rotational device flag was set to " + str(fNonRotational) + " successfully")
    except Exception as e:
        if oError is None:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))
            response = jsonify("The action was successful. But an exception occurred while composing the response.")

    return response, httpCode


# @sessionDecorator
# def i_machine_applydefaults(vmid, flags=None, *var_args_tuple):  # noqa: E501
#     """
#     Call interface method IMachine::applyDefaults

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param flags:
#     :type flags: str

#     :rtype: None
#     """

#     httpCode = HTTPStatus.OK
#     oError = None

#     oVM = var_args_tuple[0]
#     vbox_utils_logVmInfo(oVM)

#     oSession = var_args_tuple[1]
#     oCurrMachine = oSession.machine

#     if flags is None: flags = ''

#     try:
#         oCurrMachine.applyDefaults(flags)
#         oCurrMachine.saveSettings()
#     except Exception as e:
#         logging.info("Exception in i_machine_applydefaults for VM '%s': %s" % (oVM.name, str(e)))
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     response = jsonify(oError if oError is not None else "Successful")
#     return response, httpCode


@sessionDecorator
def i_machine_temporaryejectdevice(oVBoxObj, oMachineTemporaryEjectDeviceRequestBody: MachineTemporaryEjectDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::temporaryEjectDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineTemporaryEjectDeviceRequestBody: 
    :type oMachineTemporaryEjectDeviceRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    oCurrMachine = oVBoxObj
    logging.info('Passed machine Id is ' + oCurrMachine.id)

    o = oMachineTemporaryEjectDeviceRequestBody
    name = o.name
    controllerPort = o.controllerPort
    device = o.device
    fTemporaryEject = o.temporaryEject

    try:
        oCurrMachine.temporaryEjectDevice(name, controllerPort, device, fTemporaryEject)
        oCurrMachine.saveSettings()
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    try:
        response = jsonify(oError if oError is not None 
                            else "The temporary eject device flag was set to " + str(fTemporaryEject) + " successfully")
    except Exception as e:
        if oError is None:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))
            response = jsonify("The action was successful. But an exception occurred while composing the response.")

    return response, httpCode


# def i_machine_geteffectiveparavirtprovider(vmid):  # noqa: E501
#     """
#     Call interface method IMachine::getEffectiveParavirtProvider

#     :param vmid: The Id of vm
#     :type vmid: str

#     :rtype: ParavirtProviderResponse
#     """

#     vbox_utils_commonChecks()
#     httpCode = HTTPStatus.OK

#     oVM, oError = vbox_utils_find_machine(vmid)
#     if oVM is None:
#         return jsonify(oError), HTTPStatus.NOT_FOUND
#     else:
#         #set to None
#         oError = None

#     oParavirtProviderResponse = ParavirtProviderEnumWrapperResponse()
#     try:
#         nVXoxEPPValue = oVM.getEffectiveParavirtProvider()
#     except Exception as e:
#         httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
#         oError = Error(httpCode, str(e))

#     if oError is None:
#         oParavirtProviderResponse.paravirt_provider = vbox_to_swagger_paravirtprovider(nVXoxEPPValue)

#     response = jsonify(oError if oError is not None else oParavirtProviderResponse)
#     return response, httpCode


############################# Not implemented yet #############################
# def i_machine_discardsavedstate(vmid, fRemoveFile=None):  # noqa: E501
#     """
#     Call interface method IMachine::discardSavedState

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param fRemoveFile: 
#     :type fRemoveFile: bool

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_console_addencryptionpassword(vmid, oConsoleAddEncryptionPasswordRequestBody):  # noqa: E501
    """
    Call interface method IConsole::addEncryptionPassword

    :param vmid: The Id of vm
    :type vmid: str
    :param oConsoleAddEncryptionPasswordRequestBody:
    :type oConsoleAddEncryptionPasswordRequestBody: dict | bytes

    :rtype: None
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_console_addencryptionpasswords(vmid, oConsoleAddEncryptionPasswordsRequestBody):  # noqa: E501
    """
    Call interface method IConsole::addEncryptionPasswords

    :param vmid: The Id of vm
    :type vmid: str
    :param oConsoleAddEncryptionPasswordsRequestBody:
    :type oConsoleAddEncryptionPasswordsRequestBody: dict | bytes

    :rtype: None
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_console_clearallencryptionpasswords(vmid):  # noqa: E501
#     """
#     Call interface method IConsole::clearAllEncryptionPasswords

#     :param vmid: The Id of vm
#     :type vmid: str

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_console_removeencryptionpassword(vmid, id=None):  # noqa: E501
#     """
#     Call interface method IConsole::removeEncryptionPassword

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param id:
#     :type id: str

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_machine_attachhostpcidevice(vmid, oMachineAttachHostPCIDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::attachHostPCIDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineAttachHostPCIDeviceRequestBody:
    :type oMachineAttachHostPCIDeviceRequestBody: dict | bytes

    :rtype: None
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_machine_detachhostpcidevice(vmid, hostAddress=None):  # noqa: E501
#     """
#     Call interface method IMachine::detachHostPCIDevice

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param hostAddress:
#     :type hostAddress: int

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_machine_discardsettings(vmid):  # noqa: E501
#     """
#     Call interface method IMachine::discardSettings

#     :param vmid: The Id of vm
#     :type vmid: str

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_machine_exportto(vmid, oMachineExportToRequestBody):  # noqa: E501
    """
    Call interface method IMachine::exportTo

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineExportToRequestBody:
    :type oMachineExportToRequestBody: dict | bytes

    :rtype: VirtualSystemDescriptionResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_machine_lockmachine(vmid, oMachineLockMachineRequestBody):  # noqa: E501
    """
    Call interface method IMachine::lockMachine

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineLockMachineRequestBody:
    :type oMachineLockMachineRequestBody: dict | bytes

    :rtype: None
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_machine_setsettingsfilepath(vmid, settingsFilePath=None):  # noqa: E501
    """
    Call interface method IMachine::setSettingsFilePath

    :param vmid: The Id of vm
    :type vmid: str
    :param settingsFilePath:
    :type settingsFilePath: str

    :rtype: ProgressResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_virtualbox_openmachine(vmid, oVirtualBoxOpenMachineRequestBody):  # noqa: E501
    """
    Call interface method IVirtualBox::openMachine

    :param vmid: The Id of vm
    :type vmid: str
    :param oVirtualBoxOpenMachineRequestBody:
    :type oVirtualBoxOpenMachineRequestBody: dict | bytes

    :rtype: MachineResponse
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


# def i_virtualbox_registermachine(vmid, machine=None):  # noqa: E501
#     """
#     Call interface method IVirtualBox::registerMachine

#     :param vmid: The Id of vm
#     :type vmid: str
#     :param machine: Put here an ID of requested IMachine VirtualBox object
#     :type machine: str

#     :rtype: None
#     """

#     return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED
