#!/bin/sh
# $Id: x11config.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Guest Additions X11 config update script
#

#
# Copyright (C) 2006-2024 Oracle and/or its affiliates.
#
# This file is part of VirtualBox base platform packages, as
# available from https://www.virtualbox.org.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation, in version 3 of the
# License.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <https://www.gnu.org/licenses>.
#
# SPDX-License-Identifier: GPL-3.0-only
#

auto_mouse=""
auto_keyboard=""
no_bak=""
old_mouse_dev="/dev/psaux"
video_driver="vboxvideo"

tab=`printf '\t'`

ALL_SECTIONS=\
'^[ '$tab']*[Ss][Ee][Cc][Tt][Ii][Oo][Nn][ '$tab']*'\
'"\([Ii][Nn][Pp][Uu][Tt][Dd][Ee][Vv][Ii][Cc][Ee]\|'\
'[Dd][Ee][Vv][Ii][Cc][Ee]\|'\
'[Ss][Ee][Rr][Vv][Ee][Rr][Ll][Aa][Yy][Oo][Uu][Tt]\|'\
'[Ss][Cc][Rr][Ee][Ee][Nn]\|'\
'[Mm][Oo][Nn][Ii][Tt][Oo][Rr]\|'\
'[Kk][Ee][Yy][Bb][Oo][Aa][Rr][Dd]\|'\
'[Pp][Oo][Ii][Nn][Tt][Ee][Rr]\)"'
# ^\s*Section\s*"(InputDevice|Device|ServerLayout|Screen|Monitor|Keyboard|Pointer)"

KBD_SECTION='^[ '$tab']*[Ss][Ee][Cc][Tt][Ii][Oo][Nn][ '$tab']*"'\
'[Ii][Nn][Pp][Uu][Tt][Dd][Ee][Vv][Ii][Cc][Ee]"' # ^\s*section\s*\"inputdevice\"

END_SECTION='[Ee][Nn][Dd][Ss][Ee][Cc][Tt][Ii][Oo][Nn]' # EndSection

OPT_XKB='^[ '$tab']*option[ '$tab'][ '$tab']*"xkb'

DRIVER_KBD='^[ '$tab']*[Dd][Rr][Ii][Vv][Ee][Rr][ '$tab'][ '$tab']*'\
'"\(kbd\|keyboard\)"'
# ^\s*driver\s+\"(kbd|keyboard)\"

reconfigure()
{
    cfg="$1"
    tmp="$cfg.vbox.tmp"
    test -w "$cfg" || { echo "$cfg does not exist"; return; }
    rm -f "$tmp"
    test ! -e "$tmp" || { echo "Failed to delete $tmp"; return; }
    touch "$tmp"
    test -w "$tmp" || { echo "Failed to create $tmp"; return; }
    xkb_opts="`cat "$cfg" | sed -n -e "/$KBD_SECTION/,/$END_SECTION/p" |
              grep -i "$OPT_XKB"`"
    kbd_drv="`cat "$cfg" | sed -n -e "/$KBD_SECTION/,/$END_SECTION/p" |
             sed -n -e "0,/$DRIVER_KBD/s/$DRIVER_KBD/\\1/p"`"
    test -z "${kbd_drv}" && test -z "${auto_keyboard}" && kbd_drv=keyboard
    cat > "$tmp" << EOF
# VirtualBox generated configuration file
# based on $cfg.
EOF
    cat "$cfg" | sed -e "/$ALL_SECTIONS/,/$END_SECTION/s/\\(.*\\)/# \\1/" >> "$tmp"
    test -n "$kbd_drv" && cat >> "$tmp" << EOF
Section "InputDevice"
  Identifier   "Keyboard[0]"
  Driver       "$kbd_drv"
$xkb_opts
  Option       "Protocol" "Standard"
  Option       "CoreKeyboard"
EndSection
EOF
    kbd_line=""
    test -n "$kbd_drv" && kbd_line='  InputDevice  "Keyboard[0]" "CoreKeyboard"'
    test -z "$auto_mouse" &&
        cat >> "$tmp" << EOF

Section "InputDevice"
  Driver       "mouse"
  Identifier   "Mouse[1]"
  Option       "Buttons" "9"
  Option       "Device" "$old_mouse_dev"
  Option       "Name" "VirtualBox Mouse Buttons"
  Option       "Protocol" "explorerps/2"
  Option       "Vendor" "Oracle Corporation"
  Option       "ZAxisMapping" "4 5"
  Option       "CorePointer"
EndSection

Section "InputDevice"
  Driver       "vboxmouse"
  Identifier   "Mouse[2]"
  Option       "Device" "/dev/vboxguest"
  Option       "Name" "VirtualBox Mouse"
  Option       "Vendor" "Oracle Corporation"
  Option       "SendCoreEvents"
EndSection

Section "ServerLayout"
  Identifier   "Layout[all]"
${kbd_line}
  InputDevice  "Mouse[1]" "CorePointer"
  InputDevice  "Mouse[2]" "SendCoreEvents"
  Option       "Clone" "off"
  Option       "Xinerama" "off"
  Screen       "Screen[0]"
EndSection
EOF

    cat >> "$tmp" << EOF

Section "Monitor"
  Identifier   "Monitor[0]"
  ModelName    "VirtualBox Virtual Output"
  VendorName   "Oracle Corporation"
EndSection

Section "Device"
  BoardName    "VirtualBox Graphics"
  Driver       "${video_driver}"
  Identifier   "Device[0]"
  VendorName   "Oracle Corporation"
EndSection

Section "Screen"
  SubSection "Display"
    Depth      24
  EndSubSection
  Device       "Device[0]"
  Identifier   "Screen[0]"
  Monitor      "Monitor[0]"
EndSection
EOF

    test -n "$no_bak" -o -f "$cfg.vbox" || cp "$cfg" "$cfg.vbox"
    test -n "$no_bak" || mv "$cfg" "$cfg.bak"
    mv "$tmp" "$cfg"
}

while test -n "$1"
do
    case "$1" in
        --autoMouse)
            auto_mouse=1 ;;
        --autoKeyboard)
            auto_keyboard=1 ;;
        --noBak)
            no_bak=1 ;;
        --nopsaux)
            old_mouse_dev="/dev/input/mice" ;;
        --vmsvga)
            video_driver="vmware" ;;
        *)
            reconfigure "$1" ;;
    esac
    shift
done
