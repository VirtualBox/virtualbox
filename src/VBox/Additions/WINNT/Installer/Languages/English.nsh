; $Id: English.nsh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
;; @file
; NLS for English language.
;

;
; Copyright (C) 2006-2024 Oracle and/or its affiliates.
;
; This file is part of VirtualBox base platform packages, as
; available from https://www.virtualbox.org.
;
; This program is free software; you can redistribute it and/or
; modify it under the terms of the GNU General Public License
; as published by the Free Software Foundation, in version 3 of the
; License.
;
; This program is distributed in the hope that it will be useful, but
; WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
; General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program; if not, see <https://www.gnu.org/licenses>.
;
; SPDX-License-Identifier: GPL-3.0-only
;

LangString VBOX_TEST ${LANG_ENGLISH}                                "This is a test message of $(^Name)!"

LangString VBOX_NOADMIN ${LANG_ENGLISH}                             "You need administrator rights to install or uninstall the $(^Name).$\r$\nThis application will exit now."

LangString VBOX_NOTICE_ARCH_X86 ${LANG_ENGLISH}                     "This application only runs on 32-bit Windows systems. Please install the 64-bit version of $(^Name)!"
LangString VBOX_NOTICE_ARCH_AMD64 ${LANG_ENGLISH}                   "This application only runs on 64-bit Windows systems. Please install the 32-bit version of $(^Name)!"
LangString VBOX_NT4_NO_SP6 ${LANG_ENGLISH}                          "You do not seem to have Service Pack 6 for Windows NT4 installed.$\r$\nWe recommend that you install it first. Do you wish to continue anyway?"

LangString VBOX_PLATFORM_UNSUPPORTED ${LANG_ENGLISH}                "The VirtualBox Guest Additions cannot be installed on this version of Windows"

LangString VBOX_SUN_FOUND ${LANG_ENGLISH}                           "An old version of the Sun VirtualBox Guest Additions is installed in this virtual machine. This must be uninstalled before the current Guest Additions can be installed.$\r$\n$\r$\nDo you wish to uninstall the old Guest Additions now?"
LangString VBOX_SUN_ABORTED ${LANG_ENGLISH}                         "The VirtualBox Guest Additions cannot be installed until the old version has been removed.  Please remove it and try again."

LangString VBOX_INNOTEK_FOUND ${LANG_ENGLISH}                       "An old version of the innotek VirtualBox Guest Additions is installed in this virtual machine. This must be uninstalled before the current Guest Additions can be installed.$\r$\n$\r$\nDo you wish to uninstall the old Guest Additions now?"
LangString VBOX_INNOTEK_ABORTED ${LANG_ENGLISH}                     "The VirtualBox Guest Additions cannot be installed until the old version has been removed.  Please remove it and try again."

LangString VBOX_UNINSTALL_START ${LANG_ENGLISH}                     "Press OK to start the uninstallation process. This may take some time to complete."
LangString VBOX_UNINSTALL_REBOOT ${LANG_ENGLISH}                    "It is strongly recommended that you restart this virtual machine before installing the new version of the VirtualBox Guest Additions.$\r$\nPlease start the installation process again after restarting the machine.$\r$\n$\r$\nRestart now?"

LangString VBOX_COMPONENT_MAIN ${LANG_ENGLISH}                      "VirtualBox Guest Additions"
LangString VBOX_COMPONENT_MAIN_DESC ${LANG_ENGLISH}                 "VirtualBox Guest Additions main files"

LangString VBOX_COMPONENT_AUTOLOGON ${LANG_ENGLISH}                 "Auto-Logon support"
LangString VBOX_COMPONENT_AUTOLOGON_DESC ${LANG_ENGLISH}            "Enables automatic log-on support for guests"
LangString VBOX_COMPONENT_AUTOLOGON_WARN_3RDPARTY ${LANG_ENGLISH}   "Another installed application already provides automatic log-on support.$\r$\nIf you replace this support with that provided by VirtualBox the system could become unstable.$\r$\nAre you sure you wish to proceed?"

LangString VBOX_COMPONENT_D3D  ${LANG_ENGLISH}                      "Direct3D Support (Experimental)"
LangString VBOX_COMPONENT_D3D_DESC  ${LANG_ENGLISH}                 "Enables Direct3D and OpenGL acceleration for guests. In order to use it the virtual machine requires 256 MB of video memory and enabled 3D acceleration in the $\"Display$\" section of the virtual machine settings."
LangString VBOX_COMPONENT_D3D_NO_SM ${LANG_ENGLISH}                 "Windows is currently not running in safe mode so Direct3D support cannot be installed."
LangString VBOX_COMPONENT_D3D_NOT_SUPPORTED ${LANG_ENGLISH}         "The VirtualBox Direct3D support is not available on Windows $g_strWinVersion!"
;LangString VBOX_COMPONENT_D3D_HINT_VRAM ${LANG_ENGLISH}             "Please note that in order to use 3D acceleration the virtual machine requires at least 128 MB of video memory with 256 MB recommended if you wish to use it with multiple monitors.$\r$\n$\r$\nIf necessary you can change the video memory size in the $\"Display$\" section of the virtual machine settings."
LangString VBOX_COMPONENT_D3D_INVALID ${LANG_ENGLISH}               "The setup detected a corrupt/invalid DirectX installation.$\r$\n$\r$\nIn order to get Direct3D support working, consulting the VirtualBox manual is highly recommended.$\r$\n$\r$\nContinue anyway?"
LangString VBOX_COMPONENT_D3D_INVALID_MANUAL ${LANG_ENGLISH}        "Shall the VirtualBox manual be displayed now to look for a solution?"

LangString VBOX_COMPONENT_STARTMENU ${LANG_ENGLISH}                 "Start menu entries"
LangString VBOX_COMPONENT_STARTMENU_DESC ${LANG_ENGLISH}            "Creates entries in the start menu"

LangString VBOX_WFP_WARN_REPLACE ${LANG_ENGLISH}                    "In order to make ${PRODUCT_NAME} work correctly some system files have been replaced.$\r$\nIf the Windows File Protection service offers to restore the original files you should cancel the request to retain the newly installed functionality."
LangString VBOX_REBOOT_REQUIRED ${LANG_ENGLISH}                     "To apply all changes, the system must be restarted. Restart Windows now?"

LangString VBOX_EXTRACTION_COMPLETE ${LANG_ENGLISH}                 "$(^Name): Files were successfully extracted to $\"$INSTDIR$\"!"

LangString VBOX_ERROR_INST_FAILED ${LANG_ENGLISH}                   "An error occurred during installation.$\r$\nPlease refer to the log file under '$INSTDIR\install_ui.log' for more information."
LangString VBOX_ERROR_OPEN_LINK ${LANG_ENGLISH}                     "Could not open link in the default browser."

LangString VBOX_UNINST_CONFIRM ${LANG_ENGLISH}                      "Do you really want to uninstall $(^Name)?"
LangString VBOX_UNINST_SUCCESS ${LANG_ENGLISH}                      "$(^Name) have been uninstalled."
LangString VBOX_UNINST_INVALID_D3D ${LANG_ENGLISH}                  "Invalid installation of Direct3D support detected; uninstallation skipped."
LangString VBOX_UNINST_UNABLE_TO_RESTORE_D3D ${LANG_ENGLISH}        "Could not restore original Direct3D files. Please re-install DirectX."
