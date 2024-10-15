# Oracle VirtualBox
# $Id: module-autologon.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# VirtualBox Linux Guest Additions installer - autologon module
#

#
# Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

# @todo Document functions and their usage!

MOD_AUTOLOGON_DEFAULT_LIGHTDM_CONFIG="/etc/lightdm/lightdm.conf"
MOD_AUTOLOGON_DEFAULT_LIGHTDM_GREETER_DIR="/usr/share/xgreeters"

mod_autologon_init()
{
    echo "Initializing auto-logon support ..."
    return 0
}

mod_autologon_install_ex()
{
    info "Installing auto-logon support ..."

    ## Parameters:
    # Greeter directory. Defaults to /usr/share/xgreeters.
    greeter_dir="$1"
    # LightDM config. Defaults to /etc/lightdm/lightdm.conf.
    lightdm_config="$2"
    # Whether to force installation if non-compatible distribution
    # is detected.
    force="$3"

    # Check for Ubuntu and derivates. @todo Debian?
    distros="Ubuntu UbuntuStudio Edubuntu Kubuntu Lubuntu Mythbuntu Xubuntu"
    ## @todo Map Linux Mint versions to Ubuntu ones.

    ## @todo Move the distro check to a routine / globals as soon as
    ##       we have other distribution-dependent stuff.
    which lsb_release &>/dev/null
    if test "$?" -ne "0"; then
        info "Error: lsb_release not found (path set?), skipping auto-logon installation"
        return 1
    fi
    distro_name=$(lsb_release -si)
    distro_ver=$(lsb_release -sr)

    for distro_cur in ${distros}; do
        if test "$distro_name" = "$distro_cur"; then
            distro_found="true"
            break
        fi
    done

    if test -z "$distro_found"; then
        if ! test "$force" = "force"; then
            info "Error: Unsupported distribution \"$distro_name\" found, skipping auto-logon installation"
            return 1
        fi
        info "Warning: Unsupported distribution \"$distro_name\" found"
    else
        # Do we have Ubuntu 11.10 or greater?
        # Use AWK for comparison since we run on plan sh.
        echo | awk 'END { exit ( !('"$distro_ver >= 11.10"') ); }'
        if test "$?" -ne "0"; then
            if ! test "$force" = "force"; then
                info "Error: Version $distro_ver of \"$distro_name\" not supported, skipping auto-logon installation"
                return 1
            fi
            info "Warning: Unsupported \"$distro_name\" version $distro_ver found"
        fi
    fi

    # Install dependencies (lightdm and FLTK 1.3+) using apt-get.
    which apt-get &>/dev/null
    if test "$?" -ne "0"; then
        info "Error: apt-get not found (path set?), skipping auto-logon installation"
        return 1
    fi
    info "Checking and installing necessary dependencies ..."
    apt-get -qqq -y install libfltk1.3 libfltk-images1.3 || return 1
    apt-get -qqq -y install lightdm || return 1

    # Check for LightDM config.
    if ! test -f "$lightdm_config"; then
        info "Error: LightDM config \"$lightdm_config\" not found (LightDM installed?), skipping auto-logon installation"
        return 1
    fi

    # Check for /usr/share/xgreeters.
    if ! test -d "$greeter_dir"; then
        if ! test "$force" = "force"; then
            info "Error: Directory \"$greeter_dir\" does not exist, skipping auto-logon installation"
            return 1
        fi
        info "Warning: Directory \"$greeter_dir\" does not exist, creating it"
        mkdir -p -m 755 "$greeter_dir" || return 1
    fi

    # Link to required greeter files into $greeter_dir.
    add_symlink "$INSTALLATION_DIR/other/vbox-greeter.desktop" "$greeter_dir/vbox-greeter.desktop"

    # Backup and activate greeter config.
    if ! test -f "$lightdm_config.vbox-backup"; then
        info "Backing up LightDM configuration file ..."
        cp "$lightdm_config" "$lightdm_config.vbox-backup" || return 1
        chmod 644 "$lightdm_config.vbox-backup" || return 1
    fi
    sed -i -e 's/^\s*greeter-session\s*=.*/greeter-session=vbox-greeter/g' "$lightdm_config" || return 1
    chmod 644 "$lightdm_config" || return 1

    info "Auto-logon installation successful"
    return 0
}

mod_autologon_install()
{
    if [ -z "$MOD_AUTOLOGON_LIGHTDM_GREETER_DIR" ]; then
        MOD_AUTOLOGON_LIGHTDM_GREETER_DIR=$MOD_AUTOLOGON_DEFAULT_LIGHTDM_GREETER_DIR
    fi
    if [ -z "$MOD_AUTOLOGON_LIGHTDM_CONFIG" ]; then
        MOD_AUTOLOGON_LIGHTDM_CONFIG=$MOD_AUTOLOGON_DEFAULT_LIGHTDM_CONFIG
    fi

    mod_autologon_install_ex "$MOD_AUTOLOGON_LIGHTDM_GREETER_DIR" "$MOD_AUTOLOGON_LIGHTDM_CONFIG" "$MOD_AUTOLOGON_FORCE"
    return $?
}

mod_autologon_pre_uninstall()
{
    echo "Preparing to uninstall auto-logon support ..."
    return 0
}

mod_autologon_uninstall()
{
    if test -z "$MOD_AUTOLOGON_LIGHTDM_CONFIG"; then
        return 0
    fi
    info "Un-installing auto-logon support ..."

    # Switch back to original greeter.
    if test -f "$MOD_AUTOLOGON_LIGHTDM_CONFIG.vbox-backup"; then
        mv "$MOD_AUTOLOGON_LIGHTDM_CONFIG.vbox-backup" "$MOD_AUTOLOGON_LIGHTDM_CONFIG"
        if test "$?" -ne "0"; then
            info "Warning: Could not restore original LightDM config \"$MOD_AUTOLOGON_LIGHTDM_CONFIG\""
        fi
    fi

    # Remove greeter directory (if not empty).
    rm "$MOD_AUTOLOGON_LIGHTDM_GREETER_DIR" 2>/dev/null

    info "Auto-logon uninstallation successful"
    return 0
}

mod_autologon_config_save()
{
    echo "
MOD_AUTOLOGON_LIGHTDM_CONFIG='$MOD_AUTOLOGON_LIGHTDM_CONFIG'
MOD_AUTOLOGON_LIGHTDM_GREETER_DIR='$MOD_AUTOLOGON_LIGHTDM_GREETER_DIR'"
}
