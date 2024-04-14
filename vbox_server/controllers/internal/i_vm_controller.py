# pylint: disable=invalid-name
# pylint: disable=consider-using-f-string
# pylint: disable=line-too-long
# pylint: disable=undefined-variable
import sys
import platform
import connexion
import logging
# import base64
from http import HTTPStatus
from flask import jsonify
# from flask.json import loads
from connexion.lifecycle import ConnexionResponse
from werkzeug.datastructures import Headers

from vbox_server.global_settings import *
from vbox_server.utils.vbox_utils import *
from vbox_server.utils.restapi_objects_functions import *
from vbox_server.utils.decorators import session_decorator as sessionDecorator
from vbox_server.models.device_type import DeviceType
from vbox_server.models.error import Error
from vbox_server.models.progress import Progress

#################################################################################
from vbox_server.models.machine_getguestproperty_response import MachineGetguestpropertyResponse  # noqa: E501
from vbox_server.models.machine_querylogfilename_response import MachineQuerylogfilenameResponse  # noqa: E501
from vbox_server.models.machine_readlog_response import MachineReadlogResponse  # noqa: E501
from vbox_server.models.medium_array_response import MediumArrayResponse  # noqa: E501
from vbox_server.models.medium_getproperty_response import MediumGetpropertyResponse  # noqa: E501
from vbox_server.models.virtualbox_getextradatakeys_response import VirtualboxGetextradatakeysResponse  # noqa: E501

# Set logging level for module
logging.getLogger().setLevel(logging.INFO)

# Python 3 hacks:
if sys.version_info[0] >= 3:
    long = int    # pylint: disable=redefined-builtin,invalid-name
    xrange = range; # pylint: disable=redefined-builtin,invalid-name


@sessionDecorator
def i_machine_action(vmid, action, *var_args_tuple):  # noqa: E501
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

    logging.info('Passed machine Id is ' + vmid)
    logging.info('Passed action type is ' + action)

    oError = Error()
    oVM = var_args_tuple[0]

    vbox_utils_logVmInfo(oVM)

    oSession = var_args_tuple[1]
    oProgress = None
    oCurrMachine = oSession.machine

    oConsole = oSession.console
    ops = { 'PAUSE':            lambda: oConsole.pause(),
            'RESTORE':          lambda: oConsole.resume(),
            'STOP':             lambda: oConsole.powerDown(),
            'ACPIPOWERBUTTON':  lambda: oConsole.powerButton(),
            'STARTANDPAUSE':    lambda: oConsole.powerUpPaused(),
            'ACPISLEEP':        lambda: oConsole.sleepButton(),#doesn't work#
            'RESET':            lambda: oConsole.reset(),
            'SAVE':             lambda: oCurrMachine.saveState(),
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


def i_console_pause(vmid):  # noqa: E501
      """
      Call interface method IConsole::pause

      :param vmid: The Id of vm
      :type vmid: str

      :rtype: None
      """
      return i_machine_action(vmid, "PAUSE")


def i_console_powerbutton(vmid):  # noqa: E501
      """
      Call interface method IConsole::powerButton

      :param vmid: The Id of vm
      :type vmid: str

      :rtype: None
      """
      return i_machine_action(vmid, "ACPIPOWERBUTTON")


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


def i_console_reset(vmid):  # noqa: E501
    """
    Call interface method IConsole::reset

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: None
    """

    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED


def i_console_resume(vmid):  # noqa: E501
    """
    Call interface method IConsole::resume

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: None
    """
    return i_machine_action(vmid, "RESTORE")


def i_console_sleepbutton(vmid):  # noqa: E501
    """
    Call interface method IConsole::sleepButton

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: None
    """
    return i_machine_action(vmid, "ACPISLEEP")


@sessionDecorator
def i_machine_deleteconfig(vmid, media=None, *var_args_tuple):
    """
    Call interface method IMachine::deleteConfig

    :param vmid: The Id of vm
    :type vmid: str
    :param media: Put here an ID of requested IMedium VirtualBox object
    :type media: List[str]
    """

    oVM = var_args_tuple[0]
    oError = None
    httpCode = HTTPStatus.OK

    vbox_utils_commonChecks()

    logging.info('Passed machine Id is ' + vmid)

    logging.info("Try to remove the machine " + oVM.name + " (UUID " + oVM.id + ")")

    oSession = var_args_tuple[1]
    oCurrMachine = oSession.machine
    oProgress = None
    olDisks = []
    try:
        vbox_utils_detachVmDevice(oCurrMachine)
        olDisks = oCurrMachine.unregister(ctx['const'].CleanupMode_Full)
    except Exception as e:
        logging.info("Can't delete VM '%s': %s" % (oVM.name, str(e)))
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is None:
        try:
            oProgress = oCurrMachine.deleteConfig(olDisks)
        except Exception as e:
            logging.info("Can't delete VM '%s': %s" % (oVM.name, str(e)))
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR            
            oError = Error(httpCode, str(e))

    data = {
        'progress id': oProgress.id if oProgress is not None else 'Null',
    }

    response = jsonify(oError if oError is not None else data)

    return response, httpCode


def i_machine_getbootorder(vmid, position=None):  # noqa: E501
    """
    Call interface method IMachine::getBootOrder

    :param vmid: The Id of vm
    :type vmid: str
    :param position: 
    :type position: int

    :rtype: DeviceTypeResponse
    """

    vbox_utils_commonChecks()

    oVM = None
    oError = None
    httpCode = HTTPStatus.OK

    logging.info('Passed machine Id is ' + vmid)

    oVM, oError = vbox_utils_find_machine(vmid)
    if oError is not None:
        return jsonify(oError), HTTPStatus.NOT_FOUND

    oDeviceType = DeviceType()
    if oVM is not None:
        try:
            oVBoxMediumdeviceType = oVM.getBootOrder(position)
            oDeviceType = ctx[ 'global'].getEnumValueName('DeviceType', oVBoxMediumdeviceType)
            logging.info('The command result is ' + str(oDeviceType))            
        except Exception as e:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oDeviceType)
    return response, httpCode


def i_machine_getextradata(vmid, key=None):  # noqa: E501
    """
    Call interface method IMachine::getExtraData

    :param vmid: The Id of vm
    :type vmid: str
    :param key: 
    :type key: str

    :rtype: MediumGetpropertyResponse
    """

    vbox_utils_commonChecks()

    oVM = None
    oError = None
    httpCode = HTTPStatus.OK

    logging.info('Passed machine Id is ' + vmid)

    oVM, oError = vbox_utils_find_machine(vmid)
    if oError is not None:
        return jsonify(oError), HTTPStatus.NOT_FOUND

    oMediumGetpropertyResponse = MediumGetpropertyResponse()
    if oVM is not None:
        try:
            res = oVM.getExtraData(key)
            oMediumGetpropertyResponse.value = res
            if res!='':
                logging.info('Successfully get the value of VM extra data ' + key)
                logging.info('The command result is ' + res)
            else:
                logging.info('Unknown extra data or the value is empty ')

        except Exception as e:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oMediumGetpropertyResponse)
    return response, httpCode


def i_machine_getextradatakeys(vmid):  # noqa: E501
    """
    Call interface method IMachine::getExtraDataKeys

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: VirtualboxGetextradatakeysResponse
    """

    vbox_utils_commonChecks()

    oVM = None
    oError = None
    httpCode = HTTPStatus.OK

    logging.info('Passed machine Id is ' + vmid)

    oVM, oError = vbox_utils_find_machine(vmid)
    if oError is not None:
        return jsonify(oError), HTTPStatus.NOT_FOUND

    oVirtualboxGetextradatakeysResponse = VirtualboxGetextradatakeysResponse()
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

    oVM = None
    oError = None
    httpCode = HTTPStatus.OK

    logging.info('Passed machine Id is ' + vmid)

    oVM, oError = vbox_utils_find_machine(vmid)
    if oError is not None:
        return jsonify(oError), HTTPStatus.NOT_FOUND

    oMachineGetguestpropertyResponse = MachineGetguestpropertyResponse()
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


def i_machine_launchvmprocess(vmid, oMachineLaunchVMProcessRequestBody):  # noqa: E501
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
    environmentChanges = o.environment_changes

    vbox_utils_commonChecks()

    print ('Passed machine Id is ' + vmid)

    oVM, oError = vbox_utils_find_machine(vmid)
    if oError is not None:
        return jsonify(oError), HTTPStatus.NOT_FOUND

    vbox_utils_logVmInfo(oVM)
    
    #todo: uuid conversion and check should be here
    if oSessionId is None or oSessionId=="None" or oSessionId=="":
        oSession = ctx['global'].getSessionObject() 
    oProgress = None

    try:  
        logging.info ('Trying to call oVM.launchVMProcess()')
        oProgress = oVM.launchVMProcess(oSession, name, environmentChanges)

        if oProgress is not None: logging.info ('Progress Id is ' + oProgress.id)
        oProgress.waitForCompletion(-1)
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR  
        oError = Error(httpCode, str(e))  

    logging.info ('Session name is ' + oSession.name)# returns GUI/Qt always
    logging.info ('Session state is ' + ctx['global'].getEnumValueName('SessionState', oSession.state))

    if oSession is not None:
        # Try close it.
        try:
            if oSession.state == ctx['const'].SessionState_Locked:
                oSession.unlockMachine()
                logging.info ('Unlocked the current machine ' + oVM.id)
                oSession = None
        except:
            logging.info('Exception trying unlock machine, close session or set session name')
            try:    fIgnore = oSession.state == ctx['const'].SessionState_Unlocked
            except: fIgnore = False
        
            if fIgnore:
                oSession  = None # Must prevent a retry during GC.
            else:
                logging.warning ('ISession::unlockMachine failed on %s' % (oSession))

    resProgress = Progress(oProgress.id)

    response = jsonify(oError if oError is not None else resProgress)

    return response, httpCode


def i_machine_querylogfilename(vmid, idx=None):  # noqa: E501
    """
    Call interface method IMachine::queryLogFilename

    :param vmid: The Id of vm
    :type vmid: str
    :param idx: 
    :type idx: int

    :rtype: MachineQuerylogfilenameResponse
    """
    oError = None
    httpCode = HTTPStatus.OK

    oVM, oError = vbox_utils_find_machine(vmid)
    if oError is not None:
        return jsonify(oError), HTTPStatus.NOT_FOUND

    oMachineQuerylogfilenameResponse = MachineQuerylogfilenameResponse()

    try:
        if idx is None:
            idx = 0
        sFilename = oVM.queryLogFilename(idx)
        oMachineQuerylogfilenameResponse.filename = sFilename

    except Exception as e:
        logging.info("Can't find VM's log file '%d': %s" % (idx, str(e)))
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oMachineQuerylogfilenameResponse)
    return response, httpCode


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

    oError = None
    httpCode = HTTPStatus.OK

    oVM, oError = vbox_utils_find_machine(vmid)
    if oError is not None:
        return jsonify(oError), HTTPStatus.NOT_FOUND

    oMachineReadlogResponse = MachineReadlogResponse()
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
def i_machine_createsharedfolder(vmid, oMachineCreateSharedFolderRequestBody, *var_args_tuple):  # noqa: E501
    """
    Call interface method IMachine::createSharedFolder

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineCreateSharedFolderRequestBody: 
    :type oMachineCreateSharedFolderRequestBody: dict | bytes

    :rtype: None
    """

    oVM = var_args_tuple[0]
    oSession = var_args_tuple[1]
    oCurrMachine = oSession.machine

    o = oMachineCreateSharedFolderRequestBody
    name = o.name
    host_path = o.host_path
    fWritable = o.writable
    fAutomount = o.automount
    auto_mount_point = o.auto_mount_point

    logging.info("Try to create the shared folder " + name + " for machine " + oVM.name + " (UUID " + oVM.id + ")")

    oError = None
    httpCode = HTTPStatus.OK

    found = False
    for sf in ctx['global'].getArray(oVM, 'sharedFolders'):
        if sf.name == name:
            logging.info("The shared folder with the name %s exists" % (name))
            httpCode = HTTPStatus.PRECONDITION_FAILED
            oError = Error(httpCode, "The shared folder with the name %s exists" % (name))
            found = True
            break

    if oError is None:
        try:
            # No return result check
            oCurrMachine.createSharedFolder(name, host_path, fWritable, fAutomount, auto_mount_point)
            logging.info("Created the shared folder %s" % (name))

            #Don't forget to save
            oCurrMachine.saveSettings()

        except Exception as e:
            logging.info("Can't create shared folder %s" % (name))
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else "Successfully created the shared folder")
    return response, httpCode


@sessionDecorator
def i_machine_removesharedfolder(vmid, name=None, *var_args_tuple):  # noqa: E501
    """
    Call interface method IMachine::removeSharedFolder

    :param vmid: The Id of vm
    :type vmid: str
    :param name: 
    :type name: str

    :rtype: None
    """

    oVM = var_args_tuple[0]
    oSession = var_args_tuple[1]
    oCurrMachine = oSession.machine
    oError = None
    httpCode = HTTPStatus.OK

    logging.info("Try to remove the shared folder " + name + " for machine " + oVM.name + " (UUID " + oVM.id + ")")

    oCurrMachine = oSession.machine
    oError = None
    httpCode = HTTPStatus.OK

    found = False
    for sf in ctx['global'].getArray(oVM, 'sharedFolders'):
        if sf.name == name:
            try:
                # No return result check.
                # removeSharedFolder returns None instead of the result S_OK.
                oCurrMachine.removeSharedFolder(name)
                logging.info("1. Removed the shared folder %s" % (name))

                #Don't forget to save
                oCurrMachine.saveSettings()
                found = True
                break

            except Exception as e:
                logging.info("Can't remove shared folder %s" % (name))
                httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
                oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else "Successfully removed the shared folder")
    return response, httpCode


@sessionDecorator
def i_machine_savesettings(vmid, *var_args_tuple):  # noqa: E501
    """
    Call interface method IMachine::saveSettings

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    logging.info('Passed machine Id is ' + vmid)

    oSession = var_args_tuple[1]
    oCurrMachine = oSession.machine

    if oCurrMachine is not None:
        try:
            oCurrMachine.saveSettings()
        except Exception as e:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))

    if oError is not None:
        return jsonify(oError), httpCode

    return "Machine's settings has been successfully saved " + "(uuid " + vmid + ")"


def i_machine_savestate(vmid):  # noqa: E501
    """
    Call interface method IMachine::saveState

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: ProgressResponse
    """
    return i_machine_action(vmid, 'SAVE')


@sessionDecorator
def i_machine_setbootorder(vmid, oMachineSetBootOrderRequestBody, *var_args_tuple):  # noqa: E501
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

    logging.info('Passed machine Id is ' + vmid)

    oSession = var_args_tuple[1]
    oCurrMachine = oSession.machine

    o = oMachineSetBootOrderRequestBody
    device = o.device
    position = o.position

    if oCurrMachine is not None:
        try:
            if device == "FLOPPY":
                device = ctx['const'].DeviceType_Floppy
            elif device == "DVD":
                device = ctx['const'].DeviceType_DVD
            elif device == "HARDDISK":
                device = ctx['const'].DeviceType_HardDisk
            elif device == "NETWORK":
                device = ctx['const'].DeviceType_Network
            else:
                return "The requested device " + str(o.device) + " is not supported for booting", HTTPStatus.NOT_FOUND

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
def i_machine_setextradata(vmid, oMachineSetExtraDataRequestBody, *var_args_tuple):  # noqa: E501
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

    logging.info('Passed machine Id is ' + vmid)

    oSession = var_args_tuple[1]
    oCurrMachine = oSession.machine

    o = oMachineSetExtraDataRequestBody
    if oCurrMachine is not None:
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
def i_machine_setguestproperty(vmid, oMachineSetGuestPropertyRequestBody, *var_args_tuple):  # noqa: E501
    """
    Call interface method IMachine::setGuestProperty

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineSetGuestPropertyRequestBody:
    :type oMachineSetGuestPropertyRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK

    logging.info('Passed machine Id is ' + vmid)

    oSession = var_args_tuple[1]
    oCurrMachine = oSession.machine

    o = oMachineSetGuestPropertyRequestBody
    if oCurrMachine is not None:
        try:
            logging.info(o)
            logging.info(o._property)
            logging.info(o.value)
            oCurrMachine.setGuestProperty(o._property, o.value, '')
            oCurrMachine.saveSettings()
            logging.info("Successfully set VM guest property "  + "'" + o._property  + "'" + " to value " + "'" + o.value + "'")
        except Exception as e:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))

    if oError is not None:
        return jsonify(oError), httpCode

    return "Successfully set VM guest property "  + "'" + o._property  + "'" + " to value " + "'" + o.value + "'"


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

    oVM = None
    oError = None
    httpCode = HTTPStatus.OK

    print ('Passed machine Id is ' + vmid)
    if cleanupMode is not None: logging.info ('Passed cleanupMode is ' + cleanupMode)
    else: cleanupMode = 'FULL'

    oVM, oError = vbox_utils_find_machine(vmid)
    if oError is not None:
        return jsonify(oError), HTTPStatus.NOT_FOUND

    oMediumList = []
    oMediumArrayResponse = None

    if oVM is not None:
        vbox_utils_logVmInfo(oVM)

        try:
            if cleanupMode == 'FULL':
                cleanupMode = ctx['const'].CleanupMode_Full
            elif cleanupMode == 'UNREGISTERONLY':
                cleanupMode = ctx['const'].CleanupMode_UnregisterOnly
            elif cleanupMode == 'DETACHALLRETURNNONE':
                cleanupMode = ctx['const'].CleanupMode_DetachAllReturnNone
            elif cleanupMode == 'DETACHALLRETURNHARDDISKSONLY':
                cleanupMode = ctx['const'].CleanupMode_DetachAllReturnHardDisksOnly
            else:
                return "The requested cleanup mode " + str(cleanupMode) + " wasn't found", HTTPStatus.NOT_FOUND

            olDisks = oVM.unregister(cleanupMode)
            try:
                oMediumArrayResponse = MediumArrayResponse()
                oMediumArrayResponse.media = oMediumList
            except Exception as e:
                oMediumArrayResponse = None
                httpCode = HTTPStatus.OK
                oError = Error(httpCode, str(e))

            logging.info ('Successfully unregistered VM %s' + vmid)

        except Exception as e:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))

    response = jsonify(oMediumArrayResponse if oMediumArrayResponse is not None else oError)

    return response, httpCode


def i_virtualbox_createmachine(oVirtualBoxCreateMachineRequestBody):  # noqa: E501
    """
    Call interface method IVirtualBox::createMachine

    :param oVirtualBoxCreateMachineRequestBody: 
    :type oVirtualBoxCreateMachineRequestBody: dict | bytes

    :rtype: MachineResponse
    """

    vbox_utils_commonChecks()

    oVM = None
    oError = None
    httpCode = HTTPStatus.OK

    o = oVirtualBoxCreateMachineRequestBody
    print(o)
    name = o.name
    osTypeId = o.os_type_id
    groups = o.groups
    flags = o.flags
    settingsFile = o.settings_file# check or ignore?

    platform = o.platform
    logging.info('The passed PlatformArchitecture is ' + str(platform))

    if platform == "X86":
        platform = ctx['const'].PlatformArchitecture_x86
    elif platform == "ARM":
        platform = ctx['const'].PlatformArchitecture_ARM
    else:#default is NONE
        platform = ctx['const'].PlatformArchitecture_None

    logging.info('The converted PlatformArchitecture is ' + str(platform))

    cipher = o.cipher
    password_id = o.password_id
    password = o.password

    oVM, oError = vbox_utils_find_machine(name)
    if oError is None and oVM is not None:
        httpCode = HTTPStatus.PRECONDITION_FAILED
        oError = Error(httpCode, "Machine with the name %s has already registered in VirtualBox" % (name))
        return jsonify(oError), httpCode

    try:
        ctx['vb'].getGuestOSType(osTypeId)
    except Exception as e:
        httpCode = HTTPStatus.PRECONDITION_FAILED
        oError = Error(httpCode, str(e))
        return jsonify(oError), httpCode 

    oVBox = ctx['vb']

    try:
        oVM = oVBox.createMachine(settingsFile, name, platform, groups, osTypeId, flags, cipher, password_id, password)
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


def i_virtualbox_findmachine(vmid, select=None, nameOrId=None):  # noqa: E501
    """
    Call interface method IVirtualBox::findMachine

    :param vmid: The Id of vm
    :type vmid: str
    :param select: The object attributes separated by comma
    :type select: str
    :param nameOrId: 
    :type nameOrId: str

    :rtype: MachineResponse
    """

    vbox_utils_commonChecks()

    oVM = None
    oError = None
    httpCode = HTTPStatus.OK

    logging.info ('Passed machine Id is ' + vmid)
    if select is not None: logging.info ('Passed attributes are ' + select)

    oVM, oError = vbox_utils_find_machine(vmid)
    if oError is not None:
        return jsonify(oError), HTTPStatus.NOT_FOUND

    if oVM is not None:
        vbox_utils_logVmInfo(oVM)

        oMachine = None
        try:
            oMachine = i_fill_machine(oVM, select)
            logging.info ('Successful i_fill_machine(oVM, select)')

        except Exception as e:
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))

    response = jsonify(oMachine if oMachine is not None else oError)

    return response, httpCode


@sessionDecorator
def i_machine_attachdevice(vmid, oMachineAttachDeviceRequestBody, *var_args_tuple):  # noqa: E501
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

    logging.info('Passed machine Id is ' + vmid)

    o = oMachineAttachDeviceRequestBody
    name = o.name
    port = o.controller_port
    slot = o.device
    devtype = o.type
    mediumPath = o.medium

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

    if devtype == "FLOPPY":
        devtype = ctx['const'].DeviceType_Floppy
    elif devtype == "DVD":
        devtype = ctx['const'].DeviceType_DVD
    elif devtype == "HARDDISK":
        devtype = ctx['const'].DeviceType_HardDisk
    else:
        return "The requested type " + str(devtype) + " is not supported", HTTPStatus.NOT_FOUND

    oVboxMedium = None
    try:
        if mediumPath is not None or len(mediumPath)>0:
            oVboxMedium = ctx['vb'].openMedium(mediumPath, ctx['global'].constants.DeviceType_HardDisk, ctx['global'].constants.AccessMode_ReadWrite, False)
            if oVboxMedium is not None:
                oCurrMachine.attachDevice(name, port, slot, devtype, oVboxMedium)
            else:
                httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
                oError = Error(httpCode, "Something went wrong during opening the medium " + str(mediumPath))
        else:
            oCurrMachine.attachDevice(name, port, slot, devtype, None)

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
        if oVboxMedium is not None:
            response = response + " The device is " + mediumPath

    return response, httpCode


@sessionDecorator
def i_machine_detachdevice(vmid, oMachineDetachDeviceRequestBody, *var_args_tuple):  # noqa: E501
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

    logging.info('Passed machine Id is ' + vmid)

    oVM = var_args_tuple[0]
    oSession = var_args_tuple[1]
    oCurrMachine = oSession.machine
    machineState = oSession.machine.state

    o = oMachineDetachDeviceRequestBody
    name = o.name
    port = o.controller_port
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
        oResponse, httpCode = i_machine_getmediumattachment(vmid, None, name, port, slot)

        if oResponse.is_json is True: logging.info(oResponse.get_json())

        if httpCode == HTTPStatus.OK:
            logging.info("Try to detach device from the machine " + oVM.name + " (UUID " + oVM.id + ")")
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

    oError = None
    httpCode = HTTPStatus.OK

    vbox_utils_commonChecks()

    logging.info('Passed machine Id is ' + vmid)
    logging.info('Passed name is ' + name)
    logging.info('Passed controllerPort is ' + str(controllerPort))
    logging.info('Passed device is ' + str(device))

    oVM, oError = vbox_utils_find_machine(vmid)
    if oError is not None:
        return jsonify(oError), HTTPStatus.NOT_FOUND

    oMediumAttachment = MediumAttachment()
    try:
        olAttachments = ctx['global'].getArray(oVM, 'mediumAttachments')
        for item in olAttachments:
            if item.controller==name and item.port==controllerPort and item.device==device:
                oMediumAttachment = i_fill_medium_attachment(item, select)
    except Exception as e:
        oMediumAttachment = None
        logging.info("Can't get medium attachment for VM '%s': %s" % (oVM.name, str(e)))
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    response = jsonify(oError if oError is not None else oMediumAttachment)
    return response, httpCode


@sessionDecorator
def i_machine_mountmedium(vmid, mediumid, oMachineMountMediumRequestBody, *var_args_tuple):  # noqa: E501
    """
    Call interface method IMachine::mountMedium

    :param vmid: The Id of vm
    :type vmid: str
    :param mediumid: The Id of medium
    :type mediumid: str
    :param oMachineMountMediumRequestBody:
    :type oMachineMountMediumRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oError = None
    httpCode = HTTPStatus.OK
    oSession = var_args_tuple[1]

    o = oMachineMountMediumRequestBody
    print(o)
    name = o.name
    controller_port = o.controller_port
    device = o.device
    force = o.force

    oFoundMedium = None
    try:
        lTypes = {'DVDImages', 'floppyImages'}
        oVBoxVirtualBox = ctx['vb']

        for disktype in lTypes:
            ol_disks = ctx['global'].getArray(oVBoxVirtualBox, disktype)
            for item in ol_disks:
                o = i_fill_partial_medium(item, 'id')
                if o.id == mediumid:
                    oFoundMedium = item
                    logging.info('Found medium with id ' + mediumid)
                    break

            if oFoundMedium is not None:
                break

    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        logging.info('Exception during finding the passed medium with uuid ' + mediumid)
        oError = Error(httpCode, 'Exception during finding the passed medium with uuid ' + mediumid)
        return jsonify(oError), httpCode

    if oFoundMedium is None:
        httpCode = HTTPStatus.NOT_FOUND
        oError = Error(httpCode, 'The passed medium with uuid ' + mediumid + ' wasn\'t found among DVD or floppy images')
        return jsonify(oError), httpCode

    oCurrMachine = oSession.machine

    try:
        oCurrMachine.mountMedium(name, controller_port, device, oFoundMedium, force)
        oCurrMachine.saveSettings()
    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is not None:
        return jsonify(oError), httpCode

    return 'The passed medium with uuid ' + mediumid +' has been successfully mounted', httpCode



@sessionDecorator
def i_machine_unmountmedium(vmid, mediumid, oMachineUnmountMediumRequestBody, *var_args_tuple):  # noqa: E501
    """
    Call interface method IMachine::unmountMedium

    :param vmid: The Id of vm
    :type vmid: str
    :param mediumid: The Id of medium
    :type mediumid: str
    :param oMachineUnmountMediumRequestBody:
    :type oMachineUnmountMediumRequestBody: dict | bytes

    :rtype: None
    """

    vbox_utils_commonChecks()

    oVM = var_args_tuple[0]
    oError = None
    httpCode = HTTPStatus.OK

    o = oMachineUnmountMediumRequestBody
    print(o)
    name = o.name
    controller_port = o.controller_port
    device = o.device
    force = o.force

    oMediumAttachement = None
    oMedium = None
    found = False

    if oVM is not None:
        try:
            if mediumid is None or mediumid=='' or mediumid=='noid':
                oVBoxMedium = oVM.getMedium(name, controller_port, device)
                oMedium = i_fill_medium(oVBoxMedium)
                if oMedium.device_type == 'HardDisk':
                    raise 'Wrong medium device type. Must be DVD or Floppy.'
                logging.info('Successfully found the medium on the controller ' + name + ' on port ' + str(controller_port) + ' on device ' + str(device))
                found = True
            else:
                ol_medium_attachments = ctx['global'].getArray(oVM,'mediumAttachments')
                for item in ol_medium_attachments:
                    oMediumAttachement = i_fill_medium_attachment(item)
                    if oMediumAttachement.medium == mediumid:
                        if oMediumAttachement.type == 'HardDisk':
                            raise 'Wrong medium device type. Must be DVD or Floppy.'
                        logging.info('Successfully found the medium with id ' + mediumid)
                        found = True
                        break

        except Exception as e:
            logging.info("Exception during finding the medium with uuid " + mediumid + '. (' + str(e) + ')')
            httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
            oError = Error(httpCode, str(e))
            return jsonify(oError), httpCode

    if found != True:
        httpCode = HTTPStatus.NOT_FOUND
        logging.info("Couldn't find the medium with uuid " + mediumid)
        oError = Error(httpCode, "Couldn't find the medium with uuid " + mediumid)
        return jsonify(oError), httpCode

    oSession = var_args_tuple[1]
    oCurrMachine = oSession.machine

    try:
        if httpCode == HTTPStatus.OK:
            if oMediumAttachement is not None:
                name = oMediumAttachement.controller
                controller_port = oMediumAttachement.port
                device = oMediumAttachement.device
                print (name, controller_port, device)
            oCurrMachine.unmountMedium(name, controller_port, device, force)
            oCurrMachine.saveSettings()

    except Exception as e:
        httpCode = HTTPStatus.INTERNAL_SERVER_ERROR
        oError = Error(httpCode, str(e))

    if oError is not None:
        return jsonify(oError), httpCode

    return 'The passed medium with uuid ' + mediumid +' has been successfully unmounted', httpCode
