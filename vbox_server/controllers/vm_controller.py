import connexion

from vbox_server.models.machine_launch_vm_process_request_body import MachineLaunchVMProcessRequestBody  # noqa: E501
from vbox_server.models.machine_set_extra_data_request_body import MachineSetExtraDataRequestBody  # noqa: E501
from vbox_server.models.machine_set_guest_property_request_body import MachineSetGuestPropertyRequestBody  # noqa: E501
from vbox_server.models.virtual_box_create_machine_request_body import VirtualBoxCreateMachineRequestBody  # noqa: E501
from vbox_server.models.machine_set_boot_order_request_body import MachineSetBootOrderRequestBody  # noqa: E501
from vbox_server.models.machine_create_shared_folder_request_body import MachineCreateSharedFolderRequestBody  # noqa: E501
from vbox_server.models.machine_attach_device_request_body import MachineAttachDeviceRequestBody  # noqa: E501
from vbox_server.models.machine_detach_device_request_body import MachineDetachDeviceRequestBody  # noqa: E501
from vbox_server.models.machine_unmount_medium_request_body import MachineUnmountMediumRequestBody  # noqa: E501
from vbox_server.models.machine_mount_medium_request_body import MachineMountMediumRequestBody  # noqa: E501

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


def machine_attachdevice(vmid, oMachineAttachDeviceRequestBody):  # noqa: E501
    """Call interface method IMachine::attachDevice

     Attaches a device and optionally mounts a medium to the given storage controller (IStorageController, identified by @a name), at the indicated port and device. This method is intended for managing storage devices in general while a machine is powered off. It can be used to attach and detach fixed and removable media. The following kind of media can be attached to a machine: &lt;ul&gt; &lt;li&gt;For fixed and removable media, you can pass in a medium that was previously opened using IVirtualBox::openMedium. &lt;/li&gt; &lt;li&gt;Only for storage devices supporting removable media (such as DVDs and floppies), you can also specify a null pointer to indicate an empty drive or one of the medium objects listed in the IHost::DVDDrives and IHost::floppyDrives arrays to indicate a host drive. For removable devices, you can also use IMachine::mountMedium to change the media while the machine is running. &lt;/li&gt; &lt;/ul&gt;  In a VM&#39;s default configuration of virtual machines, the secondary master of the IDE controller is used for a CD/DVD drive. After calling this returns successfully, a new instance of IMediumAttachment will appear in the machine&#39;s list of medium attachments (see IMachine::mediumAttachments). See IMedium and IMediumAttachment for more information about attaching media. The specified device slot must not have a device attached to it, or this method will fail. Note! You cannot attach a device to a newly created machine until this machine&#39;s settings are saved to disk using #saveSettings. Note! If the medium is being attached indirectly, a new differencing medium will implicitly be created for it and attached instead. If the changes made to the machine settings (including this indirect attachment) are later cancelled using #discardSettings, this implicitly created differencing medium will implicitly be deleted. &lt;h3&gt;Possible results&lt;/h3&gt; E_INVALIDARG: SATA device, SATA port, IDE port or IDE slot out of range, or file or UUID not found. VBOX_E_INVALID_OBJECT_STATE: Machine must be registered before media can be attached. VBOX_E_INVALID_VM_STATE: Invalid machine state. VBOX_E_OBJECT_IN_USE: A medium is already attached to this or another virtual machine.  # noqa: E501

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineAttachDeviceRequestBody:
    :type oMachineAttachDeviceRequestBody: dict | bytes

    :rtype: Union[None, Tuple[None, int], Tuple[None, int, Dict[str, str]]
    """
    if connexion.request.is_json:
        oMachineAttachDeviceRequestBody = MachineAttachDeviceRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_attachdevice(vmid, oMachineAttachDeviceRequestBody)


def machine_detachdevice(vmid, oMachineDetachDeviceRequestBody):  # noqa: E501
    """Call interface method IMachine::detachDevice

     Detaches the device attached to a device slot of the specified bus. Detaching the device from the virtual machine is deferred. This means that the medium remains associated with the machine when this method returns and gets actually de-associated only after a successful #saveSettings call. See IMedium for more detailed information about attaching media. Note! You cannot detach a device from a running machine. Note! Detaching differencing media implicitly created by #attachDevice for the indirect attachment using this method will  &lt;b&gt;not &lt;/b&gt; implicitly delete them. The IMedium::deleteStorage operation should be explicitly performed by the caller after the medium is successfully detached and the settings are saved with #saveSettings, if it is the desired action. &lt;h3&gt;Possible results&lt;/h3&gt; VBOX_E_INVALID_VM_STATE: Attempt to detach medium from a running virtual machine. VBOX_E_OBJECT_NOT_FOUND: No medium attached to given slot/bus. VBOX_E_NOT_SUPPORTED: Medium format does not support storage deletion (only for implicitly created differencing media, should not happen).  # noqa: E501

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineDetachDeviceRequestBody:
    :type oMachineDetachDeviceRequestBody: dict | bytes

    :rtype: Union[None, Tuple[None, int], Tuple[None, int, Dict[str, str]]
    """
    if connexion.request.is_json:
        oMachineDetachDeviceRequestBody = MachineDetachDeviceRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_detachdevice(vmid, oMachineDetachDeviceRequestBody)
 

def machine_mountmedium(vmid, mediumid, oMachineMountMediumRequestBody):  # noqa: E501
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
    if connexion.request.is_json:
        oMachineMountMediumRequestBody = MachineMountMediumRequestBody.from_dict(connexion.request.get_json())  # noqa: E501

    return i_machine_mountmedium(vmid, mediumid, oMachineMountMediumRequestBody)


def machine_unmountmedium(vmid, mediumid, oMachineUnmountMediumRequestBody):  # noqa: E501
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
    if connexion.request.is_json:
        oMachineUnmountMediumRequestBody = MachineUnmountMediumRequestBody.from_dict(connexion.request.get_json())  # noqa: E501

    return i_machine_unmountmedium(vmid, mediumid, oMachineUnmountMediumRequestBody)