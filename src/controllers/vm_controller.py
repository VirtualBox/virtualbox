import connexion

from vbox_server.models.machine_launch_vm_process_request_body import MachineLaunchVMProcessRequestBody  # noqa: E501
from vbox_server.models.machine_set_extra_data_request_body import MachineSetExtraDataRequestBody  # noqa: E501
from vbox_server.models.machine_set_guest_property_request_body import MachineSetGuestPropertyRequestBody  # noqa: E501
from vbox_server.models.virtual_box_create_machine_request_body import VirtualBoxCreateMachineRequestBody  # noqa: E501
from vbox_server.models.machine_set_boot_order_request_body import MachineSetBootOrderRequestBody  # noqa: E501
from vbox_server.models.machine_create_shared_folder_request_body import MachineCreateSharedFolderRequestBody  # noqa: E501
from vbox_server.controllers.internal.i_vm_controller import *


def console_pause(vmid):  # noqa: E501
    """
    Call interface method IConsole::pause

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: None
    """
    return i_console_pause(vmid)


def console_powerbutton(vmid):  # noqa: E501
    """
    Call interface method IConsole::powerButton

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: None
    """
    return i_console_powerbutton(vmid)


def console_powerdown(vmid):  # noqa: E501
    """
    Call interface method IConsole::powerDown

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: ProgressResponse
    """
    return i_console_powerdown(vmid)


def console_powerup(vmid):  # noqa: E501
    """
    Call interface method IConsole::powerUp

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: ProgressResponse
    """
    return i_console_powerup(vmid)


def console_poweruppaused(vmid):  # noqa: E501
    """
    Call interface method IConsole::powerUpPaused

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: ProgressResponse
    """
    return i_console_poweruppaused(vmid)


def console_reset(vmid):  # noqa: E501
    """
    Call interface method IConsole::reset

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: None
    """
    return i_console_reset(vmid)


def console_resume(vmid):  # noqa: E501
    """
    Call interface method IConsole::resume

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: None
    """
    return i_console_resume(vmid)


def console_sleepbutton(vmid):  # noqa: E501
    """
    Call interface method IConsole::sleepButton

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: None
    """
    return i_console_sleepbutton(vmid)


def machine_deleteconfig(vmid, media=None):  # noqa: E501
    """
    Call interface method IMachine::deleteConfig

    :param vmid: The Id of vm
    :type vmid: str
    :param media: Put here an ID of requested IMedium VirtualBox object
    :type media: List[str]

    :rtype: ProgressResponse
    """
    return i_machine_deleteconfig(vmid, media)


def machine_getbootorder(vmid, position=None):  # noqa: E501
    """
    Call interface method IMachine::getBootOrder

    :param vmid: The Id of vm
    :type vmid: str
    :param position: 
    :type position: int

    :rtype: DeviceTypeResponse
    """
    return i_machine_getbootorder(vmid, position)


def machine_getextradata(vmid, key=None):  # noqa: E501
    """
    Call interface method IMachine::getExtraData

    :param vmid: The Id of vm
    :type vmid: str
    :param key: 
    :type key: str

    :rtype: MediumGetpropertyResponse
    """
    return i_machine_getextradata(vmid, key)


def machine_getextradatakeys(vmid):  # noqa: E501
    """
    Call interface method IMachine::getExtraDataKeys

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: VirtualboxGetextradatakeysResponse
    """
    return i_machine_getextradatakeys(vmid)


def machine_launchvmprocess(vmid, oMachineLaunchVMProcessRequestBody):  # noqa: E501
    """
    Call interface method IMachine::launchVMProcess

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineLaunchVMProcessRequestBody: 
    :type oMachineLaunchVMProcessRequestBody: dict | bytes

    :rtype: ProgressResponse
    """
    if connexion.request.is_json:
        oMachineLaunchVMProcessRequestBody = MachineLaunchVMProcessRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_launchvmprocess(vmid, oMachineLaunchVMProcessRequestBody)


def machine_querylogfilename(vmid, idx=None):  # noqa: E501
    """
    Call interface method IMachine::queryLogFilename

    :param vmid: The Id of vm
    :type vmid: str
    :param idx: 
    :type idx: int

    :rtype: MachineQuerylogfilenameResponse
    """
    return i_machine_querylogfilename(vmid, idx)


def machine_readlog(vmid, idx=None, offset=None, size=None):  # noqa: E501
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
    return i_machine_readlog(vmid, idx, offset, size)


def machine_createsharedfolder(vmid, oMachineCreateSharedFolderRequestBody):  # noqa: E501
    """
    Call interface method IMachine::createSharedFolder

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineCreateSharedFolderRequestBody: 
    :type oMachineCreateSharedFolderRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oMachineCreateSharedFolderRequestBody = MachineCreateSharedFolderRequestBody.from_dict(connexion.request.get_json())  # noqa: E501

    return i_machine_createsharedfolder(vmid, oMachineCreateSharedFolderRequestBody)


def machine_removesharedfolder(vmid, name=None):  # noqa: E501
    """
    Call interface method IMachine::removeSharedFolder

    :param vmid: The Id of vm
    :type vmid: str
    :param name: 
    :type name: str

    :rtype: None
    """
    return i_machine_removesharedfolder(vmid, name)


def machine_savesettings(vmid):  # noqa: E501
    """
    Call interface method IMachine::saveSettings

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: None
    """
    return i_machine_savesettings(vmid)


def machine_savestate(vmid):  # noqa: E501
    """
    Call interface method IMachine::saveState

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: ProgressResponse
    """
    return i_machine_savestate(vmid)


def machine_setbootorder(vmid, oMachineSetBootOrderRequestBody):  # noqa: E501
    """
    Call interface method IMachine::setBootOrder

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineSetBootOrderRequestBody: 
    :type oMachineSetBootOrderRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oMachineSetBootOrderRequestBody = MachineSetBootOrderRequestBody.from_dict(connexion.request.get_json())  # noqa: E501

    return i_machine_setbootorder(vmid, oMachineSetBootOrderRequestBody)


def machine_setextradata(vmid, oMachineSetExtraDataRequestBody):  # noqa: E501
    """
    Call interface method IMachine::setExtraData

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineSetExtraDataRequestBody: 
    :type oMachineSetExtraDataRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oMachineSetExtraDataRequestBody = MachineSetExtraDataRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_setextradata(vmid, oMachineSetExtraDataRequestBody)


def machine_setguestproperty(vmid, oMachineSetGuestPropertyRequestBody):  # noqa: E501
    """
    Call interface method IMachine::setGuestProperty

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineSetGuestPropertyRequestBody: 
    :type oMachineSetGuestPropertyRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oMachineSetGuestPropertyRequestBody = MachineSetGuestPropertyRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_setguestproperty(vmid, oMachineSetGuestPropertyRequestBody)


def machine_unregister(vmid, cleanupMode=None):  # noqa: E501
    """
    Call interface method IMachine::unregister

    :param vmid: The Id of vm
    :type vmid: str
    :param cleanupMode: For the possible values of enumeration look into #/definitions/CleanupMode
    :type cleanupMode: str

    :rtype: MediumArrayResponse
    """
    return i_machine_unregister(vmid, cleanupMode)


def virtualbox_createmachine(oVirtualBoxCreateMachineRequestBody):  # noqa: E501
    """
    Call interface method IVirtualBox::createMachine

    :param oVirtualBoxCreateMachineRequestBody: 
    :type oVirtualBoxCreateMachineRequestBody: dict | bytes

    :rtype: MachineResponse
    """
    if connexion.request.is_json:
        oVirtualBoxCreateMachineRequestBody = VirtualBoxCreateMachineRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_virtualbox_createmachine(oVirtualBoxCreateMachineRequestBody)


def virtualbox_findmachine(vmid, select=None, nameOrId=None):  # noqa: E501
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
    return i_virtualbox_findmachine(vmid, select, nameOrId)


def virtualbox_registermachine(vmid, machine=None):  # noqa: E501
    """
    Call interface method IVirtualBox::registerMachine

    :param vmid: The Id of vm
    :type vmid: str
    :param machine: Put here an ID of requested IMachine VirtualBox object
    :type machine: str

    :rtype: None
    """
    return "Not implemented yet", HTTPStatus.NOT_IMPLEMENTED
