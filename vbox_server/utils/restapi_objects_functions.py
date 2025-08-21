"""VBox REST API

Copyright (c) 2025 Oracle and/or its affiliates.
Licensed under the Universal Permissive License v 1.0 as shown at https://oss.oracle.com/licenses/upl

SPDX-License-Identifier: UPL-1.0
"""

import os
import platform
import logging
from vbox_server.global_settings import *

if os.name == 'nt' or platform.system() == 'Windows':
    from pywintypes import com_error as COMException
else:
    from xpcom import COMException

# import interfaces
from vbox_server.models.virtual_box import VirtualBox
from vbox_server.models.machine import Machine
from vbox_server.models.shared_folder import SharedFolder
from vbox_server.models.medium_attachment import MediumAttachment
from vbox_server.models.bandwidth_group_type import BandwidthGroupType
from vbox_server.models.bandwidth_group import BandwidthGroup
from vbox_server.models.medium import Medium
from vbox_server.models.medium_format import MediumFormat
from vbox_server.models.progress import Progress
from vbox_server.models.session import Session
from vbox_server.models.usb_device import USBDevice
from vbox_server.models.serial_port import SerialPort
from vbox_server.models.parallel_port import ParallelPort
from vbox_server.models.snapshot import Snapshot
from vbox_server.models.platform_properties import PlatformProperties
from vbox_server.models.network_adapter import NetworkAdapter
from vbox_server.models.storage_controller import StorageController
from vbox_server.models.guest_os_type import GuestOSType
from vbox_server.models.nat_engine import NATEngine
from vbox_server.models.usb_controller import USBController

from vbox_server.models.virtual_box_error_info import VirtualBoxErrorInfo

def i_fill_virtual_box(oVBoxVirtualBox, select=None):
  """Convert the passed VirtualBox object oVBoxVirtualBox with interface IVirtualBox into Swagger object oVirtualBox"""
  
  logging.info('Enter function ')
  oVirtualBox = VirtualBox()
  try:
    if oVBoxVirtualBox is not None:
      if select is not None and len(select)>0:
        oVirtualBox = i_fill_partial_virtual_box(oVBoxVirtualBox, select)
      else:
        oVirtualBox = i_fill_whole_virtual_box(oVBoxVirtualBox)
  except Exception as e:
    logging.info('Abnormal function exit')
    oVirtualBox = None
    text = 'Exception trying to convert the VirtualBox object oVBoxVirtualBox into Swagger object oVirtualBox. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oVirtualBox


def i_fill_whole_virtual_box(oVBoxVirtualBox):
  logging.info('Enter function ')
  oVirtualBox = VirtualBox()
  try:
    if oVBoxVirtualBox is not None:
      try:
        oVirtualBox.version = oVBoxVirtualBox.version
      except Exception as e:
        logging.info('Error getting the attribute "version"')
      try:
        oVirtualBox.version_normalized = oVBoxVirtualBox.versionNormalized
      except Exception as e:
        logging.info('Error getting the attribute "versionNormalized"')
      try:
        oVirtualBox.revision = oVBoxVirtualBox.revision
      except Exception as e:
        logging.info('Error getting the attribute "revision"')
      try:
        oVirtualBox.package_type = oVBoxVirtualBox.packageType
      except Exception as e:
        logging.info('Error getting the attribute "packageType"')
      try:
        oVirtualBox.api_version = oVBoxVirtualBox.APIVersion
      except Exception as e:
        logging.info('Error getting the attribute "APIVersion"')
      try:
        oVirtualBox.api_revision = oVBoxVirtualBox.APIRevision
      except Exception as e:
        logging.info('Error getting the attribute "APIRevision"')
      try:
        oVirtualBox.home_folder = oVBoxVirtualBox.homeFolder
      except Exception as e:
        logging.info('Error getting the attribute "homeFolder"')
      try:
        oVirtualBox.settings_file_path = oVBoxVirtualBox.settingsFilePath
      except Exception as e:
        logging.info('Error getting the attribute "settingsFilePath"')
      try:
        ol_machine_groups = ctx['global'].getArray(oVBoxVirtualBox,'machineGroups')
        oVirtualBox.machine_groups = list()
        for count, item in enumerate(ol_machine_groups):
          oVirtualBox.machine_groups.append(item)
      except Exception as e:
        logging.info('Error getting the array of "machineGroups"')
      try:
        ol_hard_disks = ctx['global'].getArray(oVBoxVirtualBox,'hardDisks')
        oVirtualBox.hard_disks = list()
        for count, item in enumerate(ol_hard_disks):
          o = i_fill_medium(item)
          oVirtualBox.hard_disks.append(o)
      except Exception as e:
        logging.info('Error getting the array of "hardDisks"')
      try:
        ol_dvd_images = ctx['global'].getArray(oVBoxVirtualBox,'DVDImages')
        oVirtualBox.dvd_images = list()
        for count, item in enumerate(ol_dvd_images):
          o = i_fill_medium(item)
          oVirtualBox.dvd_images.append(o)
      except Exception as e:
        logging.info('Error getting the array of "DVDImages"')
      try:
        ol_floppy_images = ctx['global'].getArray(oVBoxVirtualBox,'floppyImages')
        oVirtualBox.floppy_images = list()
        for count, item in enumerate(ol_floppy_images):
          o = i_fill_medium(item)
          oVirtualBox.floppy_images.append(o)
      except Exception as e:
        logging.info('Error getting the array of "floppyImages"')
      try:
        ol_progress_operations = ctx['global'].getArray(oVBoxVirtualBox,'progressOperations')
        oVirtualBox.progress_operations = list()
        for count, item in enumerate(ol_progress_operations):
          o = i_fill_progress(item)
          oVirtualBox.progress_operations.append(o)
      except Exception as e:
        logging.info('Error getting the array of "progressOperations"')
      try:
        ol_guest_os_families = ctx['global'].getArray(oVBoxVirtualBox,'guestOSFamilies')
        oVirtualBox.guest_os_families = list()
        for count, item in enumerate(ol_guest_os_families):
          oVirtualBox.guest_os_families.append(item)
      except Exception as e:
        logging.info('Error getting the array of "guestOSFamilies"')
      try:
        ol_shared_folders = ctx['global'].getArray(oVBoxVirtualBox,'sharedFolders')
        oVirtualBox.shared_folders = list()
        for count, item in enumerate(ol_shared_folders):
          o = i_fill_shared_folder(item)
          oVirtualBox.shared_folders.append(o)
      except Exception as e:
        logging.info('Error getting the array of "sharedFolders"')
      try:
        ol_internal_networks = ctx['global'].getArray(oVBoxVirtualBox,'internalNetworks')
        oVirtualBox.internal_networks = list()
        for count, item in enumerate(ol_internal_networks):
          oVirtualBox.internal_networks.append(item)
      except Exception as e:
        logging.info('Error getting the array of "internalNetworks"')
      try:
        ol_generic_network_drivers = ctx['global'].getArray(oVBoxVirtualBox,'genericNetworkDrivers')
        oVirtualBox.generic_network_drivers = list()
        for count, item in enumerate(ol_generic_network_drivers):
          oVirtualBox.generic_network_drivers.append(item)
      except Exception as e:
        logging.info('Error getting the array of "genericNetworkDrivers"')
      
  except Exception as e:
    logging.info('Abnormal function exit')
    oVirtualBox = None
    text = 'Exception trying to fill the object oVirtualBox. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oVirtualBox

def i_fill_partial_virtual_box(oVBoxVirtualBox, select):
  logging.info('Enter function ')
  oVirtualBox = VirtualBox()
  try:
    if oVBoxVirtualBox is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='version':
            try:
              oVirtualBox.version = oVBoxVirtualBox.version
            except Exception as e:
              logging.info('Error getting the attribute "version"')
              raise Exception('Error getting the attribute "version"')
            
          if currAttr=='versionNormalized':
            try:
              oVirtualBox.version_normalized = oVBoxVirtualBox.versionNormalized
            except Exception as e:
              logging.info('Error getting the attribute "versionNormalized"')
              raise Exception('Error getting the attribute "versionNormalized"')
            
          if currAttr=='revision':
            try:
              oVirtualBox.revision = oVBoxVirtualBox.revision
            except Exception as e:
              logging.info('Error getting the attribute "revision"')
              raise Exception('Error getting the attribute "revision"')
            
          if currAttr=='packageType':
            try:
              oVirtualBox.package_type = oVBoxVirtualBox.packageType
            except Exception as e:
              logging.info('Error getting the attribute "packageType"')
              raise Exception('Error getting the attribute "packageType"')
            
          if currAttr=='APIVersion':
            try:
              oVirtualBox.api_version = oVBoxVirtualBox.APIVersion
            except Exception as e:
              logging.info('Error getting the attribute "APIVersion"')
              raise Exception('Error getting the attribute "APIVersion"')
            
          if currAttr=='APIRevision':
            try:
              oVirtualBox.api_revision = oVBoxVirtualBox.APIRevision
            except Exception as e:
              logging.info('Error getting the attribute "APIRevision"')
              raise Exception('Error getting the attribute "APIRevision"')
            
          if currAttr=='homeFolder':
            try:
              oVirtualBox.home_folder = oVBoxVirtualBox.homeFolder
            except Exception as e:
              logging.info('Error getting the attribute "homeFolder"')
              raise Exception('Error getting the attribute "homeFolder"')
            
          if currAttr=='settingsFilePath':
            try:
              oVirtualBox.settings_file_path = oVBoxVirtualBox.settingsFilePath
            except Exception as e:
              logging.info('Error getting the attribute "settingsFilePath"')
              raise Exception('Error getting the attribute "settingsFilePath"')
            
          if currAttr=='machineGroups':
            try:
              ol_machine_groups = ctx['global'].getArray(oVBoxVirtualBox,'machineGroups')
              oVirtualBox.machine_groups = list()
              for count, item in enumerate(ol_machine_groups):
                oVirtualBox.machine_groups.append(item)
            except Exception as e:
              logging.info('Error getting the array of "machineGroups"')
              raise Exception('Error getting the array of "machineGroups"')
            
          if currAttr=='hardDisks':
            try:
              ol_hard_disks = ctx['global'].getArray(oVBoxVirtualBox,'hardDisks')
              oVirtualBox.hard_disks = list()
              for count, item in enumerate(ol_hard_disks):
                o = i_fill_medium(item)
                oVirtualBox.hard_disks.append(o)
            except Exception as e:
              logging.info('Error getting the array of "hardDisks"')
              raise Exception('Error getting the array of "hardDisks"')
            
          if currAttr=='DVDImages':
            try:
              ol_dvd_images = ctx['global'].getArray(oVBoxVirtualBox,'DVDImages')
              oVirtualBox.dvd_images = list()
              for count, item in enumerate(ol_dvd_images):
                o = i_fill_medium(item)
                oVirtualBox.dvd_images.append(o)
            except Exception as e:
              logging.info('Error getting the array of "DVDImages"')
              raise Exception('Error getting the array of "DVDImages"')
            
          if currAttr=='floppyImages':
            try:
              ol_floppy_images = ctx['global'].getArray(oVBoxVirtualBox,'floppyImages')
              oVirtualBox.floppy_images = list()
              for count, item in enumerate(ol_floppy_images):
                o = i_fill_medium(item)
                oVirtualBox.floppy_images.append(o)
            except Exception as e:
              logging.info('Error getting the array of "floppyImages"')
              raise Exception('Error getting the array of "floppyImages"')
            
          if currAttr=='progressOperations':
            try:
              ol_progress_operations = ctx['global'].getArray(oVBoxVirtualBox,'progressOperations')
              oVirtualBox.progress_operations = list()
              for count, item in enumerate(ol_progress_operations):
                o = i_fill_progress(item)
                oVirtualBox.progress_operations.append(o)
            except Exception as e:
              logging.info('Error getting the array of "progressOperations"')
              raise Exception('Error getting the array of "progressOperations"')
            
          if currAttr=='guestOSFamilies':
            try:
              ol_guest_os_families = ctx['global'].getArray(oVBoxVirtualBox,'guestOSFamilies')
              oVirtualBox.guest_os_families = list()
              for count, item in enumerate(ol_guest_os_families):
                oVirtualBox.guest_os_families.append(item)
            except Exception as e:
              logging.info('Error getting the array of "guestOSFamilies"')
              raise Exception('Error getting the array of "guestOSFamilies"')
            
          if currAttr=='sharedFolders':
            try:
              ol_shared_folders = ctx['global'].getArray(oVBoxVirtualBox,'sharedFolders')
              oVirtualBox.shared_folders = list()
              for count, item in enumerate(ol_shared_folders):
                o = i_fill_shared_folder(item)
                oVirtualBox.shared_folders.append(o)
            except Exception as e:
              logging.info('Error getting the array of "sharedFolders"')
              raise Exception('Error getting the array of "sharedFolders"')
          
          if currAttr=='internalNetworks':
            try:
              ol_internal_networks = ctx['global'].getArray(oVBoxVirtualBox,'internalNetworks')
              oVirtualBox.internal_networks = list()
              for count, item in enumerate(ol_internal_networks):
                oVirtualBox.internal_networks.append(item)
            except Exception as e:
              logging.info('Error getting the array of "internalNetworks"')
              raise Exception('Error getting the array of "internalNetworks"')

          if currAttr=='genericNetworkDrivers':
            try:
              ol_generic_network_drivers = ctx['global'].getArray(oVBoxVirtualBox,'genericNetworkDrivers')
              oVirtualBox.generic_network_drivers = list()
              for count, item in enumerate(ol_generic_network_drivers):
                oVirtualBox.generic_network_drivers.append(item)
            except Exception as e:
              logging.info('Error getting the array of "genericNetworkDrivers"')
              raise Exception('Error getting the array of "genericNetworkDrivers"')

  except Exception as e:
    logging.info('Abnormal function exit')
    oVirtualBox = None
    text = 'Exception trying to fill the object oVirtualBox. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oVirtualBox


def i_fill_machine(oVBoxMachine, select=None):
  """Convert the passed VirtualBox object oVBoxMachine with interface IMachine into Swagger object oMachine"""
  
  logging.info('Enter function ')
  oMachine = Machine()
  try:
    if oVBoxMachine is not None:
      if select is not None and len(select)>0:
        oMachine = i_fill_partial_machine(oVBoxMachine, select)
      else:
        oMachine = i_fill_whole_machine(oVBoxMachine)
  except Exception as e:
    logging.info('Abnormal function exit')
    oMachine = None
    text = 'Exception trying to convert the VirtualBox object oVBoxMachine into Swagger object oMachine. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oMachine


def i_fill_whole_machine(oVBoxMachine):
  logging.info('Enter function ')
  oMachine = Machine()
  try:
    if oVBoxMachine is not None:
      try:
        ol_icon = ctx['global'].getArray(oVBoxMachine,'icon')
        oMachine.icon = list()
        for count, item in enumerate(ol_icon):
          oMachine.icon.append(item)
      except Exception as e:
        logging.info('Error getting the array of "icon"')
      try:
        oMachine.accessible = oVBoxMachine.accessible
      except Exception as e:
        logging.info('Error getting the attribute "accessible"')
      try:
        o_access_error = oVBoxMachine.accessError if oVBoxMachine.accessError is not None else None
        if o_access_error is not None:
          oMachine.access_error = i_fill_virtual_box_error_info(o_access_error)
        else:
          oMachine.access_error = None
      except Exception as e:
        logging.info('Error getting the interface object "accessError"')
      try:
        oMachine.name = oVBoxMachine.name
      except Exception as e:
        logging.info('Error getting the attribute "name"')
      try:
        oMachine.description = oVBoxMachine.description
      except Exception as e:
        logging.info('Error getting the attribute "description"')
      try:
        oMachine.id = oVBoxMachine.id
      except Exception as e:
        logging.info('Error getting the attribute "id"')
      try:
        ol_groups = ctx['global'].getArray(oVBoxMachine,'groups')
        oMachine.groups = list()
        for count, item in enumerate(ol_groups):
          oMachine.groups.append(item)
      except Exception as e:
        logging.info('Error getting the array of "groups"')
      try:
        oMachine.os_type_id = oVBoxMachine.OSTypeId
      except Exception as e:
        logging.info('Error getting the attribute "OSTypeId"')
      try:
        oMachine.hardware_version = oVBoxMachine.hardwareVersion
      except Exception as e:
        logging.info('Error getting the attribute "hardwareVersion"')
      try:
        oMachine.hardware_uuid = oVBoxMachine.hardwareUUID
      except Exception as e:
        logging.info('Error getting the attribute "hardwareUUID"')
      try:
        oMachine.cpu_count = oVBoxMachine.CPUCount
      except Exception as e:
        logging.info('Error getting the attribute "CPUCount"')
      try:
        oMachine.cpu_hot_plug_enabled = oVBoxMachine.CPUHotPlugEnabled
      except Exception as e:
        logging.info('Error getting the attribute "CPUHotPlugEnabled"')
      try:
        oMachine.cpu_execution_cap = oVBoxMachine.CPUExecutionCap
      except Exception as e:
        logging.info('Error getting the attribute "CPUExecutionCap"')
      try:
        oMachine.cpuid_portability_level = oVBoxMachine.CPUIDPortabilityLevel
      except Exception as e:
        logging.info('Error getting the attribute "CPUIDPortabilityLevel"')
      try:
        oMachine.memory_size = oVBoxMachine.memorySize
      except Exception as e:
        logging.info('Error getting the attribute "memorySize"')
      try:
        oMachine.memory_balloon_size = oVBoxMachine.memoryBalloonSize
      except Exception as e:
        logging.info('Error getting the attribute "memoryBalloonSize"')
      try:
        oMachine.page_fusion_enabled = oVBoxMachine.pageFusionEnabled
      except Exception as e:
        logging.info('Error getting the attribute "pageFusionEnabled"')
      try:
        oMachine.pointing_hid_type = ctx[ 'global'].getEnumValueName('PointingHIDType', oVBoxMachine.pointingHIDType)
      except Exception as e:
        logging.info('Error getting the attribute "pointingHIDType"')
      try:
        oMachine.keyboard_hid_type = ctx[ 'global'].getEnumValueName('KeyboardHIDType', oVBoxMachine.keyboardHIDType)
      except Exception as e:
        logging.info('Error getting the attribute "keyboardHIDType"')
      try:
        oMachine.snapshot_folder = oVBoxMachine.snapshotFolder
      except Exception as e:
        logging.info('Error getting the attribute "snapshotFolder"')
      try:
        oMachine.emulated_usb_card_reader_enabled = oVBoxMachine.emulatedUSBCardReaderEnabled
      except Exception as e:
        logging.info('Error getting the attribute "emulatedUSBCardReaderEnabled"')
      try:
        ol_medium_attachments = ctx['global'].getArray(oVBoxMachine,'mediumAttachments')
        oMachine.medium_attachments = list()
        for count, item in enumerate(ol_medium_attachments):
          o = i_fill_medium_attachment(item)
          oMachine.medium_attachments.append(o)
      except Exception as e:
        logging.info('Error getting the array of "mediumAttachments"')
      try:
        oMachine.settings_file_path = oVBoxMachine.settingsFilePath
      except Exception as e:
        logging.info('Error getting the attribute "settingsFilePath"')
      try:
        oMachine.session_state = ctx[ 'global'].getEnumValueName('SessionState', oVBoxMachine.sessionState)
      except Exception as e:
        logging.info('Error getting the attribute "sessionState"')
      try:
        oMachine.session_name = oVBoxMachine.sessionName
      except Exception as e:
        logging.info('Error getting the attribute "sessionName"')
      try:
        oMachine.session_pid = oVBoxMachine.sessionPID
      except Exception as e:
        logging.info('Error getting the attribute "sessionPID"')
      try:
        oMachine.state = ctx[ 'global'].getEnumValueName('MachineState', oVBoxMachine.state)
      except Exception as e:
        logging.info('Error getting the attribute "state"')
      try:
        oMachine.last_state_change = oVBoxMachine.lastStateChange
      except Exception as e:
        logging.info('Error getting the attribute "lastStateChange"')
      try:
        oMachine.state_file_path = oVBoxMachine.stateFilePath
      except Exception as e:
        logging.info('Error getting the attribute "stateFilePath"')
      try:
        oMachine.log_folder = oVBoxMachine.logFolder
      except Exception as e:
        logging.info('Error getting the attribute "logFolder"')
      try:
        oMachine.snapshot_count = oVBoxMachine.snapshotCount
      except Exception as e:
        logging.info('Error getting the attribute "snapshotCount"')
      try:
        oMachine.current_state_modified = oVBoxMachine.currentStateModified
      except Exception as e:
        logging.info('Error getting the attribute "currentStateModified"')
      try:
        ol_shared_folders = ctx['global'].getArray(oVBoxMachine,'sharedFolders')
        oMachine.shared_folders = list()
        for count, item in enumerate(ol_shared_folders):
          o = i_fill_shared_folder(item)
          oMachine.shared_folders.append(o)
      except Exception as e:
        logging.info('Error getting the array of "sharedFolders"')
      try:
        oMachine.clipboard_mode = ctx[ 'global'].getEnumValueName('ClipboardMode', oVBoxMachine.clipboardMode)
      except Exception as e:
        logging.info('Error getting the attribute "clipboardMode"')
      try:
        oMachine.clipboard_file_transfers_enabled = oVBoxMachine.clipboardFileTransfersEnabled
      except Exception as e:
        logging.info('Error getting the attribute "clipboardFileTransfersEnabled"')
      try:
        oMachine.dn_d_mode = ctx[ 'global'].getEnumValueName('DnDMode', oVBoxMachine.dnDMode)
      except Exception as e:
        logging.info('Error getting the attribute "dnDMode"')
      try:
        oMachine.teleporter_enabled = oVBoxMachine.teleporterEnabled
      except Exception as e:
        logging.info('Error getting the attribute "teleporterEnabled"')
      try:
        oMachine.teleporter_port = oVBoxMachine.teleporterPort
      except Exception as e:
        logging.info('Error getting the attribute "teleporterPort"')
      try:
        oMachine.teleporter_address = oVBoxMachine.teleporterAddress
      except Exception as e:
        logging.info('Error getting the attribute "teleporterAddress"')
      try:
        oMachine.teleporter_password = oVBoxMachine.teleporterPassword
      except Exception as e:
        logging.info('Error getting the attribute "teleporterPassword"')
      try:
        oMachine.paravirt_provider = ctx[ 'global'].getEnumValueName('ParavirtProvider', oVBoxMachine.paravirtProvider)
      except Exception as e:
        logging.info('Error getting the attribute "paravirtProvider"')
      try:
        oMachine.io_cache_enabled = oVBoxMachine.IOCacheEnabled
      except Exception as e:
        logging.info('Error getting the attribute "IOCacheEnabled"')
      try:
        oMachine.io_cache_size = oVBoxMachine.IOCacheSize
      except Exception as e:
        logging.info('Error getting the attribute "IOCacheSize"')
      try:
        oMachine.tracing_enabled = oVBoxMachine.tracingEnabled
      except Exception as e:
        logging.info('Error getting the attribute "tracingEnabled"')
      try:
        oMachine.tracing_config = oVBoxMachine.tracingConfig
      except Exception as e:
        logging.info('Error getting the attribute "tracingConfig"')
      try:
        oMachine.allow_tracing_to_access_vm = oVBoxMachine.allowTracingToAccessVM
      except Exception as e:
        logging.info('Error getting the attribute "allowTracingToAccessVM"')
      try:
        oMachine.autostart_enabled = oVBoxMachine.autostartEnabled
      except Exception as e:
        logging.info('Error getting the attribute "autostartEnabled"')
      try:
        oMachine.autostart_delay = oVBoxMachine.autostartDelay
      except Exception as e:
        logging.info('Error getting the attribute "autostartDelay"')
      try:
        oMachine.autostop_type = ctx[ 'global'].getEnumValueName('AutostopType', oVBoxMachine.autostopType)
      except Exception as e:
        logging.info('Error getting the attribute "autostopType"')
      try:
        oMachine.default_frontend = oVBoxMachine.defaultFrontend
      except Exception as e:
        logging.info('Error getting the attribute "defaultFrontend"')
      try:
        oMachine.usb_proxy_available = oVBoxMachine.USBProxyAvailable
      except Exception as e:
        logging.info('Error getting the attribute "USBProxyAvailable"')
      try:
        oMachine.vm_process_priority = ctx[ 'global'].getEnumValueName('VMProcPriority', oVBoxMachine.VMProcessPriority)
      except Exception as e:
        logging.info('Error getting the attribute "VMProcessPriority"')
      try:
        oMachine.vm_execution_engine = ctx[ 'global'].getEnumValueName('VMExecutionEngine', oVBoxMachine.VMExecutionEngine)
      except Exception as e:
        logging.info('Error getting the attribute "VMExecutionEngine"')
      try:
        oMachine.paravirt_debug = oVBoxMachine.paravirtDebug
      except Exception as e:
        logging.info('Error getting the attribute "paravirtDebug"')
      try:
        oMachine.cpu_profile = oVBoxMachine.CPUProfile
      except Exception as e:
        logging.info('Error getting the attribute "CPUProfile"')
      try:
        oMachine.state_key_id = oVBoxMachine.stateKeyId
      except Exception as e:
        logging.info('Error getting the attribute "stateKeyId"')
      try:
        oMachine.state_key_store = oVBoxMachine.stateKeyStore
      except Exception as e:
        logging.info('Error getting the attribute "stateKeyStore"')
      try:
        oMachine.log_key_id = oVBoxMachine.logKeyId
      except Exception as e:
        logging.info('Error getting the attribute "logKeyId"')
      try:
        oMachine.log_key_store = oVBoxMachine.logKeyStore
      except Exception as e:
        logging.info('Error getting the attribute "logKeyStore"')
      
  except Exception as e:
    logging.info('Abnormal function exit')
    oMachine = None
    text = 'Exception trying to fill the object oMachine. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oMachine

def i_fill_partial_machine(oVBoxMachine, select):
  logging.info('Enter function ')
  oMachine = Machine()
  try:
    if oVBoxMachine is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='icon':
            try:
              ol_icon = ctx['global'].getArray(oVBoxMachine,'icon')
              oMachine.icon = list()
              for count, item in enumerate(ol_icon):
                oMachine.icon.append(item)
            except Exception as e:
              logging.info('Error getting the array of "icon"')
              raise Exception('Error getting the array of "icon"')
            
          if currAttr=='accessible':
            try:
              oMachine.accessible = oVBoxMachine.accessible
            except Exception as e:
              logging.info('Error getting the attribute "accessible"')
              raise Exception('Error getting the attribute "accessible"')
            
          if currAttr=='accessError':
            try:
              o_access_error = oVBoxMachine.accessError if oVBoxMachine.accessError is not None else None
              if o_access_error is not None:
                oMachine.access_error = i_fill_virtual_box_error_info(o_access_error)
              else:
                oMachine.access_error = None
            except Exception as e:
              logging.info('Error getting the interface object "accessError"')
              raise Exception('Error getting the interface object "accessError"')
            
          if currAttr=='name':
            try:
              oMachine.name = oVBoxMachine.name
            except Exception as e:
              logging.info('Error getting the attribute "name"')
              raise Exception('Error getting the attribute "name"')
            
          if currAttr=='description':
            try:
              oMachine.description = oVBoxMachine.description
            except Exception as e:
              logging.info('Error getting the attribute "description"')
              raise Exception('Error getting the attribute "description"')
            
          if currAttr=='id':
            try:
              oMachine.id = oVBoxMachine.id
            except Exception as e:
              logging.info('Error getting the attribute "id"')
              raise Exception('Error getting the attribute "id"')
            
          if currAttr=='groups':
            try:
              ol_groups = ctx['global'].getArray(oVBoxMachine,'groups')
              oMachine.groups = list()
              for count, item in enumerate(ol_groups):
                oMachine.groups.append(item)
            except Exception as e:
              logging.info('Error getting the array of "groups"')
              raise Exception('Error getting the array of "groups"')
            
          if currAttr=='OSTypeId':
            try:
              oMachine.os_type_id = oVBoxMachine.OSTypeId
            except Exception as e:
              logging.info('Error getting the attribute "OSTypeId"')
              raise Exception('Error getting the attribute "OSTypeId"')
            
          if currAttr=='hardwareVersion':
            try:
              oMachine.hardware_version = oVBoxMachine.hardwareVersion
            except Exception as e:
              logging.info('Error getting the attribute "hardwareVersion"')
              raise Exception('Error getting the attribute "hardwareVersion"')
            
          if currAttr=='hardwareUUID':
            try:
              oMachine.hardware_uuid = oVBoxMachine.hardwareUUID
            except Exception as e:
              logging.info('Error getting the attribute "hardwareUUID"')
              raise Exception('Error getting the attribute "hardwareUUID"')
            
          if currAttr=='CPUCount':
            try:
              oMachine.cpu_count = oVBoxMachine.CPUCount
            except Exception as e:
              logging.info('Error getting the attribute "CPUCount"')
              raise Exception('Error getting the attribute "CPUCount"')
            
          if currAttr=='CPUHotPlugEnabled':
            try:
              oMachine.cpu_hot_plug_enabled = oVBoxMachine.CPUHotPlugEnabled
            except Exception as e:
              logging.info('Error getting the attribute "CPUHotPlugEnabled"')
              raise Exception('Error getting the attribute "CPUHotPlugEnabled"')
            
          if currAttr=='CPUExecutionCap':
            try:
              oMachine.cpu_execution_cap = oVBoxMachine.CPUExecutionCap
            except Exception as e:
              logging.info('Error getting the attribute "CPUExecutionCap"')
              raise Exception('Error getting the attribute "CPUExecutionCap"')
            
          if currAttr=='CPUIDPortabilityLevel':
            try:
              oMachine.cpuid_portability_level = oVBoxMachine.CPUIDPortabilityLevel
            except Exception as e:
              logging.info('Error getting the attribute "CPUIDPortabilityLevel"')
              raise Exception('Error getting the attribute "CPUIDPortabilityLevel"')
            
          if currAttr=='memorySize':
            try:
              oMachine.memory_size = oVBoxMachine.memorySize
            except Exception as e:
              logging.info('Error getting the attribute "memorySize"')
              raise Exception('Error getting the attribute "memorySize"')
            
          if currAttr=='memoryBalloonSize':
            try:
              oMachine.memory_balloon_size = oVBoxMachine.memoryBalloonSize
            except Exception as e:
              logging.info('Error getting the attribute "memoryBalloonSize"')
              raise Exception('Error getting the attribute "memoryBalloonSize"')
            
          if currAttr=='pageFusionEnabled':
            try:
              oMachine.page_fusion_enabled = oVBoxMachine.pageFusionEnabled
            except Exception as e:
              logging.info('Error getting the attribute "pageFusionEnabled"')
              raise Exception('Error getting the attribute "pageFusionEnabled"')

          if currAttr=='pointingHIDType':
            try:
              oMachine.pointing_hid_type = ctx[ 'global'].getEnumValueName('PointingHIDType', oVBoxMachine.pointingHIDType)
            except Exception as e:
              logging.info('Error getting the attribute "pointingHIDType"')
              raise Exception('Error getting the array of "pointingHIDType"')
            
          if currAttr=='keyboardHIDType':
            try:
              oMachine.keyboard_hid_type = ctx[ 'global'].getEnumValueName('KeyboardHIDType', oVBoxMachine.keyboardHIDType)
            except Exception as e:
              logging.info('Error getting the attribute "keyboardHIDType"')
              raise Exception('Error getting the array of "keyboardHIDType"')
            
          if currAttr=='snapshotFolder':
            try:
              oMachine.snapshot_folder = oVBoxMachine.snapshotFolder
            except Exception as e:
              logging.info('Error getting the attribute "snapshotFolder"')
              raise Exception('Error getting the attribute "snapshotFolder"')
            
          if currAttr=='emulatedUSBCardReaderEnabled':
            try:
              oMachine.emulated_usb_card_reader_enabled = oVBoxMachine.emulatedUSBCardReaderEnabled
            except Exception as e:
              logging.info('Error getting the attribute "emulatedUSBCardReaderEnabled"')
              raise Exception('Error getting the attribute "emulatedUSBCardReaderEnabled"')
            
          if currAttr=='mediumAttachments':
            try:
              ol_medium_attachments = ctx['global'].getArray(oVBoxMachine,'mediumAttachments')
              oMachine.medium_attachments = list()
              for count, item in enumerate(ol_medium_attachments):
                o = i_fill_medium_attachment(item)
                oMachine.medium_attachments.append(o)
            except Exception as e:
              logging.info('Error getting the array of "mediumAttachments"')
              raise Exception('Error getting the array of "mediumAttachments"')

          if currAttr=='settingsFilePath':
            try:
              oMachine.settings_file_path = oVBoxMachine.settingsFilePath
            except Exception as e:
              logging.info('Error getting the attribute "settingsFilePath"')
              raise Exception('Error getting the attribute "settingsFilePath"')
            
          if currAttr=='sessionState':
            try:
              oMachine.session_state = ctx[ 'global'].getEnumValueName('SessionState', oVBoxMachine.sessionState)
            except Exception as e:
              logging.info('Error getting the attribute "sessionState"')
              raise Exception('Error getting the array of "sessionState"')
            
          if currAttr=='sessionName':
            try:
              oMachine.session_name = oVBoxMachine.sessionName
            except Exception as e:
              logging.info('Error getting the attribute "sessionName"')
              raise Exception('Error getting the attribute "sessionName"')
            
          if currAttr=='sessionPID':
            try:
              oMachine.session_pid = oVBoxMachine.sessionPID
            except Exception as e:
              logging.info('Error getting the attribute "sessionPID"')
              raise Exception('Error getting the attribute "sessionPID"')
            
          if currAttr=='state':
            try:
              oMachine.state = ctx[ 'global'].getEnumValueName('MachineState', oVBoxMachine.state)
            except Exception as e:
              logging.info('Error getting the attribute "state"')
              raise Exception('Error getting the array of "state"')
            
          if currAttr=='lastStateChange':
            try:
              oMachine.last_state_change = oVBoxMachine.lastStateChange
            except Exception as e:
              logging.info('Error getting the attribute "lastStateChange"')
              raise Exception('Error getting the attribute "lastStateChange"')
            
          if currAttr=='stateFilePath':
            try:
              oMachine.state_file_path = oVBoxMachine.stateFilePath
            except Exception as e:
              logging.info('Error getting the attribute "stateFilePath"')
              raise Exception('Error getting the attribute "stateFilePath"')
            
          if currAttr=='logFolder':
            try:
              oMachine.log_folder = oVBoxMachine.logFolder
            except Exception as e:
              logging.info('Error getting the attribute "logFolder"')
              raise Exception('Error getting the attribute "logFolder"')
            
          if currAttr=='snapshotCount':
            try:
              oMachine.snapshot_count = oVBoxMachine.snapshotCount
            except Exception as e:
              logging.info('Error getting the attribute "snapshotCount"')
              raise Exception('Error getting the attribute "snapshotCount"')
            
          if currAttr=='currentStateModified':
            try:
              oMachine.current_state_modified = oVBoxMachine.currentStateModified
            except Exception as e:
              logging.info('Error getting the attribute "currentStateModified"')
              raise Exception('Error getting the attribute "currentStateModified"')
            
          if currAttr=='sharedFolders':
            try:
              ol_shared_folders = ctx['global'].getArray(oVBoxMachine,'sharedFolders')
              oMachine.shared_folders = list()
              for count, item in enumerate(ol_shared_folders):
                o = i_fill_shared_folder(item)
                oMachine.shared_folders.append(o)
            except Exception as e:
              logging.info('Error getting the array of "sharedFolders"')
              raise Exception('Error getting the array of "sharedFolders"')
            
          if currAttr=='clipboardMode':
            try:
              oMachine.clipboard_mode = ctx[ 'global'].getEnumValueName('ClipboardMode', oVBoxMachine.clipboardMode)
            except Exception as e:
              logging.info('Error getting the attribute "clipboardMode"')
              raise Exception('Error getting the array of "clipboardMode"')
            
          if currAttr=='clipboardFileTransfersEnabled':
            try:
              oMachine.clipboard_file_transfers_enabled = oVBoxMachine.clipboardFileTransfersEnabled
            except Exception as e:
              logging.info('Error getting the attribute "clipboardFileTransfersEnabled"')
              raise Exception('Error getting the attribute "clipboardFileTransfersEnabled"')
            
          if currAttr=='dnDMode':
            try:
              oMachine.dn_d_mode = ctx[ 'global'].getEnumValueName('DnDMode', oVBoxMachine.dnDMode)
            except Exception as e:
              logging.info('Error getting the attribute "dnDMode"')
              raise Exception('Error getting the array of "dnDMode"')
            
          if currAttr=='teleporterEnabled':
            try:
              oMachine.teleporter_enabled = oVBoxMachine.teleporterEnabled
            except Exception as e:
              logging.info('Error getting the attribute "teleporterEnabled"')
              raise Exception('Error getting the attribute "teleporterEnabled"')
            
          if currAttr=='teleporterPort':
            try:
              oMachine.teleporter_port = oVBoxMachine.teleporterPort
            except Exception as e:
              logging.info('Error getting the attribute "teleporterPort"')
              raise Exception('Error getting the attribute "teleporterPort"')
            
          if currAttr=='teleporterAddress':
            try:
              oMachine.teleporter_address = oVBoxMachine.teleporterAddress
            except Exception as e:
              logging.info('Error getting the attribute "teleporterAddress"')
              raise Exception('Error getting the attribute "teleporterAddress"')
            
          if currAttr=='teleporterPassword':
            try:
              oMachine.teleporter_password = oVBoxMachine.teleporterPassword
            except Exception as e:
              logging.info('Error getting the attribute "teleporterPassword"')
              raise Exception('Error getting the attribute "teleporterPassword"')
            
          if currAttr=='paravirtProvider':
            try:
              oMachine.paravirt_provider = ctx[ 'global'].getEnumValueName('ParavirtProvider', oVBoxMachine.paravirtProvider)
            except Exception as e:
              logging.info('Error getting the attribute "paravirtProvider"')
              raise Exception('Error getting the array of "paravirtProvider"')
            
          if currAttr=='IOCacheEnabled':
            try:
              oMachine.io_cache_enabled = oVBoxMachine.IOCacheEnabled
            except Exception as e:
              logging.info('Error getting the attribute "IOCacheEnabled"')
              raise Exception('Error getting the attribute "IOCacheEnabled"')
            
          if currAttr=='IOCacheSize':
            try:
              oMachine.io_cache_size = oVBoxMachine.IOCacheSize
            except Exception as e:
              logging.info('Error getting the attribute "IOCacheSize"')
              raise Exception('Error getting the attribute "IOCacheSize"')
            
          if currAttr=='tracingEnabled':
            try:
              oMachine.tracing_enabled = oVBoxMachine.tracingEnabled
            except Exception as e:
              logging.info('Error getting the attribute "tracingEnabled"')
              raise Exception('Error getting the attribute "tracingEnabled"')
            
          if currAttr=='tracingConfig':
            try:
              oMachine.tracing_config = oVBoxMachine.tracingConfig
            except Exception as e:
              logging.info('Error getting the attribute "tracingConfig"')
              raise Exception('Error getting the attribute "tracingConfig"')
            
          if currAttr=='allowTracingToAccessVM':
            try:
              oMachine.allow_tracing_to_access_vm = oVBoxMachine.allowTracingToAccessVM
            except Exception as e:
              logging.info('Error getting the attribute "allowTracingToAccessVM"')
              raise Exception('Error getting the attribute "allowTracingToAccessVM"')
            
          if currAttr=='autostartEnabled':
            try:
              oMachine.autostart_enabled = oVBoxMachine.autostartEnabled
            except Exception as e:
              logging.info('Error getting the attribute "autostartEnabled"')
              raise Exception('Error getting the attribute "autostartEnabled"')
            
          if currAttr=='autostartDelay':
            try:
              oMachine.autostart_delay = oVBoxMachine.autostartDelay
            except Exception as e:
              logging.info('Error getting the attribute "autostartDelay"')
              raise Exception('Error getting the attribute "autostartDelay"')
            
          if currAttr=='autostopType':
            try:
              oMachine.autostop_type = ctx[ 'global'].getEnumValueName('AutostopType', oVBoxMachine.autostopType)
            except Exception as e:
              logging.info('Error getting the attribute "autostopType"')
              raise Exception('Error getting the array of "autostopType"')
            
          if currAttr=='defaultFrontend':
            try:
              oMachine.default_frontend = oVBoxMachine.defaultFrontend
            except Exception as e:
              logging.info('Error getting the attribute "defaultFrontend"')
              raise Exception('Error getting the attribute "defaultFrontend"')
            
          if currAttr=='USBProxyAvailable':
            try:
              oMachine.usb_proxy_available = oVBoxMachine.USBProxyAvailable
            except Exception as e:
              logging.info('Error getting the attribute "USBProxyAvailable"')
              raise Exception('Error getting the attribute "USBProxyAvailable"')
            
          if currAttr=='VMProcessPriority':
            try:
              oMachine.vm_process_priority = ctx[ 'global'].getEnumValueName('VMProcPriority', oVBoxMachine.VMProcessPriority)
            except Exception as e:
              logging.info('Error getting the attribute "VMProcessPriority"')
              raise Exception('Error getting the array of "VMProcessPriority"')
            
          if currAttr=='VMExecutionEngine':
            try:
              oMachine.vm_execution_engine = ctx[ 'global'].getEnumValueName('VMExecutionEngine', oVBoxMachine.VMExecutionEngine)
            except Exception as e:
              logging.info('Error getting the attribute "VMExecutionEngine"')
              raise Exception('Error getting the array of "VMExecutionEngine"')
            
          if currAttr=='paravirtDebug':
            try:
              oMachine.paravirt_debug = oVBoxMachine.paravirtDebug
            except Exception as e:
              logging.info('Error getting the attribute "paravirtDebug"')
              raise Exception('Error getting the attribute "paravirtDebug"')
            
          if currAttr=='CPUProfile':
            try:
              oMachine.cpu_profile = oVBoxMachine.CPUProfile
            except Exception as e:
              logging.info('Error getting the attribute "CPUProfile"')
              raise Exception('Error getting the attribute "CPUProfile"')
            
          if currAttr=='stateKeyId':
            try:
              oMachine.state_key_id = oVBoxMachine.stateKeyId
            except Exception as e:
              logging.info('Error getting the attribute "stateKeyId"')
              raise Exception('Error getting the attribute "stateKeyId"')
            
          if currAttr=='stateKeyStore':
            try:
              oMachine.state_key_store = oVBoxMachine.stateKeyStore
            except Exception as e:
              logging.info('Error getting the attribute "stateKeyStore"')
              raise Exception('Error getting the attribute "stateKeyStore"')
            
          if currAttr=='logKeyId':
            try:
              oMachine.log_key_id = oVBoxMachine.logKeyId
            except Exception as e:
              logging.info('Error getting the attribute "logKeyId"')
              raise Exception('Error getting the attribute "logKeyId"')
            
          if currAttr=='logKeyStore':
            try:
              oMachine.log_key_store = oVBoxMachine.logKeyStore
            except Exception as e:
              logging.info('Error getting the attribute "logKeyStore"')
              raise Exception('Error getting the attribute "logKeyStore"')
            
  except Exception as e:
    logging.info('Abnormal function exit')
    oMachine = None
    text = 'Exception trying to fill the object oMachine. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oMachine


def i_fill_shared_folder(oVBoxSharedFolder, select=None):
  """Convert the passed VirtualBox object oVBoxSharedFolder with interface ISharedFolder into Swagger object oSharedFolder"""
  
  logging.info('Enter function ')
  oSharedFolder = SharedFolder()
  try:
    if oVBoxSharedFolder is not None:
      if select is not None and len(select)>0:
        oSharedFolder = i_fill_partial_shared_folder(oVBoxSharedFolder, select)
      else:
        oSharedFolder = i_fill_whole_shared_folder(oVBoxSharedFolder)
  except Exception as e:
    logging.info('Abnormal function exit')
    oSharedFolder = None
    text = 'Exception trying to convert the VirtualBox object oVBoxSharedFolder into Swagger object oSharedFolder. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oSharedFolder

def i_fill_whole_shared_folder(oVBoxSharedFolder):
  logging.info('Enter function ')
  oSharedFolder = SharedFolder()
  try:
    if oVBoxSharedFolder is not None:
      try:
        oSharedFolder.name = oVBoxSharedFolder.name
      except Exception as e:
        logging.info('Error getting the attribute "name"')
      try:
        oSharedFolder.host_path = oVBoxSharedFolder.hostPath
      except Exception as e:
        logging.info('Error getting the attribute "hostPath"')
      try:
        oSharedFolder.accessible = oVBoxSharedFolder.accessible
      except Exception as e:
        logging.info('Error getting the attribute "accessible"')
      try:
        oSharedFolder.writable = oVBoxSharedFolder.writable
      except Exception as e:
        logging.info('Error getting the attribute "writable"')
      try:
        oSharedFolder.auto_mount = oVBoxSharedFolder.autoMount
      except Exception as e:
        logging.info('Error getting the attribute "autoMount"')
      try:
        oSharedFolder.auto_mount_point = oVBoxSharedFolder.autoMountPoint
      except Exception as e:
        logging.info('Error getting the attribute "autoMountPoint"')
      try:
        oSharedFolder.last_access_error = oVBoxSharedFolder.lastAccessError
      except Exception as e:
        logging.info('Error getting the attribute "lastAccessError"')
      try:
        oSharedFolder.symlink_policy = ctx[ 'global'].getEnumValueName('SymlinkPolicy', oVBoxSharedFolder.symlinkPolicy)
      except Exception as e:
        logging.info('Error getting the attribute "symlinkPolicy"')
      
  except Exception as e:
    logging.info('Abnormal function exit')
    oSharedFolder = None
    text = 'Exception trying to fill the object oSharedFolder. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oSharedFolder

def i_fill_partial_shared_folder(oVBoxSharedFolder, select):
  logging.info('Enter function ')
  oSharedFolder = SharedFolder()
  try:
    if oVBoxSharedFolder is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='name':
            try:
              oSharedFolder.name = oVBoxSharedFolder.name
            except Exception as e:
              logging.info('Error getting the attribute "name"')
              raise Exception('Error getting the attribute "name"')
            
          if currAttr=='hostPath':
            try:
              oSharedFolder.host_path = oVBoxSharedFolder.hostPath
            except Exception as e:
              logging.info('Error getting the attribute "hostPath"')
              raise Exception('Error getting the attribute "hostPath"')
            
          if currAttr=='accessible':
            try:
              oSharedFolder.accessible = oVBoxSharedFolder.accessible
            except Exception as e:
              logging.info('Error getting the attribute "accessible"')
              raise Exception('Error getting the attribute "accessible"')
            
          if currAttr=='writable':
            try:
              oSharedFolder.writable = oVBoxSharedFolder.writable
            except Exception as e:
              logging.info('Error getting the attribute "writable"')
              raise Exception('Error getting the attribute "writable"')
            
          if currAttr=='autoMount':
            try:
              oSharedFolder.auto_mount = oVBoxSharedFolder.autoMount
            except Exception as e:
              logging.info('Error getting the attribute "autoMount"')
              raise Exception('Error getting the attribute "autoMount"')
            
          if currAttr=='autoMountPoint':
            try:
              oSharedFolder.auto_mount_point = oVBoxSharedFolder.autoMountPoint
            except Exception as e:
              logging.info('Error getting the attribute "autoMountPoint"')
              raise Exception('Error getting the attribute "autoMountPoint"')
            
          if currAttr=='lastAccessError':
            try:
              oSharedFolder.last_access_error = oVBoxSharedFolder.lastAccessError
            except Exception as e:
              logging.info('Error getting the attribute "lastAccessError"')
              raise Exception('Error getting the attribute "lastAccessError"')
            
          if currAttr=='symlinkPolicy':
            try:
              oSharedFolder.symlink_policy = ctx[ 'global'].getEnumValueName('SymlinkPolicy', oVBoxSharedFolder.symlinkPolicy)
            except Exception as e:
              logging.info('Error getting the attribute "symlinkPolicy"')
              raise Exception('Error getting the array of "symlinkPolicy"')
            
  except Exception as e:
    logging.info('Abnormal function exit')
    oSharedFolder = None
    text = 'Exception trying to fill the object oSharedFolder. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oSharedFolder


def i_fill_medium_attachment(oVBoxMediumAttachment, select=None):
  """Convert the passed VirtualBox object oVBoxMediumAttachment with interface IMediumAttachment into Swagger object oMediumAttachment"""

  logging.info('Enter function ')
  oMediumAttachment = MediumAttachment()
  try:
    if oVBoxMediumAttachment is not None:
      if select is not None and len(select)>0:
        oMediumAttachment = i_fill_partial_medium_attachment(oVBoxMediumAttachment, select)
      else:
        oMediumAttachment = i_fill_whole_medium_attachment(oVBoxMediumAttachment)
  except Exception as e:
    logging.info('Abnormal function exit')
    oMediumAttachment = None
    text = 'Exception trying to convert the VirtualBox object oVBoxMediumAttachment into Swagger object oMediumAttachment. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oMediumAttachment

def i_fill_whole_medium_attachment(oVBoxMediumAttachment):
  logging.info('Enter function ')
  oMediumAttachment = MediumAttachment()
  try:
    if oVBoxMediumAttachment is not None:
      try:
        oMediumAttachment.machine = oVBoxMediumAttachment.machine.id
      except Exception as e:
        logging.info('Error getting the attribute "machine"')
      try:
        oMediumAttachment.medium = oVBoxMediumAttachment.medium.id
      except Exception as e:
        logging.info('Error getting the attribute "medium"')
      try:
        oMediumAttachment.controller = oVBoxMediumAttachment.controller
      except Exception as e:
        logging.info('Error getting the attribute "controller"')
      try:
        oMediumAttachment.port = oVBoxMediumAttachment.port
      except Exception as e:
        logging.info('Error getting the attribute "port"')
      try:
        oMediumAttachment.device = oVBoxMediumAttachment.device
      except Exception as e:
        logging.info('Error getting the attribute "device"')
      try:
        oMediumAttachment.type = ctx[ 'global'].getEnumValueName('DeviceType', oVBoxMediumAttachment.type)
      except Exception as e:
        logging.info('Error getting the attribute "type"')
      try:
        oMediumAttachment.passthrough = oVBoxMediumAttachment.passthrough
      except Exception as e:
        logging.info('Error getting the attribute "passthrough"')
      try:
        oMediumAttachment.temporary_eject = oVBoxMediumAttachment.temporaryEject
      except Exception as e:
        logging.info('Error getting the attribute "temporaryEject"')
      try:
        oMediumAttachment.is_ejected = oVBoxMediumAttachment.isEjected
      except Exception as e:
        logging.info('Error getting the attribute "isEjected"')
      try:
        oMediumAttachment.non_rotational = oVBoxMediumAttachment.nonRotational
      except Exception as e:
        logging.info('Error getting the attribute "nonRotational"')
      try:
        oMediumAttachment.discard = oVBoxMediumAttachment.discard
      except Exception as e:
        logging.info('Error getting the attribute "discard"')
      try:
        oMediumAttachment.hot_pluggable = oVBoxMediumAttachment.hotPluggable
      except Exception as e:
        logging.info('Error getting the attribute "hotPluggable"')
      try:
        o_bandwidth_group = oVBoxMediumAttachment.bandwidthGroup if oVBoxMediumAttachment.bandwidthGroup is not None else None
        if o_bandwidth_group is not None:
          oMediumAttachment.bandwidth_group = i_fill_bandwidth_group(o_bandwidth_group)
        else:
          oMediumAttachment.bandwidth_group = None
      except Exception as e:
        logging.info('Error getting the interface object "bandwidthGroup"')
      
  except Exception as e:
    logging.info('Abnormal function exit')
    oMediumAttachment = None
    text = 'Exception trying to fill the object oMediumAttachment. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oMediumAttachment

def i_fill_partial_medium_attachment(oVBoxMediumAttachment, select):
  logging.info('Enter function ')
  oMediumAttachment = MediumAttachment()
  try:
    if oVBoxMediumAttachment is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='machine':
            try:
              oMediumAttachment.machine = oVBoxMediumAttachment.machine.id
            except Exception as e:
              logging.info('Error getting the attribute "machine"')
              raise Exception('Error getting the attribute "machine"')
            
          if currAttr=='medium':
            try:
              oMediumAttachment.medium = oVBoxMediumAttachment.medium.id
            except Exception as e:
              logging.info('Error getting the attribute "medium"')
              raise Exception('Error getting the attribute "medium"')
            
          if currAttr=='controller':
            try:
              oMediumAttachment.controller = oVBoxMediumAttachment.controller
            except Exception as e:
              logging.info('Error getting the attribute "controller"')
              raise Exception('Error getting the attribute "controller"')
            
          if currAttr=='port':
            try:
              oMediumAttachment.port = oVBoxMediumAttachment.port
            except Exception as e:
              logging.info('Error getting the attribute "port"')
              raise Exception('Error getting the attribute "port"')
            
          if currAttr=='device':
            try:
              oMediumAttachment.device = oVBoxMediumAttachment.device
            except Exception as e:
              logging.info('Error getting the attribute "device"')
              raise Exception('Error getting the attribute "device"')
            
          if currAttr=='type':
            try:
              oMediumAttachment.type = ctx[ 'global'].getEnumValueName('DeviceType', oVBoxMediumAttachment.type)
            except Exception as e:
              logging.info('Error getting the attribute "type"')
              raise Exception('Error getting the array of "type"')
            
          if currAttr=='passthrough':
            try:
              oMediumAttachment.passthrough = oVBoxMediumAttachment.passthrough
            except Exception as e:
              logging.info('Error getting the attribute "passthrough"')
              raise Exception('Error getting the attribute "passthrough"')
            
          if currAttr=='temporaryEject':
            try:
              oMediumAttachment.temporary_eject = oVBoxMediumAttachment.temporaryEject
            except Exception as e:
              logging.info('Error getting the attribute "temporaryEject"')
              raise Exception('Error getting the attribute "temporaryEject"')
            
          if currAttr=='isEjected':
            try:
              oMediumAttachment.is_ejected = oVBoxMediumAttachment.isEjected
            except Exception as e:
              logging.info('Error getting the attribute "isEjected"')
              raise Exception('Error getting the attribute "isEjected"')
            
          if currAttr=='nonRotational':
            try:
              oMediumAttachment.non_rotational = oVBoxMediumAttachment.nonRotational
            except Exception as e:
              logging.info('Error getting the attribute "nonRotational"')
              raise Exception('Error getting the attribute "nonRotational"')
            
          if currAttr=='discard':
            try:
              oMediumAttachment.discard = oVBoxMediumAttachment.discard
            except Exception as e:
              logging.info('Error getting the attribute "discard"')
              raise Exception('Error getting the attribute "discard"')
            
          if currAttr=='hotPluggable':
            try:
              oMediumAttachment.hot_pluggable = oVBoxMediumAttachment.hotPluggable
            except Exception as e:
              logging.info('Error getting the attribute "hotPluggable"')
              raise Exception('Error getting the attribute "hotPluggable"')
            
          if currAttr=='bandwidthGroup':
            try:
              o_bandwidth_group = oVBoxMediumAttachment.bandwidthGroup if oVBoxMediumAttachment.bandwidthGroup is not None else None
              if o_bandwidth_group is not None:
                oMediumAttachment.bandwidth_group = i_fill_bandwidth_group(o_bandwidth_group)
              else:
                oMediumAttachment.bandwidth_group = None
            except Exception as e:
              logging.info('Error getting the interface object "bandwidthGroup"')
              raise Exception('Error getting the interface object "bandwidthGroup"')
            
  except Exception as e:
    logging.info('Abnormal function exit')
    oMediumAttachment = None
    text = 'Exception trying to fill the object oMediumAttachment. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oMediumAttachment


def i_fill_bandwidth_group(oVBoxBandwidthGroup, select=None):
  """Convert the passed VirtualBox object oVBoxBandwidthGroup with interface IBandwidthGroup into Swagger object oBandwidthGroup"""

  logging.info('Enter function ')
  oBandwidthGroup = BandwidthGroup()
  try:
    if oVBoxBandwidthGroup is not None:
      if select is not None and len(select)>0:
        oBandwidthGroup = i_fill_partial_bandwidth_group(oVBoxBandwidthGroup, select)
      else:
        oBandwidthGroup = i_fill_whole_bandwidth_group(oVBoxBandwidthGroup)
  except Exception as e:
    logging.info('Abnormal function exit')
    oBandwidthGroup = None
    text = 'Exception trying to convert the VirtualBox object oVBoxBandwidthGroup into Swagger object oBandwidthGroup. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oBandwidthGroup

def i_fill_whole_bandwidth_group(oVBoxBandwidthGroup):
  logging.info('Enter function ')
  oBandwidthGroup = BandwidthGroup()
  try:
    if oVBoxBandwidthGroup is not None:
      try:
        oBandwidthGroup.name = oVBoxBandwidthGroup.name
      except Exception as e:
        logging.info('Error getting the attribute "name"')
      try:
        oBandwidthGroup.type = ctx[ 'global'].getEnumValueName('BandwidthGroupType', oVBoxBandwidthGroup.type)
      except Exception as e:
        logging.info('Error getting the attribute "type"')
      try:
        oBandwidthGroup.reference = oVBoxBandwidthGroup.reference
      except Exception as e:
        logging.info('Error getting the attribute "reference"')
      try:
        oBandwidthGroup.max_bytes_per_sec = oVBoxBandwidthGroup.maxBytesPerSec
      except Exception as e:
        logging.info('Error getting the attribute "maxBytesPerSec"')
      
  except Exception as e:
    logging.info('Abnormal function exit')
    oBandwidthGroup = None
    text = 'Exception trying to fill the object oBandwidthGroup. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oBandwidthGroup

def i_fill_partial_bandwidth_group(oVBoxBandwidthGroup, select):
  logging.info('Enter function ')
  oBandwidthGroup = BandwidthGroup()
  try:
    if oVBoxBandwidthGroup is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='name':
            try:
              oBandwidthGroup.name = oVBoxBandwidthGroup.name
            except Exception as e:
              logging.info('Error getting the attribute "name"')
              raise Exception('Error getting the attribute "name"')
            
          if currAttr=='type':
            try:
              oBandwidthGroup.type = ctx[ 'global'].getEnumValueName('BandwidthGroupType', oVBoxBandwidthGroup.type)
            except Exception as e:
              logging.info('Error getting the attribute "type"')
              raise Exception('Error getting the array of "type"')
            
          if currAttr=='reference':
            try:
              oBandwidthGroup.reference = oVBoxBandwidthGroup.reference
            except Exception as e:
              logging.info('Error getting the attribute "reference"')
              raise Exception('Error getting the attribute "reference"')
            
          if currAttr=='maxBytesPerSec':
            try:
              oBandwidthGroup.max_bytes_per_sec = oVBoxBandwidthGroup.maxBytesPerSec
            except Exception as e:
              logging.info('Error getting the attribute "maxBytesPerSec"')
              raise Exception('Error getting the attribute "maxBytesPerSec"')
            
  except Exception as e:
    logging.info('Abnormal function exit')
    oBandwidthGroup = None
    text = 'Exception trying to fill the object oBandwidthGroup. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oBandwidthGroup


def i_fill_medium(oVBoxMedium, select=None):
  """Convert the passed VirtualBox object oVBoxMedium with interface IMedium into Swagger object oMedium"""

  logging.info('Enter function ')
  oMedium = Medium()
  try:
    if oVBoxMedium is not None:
      if select is not None and len(select)>0:
        oMedium = i_fill_partial_medium(oVBoxMedium, select)
      else:
        oMedium = i_fill_whole_medium(oVBoxMedium)

  except COMException as e:
    logging.info('Abnormal function exit')
    oMedium = None
    text = 'Exception trying to convert the VirtualBox object oVBoxMedium into Swagger object oMedium. '
    exceptionText = str(e)
    raise COMException(e.errno, text +  ' {Original: ' + exceptionText + '} ')

  except Exception as e:
    logging.info('Abnormal function exit')
    oMedium = None
    text = 'Exception trying to convert the VirtualBox object oVBoxMedium into Swagger object oMedium. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oMedium

def i_fill_whole_medium(oVBoxMedium):
  logging.info('Enter function ')
  oMedium = Medium()
  try:
    if oVBoxMedium is not None:
      try:
        oMedium.id = oVBoxMedium.id
      except Exception as e:
        logging.info('Error getting the attribute "id"')
      try:
        oMedium.description = oVBoxMedium.description
      except Exception as e:
        logging.info('Error getting the attribute "description"')
      try:
        oMedium.state = ctx[ 'global'].getEnumValueName('MediumState', oVBoxMedium.state)
      except Exception as e:
        logging.info('Error getting the attribute "state"')
      try:
        ol_variant = ctx['global'].getArray(oVBoxMedium, 'variant')
        oMedium.variant = list()
        for count, item in enumerate(ol_variant):
          o = ctx['global'].getEnumValueName('MediumVariant', item)
          if oMedium.variant.count(o) == 0 : oMedium.variant.append(o)
      except Exception as e:
        logging.info('Error getting the array of "variant"')
      try:
        oMedium.location = oVBoxMedium.location
      except Exception as e:
        logging.info('Error getting the attribute "location"')
      try:
        oMedium.name = oVBoxMedium.name
      except Exception as e:
        logging.info('Error getting the attribute "name"')
      try:
        oMedium.device_type = ctx[ 'global'].getEnumValueName('DeviceType', oVBoxMedium.deviceType)
      except Exception as e:
        logging.info('Error getting the attribute "deviceType"')
      try:
        oMedium.host_drive = oVBoxMedium.hostDrive
      except Exception as e:
        logging.info('Error getting the attribute "hostDrive"')
      try:
        oMedium.size = oVBoxMedium.size
      except Exception as e:
        logging.info('Error getting the attribute "size"')
      try:
        oMedium.format = oVBoxMedium.format
      except Exception as e:
        logging.info('Error getting the attribute "format"')
      try:
        o_medium_format = oVBoxMedium.mediumFormat if oVBoxMedium.mediumFormat is not None else None
        if o_medium_format is not None:
          oMedium.medium_format = i_fill_medium_format(o_medium_format)
        else:
          oMedium.medium_format = None
      except Exception as e:
        logging.info('Error getting the interface object "mediumFormat"')
      try:
        oMedium.type = ctx[ 'global'].getEnumValueName('MediumType', oVBoxMedium.type)
      except Exception as e:
        logging.info('Error getting the attribute "type"')
      try:
        ol_allowed_types = ctx['global'].getArray(oVBoxMedium, 'allowedTypes')
        oMedium.allowed_types = list()
        for count, item in enumerate(ol_allowed_types):
          o = ctx['global'].getEnumValueName('MediumType', item)
          if oMedium.allowed_types.count(o) == 0 : oMedium.allowed_types.append(o)
      except Exception as e:
        logging.info('Error getting the array of "allowedTypes"')
      try:
        oMedium.parent = oVBoxMedium.parent.id
      except Exception as e:
        logging.info('Error getting the attribute "parent"')
      try:
        ol_children = ctx['global'].getArray(oVBoxMedium,'children')
        oMedium.children = list()
        for count, item in enumerate(ol_children):
          oMedium.children.append(item.id)
      except Exception as e:
        logging.info('Error getting the array of "children"')
      try:
        oMedium.base = oVBoxMedium.base.id
      except Exception as e:
        logging.info('Error getting the attribute "base"')
      try:
        oMedium.read_only = oVBoxMedium.readOnly
      except Exception as e:
        logging.info('Error getting the attribute "readOnly"')
      try:
        oMedium.logical_size = oVBoxMedium.logicalSize
      except Exception as e:
        logging.info('Error getting the attribute "logicalSize"')
      try:
        oMedium.auto_reset = oVBoxMedium.autoReset
      except Exception as e:
        logging.info('Error getting the attribute "autoReset"')
      try:
        oMedium.last_access_error = oVBoxMedium.lastAccessError
      except Exception as e:
        logging.info('Error getting the attribute "lastAccessError"')
      try:
        ol_machine_ids = ctx['global'].getArray(oVBoxMedium,'machineIds')
        oMedium.machine_ids = list()
        for count, item in enumerate(ol_machine_ids):
          oMedium.machine_ids.append(item)
      except Exception as e:
        logging.info('Error getting the array of "machineIds"')
      
  except Exception as e:
    logging.info('Abnormal function exit')
    oMedium = None
    text = 'Exception trying to fill the object oMedium. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oMedium

def i_fill_partial_medium(oVBoxMedium, select):
  logging.info('Enter function ')
  oMedium = Medium()
  try:
    if oVBoxMedium is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='id':
            try:
              oMedium.id = oVBoxMedium.id
            except Exception as e:
              logging.info('Error getting the attribute "id"')
              raise Exception('Error getting the attribute "id"')
            
          if currAttr=='description':
            try:
              oMedium.description = oVBoxMedium.description
            except Exception as e:
              logging.info('Error getting the attribute "description"')
              raise Exception('Error getting the attribute "description"')
            
          if currAttr=='state':
            try:
              oMedium.state = ctx[ 'global'].getEnumValueName('MediumState', oVBoxMedium.state)
            except Exception as e:
              logging.info('Error getting the attribute "state"')
              raise Exception('Error getting the array of "state"')
            
          if currAttr=='variant':
            try:
              ol_variant = ctx['global'].getArray(oVBoxMedium, 'variant')
              oMedium.variant = list()
              for count, item in enumerate(ol_variant):
                o = ctx['global'].getEnumValueName('MediumVariant', item)
                if oMedium.variant.count(o) == 0 : oMedium.variant.append(o)
            except Exception as e:
              logging.info('Error getting the array of "variant"')
              raise Exception('Error getting the array of "variant"')
            
          if currAttr=='location':
            try:
              oMedium.location = oVBoxMedium.location
            except Exception as e:
              logging.info('Error getting the attribute "location"')
              raise Exception('Error getting the attribute "location"')
            
          if currAttr=='name':
            try:
              oMedium.name = oVBoxMedium.name
            except Exception as e:
              logging.info('Error getting the attribute "name"')
              raise Exception('Error getting the attribute "name"')
            
          if currAttr=='deviceType':
            try:
              oMedium.device_type = ctx[ 'global'].getEnumValueName('DeviceType', oVBoxMedium.deviceType)
            except Exception as e:
              logging.info('Error getting the attribute "deviceType"')
              raise Exception('Error getting the array of "deviceType"')
            
          if currAttr=='hostDrive':
            try:
              oMedium.host_drive = oVBoxMedium.hostDrive
            except Exception as e:
              logging.info('Error getting the attribute "hostDrive"')
              raise Exception('Error getting the attribute "hostDrive"')
            
          if currAttr=='size':
            try:
              oMedium.size = oVBoxMedium.size
            except Exception as e:
              logging.info('Error getting the attribute "size"')
              raise Exception('Error getting the attribute "size"')
            
          if currAttr=='format':
            try:
              oMedium.format = oVBoxMedium.format
            except Exception as e:
              logging.info('Error getting the attribute "format"')
              raise Exception('Error getting the attribute "format"')
            
          if currAttr=='mediumFormat':
            try:
              o_medium_format = oVBoxMedium.mediumFormat if oVBoxMedium.mediumFormat is not None else None
              if o_medium_format is not None:
                oMedium.medium_format = i_fill_medium_format(o_medium_format)
              else:
                oMedium.medium_format = None
            except Exception as e:
              logging.info('Error getting the interface object "mediumFormat"')
              raise Exception('Error getting the interface object "mediumFormat"')
            
          if currAttr=='type':
            try:
              oMedium.type = ctx[ 'global'].getEnumValueName('MediumType', oVBoxMedium.type)
            except Exception as e:
              logging.info('Error getting the attribute "type"')
              raise Exception('Error getting the array of "type"')
            
          if currAttr=='allowedTypes':
            try:
              ol_allowed_types = ctx['global'].getArray(oVBoxMedium, 'allowedTypes')
              oMedium.allowed_types = list()
              for count, item in enumerate(ol_allowed_types):
                o = ctx['global'].getEnumValueName('MediumType', item)
                if oMedium.allowed_types.count(o) == 0 : oMedium.allowed_types.append(o)
            except Exception as e:
              logging.info('Error getting the array of "allowedTypes"')
              raise Exception('Error getting the array of "allowedTypes"')
            
          if currAttr=='parent':
            try:
              oMedium.parent = oVBoxMedium.parent.id
            except Exception as e:
              logging.info('Error getting the attribute "parent"')
              raise Exception('Error getting the attribute "parent"')
            
          if currAttr=='children':
            try:
              ol_children = ctx['global'].getArray(oVBoxMedium,'children')
              oMedium.children = list()
              for count, item in enumerate(ol_children):
                oMedium.children.append(item.id)
            except Exception as e:
              logging.info('Error getting the array of "children"')
              raise Exception('Error getting the array of "children"')
            
          if currAttr=='base':
            try:
              oMedium.base = oVBoxMedium.base.id
            except Exception as e:
              logging.info('Error getting the attribute "base"')
              raise Exception('Error getting the attribute "base"')
            
          if currAttr=='readOnly':
            try:
              oMedium.read_only = oVBoxMedium.readOnly
            except Exception as e:
              logging.info('Error getting the attribute "readOnly"')
              raise Exception('Error getting the attribute "readOnly"')
            
          if currAttr=='logicalSize':
            try:
              oMedium.logical_size = oVBoxMedium.logicalSize
            except Exception as e:
              logging.info('Error getting the attribute "logicalSize"')
              raise Exception('Error getting the attribute "logicalSize"')
            
          if currAttr=='autoReset':
            try:
              oMedium.auto_reset = oVBoxMedium.autoReset
            except Exception as e:
              logging.info('Error getting the attribute "autoReset"')
              raise Exception('Error getting the attribute "autoReset"')
            
          if currAttr=='lastAccessError':
            try:
              oMedium.last_access_error = oVBoxMedium.lastAccessError
            except Exception as e:
              logging.info('Error getting the attribute "lastAccessError"')
              raise Exception('Error getting the attribute "lastAccessError"')
            
          if currAttr=='machineIds':
            try:
              ol_machine_ids = ctx['global'].getArray(oVBoxMedium,'machineIds')
              oMedium.machine_ids = list()
              for count, item in enumerate(ol_machine_ids):
                oMedium.machine_ids.append(item)
            except Exception as e:
              logging.info('Error getting the array of "machineIds"')
              raise Exception('Error getting the array of "machineIds"')
            
  except Exception as e:
    logging.info('Abnormal function exit')
    oMedium = None
    text = 'Exception trying to fill the object oMedium. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oMedium


def i_fill_medium_format(oVBoxMediumFormat, select=None):
  """Convert the passed VirtualBox object oVBoxMediumFormat with interface IMediumFormat into Swagger object oMediumFormat"""

  logging.info('Enter function ')
  oMediumFormat = MediumFormat()
  try:
    if oVBoxMediumFormat is not None:
      if select is not None and len(select)>0:
        oMediumFormat = i_fill_partial_medium_format(oVBoxMediumFormat, select)
      else:
        oMediumFormat = i_fill_whole_medium_format(oVBoxMediumFormat)
  except Exception as e:
    logging.info('Abnormal function exit')
    oMediumFormat = None
    text = 'Exception trying to convert the VirtualBox object oVBoxMediumFormat into Swagger object oMediumFormat. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oMediumFormat

def i_fill_whole_medium_format(oVBoxMediumFormat):
  logging.info('Enter function ')
  oMediumFormat = MediumFormat()
  try:
    if oVBoxMediumFormat is not None:
      try:
        oMediumFormat.id = oVBoxMediumFormat.id
      except Exception as e:
        logging.info('Error getting the attribute "id"')
      try:
        oMediumFormat.name = oVBoxMediumFormat.name
      except Exception as e:
        logging.info('Error getting the attribute "name"')
      try:
        ol_capabilities = ctx['global'].getArray(oVBoxMediumFormat, 'capabilities')
        oMediumFormat.capabilities = list()
        for count, item in enumerate(ol_capabilities):
          o = ctx['global'].getEnumValueName('MediumFormatCapabilities', item)
          if oMediumFormat.capabilities.count(o) == 0 : oMediumFormat.capabilities.append(o)
      except Exception as e:
        logging.info('Error getting the array of "capabilities"')
      
  except Exception as e:
    logging.info('Abnormal function exit')
    oMediumFormat = None
    text = 'Exception trying to fill the object oMediumFormat. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oMediumFormat

def i_fill_partial_medium_format(oVBoxMediumFormat, select):
  logging.info('Enter function ')
  oMediumFormat = MediumFormat()
  try:
    if oVBoxMediumFormat is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='id':
            try:
              oMediumFormat.id = oVBoxMediumFormat.id
            except Exception as e:
              logging.info('Error getting the attribute "id"')
              raise Exception('Error getting the attribute "id"')
            
          if currAttr=='name':
            try:
              oMediumFormat.name = oVBoxMediumFormat.name
            except Exception as e:
              logging.info('Error getting the attribute "name"')
              raise Exception('Error getting the attribute "name"')
            
          if currAttr=='capabilities':
            try:
              ol_capabilities = ctx['global'].getArray(oVBoxMediumFormat, 'capabilities')
              oMediumFormat.capabilities = list()
              for count, item in enumerate(ol_capabilities):
                o = ctx['global'].getEnumValueName('MediumFormatCapabilities', item)
                if oMediumFormat.capabilities.count(o) == 0 : oMediumFormat.capabilities.append(o)
            except Exception as e:
              logging.info('Error getting the array of "capabilities"')
              raise Exception('Error getting the array of "capabilities"')
            
  except Exception as e:
    logging.info('Abnormal function exit')
    oMediumFormat = None
    text = 'Exception trying to fill the object oMediumFormat. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oMediumFormat


def i_fill_progress(oVBoxProgress, select=None):
  """Convert the passed VirtualBox object oVBoxProgress with interface IProgress into Swagger object oProgress"""

  logging.info('Enter function ')
  oProgress = Progress()
  try:
    if oVBoxProgress is not None:
      if select is not None and len(select)>0:
        oProgress = i_fill_partial_progress(oVBoxProgress, select)
      else:
        oProgress = i_fill_whole_progress(oVBoxProgress)
  except Exception as e:
    logging.info('Abnormal function exit')
    oProgress = None
    text = 'Exception trying to convert the VirtualBox object oVBoxProgress into Swagger object oProgress. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oProgress

def i_fill_whole_progress(oVBoxProgress):
  logging.info('Enter function ')
  oProgress = Progress()
  try:
    if oVBoxProgress is not None:
      try:
        oProgress.id = oVBoxProgress.id
      except Exception as e:
        logging.info('Error getting the attribute "id"')
      try:
        oProgress.description = oVBoxProgress.description
      except Exception as e:
        logging.info('Error getting the attribute "description"')
      try:
        oProgress.cancelable = oVBoxProgress.cancelable
      except Exception as e:
        logging.info('Error getting the attribute "cancelable"')
      try:
        oProgress.percent = oVBoxProgress.percent
      except Exception as e:
        logging.info('Error getting the attribute "percent"')
      try:
        oProgress.time_remaining = oVBoxProgress.timeRemaining
      except Exception as e:
        logging.info('Error getting the attribute "timeRemaining"')
      try:
        oProgress.completed = oVBoxProgress.completed
      except Exception as e:
        logging.info('Error getting the attribute "completed"')
      try:
        oProgress.canceled = oVBoxProgress.canceled
      except Exception as e:
        logging.info('Error getting the attribute "canceled"')
      try:
        oProgress.result_code = oVBoxProgress.resultCode
      except Exception as e:
        logging.info('Error getting the attribute "resultCode"')
      try:
        o_error_info = oVBoxProgress.errorInfo if oVBoxProgress.errorInfo is not None else None
        if o_error_info is not None:
          oProgress.error_info = i_fill_virtual_box_error_info(o_error_info)
        else:
          oProgress.error_info = None
      except Exception as e:
        logging.info('Error getting the interface object "errorInfo"')
      try:
        oProgress.operation_count = oVBoxProgress.operationCount
      except Exception as e:
        logging.info('Error getting the attribute "operationCount"')
      try:
        oProgress.operation = oVBoxProgress.operation
      except Exception as e:
        logging.info('Error getting the attribute "operation"')
      try:
        oProgress.operation_description = oVBoxProgress.operationDescription
      except Exception as e:
        logging.info('Error getting the attribute "operationDescription"')
      try:
        oProgress.operation_percent = oVBoxProgress.operationPercent
      except Exception as e:
        logging.info('Error getting the attribute "operationPercent"')
      try:
        oProgress.operation_weight = oVBoxProgress.operationWeight
      except Exception as e:
        logging.info('Error getting the attribute "operationWeight"')
      try:
        oProgress.timeout = oVBoxProgress.timeout
      except Exception as e:
        logging.info('Error getting the attribute "timeout"')
      
  except Exception as e:
    logging.info('Abnormal function exit')
    oProgress = None
    text = 'Exception trying to fill the object oProgress. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oProgress

def i_fill_partial_progress(oVBoxProgress, select):
  logging.info('Enter function ')
  oProgress = Progress()
  try:
    if oVBoxProgress is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='id':
            try:
              oProgress.id = oVBoxProgress.id
            except Exception as e:
              logging.info('Error getting the attribute "id"')
              raise Exception('Error getting the attribute "id"')
            
          if currAttr=='description':
            try:
              oProgress.description = oVBoxProgress.description
            except Exception as e:
              logging.info('Error getting the attribute "description"')
              raise Exception('Error getting the attribute "description"')
          
          if currAttr=='cancelable':
            try:
              oProgress.cancelable = oVBoxProgress.cancelable
            except Exception as e:
              logging.info('Error getting the attribute "cancelable"')
              raise Exception('Error getting the attribute "cancelable"')
            
          if currAttr=='percent':
            try:
              oProgress.percent = oVBoxProgress.percent
            except Exception as e:
              logging.info('Error getting the attribute "percent"')
              raise Exception('Error getting the attribute "percent"')
            
          if currAttr=='timeRemaining':
            try:
              oProgress.time_remaining = oVBoxProgress.timeRemaining
            except Exception as e:
              logging.info('Error getting the attribute "timeRemaining"')
              raise Exception('Error getting the attribute "timeRemaining"')
            
          if currAttr=='completed':
            try:
              oProgress.completed = oVBoxProgress.completed
            except Exception as e:
              logging.info('Error getting the attribute "completed"')
              raise Exception('Error getting the attribute "completed"')
            
          if currAttr=='canceled':
            try:
              oProgress.canceled = oVBoxProgress.canceled
            except Exception as e:
              logging.info('Error getting the attribute "canceled"')
              raise Exception('Error getting the attribute "canceled"')
            
          if currAttr=='resultCode':
            try:
              oProgress.result_code = oVBoxProgress.resultCode
            except Exception as e:
              logging.info('Error getting the attribute "resultCode"')
              raise Exception('Error getting the attribute "resultCode"')
            
          if currAttr=='errorInfo':
            try:
              o_error_info = oVBoxProgress.errorInfo if oVBoxProgress.errorInfo is not None else None
              if o_error_info is not None:
                oProgress.error_info = i_fill_virtual_box_error_info(o_error_info)
              else:
                oProgress.error_info = None
            except Exception as e:
              logging.info('Error getting the interface object "errorInfo"')
              raise Exception('Error getting the interface object "errorInfo"')
            
          if currAttr=='operationCount':
            try:
              oProgress.operation_count = oVBoxProgress.operationCount
            except Exception as e:
              logging.info('Error getting the attribute "operationCount"')
              raise Exception('Error getting the attribute "operationCount"')
            
          if currAttr=='operation':
            try:
              oProgress.operation = oVBoxProgress.operation
            except Exception as e:
              logging.info('Error getting the attribute "operation"')
              raise Exception('Error getting the attribute "operation"')
            
          if currAttr=='operationDescription':
            try:
              oProgress.operation_description = oVBoxProgress.operationDescription
            except Exception as e:
              logging.info('Error getting the attribute "operationDescription"')
              raise Exception('Error getting the attribute "operationDescription"')
            
          if currAttr=='operationPercent':
            try:
              oProgress.operation_percent = oVBoxProgress.operationPercent
            except Exception as e:
              logging.info('Error getting the attribute "operationPercent"')
              raise Exception('Error getting the attribute "operationPercent"')
            
          if currAttr=='operationWeight':
            try:
              oProgress.operation_weight = oVBoxProgress.operationWeight
            except Exception as e:
              logging.info('Error getting the attribute "operationWeight"')
              raise Exception('Error getting the attribute "operationWeight"')
            
          if currAttr=='timeout':
            try:
              oProgress.timeout = oVBoxProgress.timeout
            except Exception as e:
              logging.info('Error getting the attribute "timeout"')
              raise Exception('Error getting the attribute "timeout"')
            
  except Exception as e:
    logging.info('Abnormal function exit')
    oProgress = None
    text = 'Exception trying to fill the object oProgress. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oProgress


def i_fill_virtual_box_error_info(oVBoxVirtualBoxErrorInfo, select=None):
  """Convert the passed VirtualBox object oVBoxVirtualBoxErrorInfo with interface IVirtualBoxErrorInfo into Swagger object oVirtualBoxErrorInfo"""

  logging.info('Enter function ')
  oVirtualBoxErrorInfo = VirtualBoxErrorInfo()
  try:
    if oVBoxVirtualBoxErrorInfo is not None:
      if select is not None and len(select)>0:
        oVirtualBoxErrorInfo = i_fill_partial_virtual_box_error_info(oVBoxVirtualBoxErrorInfo, select)
      else:
        oVirtualBoxErrorInfo = i_fill_whole_virtual_box_error_info(oVBoxVirtualBoxErrorInfo)
  except Exception as e:
    logging.info('Abnormal function exit')
    oVirtualBoxErrorInfo = None
    text = 'Exception trying to convert the VirtualBox object oVBoxVirtualBoxErrorInfo into Swagger object oVirtualBoxErrorInfo. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oVirtualBoxErrorInfo

def i_fill_whole_virtual_box_error_info(oVBoxVirtualBoxErrorInfo):
  logging.info('Enter function ')
  oVirtualBoxErrorInfo = VirtualBoxErrorInfo()
  try:
    if oVBoxVirtualBoxErrorInfo is not None:
      try:
        oVirtualBoxErrorInfo.result_code = oVBoxVirtualBoxErrorInfo.resultCode
      except Exception as e:
        logging.info('Error getting the attribute "resultCode"')
      try:
        oVirtualBoxErrorInfo.result_detail = oVBoxVirtualBoxErrorInfo.resultDetail
      except Exception as e:
        logging.info('Error getting the attribute "resultDetail"')
      try:
        oVirtualBoxErrorInfo.interface_id = oVBoxVirtualBoxErrorInfo.interfaceID
      except Exception as e:
        logging.info('Error getting the attribute "interfaceID"')
      try:
        oVirtualBoxErrorInfo.component = oVBoxVirtualBoxErrorInfo.component
      except Exception as e:
        logging.info('Error getting the attribute "component"')
      try:
        oVirtualBoxErrorInfo.text = oVBoxVirtualBoxErrorInfo.text
      except Exception as e:
        logging.info('Error getting the attribute "text"')
      try:
        o_next = oVBoxVirtualBoxErrorInfo.next if oVBoxVirtualBoxErrorInfo.next is not None else None
        if o_next is not None:
          oVirtualBoxErrorInfo.next = i_fill_virtual_box_error_info(o_next)
        else:
          oVirtualBoxErrorInfo.next = None
      except Exception as e:
        logging.info('Error getting the interface object "next"')

  except Exception as e:
    logging.info('Abnormal function exit')
    oVirtualBoxErrorInfo = None
    text = 'Exception trying to fill the object oVirtualBoxErrorInfo. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oVirtualBoxErrorInfo

def i_fill_partial_virtual_box_error_info(oVBoxVirtualBoxErrorInfo, select):
  logging.info('Enter function ')
  oVirtualBoxErrorInfo = VirtualBoxErrorInfo()
  try:
    if oVBoxVirtualBoxErrorInfo is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='resultCode':
            try:
              oVirtualBoxErrorInfo.result_code = oVBoxVirtualBoxErrorInfo.resultCode
            except Exception as e:
              logging.info('Error getting the attribute "resultCode"')
              raise Exception('Error getting the attribute "resultCode"')

          if currAttr=='resultDetail':
            try:
              oVirtualBoxErrorInfo.result_detail = oVBoxVirtualBoxErrorInfo.resultDetail
            except Exception as e:
              logging.info('Error getting the attribute "resultDetail"')
              raise Exception('Error getting the attribute "resultDetail"')

          if currAttr=='interfaceID':
            try:
              oVirtualBoxErrorInfo.interface_id = oVBoxVirtualBoxErrorInfo.interfaceID
            except Exception as e:
              logging.info('Error getting the attribute "interfaceID"')
              raise Exception('Error getting the attribute "interfaceID"')

          if currAttr=='component':
            try:
              oVirtualBoxErrorInfo.component = oVBoxVirtualBoxErrorInfo.component
            except Exception as e:
              logging.info('Error getting the attribute "component"')
              raise Exception('Error getting the attribute "component"')

          if currAttr=='text':
            try:
              oVirtualBoxErrorInfo.text = oVBoxVirtualBoxErrorInfo.text
            except Exception as e:
              logging.info('Error getting the attribute "text"')
              raise Exception('Error getting the attribute "text"')

          if currAttr=='next':
            try:
              o_next = oVBoxVirtualBoxErrorInfo.next if oVBoxVirtualBoxErrorInfo.next is not None else None
              if o_next is not None:
                oVirtualBoxErrorInfo.next = i_fill_virtual_box_error_info(o_next)
              else:
                oVirtualBoxErrorInfo.next = None
            except Exception as e:
              logging.info('Error getting the interface object "next"')
              raise Exception('Error getting the interface object "next"')

  except Exception as e:
    logging.info('Abnormal function exit')
    oVirtualBoxErrorInfo = None
    text = 'Exception trying to fill the object oVirtualBoxErrorInfo. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oVirtualBoxErrorInfo

def i_fill_session(oVBoxSession, select=None):
  """Convert the passed VirtualBox object oVBoxSession with interface ISession into Swagger object oSession"""

  logging.info('Enter function ')
  oSession = Session()
  try:
    if oVBoxSession is not None:
      if select is not None and len(select)>0:
        oSession = i_fill_partial_session(oVBoxSession, select)
      else:
        oSession = i_fill_whole_session(oVBoxSession)
  except Exception as e:
    logging.info('Abnormal function exit')
    oSession = None
    text = 'Exception trying to convert the VirtualBox object oVBoxSession into Swagger object oSession. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oSession

def i_fill_whole_session(oVBoxSession):
  logging.info('Enter function ')
  oSession = Session()
  try:
    if oVBoxSession is not None:
      try:
        oSession.state = ctx[ 'global'].getEnumValueName('SessionState', oVBoxSession.state)
      except Exception as e:
        logging.info('Error getting the attribute "state"')
      try:
        oSession.type = ctx[ 'global'].getEnumValueName('SessionType', oVBoxSession.type)
      except Exception as e:
        logging.info('Error getting the attribute "type"')
      try:
        oSession.name = oVBoxSession.name
      except Exception as e:
        logging.info('Error getting the attribute "name"')
      try:
        oSession.machine = oVBoxSession.machine.id
      except Exception as e:
        logging.info('Error getting the attribute "machine"')
      
  except Exception as e:
    logging.info('Abnormal function exit')
    oSession = None
    text = 'Exception trying to fill the object oSession. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oSession

def i_fill_partial_session(oVBoxSession, select):
  logging.info('Enter function ')
  oSession = Session()
  try:
    if oVBoxSession is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='state':
            try:
              oSession.state = ctx[ 'global'].getEnumValueName('SessionState', oVBoxSession.state)
            except Exception as e:
              logging.info('Error getting the attribute "state"')
              raise Exception('Error getting the array of "state"')
            
          if currAttr=='type':
            try:
              oSession.type = ctx[ 'global'].getEnumValueName('SessionType', oVBoxSession.type)
            except Exception as e:
              logging.info('Error getting the attribute "type"')
              raise Exception('Error getting the array of "type"')
            
          if currAttr=='name':
            try:
              oSession.name = oVBoxSession.name
            except Exception as e:
              logging.info('Error getting the attribute "name"')
              raise Exception('Error getting the attribute "name"')
            
          if currAttr=='machine':
            try:
              oSession.machine = oVBoxSession.machine.id
            except Exception as e:
              logging.info('Error getting the attribute "machine"')
              raise Exception('Error getting the attribute "machine"')
            
  except Exception as e:
    logging.info('Abnormal function exit')
    oSession = None
    text = 'Exception trying to fill the object oSession. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oSession

def i_fill_usb_device(oVBoxUSBDevice, select=None):
  """Convert the passed VirtualBox object oVBoxUSBDevice with interface IUSBDevice into Swagger object oUSBDevice"""
  
  logging.info('Enter function ')
  oUSBDevice = USBDevice()
  try:
    if oVBoxUSBDevice is not None:
      if select is not None and len(select)>0:
        oUSBDevice = i_fill_partial_usb_device(oVBoxUSBDevice, select)
      else:
        oUSBDevice = i_fill_whole_usb_device(oVBoxUSBDevice)
  except Exception as e:
    logging.info('Abnormal function exit')
    oUSBDevice = None
    text = 'Exception trying to convert the VirtualBox object oVBoxUSBDevice into Swagger object oUSBDevice. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oUSBDevice

def i_fill_whole_usb_device(oVBoxUSBDevice):
  logging.info('Enter function ')
  oUSBDevice = USBDevice()
  try:
    if oVBoxUSBDevice is not None:
      try:
        oUSBDevice.id = oVBoxUSBDevice.id
      except Exception as e:
        logging.info('Error getting the attribute "id"')
      try:
        oUSBDevice.vendor_id = oVBoxUSBDevice.vendorId
      except Exception as e:
        logging.info('Error getting the attribute "vendorId"')
      try:
        oUSBDevice.product_id = oVBoxUSBDevice.productId
      except Exception as e:
        logging.info('Error getting the attribute "productId"')
      try:
        oUSBDevice.revision = oVBoxUSBDevice.revision
      except Exception as e:
        logging.info('Error getting the attribute "revision"')
      try:
        oUSBDevice.manufacturer = oVBoxUSBDevice.manufacturer
      except Exception as e:
        logging.info('Error getting the attribute "manufacturer"')
      try:
        oUSBDevice.product = oVBoxUSBDevice.product
      except Exception as e:
        logging.info('Error getting the attribute "product"')
      try:
        oUSBDevice.serial_number = oVBoxUSBDevice.serialNumber
      except Exception as e:
        logging.info('Error getting the attribute "serialNumber"')
      try:
        oUSBDevice.address = oVBoxUSBDevice.address
      except Exception as e:
        logging.info('Error getting the attribute "address"')
      try:
        oUSBDevice.port = oVBoxUSBDevice.port
      except Exception as e:
        logging.info('Error getting the attribute "port"')
      try:
        oUSBDevice.port_path = oVBoxUSBDevice.portPath
      except Exception as e:
        logging.info('Error getting the attribute "portPath"')
      try:
        oUSBDevice.version = oVBoxUSBDevice.version
      except Exception as e:
        logging.info('Error getting the attribute "version"')
      try:
        oUSBDevice.speed = ctx[ 'global'].getEnumValueName('USBConnectionSpeed', oVBoxUSBDevice.speed)
      except Exception as e:
        logging.info('Error getting the attribute "speed"')
      try:
        oUSBDevice.remote = oVBoxUSBDevice.remote
      except Exception as e:
        logging.info('Error getting the attribute "remote"')
      try:
        ol_device_info = ctx['global'].getArray(oVBoxUSBDevice,'deviceInfo')
        oUSBDevice.device_info = list()
        for count, item in enumerate(ol_device_info):
          oUSBDevice.device_info.append(item)
      except Exception as e:
        logging.info('Error getting the array of "deviceInfo"')
      try:
        oUSBDevice.backend = oVBoxUSBDevice.backend
      except Exception as e:
        logging.info('Error getting the attribute "backend"')
      
  except Exception as e:
    logging.info('Abnormal function exit')
    oUSBDevice = None
    text = 'Exception trying to fill the object oUSBDevice. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oUSBDevice

def i_fill_partial_usb_device(oVBoxUSBDevice, select):
  logging.info('Enter function ')
  oUSBDevice = USBDevice()
  try:
    if oVBoxUSBDevice is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='id':
            try:
              oUSBDevice.id = oVBoxUSBDevice.id
            except Exception as e:
              logging.info('Error getting the attribute "id"')
              raise Exception('Error getting the attribute "id"')
            
          if currAttr=='vendorId':
            try:
              oUSBDevice.vendor_id = oVBoxUSBDevice.vendorId
            except Exception as e:
              logging.info('Error getting the attribute "vendorId"')
              raise Exception('Error getting the attribute "vendorId"')
            
          if currAttr=='productId':
            try:
              oUSBDevice.product_id = oVBoxUSBDevice.productId
            except Exception as e:
              logging.info('Error getting the attribute "productId"')
              raise Exception('Error getting the attribute "productId"')
            
          if currAttr=='revision':
            try:
              oUSBDevice.revision = oVBoxUSBDevice.revision
            except Exception as e:
              logging.info('Error getting the attribute "revision"')
              raise Exception('Error getting the attribute "revision"')
            
          if currAttr=='manufacturer':
            try:
              oUSBDevice.manufacturer = oVBoxUSBDevice.manufacturer
            except Exception as e:
              logging.info('Error getting the attribute "manufacturer"')
              raise Exception('Error getting the attribute "manufacturer"')
            
          if currAttr=='product':
            try:
              oUSBDevice.product = oVBoxUSBDevice.product
            except Exception as e:
              logging.info('Error getting the attribute "product"')
              raise Exception('Error getting the attribute "product"')
            
          if currAttr=='serialNumber':
            try:
              oUSBDevice.serial_number = oVBoxUSBDevice.serialNumber
            except Exception as e:
              logging.info('Error getting the attribute "serialNumber"')
              raise Exception('Error getting the attribute "serialNumber"')
            
          if currAttr=='address':
            try:
              oUSBDevice.address = oVBoxUSBDevice.address
            except Exception as e:
              logging.info('Error getting the attribute "address"')
              raise Exception('Error getting the attribute "address"')
            
          if currAttr=='port':
            try:
              oUSBDevice.port = oVBoxUSBDevice.port
            except Exception as e:
              logging.info('Error getting the attribute "port"')
              raise Exception('Error getting the attribute "port"')
            
          if currAttr=='portPath':
            try:
              oUSBDevice.port_path = oVBoxUSBDevice.portPath
            except Exception as e:
              logging.info('Error getting the attribute "portPath"')
              raise Exception('Error getting the attribute "portPath"')
            
          if currAttr=='version':
            try:
              oUSBDevice.version = oVBoxUSBDevice.version
            except Exception as e:
              logging.info('Error getting the attribute "version"')
              raise Exception('Error getting the attribute "version"')
            
          if currAttr=='speed':
            try:
              oUSBDevice.speed = ctx[ 'global'].getEnumValueName('USBConnectionSpeed', oVBoxUSBDevice.speed)
            except Exception as e:
              logging.info('Error getting the attribute "speed"')
              raise Exception('Error getting the array of "speed"')
            
          if currAttr=='remote':
            try:
              oUSBDevice.remote = oVBoxUSBDevice.remote
            except Exception as e:
              logging.info('Error getting the attribute "remote"')
              raise Exception('Error getting the attribute "remote"')
            
          if currAttr=='deviceInfo':
            try:
              ol_device_info = ctx['global'].getArray(oVBoxUSBDevice,'deviceInfo')
              oUSBDevice.device_info = list()
              for count, item in enumerate(ol_device_info):
                oUSBDevice.device_info.append(item)
            except Exception as e:
              logging.info('Error getting the array of "deviceInfo"')
              raise Exception('Error getting the array of "deviceInfo"')
            
          if currAttr=='backend':
            try:
              oUSBDevice.backend = oVBoxUSBDevice.backend
            except Exception as e:
              logging.info('Error getting the attribute "backend"')
              raise Exception('Error getting the attribute "backend"')
            
  except Exception as e:
    logging.info('Abnormal function exit')
    oUSBDevice = None
    text = 'Exception trying to fill the object oUSBDevice. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oUSBDevice

def i_fill_serial_port(oVBoxSerialPort, select=None):
  """Convert the passed VirtualBox object oVBoxSerialPort with interface ISerialPort into Swagger object oSerialPort"""
  
  logging.info('Enter function ')
  oSerialPort = SerialPort()
  try:
    if oVBoxSerialPort is not None:
      if select is not None and len(select)>0:
        oSerialPort = i_fill_partial_serial_port(oVBoxSerialPort, select)
      else:
        oSerialPort = i_fill_whole_serial_port(oVBoxSerialPort)
  except Exception as e:
    logging.info('Abnormal function exit')
    oSerialPort = None
    text = 'Exception trying to convert the VirtualBox object oVBoxSerialPort into Swagger object oSerialPort. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oSerialPort

def i_fill_whole_serial_port(oVBoxSerialPort):
  logging.info('Enter function ')
  oSerialPort = SerialPort()
  try:
    if oVBoxSerialPort is not None:
      try:
        oSerialPort.slot = oVBoxSerialPort.slot
      except Exception as e:
        logging.info('Error getting the attribute "slot"')
      try:
        oSerialPort.enabled = oVBoxSerialPort.enabled
      except Exception as e:
        logging.info('Error getting the attribute "enabled"')
      try:
        oSerialPort.io_address = oVBoxSerialPort.IOAddress
      except Exception as e:
        logging.info('Error getting the attribute "IOAddress"')
      try:
        oSerialPort.irq = oVBoxSerialPort.IRQ
      except Exception as e:
        logging.info('Error getting the attribute "IRQ"')
      try:
        oSerialPort.host_mode = ctx[ 'global'].getEnumValueName('PortMode', oVBoxSerialPort.hostMode)
      except Exception as e:
        logging.info('Error getting the attribute "hostMode"')
      try:
        oSerialPort.server = oVBoxSerialPort.server
      except Exception as e:
        logging.info('Error getting the attribute "server"')
      try:
        oSerialPort.path = oVBoxSerialPort.path
      except Exception as e:
        logging.info('Error getting the attribute "path"')
      try:
        oSerialPort.uart_type = ctx[ 'global'].getEnumValueName('UartType', oVBoxSerialPort.uartType)
      except Exception as e:
        logging.info('Error getting the attribute "uartType"')
      
  except Exception as e:
    logging.info('Abnormal function exit')
    oSerialPort = None
    text = 'Exception trying to fill the object oSerialPort. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oSerialPort

def i_fill_partial_serial_port(oVBoxSerialPort, select):
  logging.info('Enter function ')
  oSerialPort = SerialPort()
  try:
    if oVBoxSerialPort is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='slot':
            try:
              oSerialPort.slot = oVBoxSerialPort.slot
            except Exception as e:
              logging.info('Error getting the attribute "slot"')
              raise Exception('Error getting the attribute "slot"')
            
          if currAttr=='enabled':
            try:
              oSerialPort.enabled = oVBoxSerialPort.enabled
            except Exception as e:
              logging.info('Error getting the attribute "enabled"')
              raise Exception('Error getting the attribute "enabled"')
            
          if currAttr=='IOAddress':
            try:
              oSerialPort.io_address = oVBoxSerialPort.IOAddress
            except Exception as e:
              logging.info('Error getting the attribute "IOAddress"')
              raise Exception('Error getting the attribute "IOAddress"')
            
          if currAttr=='IRQ':
            try:
              oSerialPort.irq = oVBoxSerialPort.IRQ
            except Exception as e:
              logging.info('Error getting the attribute "IRQ"')
              raise Exception('Error getting the attribute "IRQ"')
            
          if currAttr=='hostMode':
            try:
              oSerialPort.host_mode = ctx[ 'global'].getEnumValueName('PortMode', oVBoxSerialPort.hostMode)
            except Exception as e:
              logging.info('Error getting the attribute "hostMode"')
              raise Exception('Error getting the array of "hostMode"')
            
          if currAttr=='server':
            try:
              oSerialPort.server = oVBoxSerialPort.server
            except Exception as e:
              logging.info('Error getting the attribute "server"')
              raise Exception('Error getting the attribute "server"')
            
          if currAttr=='path':
            try:
              oSerialPort.path = oVBoxSerialPort.path
            except Exception as e:
              logging.info('Error getting the attribute "path"')
              raise Exception('Error getting the attribute "path"')
            
          if currAttr=='uartType':
            try:
              oSerialPort.uart_type = ctx[ 'global'].getEnumValueName('UartType', oVBoxSerialPort.uartType)
            except Exception as e:
              logging.info('Error getting the attribute "uartType"')
              raise Exception('Error getting the array of "uartType"')
            
  except Exception as e:
    logging.info('Abnormal function exit')
    oSerialPort = None
    text = 'Exception trying to fill the object oSerialPort. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oSerialPort

def i_fill_parallel_port(oVBoxParallelPort, select=None):
  """Convert the passed VirtualBox object oVBoxParallelPort with interface IParallelPort into Swagger object oParallelPort"""
  
  logging.info('Enter function ')
  oParallelPort = ParallelPort()
  try:
    if oVBoxParallelPort is not None:
      if select is not None and len(select)>0:
        oParallelPort = i_fill_partial_parallel_port(oVBoxParallelPort, select)
      else:
        oParallelPort = i_fill_whole_parallel_port(oVBoxParallelPort)
  except Exception as e:
    logging.info('Abnormal function exit')
    oParallelPort = None
    text = 'Exception trying to convert the VirtualBox object oVBoxParallelPort into Swagger object oParallelPort. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oParallelPort

def i_fill_whole_parallel_port(oVBoxParallelPort):
  logging.info('Enter function ')
  oParallelPort = ParallelPort()
  try:
    if oVBoxParallelPort is not None:
      try:
        oParallelPort.slot = oVBoxParallelPort.slot
      except Exception as e:
        logging.info('Error getting the attribute "slot"')
      try:
        oParallelPort.enabled = oVBoxParallelPort.enabled
      except Exception as e:
        logging.info('Error getting the attribute "enabled"')
      try:
        oParallelPort.io_base = oVBoxParallelPort.IOBase
      except Exception as e:
        logging.info('Error getting the attribute "IOBase"')
      try:
        oParallelPort.irq = oVBoxParallelPort.IRQ
      except Exception as e:
        logging.info('Error getting the attribute "IRQ"')
      try:
        oParallelPort.path = oVBoxParallelPort.path
      except Exception as e:
        logging.info('Error getting the attribute "path"')
      
  except Exception as e:
    logging.info('Abnormal function exit')
    oParallelPort = None
    text = 'Exception trying to fill the object oParallelPort. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oParallelPort

def i_fill_partial_parallel_port(oVBoxParallelPort, select):
  logging.info('Enter function ')
  oParallelPort = ParallelPort()
  try:
    if oVBoxParallelPort is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='slot':
            try:
              oParallelPort.slot = oVBoxParallelPort.slot
            except Exception as e:
              logging.info('Error getting the attribute "slot"')
              raise Exception('Error getting the attribute "slot"')
            
          if currAttr=='enabled':
            try:
              oParallelPort.enabled = oVBoxParallelPort.enabled
            except Exception as e:
              logging.info('Error getting the attribute "enabled"')
              raise Exception('Error getting the attribute "enabled"')
            
          if currAttr=='IOBase':
            try:
              oParallelPort.io_base = oVBoxParallelPort.IOBase
            except Exception as e:
              logging.info('Error getting the attribute "IOBase"')
              raise Exception('Error getting the attribute "IOBase"')
            
          if currAttr=='IRQ':
            try:
              oParallelPort.irq = oVBoxParallelPort.IRQ
            except Exception as e:
              logging.info('Error getting the attribute "IRQ"')
              raise Exception('Error getting the attribute "IRQ"')
            
          if currAttr=='path':
            try:
              oParallelPort.path = oVBoxParallelPort.path
            except Exception as e:
              logging.info('Error getting the attribute "path"')
              raise Exception('Error getting the attribute "path"')
            
  except Exception as e:
    logging.info('Abnormal function exit')
    oParallelPort = None
    text = 'Exception trying to fill the object oParallelPort. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oParallelPort

def i_fill_snapshot(oVBoxSnapshot, select=None):
  """Convert the passed VirtualBox object oVBoxSnapshot with interface ISnapshot into Swagger object oSnapshot"""
  
  logging.info('Enter function ')
  oSnapshot = Snapshot()
  try:
    if oVBoxSnapshot is not None:
      if select is not None and len(select)>0:
        oSnapshot = i_fill_partial_snapshot(oVBoxSnapshot, select)
      else:
        oSnapshot = i_fill_whole_snapshot(oVBoxSnapshot)
  except Exception as e:
    logging.info('Abnormal function exit')
    oSnapshot = None
    text = 'Exception trying to convert the VirtualBox object oVBoxSnapshot into Swagger object oSnapshot. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oSnapshot

def i_fill_whole_snapshot(oVBoxSnapshot):
  logging.info('Enter function ')
  oSnapshot = Snapshot()
  try:
    if oVBoxSnapshot is not None:
      try:
        oSnapshot.id = oVBoxSnapshot.id
      except Exception as e:
        logging.info('Error getting the attribute "id"')
      try:
        oSnapshot.name = oVBoxSnapshot.name
      except Exception as e:
        logging.info('Error getting the attribute "name"')
      try:
        oSnapshot.description = oVBoxSnapshot.description
      except Exception as e:
        logging.info('Error getting the attribute "description"')
      try:
        oSnapshot.time_stamp = oVBoxSnapshot.timeStamp
      except Exception as e:
        logging.info('Error getting the attribute "timeStamp"')
      try:
        oSnapshot.online = oVBoxSnapshot.online
      except Exception as e:
        logging.info('Error getting the attribute "online"')
      try:
        oSnapshot.machine = oVBoxSnapshot.machine.id
      except Exception as e:
        logging.info('Error getting the attribute "machine"')
      try:
        oSnapshot.parent = oVBoxSnapshot.parent.id
      except Exception as e:
        logging.info('Error getting the attribute "parent"')
      try:
        ol_children = ctx['global'].getArray(oVBoxSnapshot,'children')
        oSnapshot.children = list()
        for count, item in enumerate(ol_children):
          oSnapshot.children.append(item.id)
      except Exception as e:
        logging.info('Error getting the array of "children"')
      try:
        oSnapshot.children_count = oVBoxSnapshot.childrenCount
      except Exception as e:
        logging.info('Error getting the attribute "childrenCount"')
      
  except Exception as e:
    logging.info('Abnormal function exit')
    oSnapshot = None
    text = 'Exception trying to fill the object oSnapshot. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oSnapshot

def i_fill_partial_snapshot(oVBoxSnapshot, select):
  logging.info('Enter function ')
  oSnapshot = Snapshot()
  try:
    if oVBoxSnapshot is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='id':
            try:
              oSnapshot.id = oVBoxSnapshot.id
            except Exception as e:
              logging.info('Error getting the attribute "id"')
              raise Exception('Error getting the attribute "id"')
            
          if currAttr=='name':
            try:
              oSnapshot.name = oVBoxSnapshot.name
            except Exception as e:
              logging.info('Error getting the attribute "name"')
              raise Exception('Error getting the attribute "name"')
            
          if currAttr=='description':
            try:
              oSnapshot.description = oVBoxSnapshot.description
            except Exception as e:
              logging.info('Error getting the attribute "description"')
              raise Exception('Error getting the attribute "description"')
            
          if currAttr=='timeStamp':
            try:
              oSnapshot.time_stamp = oVBoxSnapshot.timeStamp
            except Exception as e:
              logging.info('Error getting the attribute "timeStamp"')
              raise Exception('Error getting the attribute "timeStamp"')
            
          if currAttr=='online':
            try:
              oSnapshot.online = oVBoxSnapshot.online
            except Exception as e:
              logging.info('Error getting the attribute "online"')
              raise Exception('Error getting the attribute "online"')
            
          if currAttr=='machine':
            try:
              oSnapshot.machine = oVBoxSnapshot.machine.id
            except Exception as e:
              logging.info('Error getting the attribute "machine"')
              raise Exception('Error getting the attribute "machine"')
            
          if currAttr=='parent':
            try:
              oSnapshot.parent = oVBoxSnapshot.parent.id
            except Exception as e:
              logging.info('Error getting the attribute "parent"')
              raise Exception('Error getting the attribute "parent"')
            
          if currAttr=='children':
            try:
              ol_children = ctx['global'].getArray(oVBoxSnapshot,'children')
              oSnapshot.children = list()
              for count, item in enumerate(ol_children):
                oSnapshot.children.append(item.id)
            except Exception as e:
              logging.info('Error getting the array of "children"')
              raise Exception('Error getting the array of "children"')
            
          if currAttr=='childrenCount':
            try:
              oSnapshot.children_count = oVBoxSnapshot.childrenCount
            except Exception as e:
              logging.info('Error getting the attribute "childrenCount"')
              raise Exception('Error getting the attribute "childrenCount"')
            
  except Exception as e:
    logging.info('Abnormal function exit')
    oSnapshot = None
    text = 'Exception trying to fill the object oSnapshot. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oSnapshot

def i_fill_guest_os_type(oVBoxGuestOSType, select=None):
  """Convert the passed VirtualBox object oVBoxGuestOSType with interface IGuestOSType into Swagger object oGuestOSType"""
  
  logging.info('Enter function ')
  oGuestOSType = GuestOSType()
  try:
    if oVBoxGuestOSType is not None:
      if select is not None and len(select)>0:
        oGuestOSType = i_fill_partial_guest_os_type(oVBoxGuestOSType, select)
      else:
        oGuestOSType = i_fill_whole_guest_os_type(oVBoxGuestOSType)
  except Exception as e:
    logging.info('Abnormal function exit')
    oGuestOSType = None
    text = 'Exception trying to convert the VirtualBox object oVBoxGuestOSType into Swagger object oGuestOSType. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oGuestOSType

def i_fill_whole_guest_os_type(oVBoxGuestOSType):
  logging.info('Enter function ')
  oGuestOSType = GuestOSType()
  try:
    if oVBoxGuestOSType is not None:
      try:
        oGuestOSType.family_id = oVBoxGuestOSType.familyId
      except Exception as e:
        logging.info('Error getting the attribute "familyId"')
      try:
        oGuestOSType.family_description = oVBoxGuestOSType.familyDescription
      except Exception as e:
        logging.info('Error getting the attribute "familyDescription"')
      try:
        oGuestOSType.id = oVBoxGuestOSType.id
      except Exception as e:
        logging.info('Error getting the attribute "id"')
      try:
        oGuestOSType.subtype = oVBoxGuestOSType.subtype
      except Exception as e:
        logging.info('Error getting the attribute "subtype"')
      try:
        oGuestOSType.description = oVBoxGuestOSType.description
      except Exception as e:
        logging.info('Error getting the attribute "description"')
      try:
        oGuestOSType.is64_bit = oVBoxGuestOSType.is64Bit
      except Exception as e:
        logging.info('Error getting the attribute "is64Bit"')
      try:
        oGuestOSType.platform_architecture = ctx[ 'global'].getEnumValueName('PlatformArchitecture', oVBoxGuestOSType.platformArchitecture)
      except Exception as e:
        logging.info('Error getting the attribute "platformArchitecture"')
      try:
        oGuestOSType.recommended_ioapic = oVBoxGuestOSType.recommendedIOAPIC
      except Exception as e:
        logging.info('Error getting the attribute "recommendedIOAPIC"')
      try:
        oGuestOSType.recommended_virt_ex = oVBoxGuestOSType.recommendedVirtEx
      except Exception as e:
        logging.info('Error getting the attribute "recommendedVirtEx"')
      try:
        oGuestOSType.recommended_ram = oVBoxGuestOSType.recommendedRAM
      except Exception as e:
        logging.info('Error getting the attribute "recommendedRAM"')
      try:
        oGuestOSType.recommended_graphics_controller = ctx[ 'global'].getEnumValueName('GraphicsControllerType', oVBoxGuestOSType.recommendedGraphicsController)
      except Exception as e:
        logging.info('Error getting the attribute "recommendedGraphicsController"')
      try:
        oGuestOSType.recommended_vram = oVBoxGuestOSType.recommendedVRAM
      except Exception as e:
        logging.info('Error getting the attribute "recommendedVRAM"')
      try:
        oGuestOSType.recommended2d_video_acceleration = oVBoxGuestOSType.recommended2DVideoAcceleration
      except Exception as e:
        logging.info('Error getting the attribute "recommended2DVideoAcceleration"')
      try:
        oGuestOSType.recommended3d_acceleration = oVBoxGuestOSType.recommended3DAcceleration
      except Exception as e:
        logging.info('Error getting the attribute "recommended3DAcceleration"')
      try:
        oGuestOSType.recommended_hdd = oVBoxGuestOSType.recommendedHDD
      except Exception as e:
        logging.info('Error getting the attribute "recommendedHDD"')
      try:
        oGuestOSType.adapter_type = ctx[ 'global'].getEnumValueName('NetworkAdapterType', oVBoxGuestOSType.adapterType)
      except Exception as e:
        logging.info('Error getting the attribute "adapterType"')
      try:
        oGuestOSType.recommended_pae = oVBoxGuestOSType.recommendedPAE
      except Exception as e:
        logging.info('Error getting the attribute "recommendedPAE"')
      try:
        oGuestOSType.recommended_dvd_storage_controller = ctx[ 'global'].getEnumValueName('StorageControllerType', oVBoxGuestOSType.recommendedDVDStorageController)
      except Exception as e:
        logging.info('Error getting the attribute "recommendedDVDStorageController"')
      try:
        oGuestOSType.recommended_dvd_storage_bus = ctx[ 'global'].getEnumValueName('StorageBus', oVBoxGuestOSType.recommendedDVDStorageBus)
      except Exception as e:
        logging.info('Error getting the attribute "recommendedDVDStorageBus"')
      try:
        oGuestOSType.recommended_hd_storage_controller = ctx[ 'global'].getEnumValueName('StorageControllerType', oVBoxGuestOSType.recommendedHDStorageController)
      except Exception as e:
        logging.info('Error getting the attribute "recommendedHDStorageController"')
      try:
        oGuestOSType.recommended_hd_storage_bus = ctx[ 'global'].getEnumValueName('StorageBus', oVBoxGuestOSType.recommendedHDStorageBus)
      except Exception as e:
        logging.info('Error getting the attribute "recommendedHDStorageBus"')
      try:
        oGuestOSType.recommended_firmware = ctx[ 'global'].getEnumValueName('FirmwareType', oVBoxGuestOSType.recommendedFirmware)
      except Exception as e:
        logging.info('Error getting the attribute "recommendedFirmware"')
      try:
        oGuestOSType.recommended_usbhid = oVBoxGuestOSType.recommendedUSBHID
      except Exception as e:
        logging.info('Error getting the attribute "recommendedUSBHID"')
      try:
        oGuestOSType.recommended_hpet = oVBoxGuestOSType.recommendedHPET
      except Exception as e:
        logging.info('Error getting the attribute "recommendedHPET"')
      try:
        oGuestOSType.recommended_usb_tablet = oVBoxGuestOSType.recommendedUSBTablet
      except Exception as e:
        logging.info('Error getting the attribute "recommendedUSBTablet"')
      try:
        oGuestOSType.recommended_rtc_use_utc = oVBoxGuestOSType.recommendedRTCUseUTC
      except Exception as e:
        logging.info('Error getting the attribute "recommendedRTCUseUTC"')
      try:
        oGuestOSType.recommended_chipset = ctx[ 'global'].getEnumValueName('ChipsetType', oVBoxGuestOSType.recommendedChipset)
      except Exception as e:
        logging.info('Error getting the attribute "recommendedChipset"')
      try:
        oGuestOSType.recommended_iommu_type = ctx[ 'global'].getEnumValueName('IommuType', oVBoxGuestOSType.recommendedIommuType)
      except Exception as e:
        logging.info('Error getting the attribute "recommendedIommuType"')
      try:
        oGuestOSType.recommended_audio_controller = ctx[ 'global'].getEnumValueName('AudioControllerType', oVBoxGuestOSType.recommendedAudioController)
      except Exception as e:
        logging.info('Error getting the attribute "recommendedAudioController"')
      try:
        oGuestOSType.recommended_audio_codec = ctx[ 'global'].getEnumValueName('AudioCodecType', oVBoxGuestOSType.recommendedAudioCodec)
      except Exception as e:
        logging.info('Error getting the attribute "recommendedAudioCodec"')
      try:
        oGuestOSType.recommended_floppy = oVBoxGuestOSType.recommendedFloppy
      except Exception as e:
        logging.info('Error getting the attribute "recommendedFloppy"')
      try:
        oGuestOSType.recommended_usb = oVBoxGuestOSType.recommendedUSB
      except Exception as e:
        logging.info('Error getting the attribute "recommendedUSB"')
      try:
        oGuestOSType.recommended_usb3 = oVBoxGuestOSType.recommendedUSB3
      except Exception as e:
        logging.info('Error getting the attribute "recommendedUSB3"')
      try:
        oGuestOSType.recommended_tf_reset = oVBoxGuestOSType.recommendedTFReset
      except Exception as e:
        logging.info('Error getting the attribute "recommendedTFReset"')
      try:
        oGuestOSType.recommended_x2apic = oVBoxGuestOSType.recommendedX2APIC
      except Exception as e:
        logging.info('Error getting the attribute "recommendedX2APIC"')
      try:
        oGuestOSType.recommended_cpu_count = oVBoxGuestOSType.recommendedCPUCount
      except Exception as e:
        logging.info('Error getting the attribute "recommendedCPUCount"')
      try:
        oGuestOSType.recommended_tpm_type = ctx[ 'global'].getEnumValueName('TpmType', oVBoxGuestOSType.recommendedTpmType)
      except Exception as e:
        logging.info('Error getting the attribute "recommendedTpmType"')
      try:
        oGuestOSType.recommended_secure_boot = oVBoxGuestOSType.recommendedSecureBoot
      except Exception as e:
        logging.info('Error getting the attribute "recommendedSecureBoot"')
      try:
        oGuestOSType.recommended_wddm_graphics = oVBoxGuestOSType.recommendedWDDMGraphics
      except Exception as e:
        logging.info('Error getting the attribute "recommendedWDDMGraphics"')
      try:
        oGuestOSType.guest_additions_install_package_name = oVBoxGuestOSType.guestAdditionsInstallPackageName
      except Exception as e:
        logging.info('Error getting the attribute "guestAdditionsInstallPackageName"')
      
  except Exception as e:
    logging.info('Abnormal function exit')
    oGuestOSType = None
    text = 'Exception trying to fill the object oGuestOSType. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oGuestOSType

def i_fill_partial_guest_os_type(oVBoxGuestOSType, select):
  logging.info('Enter function ')
  oGuestOSType = GuestOSType()
  try:
    if oVBoxGuestOSType is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='familyId':
            try:
              oGuestOSType.family_id = oVBoxGuestOSType.familyId
            except Exception as e:
              logging.info('Error getting the attribute "familyId"')
              raise Exception('Error getting the attribute "familyId"')
            
          if currAttr=='familyDescription':
            try:
              oGuestOSType.family_description = oVBoxGuestOSType.familyDescription
            except Exception as e:
              logging.info('Error getting the attribute "familyDescription"')
              raise Exception('Error getting the attribute "familyDescription"')
            
          if currAttr=='id':
            try:
              oGuestOSType.id = oVBoxGuestOSType.id
            except Exception as e:
              logging.info('Error getting the attribute "id"')
              raise Exception('Error getting the attribute "id"')
            
          if currAttr=='subtype':
            try:
              oGuestOSType.subtype = oVBoxGuestOSType.subtype
            except Exception as e:
              logging.info('Error getting the attribute "subtype"')
              raise Exception('Error getting the attribute "subtype"')
            
          if currAttr=='description':
            try:
              oGuestOSType.description = oVBoxGuestOSType.description
            except Exception as e:
              logging.info('Error getting the attribute "description"')
              raise Exception('Error getting the attribute "description"')
            
          if currAttr=='is64Bit':
            try:
              oGuestOSType.is64_bit = oVBoxGuestOSType.is64Bit
            except Exception as e:
              logging.info('Error getting the attribute "is64Bit"')
              raise Exception('Error getting the attribute "is64Bit"')
            
          if currAttr=='platformArchitecture':
            try:
              oGuestOSType.platform_architecture = ctx[ 'global'].getEnumValueName('PlatformArchitecture', oVBoxGuestOSType.platformArchitecture)
            except Exception as e:
              logging.info('Error getting the attribute "platformArchitecture"')
              raise Exception('Error getting the array of "platformArchitecture"')
            
          if currAttr=='recommendedIOAPIC':
            try:
              oGuestOSType.recommended_ioapic = oVBoxGuestOSType.recommendedIOAPIC
            except Exception as e:
              logging.info('Error getting the attribute "recommendedIOAPIC"')
              raise Exception('Error getting the attribute "recommendedIOAPIC"')
            
          if currAttr=='recommendedVirtEx':
            try:
              oGuestOSType.recommended_virt_ex = oVBoxGuestOSType.recommendedVirtEx
            except Exception as e:
              logging.info('Error getting the attribute "recommendedVirtEx"')
              raise Exception('Error getting the attribute "recommendedVirtEx"')
            
          if currAttr=='recommendedRAM':
            try:
              oGuestOSType.recommended_ram = oVBoxGuestOSType.recommendedRAM
            except Exception as e:
              logging.info('Error getting the attribute "recommendedRAM"')
              raise Exception('Error getting the attribute "recommendedRAM"')
            
          if currAttr=='recommendedGraphicsController':
            try:
              oGuestOSType.recommended_graphics_controller = ctx[ 'global'].getEnumValueName('GraphicsControllerType', oVBoxGuestOSType.recommendedGraphicsController)
            except Exception as e:
              logging.info('Error getting the attribute "recommendedGraphicsController"')
              raise Exception('Error getting the array of "recommendedGraphicsController"')
            
          if currAttr=='recommendedVRAM':
            try:
              oGuestOSType.recommended_vram = oVBoxGuestOSType.recommendedVRAM
            except Exception as e:
              logging.info('Error getting the attribute "recommendedVRAM"')
              raise Exception('Error getting the attribute "recommendedVRAM"')
            
          if currAttr=='recommended2DVideoAcceleration':
            try:
              oGuestOSType.recommended2d_video_acceleration = oVBoxGuestOSType.recommended2DVideoAcceleration
            except Exception as e:
              logging.info('Error getting the attribute "recommended2DVideoAcceleration"')
              raise Exception('Error getting the attribute "recommended2DVideoAcceleration"')
            
          if currAttr=='recommended3DAcceleration':
            try:
              oGuestOSType.recommended3d_acceleration = oVBoxGuestOSType.recommended3DAcceleration
            except Exception as e:
              logging.info('Error getting the attribute "recommended3DAcceleration"')
              raise Exception('Error getting the attribute "recommended3DAcceleration"')
            
          if currAttr=='recommendedHDD':
            try:
              oGuestOSType.recommended_hdd = oVBoxGuestOSType.recommendedHDD
            except Exception as e:
              logging.info('Error getting the attribute "recommendedHDD"')
              raise Exception('Error getting the attribute "recommendedHDD"')
            
          if currAttr=='adapterType':
            try:
              oGuestOSType.adapter_type = ctx[ 'global'].getEnumValueName('NetworkAdapterType', oVBoxGuestOSType.adapterType)
            except Exception as e:
              logging.info('Error getting the attribute "adapterType"')
              raise Exception('Error getting the array of "adapterType"')
            
          if currAttr=='recommendedPAE':
            try:
              oGuestOSType.recommended_pae = oVBoxGuestOSType.recommendedPAE
            except Exception as e:
              logging.info('Error getting the attribute "recommendedPAE"')
              raise Exception('Error getting the attribute "recommendedPAE"')
            
          if currAttr=='recommendedDVDStorageController':
            try:
              oGuestOSType.recommended_dvd_storage_controller = ctx[ 'global'].getEnumValueName('StorageControllerType', oVBoxGuestOSType.recommendedDVDStorageController)
            except Exception as e:
              logging.info('Error getting the attribute "recommendedDVDStorageController"')
              raise Exception('Error getting the array of "recommendedDVDStorageController"')
            
          if currAttr=='recommendedDVDStorageBus':
            try:
              oGuestOSType.recommended_dvd_storage_bus = ctx[ 'global'].getEnumValueName('StorageBus', oVBoxGuestOSType.recommendedDVDStorageBus)
            except Exception as e:
              logging.info('Error getting the attribute "recommendedDVDStorageBus"')
              raise Exception('Error getting the array of "recommendedDVDStorageBus"')
            
          if currAttr=='recommendedHDStorageController':
            try:
              oGuestOSType.recommended_hd_storage_controller = ctx[ 'global'].getEnumValueName('StorageControllerType', oVBoxGuestOSType.recommendedHDStorageController)
            except Exception as e:
              logging.info('Error getting the attribute "recommendedHDStorageController"')
              raise Exception('Error getting the array of "recommendedHDStorageController"')
            
          if currAttr=='recommendedHDStorageBus':
            try:
              oGuestOSType.recommended_hd_storage_bus = ctx[ 'global'].getEnumValueName('StorageBus', oVBoxGuestOSType.recommendedHDStorageBus)
            except Exception as e:
              logging.info('Error getting the attribute "recommendedHDStorageBus"')
              raise Exception('Error getting the array of "recommendedHDStorageBus"')
            
          if currAttr=='recommendedFirmware':
            try:
              oGuestOSType.recommended_firmware = ctx[ 'global'].getEnumValueName('FirmwareType', oVBoxGuestOSType.recommendedFirmware)
            except Exception as e:
              logging.info('Error getting the attribute "recommendedFirmware"')
              raise Exception('Error getting the array of "recommendedFirmware"')
            
          if currAttr=='recommendedUSBHID':
            try:
              oGuestOSType.recommended_usbhid = oVBoxGuestOSType.recommendedUSBHID
            except Exception as e:
              logging.info('Error getting the attribute "recommendedUSBHID"')
              raise Exception('Error getting the attribute "recommendedUSBHID"')
            
          if currAttr=='recommendedHPET':
            try:
              oGuestOSType.recommended_hpet = oVBoxGuestOSType.recommendedHPET
            except Exception as e:
              logging.info('Error getting the attribute "recommendedHPET"')
              raise Exception('Error getting the attribute "recommendedHPET"')
            
          if currAttr=='recommendedUSBTablet':
            try:
              oGuestOSType.recommended_usb_tablet = oVBoxGuestOSType.recommendedUSBTablet
            except Exception as e:
              logging.info('Error getting the attribute "recommendedUSBTablet"')
              raise Exception('Error getting the attribute "recommendedUSBTablet"')
            
          if currAttr=='recommendedRTCUseUTC':
            try:
              oGuestOSType.recommended_rtc_use_utc = oVBoxGuestOSType.recommendedRTCUseUTC
            except Exception as e:
              logging.info('Error getting the attribute "recommendedRTCUseUTC"')
              raise Exception('Error getting the attribute "recommendedRTCUseUTC"')
            
          if currAttr=='recommendedChipset':
            try:
              oGuestOSType.recommended_chipset = ctx[ 'global'].getEnumValueName('ChipsetType', oVBoxGuestOSType.recommendedChipset)
            except Exception as e:
              logging.info('Error getting the attribute "recommendedChipset"')
              raise Exception('Error getting the array of "recommendedChipset"')
            
          if currAttr=='recommendedIommuType':
            try:
              oGuestOSType.recommended_iommu_type = ctx[ 'global'].getEnumValueName('IommuType', oVBoxGuestOSType.recommendedIommuType)
            except Exception as e:
              logging.info('Error getting the attribute "recommendedIommuType"')
              raise Exception('Error getting the array of "recommendedIommuType"')
            
          if currAttr=='recommendedAudioController':
            try:
              oGuestOSType.recommended_audio_controller = ctx[ 'global'].getEnumValueName('AudioControllerType', oVBoxGuestOSType.recommendedAudioController)
            except Exception as e:
              logging.info('Error getting the attribute "recommendedAudioController"')
              raise Exception('Error getting the array of "recommendedAudioController"')
            
          if currAttr=='recommendedAudioCodec':
            try:
              oGuestOSType.recommended_audio_codec = ctx[ 'global'].getEnumValueName('AudioCodecType', oVBoxGuestOSType.recommendedAudioCodec)
            except Exception as e:
              logging.info('Error getting the attribute "recommendedAudioCodec"')
              raise Exception('Error getting the array of "recommendedAudioCodec"')
            
          if currAttr=='recommendedFloppy':
            try:
              oGuestOSType.recommended_floppy = oVBoxGuestOSType.recommendedFloppy
            except Exception as e:
              logging.info('Error getting the attribute "recommendedFloppy"')
              raise Exception('Error getting the attribute "recommendedFloppy"')
            
          if currAttr=='recommendedUSB':
            try:
              oGuestOSType.recommended_usb = oVBoxGuestOSType.recommendedUSB
            except Exception as e:
              logging.info('Error getting the attribute "recommendedUSB"')
              raise Exception('Error getting the attribute "recommendedUSB"')
            
          if currAttr=='recommendedUSB3':
            try:
              oGuestOSType.recommended_usb3 = oVBoxGuestOSType.recommendedUSB3
            except Exception as e:
              logging.info('Error getting the attribute "recommendedUSB3"')
              raise Exception('Error getting the attribute "recommendedUSB3"')
            
          if currAttr=='recommendedTFReset':
            try:
              oGuestOSType.recommended_tf_reset = oVBoxGuestOSType.recommendedTFReset
            except Exception as e:
              logging.info('Error getting the attribute "recommendedTFReset"')
              raise Exception('Error getting the attribute "recommendedTFReset"')
            
          if currAttr=='recommendedX2APIC':
            try:
              oGuestOSType.recommended_x2apic = oVBoxGuestOSType.recommendedX2APIC
            except Exception as e:
              logging.info('Error getting the attribute "recommendedX2APIC"')
              raise Exception('Error getting the attribute "recommendedX2APIC"')
            
          if currAttr=='recommendedCPUCount':
            try:
              oGuestOSType.recommended_cpu_count = oVBoxGuestOSType.recommendedCPUCount
            except Exception as e:
              logging.info('Error getting the attribute "recommendedCPUCount"')
              raise Exception('Error getting the attribute "recommendedCPUCount"')
            
          if currAttr=='recommendedTpmType':
            try:
              oGuestOSType.recommended_tpm_type = ctx[ 'global'].getEnumValueName('TpmType', oVBoxGuestOSType.recommendedTpmType)
            except Exception as e:
              logging.info('Error getting the attribute "recommendedTpmType"')
              raise Exception('Error getting the array of "recommendedTpmType"')
            
          if currAttr=='recommendedSecureBoot':
            try:
              oGuestOSType.recommended_secure_boot = oVBoxGuestOSType.recommendedSecureBoot
            except Exception as e:
              logging.info('Error getting the attribute "recommendedSecureBoot"')
              raise Exception('Error getting the attribute "recommendedSecureBoot"')
            
          if currAttr=='recommendedWDDMGraphics':
            try:
              oGuestOSType.recommended_wddm_graphics = oVBoxGuestOSType.recommendedWDDMGraphics
            except Exception as e:
              logging.info('Error getting the attribute "recommendedWDDMGraphics"')
              raise Exception('Error getting the attribute "recommendedWDDMGraphics"')
            
          if currAttr=='guestAdditionsInstallPackageName':
            try:
              oGuestOSType.guest_additions_install_package_name = oVBoxGuestOSType.guestAdditionsInstallPackageName
            except Exception as e:
              logging.info('Error getting the attribute "guestAdditionsInstallPackageName"')
              raise Exception('Error getting the attribute "guestAdditionsInstallPackageName"')
            
  except Exception as e:
    logging.info('Abnormal function exit')
    oGuestOSType = None
    text = 'Exception trying to fill the object oGuestOSType. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oGuestOSType

def i_fill_platform_properties(oVBoxPlatformProperties, select=None):
  """Convert the passed VirtualBox object oVBoxPlatformProperties with interface IPlatformProperties into Swagger object oPlatformProperties"""
  
  logging.info('Enter function ')
  oPlatformProperties = PlatformProperties()
  try:
    if oVBoxPlatformProperties is not None:
      if select is not None and len(select)>0:
        oPlatformProperties = i_fill_partial_platform_properties(oVBoxPlatformProperties, select)
      else:
        oPlatformProperties = i_fill_whole_platform_properties(oVBoxPlatformProperties)
  except Exception as e:
    logging.info('Abnormal function exit')
    oPlatformProperties = None
    text = 'Exception trying to convert the VirtualBox object oVBoxPlatformProperties into Swagger object oPlatformProperties. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oPlatformProperties

def i_fill_whole_platform_properties(oVBoxPlatformProperties):
  logging.info('Enter function ')
  oPlatformProperties = PlatformProperties()
  try:
    if oVBoxPlatformProperties is not None:
      try:
        oPlatformProperties.raw_mode_supported = oVBoxPlatformProperties.rawModeSupported
      except Exception as e:
        logging.info('Error getting the attribute "rawModeSupported"')
      try:
        oPlatformProperties.exclusive_hw_virt = oVBoxPlatformProperties.exclusiveHwVirt
      except Exception as e:
        logging.info('Error getting the attribute "exclusiveHwVirt"')
      try:
        oPlatformProperties.serial_port_count = oVBoxPlatformProperties.serialPortCount
      except Exception as e:
        logging.info('Error getting the attribute "serialPortCount"')
      try:
        oPlatformProperties.parallel_port_count = oVBoxPlatformProperties.parallelPortCount
      except Exception as e:
        logging.info('Error getting the attribute "parallelPortCount"')
      try:
        oPlatformProperties.max_boot_position = oVBoxPlatformProperties.maxBootPosition
      except Exception as e:
        logging.info('Error getting the attribute "maxBootPosition"')
      try:
        ol_supported_paravirt_providers = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedParavirtProviders')
        oPlatformProperties.supported_paravirt_providers = list()
        for count, item in enumerate(ol_supported_paravirt_providers):
          o = ctx['global'].getEnumValueName('ParavirtProvider', item)
          if oPlatformProperties.supported_paravirt_providers.count(o) == 0 : oPlatformProperties.supported_paravirt_providers.append(o)
      except Exception as e:
        logging.info('Error getting the array of "supportedParavirtProviders"')
      try:
        ol_supported_firmware_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedFirmwareTypes')
        oPlatformProperties.supported_firmware_types = list()
        for count, item in enumerate(ol_supported_firmware_types):
          o = ctx['global'].getEnumValueName('FirmwareType', item)
          if oPlatformProperties.supported_firmware_types.count(o) == 0 : oPlatformProperties.supported_firmware_types.append(o)
      except Exception as e:
        logging.info('Error getting the array of "supportedFirmwareTypes"')
      try:
        ol_supported_guest_os_types = ctx['global'].getArray(oVBoxPlatformProperties,'supportedGuestOSTypes')
        oPlatformProperties.supported_guest_os_types = list()
        for count, item in enumerate(ol_supported_guest_os_types):
          o = i_fill_guest_os_type(item)
          oPlatformProperties.supported_guest_os_types.append(o)
      except Exception as e:
        logging.info('Error getting the array of "supportedGuestOSTypes"')
      try:
        ol_supported_gfx_controller_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedGfxControllerTypes')
        oPlatformProperties.supported_gfx_controller_types = list()
        for count, item in enumerate(ol_supported_gfx_controller_types):
          o = ctx['global'].getEnumValueName('GraphicsControllerType', item)
          if oPlatformProperties.supported_gfx_controller_types.count(o) == 0 : oPlatformProperties.supported_gfx_controller_types.append(o)
      except Exception as e:
        logging.info('Error getting the array of "supportedGfxControllerTypes"')
      try:
        ol_supported_net_adp_promisc_mode_pols = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedNetAdpPromiscModePols')
        oPlatformProperties.supported_net_adp_promisc_mode_pols = list()
        for count, item in enumerate(ol_supported_net_adp_promisc_mode_pols):
          o = ctx['global'].getEnumValueName('NetworkAdapterPromiscModePolicy', item)
          if oPlatformProperties.supported_net_adp_promisc_mode_pols.count(o) == 0 : oPlatformProperties.supported_net_adp_promisc_mode_pols.append(o)
      except Exception as e:
        logging.info('Error getting the array of "supportedNetAdpPromiscModePols"')
      try:
        ol_supported_network_adapter_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedNetworkAdapterTypes')
        oPlatformProperties.supported_network_adapter_types = list()
        for count, item in enumerate(ol_supported_network_adapter_types):
          o = ctx['global'].getEnumValueName('NetworkAdapterType', item)
          if oPlatformProperties.supported_network_adapter_types.count(o) == 0 : oPlatformProperties.supported_network_adapter_types.append(o)
      except Exception as e:
        logging.info('Error getting the array of "supportedNetworkAdapterTypes"')
      try:
        ol_supported_uart_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedUartTypes')
        oPlatformProperties.supported_uart_types = list()
        for count, item in enumerate(ol_supported_uart_types):
          o = ctx['global'].getEnumValueName('UartType', item)
          if oPlatformProperties.supported_uart_types.count(o) == 0 : oPlatformProperties.supported_uart_types.append(o)
      except Exception as e:
        logging.info('Error getting the array of "supportedUartTypes"')
      try:
        ol_supported_usb_controller_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedUSBControllerTypes')
        oPlatformProperties.supported_usb_controller_types = list()
        for count, item in enumerate(ol_supported_usb_controller_types):
          o = ctx['global'].getEnumValueName('USBControllerType', item)
          if oPlatformProperties.supported_usb_controller_types.count(o) == 0 : oPlatformProperties.supported_usb_controller_types.append(o)
      except Exception as e:
        logging.info('Error getting the array of "supportedUSBControllerTypes"')
      try:
        ol_supported_audio_controller_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedAudioControllerTypes')
        oPlatformProperties.supported_audio_controller_types = list()
        for count, item in enumerate(ol_supported_audio_controller_types):
          o = ctx['global'].getEnumValueName('AudioControllerType', item)
          if oPlatformProperties.supported_audio_controller_types.count(o) == 0 : oPlatformProperties.supported_audio_controller_types.append(o)
      except Exception as e:
        logging.info('Error getting the array of "supportedAudioControllerTypes"')
      try:
        ol_supported_boot_devices = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedBootDevices')
        oPlatformProperties.supported_boot_devices = list()
        for count, item in enumerate(ol_supported_boot_devices):
          o = ctx['global'].getEnumValueName('DeviceType', item)
          if oPlatformProperties.supported_boot_devices.count(o) == 0 : oPlatformProperties.supported_boot_devices.append(o)
      except Exception as e:
        logging.info('Error getting the array of "supportedBootDevices"')
      try:
        ol_supported_storage_buses = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedStorageBuses')
        oPlatformProperties.supported_storage_buses = list()
        for count, item in enumerate(ol_supported_storage_buses):
          o = ctx['global'].getEnumValueName('StorageBus', item)
          if oPlatformProperties.supported_storage_buses.count(o) == 0 : oPlatformProperties.supported_storage_buses.append(o)
      except Exception as e:
        logging.info('Error getting the array of "supportedStorageBuses"')
      try:
        ol_supported_storage_controller_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedStorageControllerTypes')
        oPlatformProperties.supported_storage_controller_types = list()
        for count, item in enumerate(ol_supported_storage_controller_types):
          o = ctx['global'].getEnumValueName('StorageControllerType', item)
          if oPlatformProperties.supported_storage_controller_types.count(o) == 0 : oPlatformProperties.supported_storage_controller_types.append(o)
      except Exception as e:
        logging.info('Error getting the array of "supportedStorageControllerTypes"')
      try:
        ol_supported_chipset_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedChipsetTypes')
        oPlatformProperties.supported_chipset_types = list()
        for count, item in enumerate(ol_supported_chipset_types):
          o = ctx['global'].getEnumValueName('ChipsetType', item)
          if oPlatformProperties.supported_chipset_types.count(o) == 0 : oPlatformProperties.supported_chipset_types.append(o)
      except Exception as e:
        logging.info('Error getting the array of "supportedChipsetTypes"')
      try:
        ol_supported_iommu_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedIommuTypes')
        oPlatformProperties.supported_iommu_types = list()
        for count, item in enumerate(ol_supported_iommu_types):
          o = ctx['global'].getEnumValueName('IommuType', item)
          if oPlatformProperties.supported_iommu_types.count(o) == 0 : oPlatformProperties.supported_iommu_types.append(o)
      except Exception as e:
        logging.info('Error getting the array of "supportedIommuTypes"')
      try:
        ol_supported_tpm_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedTpmTypes')
        oPlatformProperties.supported_tpm_types = list()
        for count, item in enumerate(ol_supported_tpm_types):
          o = ctx['global'].getEnumValueName('TpmType', item)
          if oPlatformProperties.supported_tpm_types.count(o) == 0 : oPlatformProperties.supported_tpm_types.append(o)
      except Exception as e:
        logging.info('Error getting the array of "supportedTpmTypes"')
      
  except Exception as e:
    logging.info('Abnormal function exit')
    oPlatformProperties = None
    text = 'Exception trying to fill the object oPlatformProperties. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oPlatformProperties

def i_fill_partial_platform_properties(oVBoxPlatformProperties, select):
  logging.info('Enter function ')
  oPlatformProperties = PlatformProperties()
  try:
    if oVBoxPlatformProperties is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='rawModeSupported':
            try:
              oPlatformProperties.raw_mode_supported = oVBoxPlatformProperties.rawModeSupported
            except Exception as e:
              logging.info('Error getting the attribute "rawModeSupported"')
              raise Exception('Error getting the attribute "rawModeSupported"')
            
          if currAttr=='exclusiveHwVirt':
            try:
              oPlatformProperties.exclusive_hw_virt = oVBoxPlatformProperties.exclusiveHwVirt
            except Exception as e:
              logging.info('Error getting the attribute "exclusiveHwVirt"')
              raise Exception('Error getting the attribute "exclusiveHwVirt"')
            
          if currAttr=='serialPortCount':
            try:
              oPlatformProperties.serial_port_count = oVBoxPlatformProperties.serialPortCount
            except Exception as e:
              logging.info('Error getting the attribute "serialPortCount"')
              raise Exception('Error getting the attribute "serialPortCount"')
            
          if currAttr=='parallelPortCount':
            try:
              oPlatformProperties.parallel_port_count = oVBoxPlatformProperties.parallelPortCount
            except Exception as e:
              logging.info('Error getting the attribute "parallelPortCount"')
              raise Exception('Error getting the attribute "parallelPortCount"')
            
          if currAttr=='maxBootPosition':
            try:
              oPlatformProperties.max_boot_position = oVBoxPlatformProperties.maxBootPosition
            except Exception as e:
              logging.info('Error getting the attribute "maxBootPosition"')
              raise Exception('Error getting the attribute "maxBootPosition"')
            
          if currAttr=='supportedParavirtProviders':
            try:
              ol_supported_paravirt_providers = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedParavirtProviders')
              oPlatformProperties.supported_paravirt_providers = list()
              for count, item in enumerate(ol_supported_paravirt_providers):
                o = ctx['global'].getEnumValueName('ParavirtProvider', item)
                if oPlatformProperties.supported_paravirt_providers.count(o) == 0 : oPlatformProperties.supported_paravirt_providers.append(o)
            except Exception as e:
              logging.info('Error getting the array of "supportedParavirtProviders"')
              raise Exception('Error getting the array of "supportedParavirtProviders"')
            
          if currAttr=='supportedFirmwareTypes':
            try:
              ol_supported_firmware_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedFirmwareTypes')
              oPlatformProperties.supported_firmware_types = list()
              for count, item in enumerate(ol_supported_firmware_types):
                o = ctx['global'].getEnumValueName('FirmwareType', item)
                if oPlatformProperties.supported_firmware_types.count(o) == 0 : oPlatformProperties.supported_firmware_types.append(o)
            except Exception as e:
              logging.info('Error getting the array of "supportedFirmwareTypes"')
              raise Exception('Error getting the array of "supportedFirmwareTypes"')
            
          if currAttr=='supportedGuestOSTypes':
            try:
              ol_supported_guest_os_types = ctx['global'].getArray(oVBoxPlatformProperties,'supportedGuestOSTypes')
              oPlatformProperties.supported_guest_os_types = list()
              for count, item in enumerate(ol_supported_guest_os_types):
                o = i_fill_guest_os_type(item)
                oPlatformProperties.supported_guest_os_types.append(o)
            except Exception as e:
              logging.info('Error getting the array of "supportedGuestOSTypes"')
              raise Exception('Error getting the array of "supportedGuestOSTypes"')
            
          if currAttr=='supportedGfxControllerTypes':
            try:
              ol_supported_gfx_controller_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedGfxControllerTypes')
              oPlatformProperties.supported_gfx_controller_types = list()
              for count, item in enumerate(ol_supported_gfx_controller_types):
                o = ctx['global'].getEnumValueName('GraphicsControllerType', item)
                if oPlatformProperties.supported_gfx_controller_types.count(o) == 0 : oPlatformProperties.supported_gfx_controller_types.append(o)
            except Exception as e:
              logging.info('Error getting the array of "supportedGfxControllerTypes"')
              raise Exception('Error getting the array of "supportedGfxControllerTypes"')
            
          if currAttr=='supportedNetAdpPromiscModePols':
            try:
              ol_supported_net_adp_promisc_mode_pols = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedNetAdpPromiscModePols')
              oPlatformProperties.supported_net_adp_promisc_mode_pols = list()
              for count, item in enumerate(ol_supported_net_adp_promisc_mode_pols):
                o = ctx['global'].getEnumValueName('NetworkAdapterPromiscModePolicy', item)
                if oPlatformProperties.supported_net_adp_promisc_mode_pols.count(o) == 0 : oPlatformProperties.supported_net_adp_promisc_mode_pols.append(o)
            except Exception as e:
              logging.info('Error getting the array of "supportedNetAdpPromiscModePols"')
              raise Exception('Error getting the array of "supportedNetAdpPromiscModePols"')
            
          if currAttr=='supportedNetworkAdapterTypes':
            try:
              ol_supported_network_adapter_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedNetworkAdapterTypes')
              oPlatformProperties.supported_network_adapter_types = list()
              for count, item in enumerate(ol_supported_network_adapter_types):
                o = ctx['global'].getEnumValueName('NetworkAdapterType', item)
                if oPlatformProperties.supported_network_adapter_types.count(o) == 0 : oPlatformProperties.supported_network_adapter_types.append(o)
            except Exception as e:
              logging.info('Error getting the array of "supportedNetworkAdapterTypes"')
              raise Exception('Error getting the array of "supportedNetworkAdapterTypes"')
            
          if currAttr=='supportedUartTypes':
            try:
              ol_supported_uart_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedUartTypes')
              oPlatformProperties.supported_uart_types = list()
              for count, item in enumerate(ol_supported_uart_types):
                o = ctx['global'].getEnumValueName('UartType', item)
                if oPlatformProperties.supported_uart_types.count(o) == 0 : oPlatformProperties.supported_uart_types.append(o)
            except Exception as e:
              logging.info('Error getting the array of "supportedUartTypes"')
              raise Exception('Error getting the array of "supportedUartTypes"')
            
          if currAttr=='supportedUSBControllerTypes':
            try:
              ol_supported_usb_controller_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedUSBControllerTypes')
              oPlatformProperties.supported_usb_controller_types = list()
              for count, item in enumerate(ol_supported_usb_controller_types):
                o = ctx['global'].getEnumValueName('USBControllerType', item)
                if oPlatformProperties.supported_usb_controller_types.count(o) == 0 : oPlatformProperties.supported_usb_controller_types.append(o)
            except Exception as e:
              logging.info('Error getting the array of "supportedUSBControllerTypes"')
              raise Exception('Error getting the array of "supportedUSBControllerTypes"')
            
          if currAttr=='supportedAudioControllerTypes':
            try:
              ol_supported_audio_controller_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedAudioControllerTypes')
              oPlatformProperties.supported_audio_controller_types = list()
              for count, item in enumerate(ol_supported_audio_controller_types):
                o = ctx['global'].getEnumValueName('AudioControllerType', item)
                if oPlatformProperties.supported_audio_controller_types.count(o) == 0 : oPlatformProperties.supported_audio_controller_types.append(o)
            except Exception as e:
              logging.info('Error getting the array of "supportedAudioControllerTypes"')
              raise Exception('Error getting the array of "supportedAudioControllerTypes"')
            
          if currAttr=='supportedBootDevices':
            try:
              ol_supported_boot_devices = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedBootDevices')
              oPlatformProperties.supported_boot_devices = list()
              for count, item in enumerate(ol_supported_boot_devices):
                o = ctx['global'].getEnumValueName('DeviceType', item)
                if oPlatformProperties.supported_boot_devices.count(o) == 0 : oPlatformProperties.supported_boot_devices.append(o)
            except Exception as e:
              logging.info('Error getting the array of "supportedBootDevices"')
              raise Exception('Error getting the array of "supportedBootDevices"')
            
          if currAttr=='supportedStorageBuses':
            try:
              ol_supported_storage_buses = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedStorageBuses')
              oPlatformProperties.supported_storage_buses = list()
              for count, item in enumerate(ol_supported_storage_buses):
                o = ctx['global'].getEnumValueName('StorageBus', item)
                if oPlatformProperties.supported_storage_buses.count(o) == 0 : oPlatformProperties.supported_storage_buses.append(o)
            except Exception as e:
              logging.info('Error getting the array of "supportedStorageBuses"')
              raise Exception('Error getting the array of "supportedStorageBuses"')
            
          if currAttr=='supportedStorageControllerTypes':
            try:
              ol_supported_storage_controller_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedStorageControllerTypes')
              oPlatformProperties.supported_storage_controller_types = list()
              for count, item in enumerate(ol_supported_storage_controller_types):
                o = ctx['global'].getEnumValueName('StorageControllerType', item)
                if oPlatformProperties.supported_storage_controller_types.count(o) == 0 : oPlatformProperties.supported_storage_controller_types.append(o)
            except Exception as e:
              logging.info('Error getting the array of "supportedStorageControllerTypes"')
              raise Exception('Error getting the array of "supportedStorageControllerTypes"')
            
          if currAttr=='supportedChipsetTypes':
            try:
              ol_supported_chipset_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedChipsetTypes')
              oPlatformProperties.supported_chipset_types = list()
              for count, item in enumerate(ol_supported_chipset_types):
                o = ctx['global'].getEnumValueName('ChipsetType', item)
                if oPlatformProperties.supported_chipset_types.count(o) == 0 : oPlatformProperties.supported_chipset_types.append(o)
            except Exception as e:
              logging.info('Error getting the array of "supportedChipsetTypes"')
              raise Exception('Error getting the array of "supportedChipsetTypes"')
            
          if currAttr=='supportedIommuTypes':
            try:
              ol_supported_iommu_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedIommuTypes')
              oPlatformProperties.supported_iommu_types = list()
              for count, item in enumerate(ol_supported_iommu_types):
                o = ctx['global'].getEnumValueName('IommuType', item)
                if oPlatformProperties.supported_iommu_types.count(o) == 0 : oPlatformProperties.supported_iommu_types.append(o)
            except Exception as e:
              logging.info('Error getting the array of "supportedIommuTypes"')
              raise Exception('Error getting the array of "supportedIommuTypes"')
            
          if currAttr=='supportedTpmTypes':
            try:
              ol_supported_tpm_types = ctx['global'].getArray(oVBoxPlatformProperties, 'supportedTpmTypes')
              oPlatformProperties.supported_tpm_types = list()
              for count, item in enumerate(ol_supported_tpm_types):
                o = ctx['global'].getEnumValueName('TpmType', item)
                if oPlatformProperties.supported_tpm_types.count(o) == 0 : oPlatformProperties.supported_tpm_types.append(o)
            except Exception as e:
              logging.info('Error getting the array of "supportedTpmTypes"')
              raise Exception('Error getting the array of "supportedTpmTypes"')
            
  except Exception as e:
    logging.info('Abnormal function exit')
    oPlatformProperties = None
    text = 'Exception trying to fill the object oPlatformProperties. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oPlatformProperties

def i_fill_nat_engine(oVBoxNATEngine, select=None):
  """Convert the passed VirtualBox object oVBoxNATEngine with interface INATEngine into Swagger object oNATEngine"""
  
  logging.info('Enter function ')
  oNATEngine = NATEngine()
  try:
    if oVBoxNATEngine is not None:
      if select is not None and len(select)>0:
        oNATEngine = i_fill_partial_nat_engine(oVBoxNATEngine, select)
      else:
        oNATEngine = i_fill_whole_nat_engine(oVBoxNATEngine)
  except Exception as e:
    logging.info('Abnormal function exit')
    oNATEngine = None
    text = 'Exception trying to convert the VirtualBox object oVBoxNATEngine into Swagger object oNATEngine. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oNATEngine

def i_fill_whole_nat_engine(oVBoxNATEngine):
  logging.info('Enter function ')
  oNATEngine = NATEngine()
  try:
    if oVBoxNATEngine is not None:
      try:
        oNATEngine.network = oVBoxNATEngine.network
      except Exception as e:
        logging.info('Error getting the attribute "network"')
      try:
        oNATEngine.host_ip = oVBoxNATEngine.hostIP
      except Exception as e:
        logging.info('Error getting the attribute "hostIP"')
      try:
        oNATEngine.tftp_prefix = oVBoxNATEngine.TFTPPrefix
      except Exception as e:
        logging.info('Error getting the attribute "TFTPPrefix"')
      try:
        oNATEngine.tftp_boot_file = oVBoxNATEngine.TFTPBootFile
      except Exception as e:
        logging.info('Error getting the attribute "TFTPBootFile"')
      try:
        oNATEngine.tftp_next_server = oVBoxNATEngine.TFTPNextServer
      except Exception as e:
        logging.info('Error getting the attribute "TFTPNextServer"')
      try:
        oNATEngine.alias_mode = oVBoxNATEngine.aliasMode
      except Exception as e:
        logging.info('Error getting the attribute "aliasMode"')
      try:
        oNATEngine.dns_pass_domain = oVBoxNATEngine.DNSPassDomain
      except Exception as e:
        logging.info('Error getting the attribute "DNSPassDomain"')
      try:
        oNATEngine.dns_proxy = oVBoxNATEngine.DNSProxy
      except Exception as e:
        logging.info('Error getting the attribute "DNSProxy"')
      try:
        oNATEngine.dns_use_host_resolver = oVBoxNATEngine.DNSUseHostResolver
      except Exception as e:
        logging.info('Error getting the attribute "DNSUseHostResolver"')
      try:
        ol_redirects = ctx['global'].getArray(oVBoxNATEngine,'redirects')
        oNATEngine.redirects = list()
        for count, item in enumerate(ol_redirects):
          oNATEngine.redirects.append(item)
      except Exception as e:
        logging.info('Error getting the array of "redirects"')
      try:
        oNATEngine.localhost_reachable = oVBoxNATEngine.localhostReachable
      except Exception as e:
        logging.info('Error getting the attribute "localhostReachable"')
      
  except Exception as e:
    logging.info('Abnormal function exit')
    oNATEngine = None
    text = 'Exception trying to fill the object oNATEngine. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oNATEngine

def i_fill_partial_nat_engine(oVBoxNATEngine, select):
  logging.info('Enter function ')
  oNATEngine = NATEngine()
  try:
    if oVBoxNATEngine is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='network':
            try:
              oNATEngine.network = oVBoxNATEngine.network
            except Exception as e:
              logging.info('Error getting the attribute "network"')
              raise Exception('Error getting the attribute "network"')
            
          if currAttr=='hostIP':
            try:
              oNATEngine.host_ip = oVBoxNATEngine.hostIP
            except Exception as e:
              logging.info('Error getting the attribute "hostIP"')
              raise Exception('Error getting the attribute "hostIP"')
            
          if currAttr=='TFTPPrefix':
            try:
              oNATEngine.tftp_prefix = oVBoxNATEngine.TFTPPrefix
            except Exception as e:
              logging.info('Error getting the attribute "TFTPPrefix"')
              raise Exception('Error getting the attribute "TFTPPrefix"')
            
          if currAttr=='TFTPBootFile':
            try:
              oNATEngine.tftp_boot_file = oVBoxNATEngine.TFTPBootFile
            except Exception as e:
              logging.info('Error getting the attribute "TFTPBootFile"')
              raise Exception('Error getting the attribute "TFTPBootFile"')
            
          if currAttr=='TFTPNextServer':
            try:
              oNATEngine.tftp_next_server = oVBoxNATEngine.TFTPNextServer
            except Exception as e:
              logging.info('Error getting the attribute "TFTPNextServer"')
              raise Exception('Error getting the attribute "TFTPNextServer"')
            
          if currAttr=='aliasMode':
            try:
              oNATEngine.alias_mode = oVBoxNATEngine.aliasMode
            except Exception as e:
              logging.info('Error getting the attribute "aliasMode"')
              raise Exception('Error getting the attribute "aliasMode"')
            
          if currAttr=='DNSPassDomain':
            try:
              oNATEngine.dns_pass_domain = oVBoxNATEngine.DNSPassDomain
            except Exception as e:
              logging.info('Error getting the attribute "DNSPassDomain"')
              raise Exception('Error getting the attribute "DNSPassDomain"')
            
          if currAttr=='DNSProxy':
            try:
              oNATEngine.dns_proxy = oVBoxNATEngine.DNSProxy
            except Exception as e:
              logging.info('Error getting the attribute "DNSProxy"')
              raise Exception('Error getting the attribute "DNSProxy"')
            
          if currAttr=='DNSUseHostResolver':
            try:
              oNATEngine.dns_use_host_resolver = oVBoxNATEngine.DNSUseHostResolver
            except Exception as e:
              logging.info('Error getting the attribute "DNSUseHostResolver"')
              raise Exception('Error getting the attribute "DNSUseHostResolver"')
            
          if currAttr=='redirects':
            try:
              ol_redirects = ctx['global'].getArray(oVBoxNATEngine,'redirects')
              oNATEngine.redirects = list()
              for count, item in enumerate(ol_redirects):
                oNATEngine.redirects.append(item)
            except Exception as e:
              logging.info('Error getting the array of "redirects"')
              raise Exception('Error getting the array of "redirects"')
            
          if currAttr=='localhostReachable':
            try:
              oNATEngine.localhost_reachable = oVBoxNATEngine.localhostReachable
            except Exception as e:
              logging.info('Error getting the attribute "localhostReachable"')
              raise Exception('Error getting the attribute "localhostReachable"')
            
  except Exception as e:
    logging.info('Abnormal function exit')
    oNATEngine = None
    text = 'Exception trying to fill the object oNATEngine. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oNATEngine

def i_fill_network_adapter(oVBoxNetworkAdapter, select=None):
  """Convert the passed VirtualBox object oVBoxNetworkAdapter with interface INetworkAdapter into Swagger object oNetworkAdapter"""
  
  logging.info('Enter function ')
  oNetworkAdapter = NetworkAdapter()
  try:
    if oVBoxNetworkAdapter is not None:
      if select is not None and len(select)>0:
        oNetworkAdapter = i_fill_partial_network_adapter(oVBoxNetworkAdapter, select)
      else:
        oNetworkAdapter = i_fill_whole_network_adapter(oVBoxNetworkAdapter)
  except Exception as e:
    logging.info('Abnormal function exit')
    oNetworkAdapter = None
    text = 'Exception trying to convert the VirtualBox object oVBoxNetworkAdapter into Swagger object oNetworkAdapter. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oNetworkAdapter

def i_fill_whole_network_adapter(oVBoxNetworkAdapter):
  logging.info('Enter function ')
  oNetworkAdapter = NetworkAdapter()
  try:
    if oVBoxNetworkAdapter is not None:
      try:
        oNetworkAdapter.adapter_type = ctx[ 'global'].getEnumValueName('NetworkAdapterType', oVBoxNetworkAdapter.adapterType)
      except Exception as e:
        logging.info('Error getting the attribute "adapterType"')
      try:
        oNetworkAdapter.slot = oVBoxNetworkAdapter.slot
      except Exception as e:
        logging.info('Error getting the attribute "slot"')
      try:
        oNetworkAdapter.enabled = oVBoxNetworkAdapter.enabled
      except Exception as e:
        logging.info('Error getting the attribute "enabled"')
      try:
        oNetworkAdapter.mac_address = oVBoxNetworkAdapter.MACAddress
      except Exception as e:
        logging.info('Error getting the attribute "MACAddress"')
      try:
        oNetworkAdapter.attachment_type = ctx[ 'global'].getEnumValueName('NetworkAttachmentType', oVBoxNetworkAdapter.attachmentType)
      except Exception as e:
        logging.info('Error getting the attribute "attachmentType"')
      try:
        oNetworkAdapter.bridged_interface = oVBoxNetworkAdapter.bridgedInterface
      except Exception as e:
        logging.info('Error getting the attribute "bridgedInterface"')
      try:
        oNetworkAdapter.host_only_interface = oVBoxNetworkAdapter.hostOnlyInterface
      except Exception as e:
        logging.info('Error getting the attribute "hostOnlyInterface"')
      try:
        oNetworkAdapter.host_only_network = oVBoxNetworkAdapter.hostOnlyNetwork
      except Exception as e:
        logging.info('Error getting the attribute "hostOnlyNetwork"')
      try:
        oNetworkAdapter.internal_network = oVBoxNetworkAdapter.internalNetwork
      except Exception as e:
        logging.info('Error getting the attribute "internalNetwork"')
      try:
        oNetworkAdapter.nat_network = oVBoxNetworkAdapter.NATNetwork
      except Exception as e:
        logging.info('Error getting the attribute "NATNetwork"')
      try:
        oNetworkAdapter.generic_driver = oVBoxNetworkAdapter.genericDriver
      except Exception as e:
        logging.info('Error getting the attribute "genericDriver"')
      try:
        oNetworkAdapter.cloud_network = oVBoxNetworkAdapter.cloudNetwork
      except Exception as e:
        logging.info('Error getting the attribute "cloudNetwork"')
      try:
        oNetworkAdapter.cable_connected = oVBoxNetworkAdapter.cableConnected
      except Exception as e:
        logging.info('Error getting the attribute "cableConnected"')
      try:
        oNetworkAdapter.line_speed = oVBoxNetworkAdapter.lineSpeed
      except Exception as e:
        logging.info('Error getting the attribute "lineSpeed"')
      try:
        oNetworkAdapter.promisc_mode_policy = ctx[ 'global'].getEnumValueName('NetworkAdapterPromiscModePolicy', oVBoxNetworkAdapter.promiscModePolicy)
      except Exception as e:
        logging.info('Error getting the attribute "promiscModePolicy"')
      try:
        oNetworkAdapter.trace_enabled = oVBoxNetworkAdapter.traceEnabled
      except Exception as e:
        logging.info('Error getting the attribute "traceEnabled"')
      try:
        oNetworkAdapter.trace_file = oVBoxNetworkAdapter.traceFile
      except Exception as e:
        logging.info('Error getting the attribute "traceFile"')
      try:
        o_nat_engine = oVBoxNetworkAdapter.NATEngine if oVBoxNetworkAdapter.NATEngine is not None else None
        if o_nat_engine is not None:
          oNetworkAdapter.nat_engine = i_fill_nat_engine(o_nat_engine)
        else:
          oNetworkAdapter.nat_engine = None
      except Exception as e:
        logging.info('Error getting the interface object "NATEngine"')
      try:
        oNetworkAdapter.boot_priority = oVBoxNetworkAdapter.bootPriority
      except Exception as e:
        logging.info('Error getting the attribute "bootPriority"')
      try:
        o_bandwidth_group = oVBoxNetworkAdapter.bandwidthGroup if oVBoxNetworkAdapter.bandwidthGroup is not None else None
        if o_bandwidth_group is not None:
          oNetworkAdapter.bandwidth_group = i_fill_bandwidth_group(o_bandwidth_group)
        else:
          oNetworkAdapter.bandwidth_group = None
      except Exception as e:
        logging.info('Error getting the interface object "bandwidthGroup"')
      
  except Exception as e:
    logging.info('Abnormal function exit')
    oNetworkAdapter = None
    text = 'Exception trying to fill the object oNetworkAdapter. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oNetworkAdapter

def i_fill_partial_network_adapter(oVBoxNetworkAdapter, select):
  logging.info('Enter function ')
  oNetworkAdapter = NetworkAdapter()
  try:
    if oVBoxNetworkAdapter is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='adapterType':
            try:
              oNetworkAdapter.adapter_type = ctx[ 'global'].getEnumValueName('NetworkAdapterType', oVBoxNetworkAdapter.adapterType)
            except Exception as e:
              logging.info('Error getting the attribute "adapterType"')
              raise Exception('Error getting the array of "adapterType"')
            
          if currAttr=='slot':
            try:
              oNetworkAdapter.slot = oVBoxNetworkAdapter.slot
            except Exception as e:
              logging.info('Error getting the attribute "slot"')
              raise Exception('Error getting the attribute "slot"')
            
          if currAttr=='enabled':
            try:
              oNetworkAdapter.enabled = oVBoxNetworkAdapter.enabled
            except Exception as e:
              logging.info('Error getting the attribute "enabled"')
              raise Exception('Error getting the attribute "enabled"')
            
          if currAttr=='MACAddress':
            try:
              oNetworkAdapter.mac_address = oVBoxNetworkAdapter.MACAddress
            except Exception as e:
              logging.info('Error getting the attribute "MACAddress"')
              raise Exception('Error getting the attribute "MACAddress"')
            
          if currAttr=='attachmentType':
            try:
              oNetworkAdapter.attachment_type = ctx[ 'global'].getEnumValueName('NetworkAttachmentType', oVBoxNetworkAdapter.attachmentType)
            except Exception as e:
              logging.info('Error getting the attribute "attachmentType"')
              raise Exception('Error getting the array of "attachmentType"')
            
          if currAttr=='bridgedInterface':
            try:
              oNetworkAdapter.bridged_interface = oVBoxNetworkAdapter.bridgedInterface
            except Exception as e:
              logging.info('Error getting the attribute "bridgedInterface"')
              raise Exception('Error getting the attribute "bridgedInterface"')
            
          if currAttr=='hostOnlyInterface':
            try:
              oNetworkAdapter.host_only_interface = oVBoxNetworkAdapter.hostOnlyInterface
            except Exception as e:
              logging.info('Error getting the attribute "hostOnlyInterface"')
              raise Exception('Error getting the attribute "hostOnlyInterface"')
            
          if currAttr=='hostOnlyNetwork':
            try:
              oNetworkAdapter.host_only_network = oVBoxNetworkAdapter.hostOnlyNetwork
            except Exception as e:
              logging.info('Error getting the attribute "hostOnlyNetwork"')
              raise Exception('Error getting the attribute "hostOnlyNetwork"')
            
          if currAttr=='internalNetwork':
            try:
              oNetworkAdapter.internal_network = oVBoxNetworkAdapter.internalNetwork
            except Exception as e:
              logging.info('Error getting the attribute "internalNetwork"')
              raise Exception('Error getting the attribute "internalNetwork"')
            
          if currAttr=='NATNetwork':
            try:
              oNetworkAdapter.nat_network = oVBoxNetworkAdapter.NATNetwork
            except Exception as e:
              logging.info('Error getting the attribute "NATNetwork"')
              raise Exception('Error getting the attribute "NATNetwork"')
            
          if currAttr=='genericDriver':
            try:
              oNetworkAdapter.generic_driver = oVBoxNetworkAdapter.genericDriver
            except Exception as e:
              logging.info('Error getting the attribute "genericDriver"')
              raise Exception('Error getting the attribute "genericDriver"')
            
          if currAttr=='cloudNetwork':
            try:
              oNetworkAdapter.cloud_network = oVBoxNetworkAdapter.cloudNetwork
            except Exception as e:
              logging.info('Error getting the attribute "cloudNetwork"')
              raise Exception('Error getting the attribute "cloudNetwork"')
            
          if currAttr=='cableConnected':
            try:
              oNetworkAdapter.cable_connected = oVBoxNetworkAdapter.cableConnected
            except Exception as e:
              logging.info('Error getting the attribute "cableConnected"')
              raise Exception('Error getting the attribute "cableConnected"')
            
          if currAttr=='lineSpeed':
            try:
              oNetworkAdapter.line_speed = oVBoxNetworkAdapter.lineSpeed
            except Exception as e:
              logging.info('Error getting the attribute "lineSpeed"')
              raise Exception('Error getting the attribute "lineSpeed"')
            
          if currAttr=='promiscModePolicy':
            try:
              oNetworkAdapter.promisc_mode_policy = ctx[ 'global'].getEnumValueName('NetworkAdapterPromiscModePolicy', oVBoxNetworkAdapter.promiscModePolicy)
            except Exception as e:
              logging.info('Error getting the attribute "promiscModePolicy"')
              raise Exception('Error getting the array of "promiscModePolicy"')
            
          if currAttr=='traceEnabled':
            try:
              oNetworkAdapter.trace_enabled = oVBoxNetworkAdapter.traceEnabled
            except Exception as e:
              logging.info('Error getting the attribute "traceEnabled"')
              raise Exception('Error getting the attribute "traceEnabled"')
            
          if currAttr=='traceFile':
            try:
              oNetworkAdapter.trace_file = oVBoxNetworkAdapter.traceFile
            except Exception as e:
              logging.info('Error getting the attribute "traceFile"')
              raise Exception('Error getting the attribute "traceFile"')
            
          if currAttr=='NATEngine':
            try:
              o_nat_engine = oVBoxNetworkAdapter.NATEngine if oVBoxNetworkAdapter.NATEngine is not None else None
              if o_nat_engine is not None:
                oNetworkAdapter.nat_engine = i_fill_nat_engine(o_nat_engine)
              else:
                oNetworkAdapter.nat_engine = None
            except Exception as e:
              logging.info('Error getting the interface object "NATEngine"')
              raise Exception('Error getting the interface object "NATEngine"')
            
          if currAttr=='bootPriority':
            try:
              oNetworkAdapter.boot_priority = oVBoxNetworkAdapter.bootPriority
            except Exception as e:
              logging.info('Error getting the attribute "bootPriority"')
              raise Exception('Error getting the attribute "bootPriority"')
            
          if currAttr=='bandwidthGroup':
            try:
              o_bandwidth_group = oVBoxNetworkAdapter.bandwidthGroup if oVBoxNetworkAdapter.bandwidthGroup is not None else None
              if o_bandwidth_group is not None:
                oNetworkAdapter.bandwidth_group = i_fill_bandwidth_group(o_bandwidth_group)
              else:
                oNetworkAdapter.bandwidth_group = None
            except Exception as e:
              logging.info('Error getting the interface object "bandwidthGroup"')
              raise Exception('Error getting the interface object "bandwidthGroup"')
            
  except Exception as e:
    logging.info('Abnormal function exit')
    oNetworkAdapter = None
    text = 'Exception trying to fill the object oNetworkAdapter. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oNetworkAdapter

def i_fill_storage_controller(oVBoxStorageController, select=None):
  """Convert the passed VirtualBox object oVBoxStorageController with interface IStorageController into Swagger object oStorageController"""
  logging.info('Enter function ')
  oStorageController = StorageController()
  try:
    if oVBoxStorageController is not None:
      if select is not None and len(select)>0:
        oStorageController = i_fill_partial_storage_controller(oVBoxStorageController, select)
      else:
        oStorageController = i_fill_whole_storage_controller(oVBoxStorageController)
  except Exception as e:
    logging.info('Abnormal function exit')
    oStorageController = None
    text = 'Exception trying to convert the VirtualBox object oVBoxStorageController into Swagger object oStorageController. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oStorageController

def i_fill_whole_storage_controller(oVBoxStorageController):
  logging.info('Enter function ')
  oStorageController = StorageController()
  try:
    if oVBoxStorageController is not None:
      try:
        oStorageController.name = oVBoxStorageController.name
      except Exception as e:
        logging.info('Error getting the attribute "name"')
      try:
        oStorageController.max_devices_per_port_count = oVBoxStorageController.maxDevicesPerPortCount
      except Exception as e:
        logging.info('Error getting the attribute "maxDevicesPerPortCount"')
      try:
        oStorageController.min_port_count = oVBoxStorageController.minPortCount
      except Exception as e:
        logging.info('Error getting the attribute "minPortCount"')
      try:
        oStorageController.max_port_count = oVBoxStorageController.maxPortCount
      except Exception as e:
        logging.info('Error getting the attribute "maxPortCount"')
      try:
        oStorageController.instance = oVBoxStorageController.instance
      except Exception as e:
        logging.info('Error getting the attribute "instance"')
      try:
        oStorageController.port_count = oVBoxStorageController.portCount
      except Exception as e:
        logging.info('Error getting the attribute "portCount"')
      try:
        oStorageController.bus = ctx[ 'global'].getEnumValueName('StorageBus', oVBoxStorageController.bus)
      except Exception as e:
        logging.info('Error getting the attribute "bus"')
      try:
        oStorageController.controller_type = ctx[ 'global'].getEnumValueName('StorageControllerType', oVBoxStorageController.controllerType)
      except Exception as e:
        logging.info('Error getting the attribute "controllerType"')
      try:
        oStorageController.use_host_io_cache = oVBoxStorageController.useHostIOCache
      except Exception as e:
        logging.info('Error getting the attribute "useHostIOCache"')
      try:
        oStorageController.bootable = oVBoxStorageController.bootable
      except Exception as e:
        logging.info('Error getting the attribute "bootable"')

  except Exception as e:
    logging.info('Abnormal function exit')
    oStorageController = None
    text = 'Exception trying to fill the object oStorageController. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oStorageController

def i_fill_partial_storage_controller(oVBoxStorageController, select):
  logging.info('Enter function ')
  oStorageController = StorageController()
  try:
    if oVBoxStorageController is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='name':
            try:
              oStorageController.name = oVBoxStorageController.name
            except Exception as e:
              logging.info('Error getting the attribute "name"')
              raise Exception('Error getting the attribute "name"')

          if currAttr=='maxDevicesPerPortCount':
            try:
              oStorageController.max_devices_per_port_count = oVBoxStorageController.maxDevicesPerPortCount
            except Exception as e:
              logging.info('Error getting the attribute "maxDevicesPerPortCount"')
              raise Exception('Error getting the attribute "maxDevicesPerPortCount"')

          if currAttr=='minPortCount':
            try:
              oStorageController.min_port_count = oVBoxStorageController.minPortCount
            except Exception as e:
              logging.info('Error getting the attribute "minPortCount"')
              raise Exception('Error getting the attribute "minPortCount"')

          if currAttr=='maxPortCount':
            try:
              oStorageController.max_port_count = oVBoxStorageController.maxPortCount
            except Exception as e:
              logging.info('Error getting the attribute "maxPortCount"')
              raise Exception('Error getting the attribute "maxPortCount"')

          if currAttr=='instance':
            try:
              oStorageController.instance = oVBoxStorageController.instance
            except Exception as e:
              logging.info('Error getting the attribute "instance"')
              raise Exception('Error getting the attribute "instance"')

          if currAttr=='portCount':
            try:
              oStorageController.port_count = oVBoxStorageController.portCount
            except Exception as e:
              logging.info('Error getting the attribute "portCount"')
              raise Exception('Error getting the attribute "portCount"')

          if currAttr=='bus':
            try:
              oStorageController.bus = ctx[ 'global'].getEnumValueName('StorageBus', oVBoxStorageController.bus)
            except Exception as e:
              logging.info('Error getting the attribute "bus"')
              raise Exception('Error getting the array of "bus"')

          if currAttr=='controllerType':
            try:
              oStorageController.controller_type = ctx[ 'global'].getEnumValueName('StorageControllerType', oVBoxStorageController.controllerType)
            except Exception as e:
              logging.info('Error getting the attribute "controllerType"')
              raise Exception('Error getting the array of "controllerType"')

          if currAttr=='useHostIOCache':
            try:
              oStorageController.use_host_io_cache = oVBoxStorageController.useHostIOCache
            except Exception as e:
              logging.info('Error getting the attribute "useHostIOCache"')
              raise Exception('Error getting the attribute "useHostIOCache"')

          if currAttr=='bootable':
            try:
              oStorageController.bootable = oVBoxStorageController.bootable
            except Exception as e:
              logging.info('Error getting the attribute "bootable"')
              raise Exception('Error getting the attribute "bootable"')
            
  except Exception as e:
    logging.info('Abnormal function exit')
    oStorageController = None
    text = 'Exception trying to fill the object oStorageController. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oStorageController

def i_fill_usb_controller(oVBoxUSBController, select=None):
  """Convert the passed VirtualBox object oVBoxUSBController with interface IUSBController into Swagger object oUSBController"""
  
  logging.info('Enter function ')
  oUSBController = USBController()
  try:
    if oVBoxUSBController is not None:
      if select is not None and len(select)>0:
        oUSBController = i_fill_partial_usb_controller(oVBoxUSBController, select)
      else:
        oUSBController = i_fill_whole_usb_controller(oVBoxUSBController)
  except Exception as e:
    logging.info('Abnormal function exit')
    oUSBController = None
    text = 'Exception trying to convert the VirtualBox object oVBoxUSBController into Swagger object oUSBController. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oUSBController

def i_fill_whole_usb_controller(oVBoxUSBController):
  logging.info('Enter function ')
  oUSBController = USBController()
  try:
    if oVBoxUSBController is not None:
      try:
        oUSBController.name = oVBoxUSBController.name
      except Exception as e:
        logging.info('Error getting the attribute "name"')
      try:
        oUSBController.type = ctx[ 'global'].getEnumValueName('USBControllerType', oVBoxUSBController.type)
      except Exception as e:
        logging.info('Error getting the attribute "type"')
      try:
        oUSBController.usb_standard = oVBoxUSBController.USBStandard
      except Exception as e:
        logging.info('Error getting the attribute "USBStandard"')
      
  except Exception as e:
    logging.info('Abnormal function exit')
    oUSBController = None
    text = 'Exception trying to fill the object oUSBController. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oUSBController

def i_fill_partial_usb_controller(oVBoxUSBController, select):
  logging.info('Enter function ')
  oUSBController = USBController()
  try:
    if oVBoxUSBController is not None:
      olAttributesList = list()
      if select is not None and len(select) > 0:
        olAttributesList = select.split(',')
        logging.info(olAttributesList)
        for attr in olAttributesList:
          currAttr = attr
          if currAttr=='name':
            try:
              oUSBController.name = oVBoxUSBController.name
            except Exception as e:
              logging.info('Error getting the attribute "name"')
              raise Exception('Error getting the attribute "name"')
            
          if currAttr=='type':
            try:
              oUSBController.type = ctx[ 'global'].getEnumValueName('USBControllerType', oVBoxUSBController.type)
            except Exception as e:
              logging.info('Error getting the attribute "type"')
              raise Exception('Error getting the array of "type"')
            
          if currAttr=='USBStandard':
            try:
              oUSBController.usb_standard = oVBoxUSBController.USBStandard
            except Exception as e:
              logging.info('Error getting the attribute "USBStandard"')
              raise Exception('Error getting the attribute "USBStandard"')
            
  except Exception as e:
    logging.info('Abnormal function exit')
    oUSBController = None
    text = 'Exception trying to fill the object oUSBController. '
    exceptionText = str(e)
    raise Exception(text +  ' {Original: ' + exceptionText + '} ')
  logging.info('Normal function exit')
  return oUSBController
