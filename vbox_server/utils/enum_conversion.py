"""VBox REST API

Copyright (c) 2025 Oracle and/or its affiliates.
Licensed under the Universal Permissive License v 1.0 as shown at https://oss.oracle.com/licenses/upl

SPDX-License-Identifier: UPL-1.0
"""

# pylint: disable=invalid-name
# pylint: disable=consider-using-f-string
# pylint: disable=line-too-long
# pylint: disable=undefined-variable

from vbox_server.global_settings import *

from vbox_server.models.machine_state import MachineState  # noqa: E501
from vbox_server.models.tracked_object_state import TrackedObjectState  # noqa: E501

########################### VirtualBox -> Swagger enumeration ###########################
def vbox_to_swagger_machine_state(machineState):
    swaggerMachineState = MachineState()
    swaggerMachineState = ctx[ 'global'].getEnumValueName('MachineState', machineState)
    return swaggerMachineState.upper()

def vbox_to_swagger_tracked_object_state(trackedObjectState):
    swaggerObjState = TrackedObjectState()
    swaggerObjState = ctx[ 'global'].getEnumValueName('TrackedObjectState', trackedObjectState)
    return swaggerObjState.upper()

###########################  Swagger -> VirtualBox enumeration ###########################
def swagger_to_vbox_firmware_type(firmwareType: str):
    if firmwareType == 'BIOS':
        vBoxfirmwareType = ctx['const'].FirmwareType_BIOS
    elif firmwareType == 'EFI':
        vBoxfirmwareType = ctx['const'].FirmwareType_EFI
    elif firmwareType == 'EFI32':
        vBoxfirmwareType = ctx['const'].FirmwareType_EFI32
    elif firmwareType == 'EFI64':
        vBoxfirmwareType = ctx['const'].FirmwareType_EFI64
    elif firmwareType == 'EFIDUAL':
        vBoxfirmwareType = ctx['const'].FirmwareType_EFIDUAL
    else:
        vBoxfirmwareType = None

    return vBoxfirmwareType


def swagger_to_vbox_platform_architecture(platformArchitecture: str):
    if platformArchitecture == 'X86':
        vBoxPlatformArchitecture = ctx['const'].PlatformArchitecture_x86
    elif platformArchitecture == 'ARM':
        vBoxPlatformArchitecture = ctx['const'].PlatformArchitecture_ARM
    else:
        vBoxPlatformArchitecture = ctx['const'].PlatformArchitecture_None

    return vBoxPlatformArchitecture


def swagger_to_vbox_access_mode(accessMode: str):
    if accessMode == 'READONLY':
        vBoxAccessMode = ctx['const'].AccessMode_ReadOnly
    elif accessMode == 'READWRITE':
        vBoxAccessMode = ctx['const'].AccessMode_ReadWrite
    else:
        vBoxAccessMode = None

    return vBoxAccessMode


def swagger_to_vbox_device_type(deviceType: str):
    if deviceType == "FLOPPY":
        vBoxDeviceType = ctx['const'].DeviceType_Floppy
    elif deviceType == "DVD":
        vBoxDeviceType = ctx['const'].DeviceType_DVD
    elif deviceType == "HARDDISK":
        vBoxDeviceType = ctx['const'].DeviceType_HardDisk
    elif deviceType == "NETWORK":
        vBoxDeviceType = ctx['const'].DeviceType_Network
    else:
        vBoxDeviceType = None

    return vBoxDeviceType


def swagger_to_vbox_cleanup_mode(cleanupMode: str):
    if cleanupMode == 'FULL':
        vBoxCleanupMode = ctx['const'].CleanupMode_Full
    elif cleanupMode == 'UNREGISTERONLY':
        vBoxCleanupMode = ctx['const'].CleanupMode_UnregisterOnly
    elif cleanupMode == 'DETACHALLRETURNNONE':
        vBoxCleanupMode = ctx['const'].CleanupMode_DetachAllReturnNone
    elif cleanupMode == 'DETACHALLRETURNHARDDISKSONLY':
        vBoxCleanupMode = ctx['const'].CleanupMode_DetachAllReturnHardDisksOnly
    elif cleanupMode == 'DETACHALLRETURNHARDDISKSANDVMREMOVABLE':
        vBoxCleanupMode = ctx['const'].CleanupMode_DetachAllReturnHardDisksAndVMRemovable
    else:
        vBoxCleanupMode = None

    return vBoxCleanupMode


def swagger_to_vbox_clone_mode(cloneMode: str):
    if cloneMode == "MACHINESTATE":
        vBoxCloneMode = ctx['const'].CloneMode_MachineState
    elif cloneMode == "MACHINEANDCHILDSTATES":
        vBoxCloneMode = ctx['const'].CloneMode_MachineAndChildStates
    elif cloneMode == "ALLSTATES":
        vBoxCloneMode = ctx['const'].CloneMode_AllStates
    else:
        vBoxCloneMode = None

    return vBoxCloneMode


def swagger_to_vbox_machine_state(machineState: str):
    if machineState == "POWEREDOFF":
        vBoxMachineState = ctx['const'].MachineState_PoweredOff
    elif machineState == "SAVED":
        vBoxMachineState = ctx['const'].MachineState_Saved
    elif machineState == "TELEPORTED":
        vBoxMachineState = ctx['const'].MachineState_Teleported
    elif machineState == "ABORTED":
        vBoxMachineState = ctx['const'].MachineState_Aborted
    elif machineState == "ABORTEDSAVED":
        vBoxMachineState = ctx['const'].MachineState_AbortedSaved
    elif machineState == "RUNNING":
        vBoxMachineState = ctx['const'].MachineState_Running
    elif machineState == "PAUSED":
        vBoxMachineState = ctx['const'].MachineState_Paused
    elif machineState == "STUCK":
        vBoxMachineState = ctx['const'].MachineState_Stuck
    elif machineState == "TELEPORTING":
        vBoxMachineState = ctx['const'].MachineState_Teleporting
    elif machineState == "LIVESNAPSHOTTING":
        vBoxMachineState = ctx['const'].MachineState_LiveSnapshotting
    elif machineState == "STARTING":
        vBoxMachineState = ctx['const'].MachineState_Starting
    elif machineState == "STOPPING":
        vBoxMachineState = ctx['const'].MachineState_Stopping
    elif machineState == "SAVING":
        vBoxMachineState = ctx['const'].MachineState_Saving
    elif machineState == "RESTORING":
        vBoxMachineState = ctx['const'].MachineState_Restoring
    elif machineState == "TELEPORTINGPAUSEDVM":
        vBoxMachineState = ctx['const'].MachineState_TeleportingPausedVM
    elif machineState == "TELEPORTINGIN":
        vBoxMachineState = ctx['const'].MachineState_TeleportingIn
    elif machineState == "DELETINGSNAPSHOTONLINE":
        vBoxMachineState = ctx['const'].MachineState_DeletingSnapshotOnline
    elif machineState == "DELETINGSNAPSHOTPAUSED":
        vBoxMachineState = ctx['const'].MachineState_DeletingSnapshotPaused
    elif machineState == "ONLINESNAPSHOTTING":
        vBoxMachineState = ctx['const'].MachineState_OnlineSnapshotting
    elif machineState == "RESTORINGSNAPSHOT":
        vBoxMachineState = ctx['const'].MachineState_RestoringSnapshot
    elif machineState == "DELETINGSNAPSHOT":
        vBoxMachineState = ctx['const'].MachineState_DeletingSnapshot
    elif machineState == "SETTINGUP":
        vBoxMachineState = ctx['const'].MachineState_SettingUp
    elif machineState == "SNAPSHOTTING":
        vBoxMachineState = ctx['const'].MachineState_Snapshotting
    else:
        vBoxMachineState = None

    return vBoxMachineState


def swagger_to_vbox_hw_virt_ex_property(property: str):
    if property == "ENABLED":
        vBoxHWVirtExProperty = ctx['const'].HWVirtExPropertyType_Enabled
    elif property == "VPID":
        vBoxHWVirtExProperty = ctx['const'].HWVirtExPropertyType_VPID
    elif property == "NESTEDPAGING":
        vBoxHWVirtExProperty = ctx['const'].HWVirtExPropertyType_NestedPaging
    elif property == "LARGEPAGES":
        vBoxHWVirtExProperty = ctx['const'].HWVirtExPropertyType_LargePages
    elif property == "FORCE":
        vBoxHWVirtExProperty = ctx['const'].HWVirtExPropertyType_Force
    elif property == "USENATIVEAPI":
        vBoxHWVirtExProperty = ctx['const'].HWVirtExPropertyType_UseNativeApi
    elif property == "VIRTVMSAVEVMLOAD":
        vBoxHWVirtExProperty = ctx['const'].HWVirtExPropertyType_VirtVmsaveVmload
    elif property == "UNRESTRICTEDEXECUTION":
        vBoxHWVirtExProperty = ctx['const'].HWVirtExPropertyType_UnrestrictedExecution
    else:
        vBoxHWVirtExProperty = None # ctx['const'].HWVirtExPropertyType_Null
    
    return vBoxHWVirtExProperty


def swagger_to_vbox_cpu_x86_property(property: str):
    if property == "PAE":
        vBoxCPUProperty = ctx['const'].CPUPropertyTypeX86_PAE
    elif property == "LONGMODE":
        vBoxCPUProperty = ctx['const'].CPUPropertyTypeX86_LongMode
    elif property == "TRIPLEFAULTRESET":
        vBoxCPUProperty = ctx['const'].CPUPropertyTypeX86_TripleFaultReset
    elif property == "APIC":
        vBoxCPUProperty = ctx['const'].CPUPropertyTypeX86_APIC
    elif property == "X2APIC":
        vBoxCPUProperty = ctx['const'].CPUPropertyTypeX86_X2APIC
    elif property == "IBPBONVMEXIT":
        vBoxCPUProperty = ctx['const'].CPUPropertyTypeX86_IBPBOnVMExit
    elif property == "IBPBONVMENTRY":
        vBoxCPUProperty = ctx['const'].CPUPropertyTypeX86_IBPBOnVMEntry
    elif property == "HWVIRT":
        vBoxCPUProperty = ctx['const'].CPUPropertyTypeX86_HWVirt
    elif property == "SPECCTRL":
        vBoxCPUProperty = ctx['const'].CPUPropertyTypeX86_SpecCtrl
    elif property == "SPECCTRLBYHOST":
        vBoxCPUProperty = ctx['const'].CPUPropertyTypeX86_SpecCtrlByHost
    elif property == "L1DFLUSHONEMTSCHEDULING":
        vBoxCPUProperty = ctx['const'].CPUPropertyTypeX86_L1DFlushOnEMTScheduling
    elif property == "L1DFLUSHONVMENTRY":
        vBoxCPUProperty = ctx['const'].CPUPropertyTypeX86_L1DFlushOnVMEntry
    elif property == "MDSCLEARONEMTSCHEDULING":
        vBoxCPUProperty = ctx['const'].CPUPropertyTypeX86_MDSClearOnEMTScheduling
    elif property == "MDSCLEARONVMENTRY":
        vBoxCPUProperty = ctx['const'].CPUPropertyTypeX86_MDSClearOnVMEntry
    else:
        vBoxCPUProperty = None # ctx['const'].CPUPropertyTypeX86_Null

    return vBoxCPUProperty


def swagger_to_vbox_cpu_arm_property(property: str):
    if property == "HWVIRT":
        vBoxCPUProperty = ctx['const'].CPUPropertyTypeARM_HWVirt
    else:
        vBoxCPUProperty = None # ctx['const'].CPUPropertyTypeARM_Null

    return vBoxCPUProperty


def swagger_to_vbox_storage_bus(storageBus: str):
    if storageBus == "IDE":
        vBoxStorageBus = ctx['const'].StorageBus_IDE
    elif storageBus == "SATA":
        vBoxStorageBus = ctx['const'].StorageBus_SATA
    elif storageBus == "SCSI":
        vBoxStorageBus = ctx['const'].StorageBus_SCSI
    elif storageBus == "FLOPPY":
        vBoxStorageBus = ctx['const'].StorageBus_Floppy
    elif storageBus == "SAS":
        vBoxStorageBus = ctx['const'].StorageBus_SAS
    elif storageBus == "USB":
        vBoxStorageBus = ctx['const'].StorageBus_USB
    elif storageBus == "PCIE":
        vBoxStorageBus = ctx['const'].StorageBus_PCIe
    elif storageBus == "VIRTIOSCSI":
        vBoxStorageBus = ctx['const'].StorageBus_VirtioSCSI
    else:
        vBoxStorageBus = None

    return vBoxStorageBus