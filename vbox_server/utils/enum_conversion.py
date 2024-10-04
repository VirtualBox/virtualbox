# pylint: disable=invalid-name
# pylint: disable=consider-using-f-string
# pylint: disable=line-too-long
# pylint: disable=undefined-variable

from vbox_server.global_settings import *

from vbox_server.models.machine_state import MachineState  # noqa: E501

########################### VirtualBox -> Swagger enumeration ###########################
def vbox_to_swagger_machine_state(machineState):
    swaggerMachineState = MachineState()

    if machineState == ctx['const'].MachineState_PoweredOff:
        swaggerMachineState = MachineState.POWEREDOFF
    elif machineState == ctx['const'].MachineState_Saved:
        swaggerMachineState = MachineState.SAVED
    elif machineState == ctx['const'].MachineState_Teleported:
        swaggerMachineState = MachineState.TELEPORTED
    elif machineState == ctx['const'].MachineState_Aborted:
        swaggerMachineState = MachineState.ABORTED
    elif machineState == ctx['const'].MachineState_AbortedSaved:
        swaggerMachineState = MachineState.ABORTEDSAVED
    elif machineState == ctx['const'].MachineState_Running:
        swaggerMachineState = MachineState.RUNNING
    elif machineState == ctx['const'].MachineState_Paused:
        swaggerMachineState = MachineState.PAUSED
    elif machineState == ctx['const'].MachineState_Stuck:
        swaggerMachineState = MachineState.STUCK
    elif machineState == ctx['const'].MachineState_Teleporting:
        swaggerMachineState = MachineState.TELEPORTING
    elif machineState == ctx['const'].MachineState_LiveSnapshotting:
        swaggerMachineState = MachineState.LIVESNAPSHOTTING
    elif machineState == ctx['const'].MachineState_Starting:
        swaggerMachineState = MachineState.STARTING
    elif machineState == ctx['const'].MachineState_Stopping:
        swaggerMachineState = MachineState.STOPPING
    elif machineState == ctx['const'].MachineState_Saving:
        swaggerMachineState = MachineState.SAVING
    elif machineState == ctx['const'].MachineState_Restoring:
        swaggerMachineState = MachineState.RESTORING
    elif machineState == ctx['const'].MachineState_TeleportingPausedVM:
        swaggerMachineState = MachineState.TELEPORTINGPAUSEDVM
    elif machineState == ctx['const'].MachineState_TeleportingIn:
        swaggerMachineState = MachineState.TELEPORTINGIN
    elif machineState == ctx['const'].MachineState_DeletingSnapshotOnline:
        swaggerMachineState = MachineState.DELETINGSNAPSHOTONLINE
    elif machineState == ctx['const'].MachineState_DeletingSnapshotPaused:
        swaggerMachineState = MachineState.DELETINGSNAPSHOTPAUSED
    elif machineState == ctx['const'].MachineState_OnlineSnapshotting:
        swaggerMachineState = MachineState.ONLINESNAPSHOTTING
    elif machineState == ctx['const'].MachineState_RestoringSnapshot:
        swaggerMachineState = MachineState.RESTORINGSNAPSHOT
    elif machineState == ctx['const'].MachineState_DeletingSnapshot:
        swaggerMachineState = MachineState.DELETINGSNAPSHOT
    elif machineState == ctx['const'].MachineState_SettingUp:
        swaggerMachineState = MachineState.SETTINGUP
    elif machineState == ctx['const'].MachineState_Snapshotting:
        swaggerMachineState = MachineState.SNAPSHOTTING
    else:
        swaggerMachineState = MachineState.NULL

    return swaggerMachineState


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
    if platformArchitecture == 'x86':
        vBoxPlatformArchitecture = ctx['const'].PlatformArchitecture_x86
    elif platformArchitecture == 'EFI':
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