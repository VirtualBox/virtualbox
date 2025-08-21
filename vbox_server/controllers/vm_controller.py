"""VBox REST API

Copyright (c) 2025 Oracle and/or its affiliates.
Licensed under the Universal Permissive License v 1.0 as shown at https://oss.oracle.com/licenses/upl

SPDX-License-Identifier: UPL-1.0
"""

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

############################# Not implemented yet #############################
from vbox_server.models.console_add_encryption_password_request_body import ConsoleAddEncryptionPasswordRequestBody  # noqa: E501
from vbox_server.models.console_add_encryption_passwords_request_body import ConsoleAddEncryptionPasswordsRequestBody  # noqa: E501
from vbox_server.models.console_attach_usb_device_request_body import ConsoleAttachUSBDeviceRequestBody  # noqa: E501
from vbox_server.models.console_create_shared_folder_request_body import ConsoleCreateSharedFolderRequestBody  # noqa: E501
from vbox_server.models.machine_add_storage_controller_request_body import MachineAddStorageControllerRequestBody  # noqa: E501
from vbox_server.models.machine_add_usb_controller_request_body import MachineAddUSBControllerRequestBody  # noqa: E501
from vbox_server.models.machine_attach_device_without_medium_request_body import MachineAttachDeviceWithoutMediumRequestBody  # noqa: E501
from vbox_server.models.machine_attach_host_pci_device_request_body import MachineAttachHostPCIDeviceRequestBody  # noqa: E501
from vbox_server.models.machine_clone_to_request_body import MachineCloneToRequestBody  # noqa: E501
from vbox_server.models.machine_delete_snapshot_range_request_body import MachineDeleteSnapshotRangeRequestBody  # noqa: E501
from vbox_server.models.machine_export_to_request_body import MachineExportToRequestBody  # noqa: E501
from vbox_server.models.machine_lock_machine_request_body import MachineLockMachineRequestBody  # noqa: E501
from vbox_server.models.machine_move_to_request_body import MachineMoveToRequestBody  # noqa: E501
from vbox_server.models.machine_non_rotational_device_request_body import MachineNonRotationalDeviceRequestBody  # noqa: E501
from vbox_server.models.machine_passthrough_device_request_body import MachinePassthroughDeviceRequestBody  # noqa: E501
from vbox_server.models.machine_set_auto_discard_for_device_request_body import MachineSetAutoDiscardForDeviceRequestBody  # noqa: E501
from vbox_server.models.machine_set_bandwidth_group_for_device_request_body import MachineSetBandwidthGroupForDeviceRequestBody  # noqa: E501
from vbox_server.models.machine_set_hot_pluggable_for_device_request_body import MachineSetHotPluggableForDeviceRequestBody  # noqa: E501
from vbox_server.models.machine_set_no_bandwidth_group_for_device_request_body import MachineSetNoBandwidthGroupForDeviceRequestBody  # noqa: E501
from vbox_server.models.machine_set_storage_controller_bootable_request_body import MachineSetStorageControllerBootableRequestBody  # noqa: E501
from vbox_server.models.machine_take_snapshot_request_body import MachineTakeSnapshotRequestBody  # noqa: E501
from vbox_server.models.machine_temporary_eject_device_request_body import MachineTemporaryEjectDeviceRequestBody  # noqa: E501
from vbox_server.models.platform_x86_set_cpu_property_request_body import PlatformX86SetCPUPropertyRequestBody  # noqa: E501
from vbox_server.models.platform_x86_set_hw_virt_ex_property_request_body import PlatformX86SetHWVirtExPropertyRequestBody  # noqa: E501
from vbox_server.models.virtual_box_open_machine_request_body import VirtualBoxOpenMachineRequestBody  
############################# Not implemented yet #############################

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


############################# Not implemented yet #############################
def console_addencryptionpassword(vmid, oConsoleAddEncryptionPasswordRequestBody):  # noqa: E501
    """
    Call interface method IConsole::addEncryptionPassword

    :param vmid: The Id of vm
    :type vmid: str
    :param oConsoleAddEncryptionPasswordRequestBody: 
    :type oConsoleAddEncryptionPasswordRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oConsoleAddEncryptionPasswordRequestBody = ConsoleAddEncryptionPasswordRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_console_addencryptionpassword(vmid, oConsoleAddEncryptionPasswordRequestBody)


def console_addencryptionpasswords(vmid, oConsoleAddEncryptionPasswordsRequestBody):  # noqa: E501
    """
    Call interface method IConsole::addEncryptionPasswords

    :param vmid: The Id of vm
    :type vmid: str
    :param oConsoleAddEncryptionPasswordsRequestBody: 
    :type oConsoleAddEncryptionPasswordsRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oConsoleAddEncryptionPasswordsRequestBody = ConsoleAddEncryptionPasswordsRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_console_addencryptionpasswords(vmid, oConsoleAddEncryptionPasswordsRequestBody)


def console_attachusbdevice(vmid, oConsoleAttachUSBDeviceRequestBody):  # noqa: E501
    """
    Call interface method IConsole::attachUSBDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oConsoleAttachUSBDeviceRequestBody: 
    :type oConsoleAttachUSBDeviceRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oConsoleAttachUSBDeviceRequestBody = ConsoleAttachUSBDeviceRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_console_attachusbdevice(vmid, oConsoleAttachUSBDeviceRequestBody)


def console_clearallencryptionpasswords(vmid):  # noqa: E501
    """
    Call interface method IConsole::clearAllEncryptionPasswords

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: None
    """
    return i_console_clearallencryptionpasswords(vmid)


def console_createsharedfolder(vmid, oConsoleCreateSharedFolderRequestBody):  # noqa: E501
    """
    Call interface method IConsole::createSharedFolder

    :param vmid: The Id of vm
    :type vmid: str
    :param oConsoleCreateSharedFolderRequestBody: 
    :type oConsoleCreateSharedFolderRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oConsoleCreateSharedFolderRequestBody = ConsoleCreateSharedFolderRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_console_createsharedfolder(vmid, oConsoleCreateSharedFolderRequestBody)


def console_detachusbdevice(vmid, id=None):  # noqa: E501
    """
    Call interface method IConsole::detachUSBDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param id: 
    :type id: str

    :rtype: USBDeviceResponse
    """
    return i_console_detachusbdevice(vmid, id)


def console_findusbdevicebyaddress(vmid, select=None, name=None):  # noqa: E501
    """
    Call interface method IConsole::findUSBDeviceByAddress

    :param vmid: The Id of vm
    :type vmid: str
    :param select: The object attributes separated by comma
    :type select: str
    :param name: 
    :type name: str

    :rtype: USBDeviceResponse
    """
    return i_console_findusbdevicebyaddress(vmid, select, name)


def console_findusbdevicebyid(vmid, select=None, id=None):  # noqa: E501
    """
    Call interface method IConsole::findUSBDeviceById

    :param vmid: The Id of vm
    :type vmid: str
    :param select: The object attributes separated by comma
    :type select: str
    :param id: 
    :type id: str

    :rtype: USBDeviceResponse
    """
    return i_console_findusbdevicebyid(vmid, select, id)


def console_removeencryptionpassword(vmid, id=None):  # noqa: E501
    """
    Call interface method IConsole::removeEncryptionPassword

    :param vmid: The Id of vm
    :type vmid: str
    :param id: 
    :type id: str

    :rtype: None
    """
    return i_console_removeencryptionpassword(vmid, id)


def console_removesharedfolder(vmid, name=None):  # noqa: E501
    """
    Call interface method IConsole::removeSharedFolder

    :param vmid: The Id of vm
    :type vmid: str
    :param name: 
    :type name: str

    :rtype: None
    """
    return i_console_removesharedfolder(vmid, name)


def machine_addstoragecontroller(vmid, oMachineAddStorageControllerRequestBody):  # noqa: E501
    """
    Call interface method IMachine::addStorageController

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineAddStorageControllerRequestBody: 
    :type oMachineAddStorageControllerRequestBody: dict | bytes

    :rtype: StorageControllerResponse
    """
    if connexion.request.is_json:
        oMachineAddStorageControllerRequestBody = MachineAddStorageControllerRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_addstoragecontroller(vmid, oMachineAddStorageControllerRequestBody)


def machine_addusbcontroller(vmid, oMachineAddUSBControllerRequestBody):  # noqa: E501
    """
    Call interface method IMachine::addUSBController

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineAddUSBControllerRequestBody: 
    :type oMachineAddUSBControllerRequestBody: dict | bytes

    :rtype: USBControllerResponse
    """
    if connexion.request.is_json:
        oMachineAddUSBControllerRequestBody = MachineAddUSBControllerRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_addusbcontroller(vmid, oMachineAddUSBControllerRequestBody)


def machine_applydefaults(vmid, flags=None):  # noqa: E501
    """
    Call interface method IMachine::applyDefaults

    :param vmid: The Id of vm
    :type vmid: str
    :param flags: 
    :type flags: str

    :rtype: None
    """
    return i_machine_applydefaults(vmid, flags)


def machine_attachdevicewithoutmedium(vmid, oMachineAttachDeviceWithoutMediumRequestBody):  # noqa: E501
    """
    Call interface method IMachine::attachDeviceWithoutMedium

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineAttachDeviceWithoutMediumRequestBody: 
    :type oMachineAttachDeviceWithoutMediumRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oMachineAttachDeviceWithoutMediumRequestBody = MachineAttachDeviceWithoutMediumRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_attachdevicewithoutmedium(vmid, oMachineAttachDeviceWithoutMediumRequestBody)


def machine_attachhostpcidevice(vmid, oMachineAttachHostPCIDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::attachHostPCIDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineAttachHostPCIDeviceRequestBody: 
    :type oMachineAttachHostPCIDeviceRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oMachineAttachHostPCIDeviceRequestBody = MachineAttachHostPCIDeviceRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_attachhostpcidevice(vmid, oMachineAttachHostPCIDeviceRequestBody)


def machine_cloneto(vmid, oMachineCloneToRequestBody):  # noqa: E501
    """
    Call interface method IMachine::cloneTo

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineCloneToRequestBody: 
    :type oMachineCloneToRequestBody: dict | bytes

    :rtype: ProgressResponse
    """
    if connexion.request.is_json:
        oMachineCloneToRequestBody = MachineCloneToRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_cloneto(vmid, oMachineCloneToRequestBody)


def machine_deleteguestproperty(vmid, name=None):  # noqa: E501
    """
    Call interface method IMachine::deleteGuestProperty

    :param vmid: The Id of vm
    :type vmid: str
    :param name: 
    :type name: str

    :rtype: None
    """
    return i_machine_deleteguestproperty(vmid, name)


def machine_deletesnapshot(vmid, id=None):  # noqa: E501
    """
    Call interface method IMachine::deleteSnapshot

    :param vmid: The Id of vm
    :type vmid: str
    :param id: 
    :type id: str

    :rtype: ProgressResponse
    """
    return i_machine_deletesnapshot(vmid, id)


def machine_deletesnapshotandallchildren(vmid, id=None):  # noqa: E501
    """
    Call interface method IMachine::deleteSnapshotAndAllChildren

    :param vmid: The Id of vm
    :type vmid: str
    :param id: 
    :type id: str

    :rtype: ProgressResponse
    """
    return i_machine_deletesnapshotandallchildren(vmid, id)


def machine_deletesnapshotrange(vmid, oMachineDeleteSnapshotRangeRequestBody):  # noqa: E501
    """
    Call interface method IMachine::deleteSnapshotRange

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineDeleteSnapshotRangeRequestBody: 
    :type oMachineDeleteSnapshotRangeRequestBody: dict | bytes

    :rtype: ProgressResponse
    """
    if connexion.request.is_json:
        oMachineDeleteSnapshotRangeRequestBody = MachineDeleteSnapshotRangeRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_deletesnapshotrange(vmid, oMachineDeleteSnapshotRangeRequestBody)


def machine_detachhostpcidevice(vmid, hostAddress=None):  # noqa: E501
    """
    Call interface method IMachine::detachHostPCIDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param hostAddress: 
    :type hostAddress: int

    :rtype: None
    """
    return i_machine_detachhostpcidevice(vmid, hostAddress)


def machine_discardsavedstate(vmid, fRemoveFile=None):  # noqa: E501
    """
    Call interface method IMachine::discardSavedState

    :param vmid: The Id of vm
    :type vmid: str
    :param fRemoveFile: 
    :type fRemoveFile: bool

    :rtype: None
    """
    return i_machine_discardsavedstate(vmid, fRemoveFile)


def machine_discardsettings(vmid):  # noqa: E501
    """
    Call interface method IMachine::discardSettings

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: None
    """
    return i_machine_discardsettings(vmid)


def machine_exportto(vmid, oMachineExportToRequestBody):  # noqa: E501
    """
    Call interface method IMachine::exportTo

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineExportToRequestBody: 
    :type oMachineExportToRequestBody: dict | bytes

    :rtype: VirtualSystemDescriptionResponse
    """
    if connexion.request.is_json:
        oMachineExportToRequestBody = MachineExportToRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_exportto(vmid, oMachineExportToRequestBody)


def machine_findsnapshot(vmid, select=None, nameOrId=None):  # noqa: E501
    """
    Call interface method IMachine::findSnapshot

    :param vmid: The Id of vm
    :type vmid: str
    :param select: The object attributes separated by comma
    :type select: str
    :param nameOrId: 
    :type nameOrId: str

    :rtype: SnapshotResponse
    """
    return i_machine_findsnapshot(vmid, select, nameOrId)


def machine_getcpustatus(vmid, cpu=None):  # noqa: E501
    """
    Call interface method IMachine::getCPUStatus

    :param vmid: The Id of vm
    :type vmid: str
    :param cpu: 
    :type cpu: int

    :rtype: MachineGetcpustatusResponse
    """
    return i_machine_getcpustatus(vmid, cpu)


def machine_geteffectiveparavirtprovider(vmid):  # noqa: E501
    """
    Call interface method IMachine::getEffectiveParavirtProvider

    :param vmid: The Id of vm
    :type vmid: str

    :rtype: ParavirtProviderResponse
    """
    return i_machine_geteffectiveparavirtprovider(vmid)


def machine_getmedium(vmid, mediumid, select=None, name=None, controllerPort=None, device=None):  # noqa: E501
    """
    Call interface method IMachine::getMedium

    :param vmid: The Id of vm
    :type vmid: str
    :param mediumid: The Id of medium
    :type mediumid: str
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
    return i_machine_getmedium(vmid, mediumid, select, name, controllerPort, device)


def machine_getmediumattachmentsofcontroller(vmid, select=None, name=None):  # noqa: E501
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
    return i_machine_getmediumattachmentsofcontroller(vmid, select, name)


def machine_getnetworkadapter(vmid, select=None, slot=None):  # noqa: E501
    """
    Call interface method IMachine::getNetworkAdapter

    :param vmid: The Id of vm
    :type vmid: str
    :param select: The object attributes separated by comma
    :type select: str
    :param slot: 
    :type slot: int

    :rtype: NetworkAdapterResponse
    """
    return i_machine_getnetworkadapter(vmid, select, slot)


def machine_getparallelport(vmid, select=None, slot=None):  # noqa: E501
    """
    Call interface method IMachine::getParallelPort

    :param vmid: The Id of vm
    :type vmid: str
    :param select: The object attributes separated by comma
    :type select: str
    :param slot: 
    :type slot: int

    :rtype: ParallelPortResponse
    """
    return i_machine_getparallelport(vmid, select, slot)


def machine_getserialport(vmid, select=None, slot=None):  # noqa: E501
    """
    Call interface method IMachine::getSerialPort

    :param vmid: The Id of vm
    :type vmid: str
    :param select: The object attributes separated by comma
    :type select: str
    :param slot: 
    :type slot: int

    :rtype: SerialPortResponse
    """
    return i_machine_getserialport(vmid, select, slot)


def machine_getstoragecontrollerbyinstance(vmid, select=None, connectionType=None, instance=None):  # noqa: E501
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

    :rtype: StorageControllerResponse1
    """
    return i_machine_getstoragecontrollerbyinstance(vmid, select, connectionType, instance)


def machine_getstoragecontrollerbyname(vmid, select=None, name=None):  # noqa: E501
    """
    Call interface method IMachine::getStorageControllerByName

    :param vmid: The Id of vm
    :type vmid: str
    :param select: The object attributes separated by comma
    :type select: str
    :param name: 
    :type name: str

    :rtype: StorageControllerResponse1
    """
    return i_machine_getstoragecontrollerbyname(vmid, select, name)


def machine_getusbcontrollerbyname(vmid, select=None, name=None):  # noqa: E501
    """
    Call interface method IMachine::getUSBControllerByName

    :param vmid: The Id of vm
    :type vmid: str
    :param select: The object attributes separated by comma
    :type select: str
    :param name: 
    :type name: str

    :rtype: USBControllerResponse
    """
    return i_machine_getusbcontrollerbyname(vmid, select, name)


def machine_getusbcontrollercountbytype(vmid, type=None):  # noqa: E501
    """
    Call interface method IMachine::getUSBControllerCountByType

    :param vmid: The Id of vm
    :type vmid: str
    :param type: For the possible values of enumeration look into #/definitions/USBControllerType
    :type type: str

    :rtype: MachineGetusbcontrollercountbytypeResponse
    """
    return i_machine_getusbcontrollercountbytype(vmid, type)


def machine_hotplugcpu(vmid, cpu=None):  # noqa: E501
    """
    Call interface method IMachine::hotPlugCPU

    :param vmid: The Id of vm
    :type vmid: str
    :param cpu: 
    :type cpu: int

    :rtype: None
    """
    return i_machine_hotplugcpu(vmid, cpu)


def machine_hotunplugcpu(vmid, cpu=None):  # noqa: E501
    """
    Call interface method IMachine::hotUnplugCPU

    :param vmid: The Id of vm
    :type vmid: str
    :param cpu: 
    :type cpu: int

    :rtype: None
    """
    return i_machine_hotunplugcpu(vmid, cpu)


def machine_lockmachine(vmid, oMachineLockMachineRequestBody):  # noqa: E501
    """
    Call interface method IMachine::lockMachine

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineLockMachineRequestBody: 
    :type oMachineLockMachineRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oMachineLockMachineRequestBody = MachineLockMachineRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_lockmachine(vmid, oMachineLockMachineRequestBody)


def machine_moveto(vmid, oMachineMoveToRequestBody):  # noqa: E501
    """
    Call interface method IMachine::moveTo

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineMoveToRequestBody: 
    :type oMachineMoveToRequestBody: dict | bytes

    :rtype: ProgressResponse
    """
    if connexion.request.is_json:
        oMachineMoveToRequestBody = MachineMoveToRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_moveto(vmid, oMachineMoveToRequestBody)


def machine_nonrotationaldevice(vmid, oMachineNonRotationalDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::nonRotationalDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineNonRotationalDeviceRequestBody: 
    :type oMachineNonRotationalDeviceRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oMachineNonRotationalDeviceRequestBody = MachineNonRotationalDeviceRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_nonrotationaldevice(vmid, oMachineNonRotationalDeviceRequestBody)


def machine_passthroughdevice(vmid, oMachinePassthroughDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::passthroughDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachinePassthroughDeviceRequestBody: 
    :type oMachinePassthroughDeviceRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oMachinePassthroughDeviceRequestBody = MachinePassthroughDeviceRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_passthroughdevice(vmid, oMachinePassthroughDeviceRequestBody)


def machine_nonrotationaldevice(vmid, oMachineNonRotationalDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::nonRotationalDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineNonRotationalDeviceRequestBody: 
    :type oMachineNonRotationalDeviceRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oMachineNonRotationalDeviceRequestBody = MachineNonRotationalDeviceRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_nonrotationaldevice(vmid, oMachineNonRotationalDeviceRequestBody)


def machine_passthroughdevice(vmid, oMachinePassthroughDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::passthroughDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachinePassthroughDeviceRequestBody: 
    :type oMachinePassthroughDeviceRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oMachinePassthroughDeviceRequestBody = MachinePassthroughDeviceRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_passthroughdevice(vmid, oMachinePassthroughDeviceRequestBody)


def machine_removestoragecontroller(vmid, name=None):  # noqa: E501
    """
    Call interface method IMachine::removeStorageController

    :param vmid: The Id of vm
    :type vmid: str
    :param name: 
    :type name: str

    :rtype: None
    """
    return i_machine_removestoragecontroller(vmid, name)


def machine_removeusbcontroller(vmid, name=None):  # noqa: E501
    """
    Call interface method IMachine::removeUSBController

    :param vmid: The Id of vm
    :type vmid: str
    :param name: 
    :type name: str

    :rtype: None
    """
    return i_machine_removeusbcontroller(vmid, name)


def machine_restoresnapshot(vmid, snapshot=None):  # noqa: E501
    """
    Call interface method IMachine::restoreSnapshot

    :param vmid: The Id of vm
    :type vmid: str
    :param snapshot: Put here an ID of requested ISnapshot VirtualBox object
    :type snapshot: str

    :rtype: ProgressResponse
    """
    return i_machine_restoresnapshot(vmid, snapshot)


def machine_setautodiscardfordevice(vmid, oMachineSetAutoDiscardForDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::setAutoDiscardForDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineSetAutoDiscardForDeviceRequestBody: 
    :type oMachineSetAutoDiscardForDeviceRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oMachineSetAutoDiscardForDeviceRequestBody = MachineSetAutoDiscardForDeviceRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_setautodiscardfordevice(vmid, oMachineSetAutoDiscardForDeviceRequestBody)


def machine_setbandwidthgroupfordevice(vmid, oMachineSetBandwidthGroupForDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::setBandwidthGroupForDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineSetBandwidthGroupForDeviceRequestBody: 
    :type oMachineSetBandwidthGroupForDeviceRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oMachineSetBandwidthGroupForDeviceRequestBody = MachineSetBandwidthGroupForDeviceRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_setbandwidthgroupfordevice(vmid, oMachineSetBandwidthGroupForDeviceRequestBody)


def machine_sethotpluggablefordevice(vmid, oMachineSetHotPluggableForDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::setHotPluggableForDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineSetHotPluggableForDeviceRequestBody: 
    :type oMachineSetHotPluggableForDeviceRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oMachineSetHotPluggableForDeviceRequestBody = MachineSetHotPluggableForDeviceRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_sethotpluggablefordevice(vmid, oMachineSetHotPluggableForDeviceRequestBody)


def machine_setnobandwidthgroupfordevice(vmid, oMachineSetNoBandwidthGroupForDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::setNoBandwidthGroupForDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineSetNoBandwidthGroupForDeviceRequestBody: 
    :type oMachineSetNoBandwidthGroupForDeviceRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oMachineSetNoBandwidthGroupForDeviceRequestBody = MachineSetNoBandwidthGroupForDeviceRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_setnobandwidthgroupfordevice(vmid, oMachineSetNoBandwidthGroupForDeviceRequestBody)


def machine_setsettingsfilepath(vmid, settingsFilePath=None):  # noqa: E501
    """
    Call interface method IMachine::setSettingsFilePath

    :param vmid: The Id of vm
    :type vmid: str
    :param settingsFilePath: 
    :type settingsFilePath: str

    :rtype: ProgressResponse
    """
    return i_machine_setsettingsfilepath(vmid, settingsFilePath)


def machine_setstoragecontrollerbootable(vmid, oMachineSetStorageControllerBootableRequestBody):  # noqa: E501
    """
    Call interface method IMachine::setStorageControllerBootable

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineSetStorageControllerBootableRequestBody: 
    :type oMachineSetStorageControllerBootableRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oMachineSetStorageControllerBootableRequestBody = MachineSetStorageControllerBootableRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_setstoragecontrollerbootable(vmid, oMachineSetStorageControllerBootableRequestBody)


def machine_takesnapshot(vmid, oMachineTakeSnapshotRequestBody):  # noqa: E501
    """
    Call interface method IMachine::takeSnapshot

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineTakeSnapshotRequestBody: 
    :type oMachineTakeSnapshotRequestBody: dict | bytes

    :rtype: MachineTakesnapshotResponse
    """
    if connexion.request.is_json:
        oMachineTakeSnapshotRequestBody = MachineTakeSnapshotRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_takesnapshot(vmid, oMachineTakeSnapshotRequestBody)


def machine_temporaryejectdevice(vmid, oMachineTemporaryEjectDeviceRequestBody):  # noqa: E501
    """
    Call interface method IMachine::temporaryEjectDevice

    :param vmid: The Id of vm
    :type vmid: str
    :param oMachineTemporaryEjectDeviceRequestBody: 
    :type oMachineTemporaryEjectDeviceRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oMachineTemporaryEjectDeviceRequestBody = MachineTemporaryEjectDeviceRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_machine_temporaryejectdevice(vmid, oMachineTemporaryEjectDeviceRequestBody)


def platformx86_getcpuproperty(vmid, _property=None):  # noqa: E501
    """
    Call interface method IPlatformX86::getCPUProperty

    :param vmid: The Id of vm
    :type vmid: str
    :param _property: For the possible values of enumeration look into #/definitions/CPUPropertyTypeX86
    :type _property: str

    :rtype: Platformx86GetcpupropertyResponse
    """
    return i_platformx86_getcpuproperty(vmid, _property)


def platformx86_gethwvirtexproperty(vmid, _property=None):  # noqa: E501
    """
    Call interface method IPlatformX86::getHWVirtExProperty

    :param vmid: The Id of vm
    :type vmid: str
    :param _property: For the possible values of enumeration look into #/definitions/HWVirtExPropertyType
    :type _property: str

    :rtype: Platformx86GetcpupropertyResponse
    """
    return i_platformx86_gethwvirtexproperty(vmid, _property)


def platformx86_setcpuproperty(vmid, oPlatformX86SetCPUPropertyRequestBody):  # noqa: E501
    """
    Call interface method IPlatformX86::setCPUProperty

    :param vmid: The Id of vm
    :type vmid: str
    :param oPlatformX86SetCPUPropertyRequestBody: 
    :type oPlatformX86SetCPUPropertyRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oPlatformX86SetCPUPropertyRequestBody = PlatformX86SetCPUPropertyRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_platformx86_setcpuproperty(vmid, oPlatformX86SetCPUPropertyRequestBody)


def platformx86_sethwvirtexproperty(vmid, oPlatformX86SetHWVirtExPropertyRequestBody):  # noqa: E501
    """
    Call interface method IPlatformX86::setHWVirtExProperty

    :param vmid: The Id of vm
    :type vmid: str
    :param oPlatformX86SetHWVirtExPropertyRequestBody: 
    :type oPlatformX86SetHWVirtExPropertyRequestBody: dict | bytes

    :rtype: None
    """
    if connexion.request.is_json:
        oPlatformX86SetHWVirtExPropertyRequestBody = PlatformX86SetHWVirtExPropertyRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_platformx86_sethwvirtexproperty(vmid, oPlatformX86SetHWVirtExPropertyRequestBody)


def virtualbox_openmachine(vmid, oVirtualBoxOpenMachineRequestBody):  # noqa: E501
    """
    Call interface method IVirtualBox::openMachine

    :param vmid: The Id of vm
    :type vmid: str
    :param oVirtualBoxOpenMachineRequestBody: 
    :type oVirtualBoxOpenMachineRequestBody: dict | bytes

    :rtype: MachineResponse
    """
    if connexion.request.is_json:
        oVirtualBoxOpenMachineRequestBody = VirtualBoxOpenMachineRequestBody.from_dict(connexion.request.get_json())  # noqa: E501
    return i_virtualbox_openmachine(vmid, oVirtualBoxOpenMachineRequestBody)


def virtualbox_registermachine(vmid, machine=None):  # noqa: E501
    """
    Call interface method IVirtualBox::registerMachine

    :param vmid: The Id of vm
    :type vmid: str
    :param machine: Put here an ID of requested IMachine VirtualBox object
    :type machine: str

    :rtype: None
    """
    return i_virtualbox_registermachine(vmid, machine)
