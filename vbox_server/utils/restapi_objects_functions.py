import logging
from vbox_server.global_settings import *

# import interfaces
from vbox_server.models.virtual_box import VirtualBox
from vbox_server.models.machine import Machine
from vbox_server.models.shared_folder import SharedFolder
from vbox_server.models.medium_attachment import MediumAttachment
from vbox_server.models.bandwidth_group_type import BandwidthGroupType
from vbox_server.models.bandwidth_group import BandwidthGroup
from vbox_server.models.medium import Medium
from vbox_server.models.medium_format import MediumFormat

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

      oVirtualBox.hard_disks = list()
      try:
        ol_hard_disks = ctx['global'].getArray(oVBoxVirtualBox,'hardDisks')
        for count, item in enumerate(ol_hard_disks):
          o = i_fill_medium(item)
          oVirtualBox.hard_disks.append(o)
      except Exception as e:
        logging.info('Error getting the array of "hardDisks"')
      oVirtualBox.dvd_images = list()
      try:
        ol_dvd_images = ctx['global'].getArray(oVBoxVirtualBox,'DVDImages')
        for count, item in enumerate(ol_dvd_images):
          o = i_fill_medium(item)
          oVirtualBox.dvd_images.append(o)
      except Exception as e:
        logging.info('Error getting the array of "DVDImages"')
      oVirtualBox.floppy_images = list()
      try:
        ol_floppy_images = ctx['global'].getArray(oVBoxVirtualBox,'floppyImages')
        for count, item in enumerate(ol_floppy_images):
          o = i_fill_medium(item)
          oVirtualBox.floppy_images.append(o)
      except Exception as e:
        logging.info('Error getting the array of "floppyImages"')

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

          if currAttr=='hardDisks':
            oVirtualBox.hard_disks = list()
            try:
              ol_hard_disks = ctx['global'].getArray(oVBoxVirtualBox,'hardDisks')
              for count, item in enumerate(ol_hard_disks):
                o = i_fill_medium(item)
                oVirtualBox.hard_disks.append(o)
            except Exception as e:
              logging.info('Error getting the array of "hardDisks"')
              raise Exception('Error getting the array of "hardDisks"')

          if currAttr=='DVDImages':
            oVirtualBox.dvd_images = list()
            try:
              ol_dvd_images = ctx['global'].getArray(oVBoxVirtualBox,'DVDImages')
              for count, item in enumerate(ol_dvd_images):
                o = i_fill_medium(item)
                oVirtualBox.dvd_images.append(o)
            except Exception as e:
              logging.info('Error getting the array of "DVDImages"')
              raise Exception('Error getting the array of "DVDImages"')

          if currAttr=='floppyImages':
            oVirtualBox.floppy_images = list()
            try:
              ol_floppy_images = ctx['global'].getArray(oVBoxVirtualBox,'floppyImages')
              for count, item in enumerate(ol_floppy_images):
                o = i_fill_medium(item)
                oVirtualBox.floppy_images.append(o)
            except Exception as e:
              logging.info('Error getting the array of "floppyImages"')
              raise Exception('Error getting the array of "floppyImages"')
            
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
      oMachine.icon = list()
      try:
        ol_icon = ctx['global'].getArray(oVBoxMachine,'icon')
        for count, item in enumerate(ol_icon):
          oMachine.icon.append(item)
      except Exception as e:
        logging.info('Error getting the array of "icon"')
      try:
        oMachine.accessible = oVBoxMachine.accessible
      except Exception as e:
        logging.info('Error getting the attribute "accessible"')
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
      oMachine.groups = list()
      try:
        ol_groups = ctx['global'].getArray(oVBoxMachine,'groups')
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
        oMachine.snapshot_folder = oVBoxMachine.snapshotFolder
      except Exception as e:
        logging.info('Error getting the attribute "snapshotFolder"')
      try:
        oMachine.emulated_usb_card_reader_enabled = oVBoxMachine.emulatedUSBCardReaderEnabled
      except Exception as e:
        logging.info('Error getting the attribute "emulatedUSBCardReaderEnabled"')
      try:
        oMachine.settings_file_path = oVBoxMachine.settingsFilePath
      except Exception as e:
        logging.info('Error getting the attribute "settingsFilePath"')
      try:
        oMachine.session_name = oVBoxMachine.sessionName
      except Exception as e:
        logging.info('Error getting the attribute "sessionName"')
      try:
        oMachine.session_pid = oVBoxMachine.sessionPID
      except Exception as e:
        logging.info('Error getting the attribute "sessionPID"')
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
        oMachine.clipboard_file_transfers_enabled = oVBoxMachine.clipboardFileTransfersEnabled
      except Exception as e:
        logging.info('Error getting the attribute "clipboardFileTransfersEnabled"')
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
        oMachine.default_frontend = oVBoxMachine.defaultFrontend
      except Exception as e:
        logging.info('Error getting the attribute "defaultFrontend"')
      try:
        oMachine.usb_proxy_available = oVBoxMachine.USBProxyAvailable
      except Exception as e:
        logging.info('Error getting the attribute "USBProxyAvailable"')
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

      oMachine.vm_process_priority = ctx[ 'global'].getEnumValueName('VMProcPriority', oVBoxMachine.VMProcessPriority)
      oMachine.autostop_type = ctx[ 'global'].getEnumValueName('AutostopType', oVBoxMachine.autostopType)
      oMachine.paravirt_provider = ctx[ 'global'].getEnumValueName('ParavirtProvider', oVBoxMachine.paravirtProvider)
      oMachine.dn_d_mode = ctx[ 'global'].getEnumValueName('DnDMode', oVBoxMachine.dnDMode)
      oMachine.clipboard_mode = ctx[ 'global'].getEnumValueName('ClipboardMode', oVBoxMachine.clipboardMode)
      oMachine.state = ctx[ 'global'].getEnumValueName('MachineState', oVBoxMachine.state)
      oMachine.session_state = ctx[ 'global'].getEnumValueName('SessionState', oVBoxMachine.sessionState)
      oMachine.pointing_hid_type = ctx[ 'global'].getEnumValueName('PointingHIDType', oVBoxMachine.pointingHIDType)
      oMachine.keyboard_hid_type = ctx[ 'global'].getEnumValueName('KeyboardHIDType', oVBoxMachine.keyboardHIDType)

      oMachine.shared_folders = list()
      try:
        ol_shared_folders = ctx['global'].getArray(oVBoxMachine,'sharedFolders')
        for count, item in enumerate(ol_shared_folders):
          o = i_fill_shared_folder(item)
          oMachine.shared_folders.append(o)
      except Exception as e:
        logging.info('Error getting the array of "sharedFolders"')
      oMachine.medium_attachments = list()
      try:
        ol_medium_attachments = ctx['global'].getArray(oVBoxMachine,'mediumAttachments')
        for count, item in enumerate(ol_medium_attachments):
          o = i_fill_medium_attachment(item)
          oMachine.medium_attachments.append(o)
      except Exception as e:
        logging.info('Error getting the array of "mediumAttachments"')

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
            oMachine.icon = list()
            try:
              ol_icon = ctx['global'].getArray(oVBoxMachine,'icon')
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
            oMachine.groups = list()
            try:
              ol_groups = ctx['global'].getArray(oVBoxMachine,'groups')
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

          if currAttr=='settingsFilePath':
            try:
              oMachine.settings_file_path = oVBoxMachine.settingsFilePath
            except Exception as e:
              logging.info('Error getting the attribute "settingsFilePath"')
              raise Exception('Error getting the attribute "settingsFilePath"')

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

          if currAttr=='clipboardFileTransfersEnabled':
            try:
              oMachine.clipboard_file_transfers_enabled = oVBoxMachine.clipboardFileTransfersEnabled
            except Exception as e:
              logging.info('Error getting the attribute "clipboardFileTransfersEnabled"')
              raise Exception('Error getting the attribute "clipboardFileTransfersEnabled"')

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

          if currAttr=='VMProcessPriority':
            oMachine.vm_process_priority = ctx[ 'global'].getEnumValueName('VMProcPriority', oVBoxMachine.VMProcessPriority)
          if currAttr=='autostopType':
            oMachine.autostop_type = ctx[ 'global'].getEnumValueName('AutostopType', oVBoxMachine.autostopType)
          if currAttr=='paravirtProvider':
            oMachine.paravirt_provider = ctx[ 'global'].getEnumValueName('ParavirtProvider', oVBoxMachine.paravirtProvider)
          if currAttr=='dnDMode':
            oMachine.dn_d_mode = ctx[ 'global'].getEnumValueName('DnDMode', oVBoxMachine.dnDMode)
          if currAttr=='clipboardMode':
            oMachine.clipboard_mode = ctx[ 'global'].getEnumValueName('ClipboardMode', oVBoxMachine.clipboardMode)
          if currAttr=='state':
            oMachine.state = ctx[ 'global'].getEnumValueName('MachineState', oVBoxMachine.state)
          if currAttr=='sessionState':
            oMachine.session_state = ctx[ 'global'].getEnumValueName('SessionState', oVBoxMachine.sessionState)
          if currAttr=='pointingHIDType':
            oMachine.pointing_hid_type = ctx[ 'global'].getEnumValueName('PointingHIDType', oVBoxMachine.pointingHIDType)
          if currAttr=='keyboardHIDType':
            oMachine.keyboard_hid_type = ctx[ 'global'].getEnumValueName('KeyboardHIDType', oVBoxMachine.keyboardHIDType)

          if currAttr=='sharedFolders':
            oMachine.shared_folders = list()
            try:
              ol_shared_folders = ctx['global'].getArray(oVBoxMachine,'sharedFolders')
              for count, item in enumerate(ol_shared_folders):
                o = i_fill_shared_folder(item)
                oMachine.shared_folders.append(o)
            except Exception as e:
              logging.info('Error getting the array of "sharedFolders"')
              raise Exception('Error getting the array of "sharedFolders"')

          if currAttr=='mediumAttachments':
            oMachine.medium_attachments = list()
            try:
              ol_medium_attachments = ctx['global'].getArray(oVBoxMachine,'mediumAttachments')
              for count, item in enumerate(ol_medium_attachments):
                o = i_fill_medium_attachment(item)
                oMachine.medium_attachments.append(o)
            except Exception as e:
              logging.info('Error getting the array of "mediumAttachments"')
              raise Exception('Error getting the array of "mediumAttachments"')

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
      oMediumAttachment.type = ctx[ 'global'].getEnumValueName('DeviceType', oVBoxMediumAttachment.type)
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
            oMediumAttachment.type = ctx[ 'global'].getEnumValueName('DeviceType', oVBoxMediumAttachment.type)

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
      oBandwidthGroup.type = ctx[ 'global'].getEnumValueName('BandwidthGroupType', oVBoxBandwidthGroup.type)
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
            oBandwidthGroup.type = ctx[ 'global'].getEnumValueName('BandwidthGroupType', oVBoxBandwidthGroup.type)

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
      oMedium.state = ctx[ 'global'].getEnumValueName('MediumState', oVBoxMedium.state)
      oMedium.variant = list()
      try:
        ol_variant = ctx['global'].getArray(oVBoxMedium, 'variant')
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
      oMedium.device_type = ctx[ 'global'].getEnumValueName('DeviceType', oVBoxMedium.deviceType)
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
      oMedium.type = ctx[ 'global'].getEnumValueName('MediumType', oVBoxMedium.type)
      oMedium.allowed_types = list()
      try:
        ol_allowed_types = ctx['global'].getArray(oVBoxMedium, 'allowedTypes')
        for count, item in enumerate(ol_allowed_types):
          o = ctx['global'].getEnumValueName('MediumType', item)
          if oMedium.allowed_types.count(o) == 0 : oMedium.allowed_types.append(o)
      except Exception as e:
        logging.info('Error getting the array of "allowedTypes"')
      try:
        oMedium.parent = oVBoxMedium.parent.id
      except Exception as e:
        logging.info('Error getting the attribute "parent"')
      oMedium.children = list()
      try:
        ol_children = ctx['global'].getArray(oVBoxMedium,'children')
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
      oMedium.machine_ids = list()
      try:
        ol_machine_ids = ctx['global'].getArray(oVBoxMedium,'machineIds')
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
            oMedium.state = ctx[ 'global'].getEnumValueName('MediumState', oVBoxMedium.state)

          if currAttr=='variant':
            oMedium.variant = list()
            try:
              ol_variant = ctx['global'].getArray(oVBoxMedium, 'variant')
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
            oMedium.device_type = ctx[ 'global'].getEnumValueName('DeviceType', oVBoxMedium.deviceType)

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
            oMedium.type = ctx[ 'global'].getEnumValueName('MediumType', oVBoxMedium.type)

          if currAttr=='allowedTypes':
            oMedium.allowed_types = list()
            try:
              ol_allowed_types = ctx['global'].getArray(oVBoxMedium, 'allowedTypes')
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
            oMedium.children = list()
            try:
              ol_children = ctx['global'].getArray(oVBoxMedium,'children')
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
            oMedium.machine_ids = list()
            try:
              ol_machine_ids = ctx['global'].getArray(oVBoxMedium,'machineIds')
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
      oMediumFormat.capabilities = list()
      try:
        ol_capabilities = ctx['global'].getArray(oVBoxMediumFormat, 'capabilities')
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
            oMediumFormat.capabilities = list()
            try:
              ol_capabilities = ctx['global'].getArray(oVBoxMediumFormat, 'capabilities')
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
