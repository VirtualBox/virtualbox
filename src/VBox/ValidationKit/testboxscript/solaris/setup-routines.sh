# $Id: setup-routines.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# VirtualBox Validation Kit - TestBoxScript Service Setup on Solaris.
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
# The contents of this file may alternatively be used under the terms
# of the Common Development and Distribution License Version 1.0
# (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
# in the VirtualBox distribution, in which case the provisions of the
# CDDL are applicable instead of those of the GPL.
#
# You may elect to license modified versions of this file under the
# terms and conditions of either the GPL or the CDDL or both.
#
# SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
#

#
# Detect solaris version.
#
MY_SOLARIS_VER=`uname -r`
case "${MY_SOLARIS_VER}" in
    5.10) MY_SOLARIS_VER=10;;
    5.11) MY_SOLARIS_VER=11;;
    5.12) MY_SOLARIS_VER=12;;
    *)
        echo "Your solaris version (${MY_SOLARIS_VER}) is not supported." >&2
        exit 1;;
esac

#
# Overriding setup.sh bits.
#
MY_FGREP="/usr/xpg4/bin/fgrep" # The other one does grok -q.
if [ ! -f "${MY_ETC_SUDOERS}" ]; then # sudo isn't standard on S10.
    if [ -f "/opt/csw/etc/sudoers" ]; then
        MY_ETC_SUDOERS=/opt/csw/etc/sudoers
    fi
    if [ -f "/etc/opt/csw/sudoers" ]; then
        MY_ETC_SUDOERS=/etc/opt/csw/sudoers
    fi
fi

#
# Solaris variables.
#
MY_SVC_FMRI="svc:/system/virtualbox/testboxscript"
MY_SVCCFG="/usr/sbin/svccfg"
MY_SVCADM="/usr/sbin/svcadm"
MY_CHGRP="/usr/bin/chgrp"
MY_TR="/usr/bin/tr"
MY_TAB=`printf "\t"`

if test "${MY_SOLARIS_VER}" -lt 11; then
    # solaris 10 service import
    MY_SVC="/tmp/testboxscript.xml"
else
    # use propper manifest directory
    # /lib/svc/manifest/system for solaris 11 and higher for testboxscript.xml file

    # Since sol 11.4 the solaris testboxscript service
    # generates Warnings in /var/svc/log/system-manifest-import:default.log
    # -------- Warning!!
    # Configuring services...
    # * Warning!! Importing Zone access service  ...FAILED.

    MY_SVC="/lib/svc/manifest/system/testboxscript.xml"
fi
if test "${MY_SOLARIS_VER}" -lt 11; then ## No gsed on S10?? ARG!
    MY_SED="/usr/xpg4/bin/sed"
else
    MY_SED="/usr/bin/gsed"
fi
if test "${MY_SOLARIS_VER}" -lt 11; then
    MY_SCREEN="/opt/csw/bin/screen"
else
    MY_SCREEN="screen"
fi


check_for_cifs() {
    if [ ! -f /usr/kernel/fs/amd64/smbfs -a ! -f /usr/kernel/fs/smbfs -a  "${MY_SOLARIS_VER}" -ge 11 ]; then
        echo "error: smbfs client not installed?" >&2
        echo "Please install smbfs client support:" >&2
        echo "     pkg install system/file-system/smb" >&2
        echo "     svcadm enable svc:/system/idmap" >&2
        echo "     svcadm enable svc:/network/smb/client" >&2
        echo "     svcs svc:/system/idmap" >&2
        return 1;
    fi
    return 0;
}

##
# Loads config values from the current installation.
#
os_load_config() {
    #
    # Adjust defaults.
    #
    # - Use NFS instead of CIFS because S10 doesn't have smbfs and S11 has
    #   problems getting the password.
    # - Pass the PATH along so we'll find sudo and other stuff later.
    #
    TESTBOXSCRIPT_BUILDS_TYPE="nfs"
    TESTBOXSCRIPT_TESTRSRC_TYPE="nfs"
    TESTBOXSCRIPT_DEFAULT_BUILDS_TYPE="nfs"
    TESTBOXSCRIPT_DEFAULT_TESTRSRC_TYPE="nfs"
    TESTBOXSCRIPT_ENVVARS[${#TESTBOXSCRIPT_ENVVARS[@]}]="PATH=${PATH}";

    # Load old current.
    if "${MY_SVCCFG}" "export" "${MY_SVC_FMRI}" > /dev/null 2>&1; then
        # User. ASSUMES single quoted attribs.
        MY_TMP=`"${MY_SVCCFG}" "export" "${MY_SVC_FMRI}" \
                | ${MY_TR} '\n' ' ' \
               `;
        MY_TMP=`echo "${MY_TMP} " \
                | ${MY_SED} \
                  -e 's/>  */> /g' \
                  -e 's/  *\/>/ \/>/g' \
                  -e 's/^.*<method_credential \([^>]*\) \/>.*$/\1/' \
                  -e "s/^.*user='\([^']*\)'.*\$/\1/" \
               `;
        if [ -n "${MY_TMP}" ]; then
            TESTBOXSCRIPT_USER="${MY_TMP}";
        fi

        # Arguments. ASSUMES sub-elements. ASSUMES single quoted attribs.
        XMLARGS=`"${MY_SVCCFG}" "export" "${MY_SVC_FMRI}" \
                 | ${MY_TR} '\n' ' ' \
                `;
        case "${XMLARGS}" in
            *exec_method*)
                XMLARGS=`echo "${XMLARGS} " \
                         | ${MY_SED} \
                           -e 's/>  */> /g' \
                           -e 's/  *\/>/ \/>/g' \
                           -e "s/^.*<exec_method \([^>]*\)name='start'\([^>]*\)>.*\$/\1 \2/" \
                           -e "s/^.*exec='\([^']*\)'.*\$/\1/" \
                           -e 's/&quot;/"/g' \
                           -e 's/&lt;/</g' \
                           -e 's/&gt;/>/g' \
                           -e 's/&amp;/&/g' \
                         | ${MY_SED} \
                           -e 's/^.*testboxscript -d -m *//' \
                        `;
                eval common_testboxscript_args_to_config ${XMLARGS}
                ;;
            *)
                echo "error: ${MY_SVCCFG}" "export" "${MY_SVC_FMRI} contains no exec_method element." >&2
                echo "       Please delete the service manually and restart setup.sh" >&2
                exit 2
                ;;
        esac
    fi
}

##
# Adds one or more arguments to MY_ARGV after checking them for conformity.
#
os_add_args() {
    while [ $# -gt 0 ];
    do
        case "$1" in
            *\ *)
                echo "error: Space in option value is not allowed ($1)" >&2
                exit 1;
                ;;
            *${MY_TAB}*)
                echo "error: Tab in option value is not allowed ($1)" >&2
                exit 1;
                ;;
            *\&*)
                echo "error: Ampersand in option value is not allowed ($1)" >&2
                exit 1;
                ;;
            *\<*)
                echo "error: Greater-than in option value is not allowed ($1)" >&2
                exit 1;
                ;;
            *\>*)
                echo "error: Less-than in option value is not allowed ($1)" >&2
                exit 1;
                ;;
            *)
                MY_ARGV="${MY_ARGV} $1";
                ;;
        esac
        shift;
    done
    return 0;
}

##
# Installs, configures and starts the service.
#
os_install_service() {
    # Only NFS for S10.
    if [ "${MY_SOLARIS_VER}" -lt 11 ]; then
        if [ "${TESTBOXSCRIPT_BUILDS_TYPE}" != "nfs" -o "${TESTBOXSCRIPT_TESTRSRC_TYPE}" != "nfs" ]; then
            echo "On solaris 10 both share types must be 'nfs', cifs (smbfs) is not supported." >&2
            return 1;
        fi
    fi

    # Calc the command line.
    MY_ARGV=""
    common_compile_testboxscript_command_line

    # Create the service xml config file.
    cat > "${MY_SVC}" <<EOF
<?xml version='1.0'?>
<!DOCTYPE service_bundle SYSTEM "/usr/share/lib/xml/dtd/service_bundle.dtd.1">
<service_bundle type='manifest' name='export'>
    <service name='system/virtualbox/testboxscript' type='service' version='1'>
        <create_default_instance enabled='false' />
        <single_instance/>

        <!-- Wait for the network to start up -->
        <dependency name='milestone-network' grouping='require_all' restart_on='none' type='service'>
            <service_fmri value='svc:/milestone/network:default' />
        </dependency>

        <!-- We wish to be started as late as possible... so go crazy with deps. -->
        <dependency name='milestone-devices' grouping='require_all' restart_on='none' type='service'>
            <service_fmri value='svc:/milestone/devices:default' />
        </dependency>
        <dependency name='multi-user'        grouping='require_all' restart_on='none' type='service'>
            <service_fmri value='svc:/milestone/multi-user:default' />
        </dependency>
        <dependency name='multi-user-server' grouping='require_all' restart_on='none' type='service'>
            <service_fmri value='svc:/milestone/multi-user-server:default' />
        </dependency>
        <dependency name='filesystem-local'  grouping='require_all' restart_on='none' type='service'>
            <service_fmri value='svc:/system/filesystem/local:default' />
        </dependency>
        <dependency name='filesystem-autofs' grouping='require_all' restart_on='none' type='service'>
            <service_fmri value='svc:/system/filesystem/autofs:default' />
        </dependency>
EOF
    if [ "`uname -r`" = "5.10" ]; then # Seems to be gone in S11?
        cat >> "${MY_SVC}" <<EOF
            <dependency name='filesystem-volfs'  grouping='require_all' restart_on='none' type='service'>
            <service_fmri value='svc:/system/filesystem/volfs:default' />
        </dependency>
EOF
    fi
    cat >> "${MY_SVC}" <<EOF
        <!-- start + stop methods -->
        <exec_method type='method' name='start' exec='${MY_SCREEN} -S testboxscript -d -m ${MY_ARGV}'
            timeout_seconds='30'>
            <method_context working_directory='${TESTBOXSCRIPT_DIR}'>
                <method_credential user='${TESTBOXSCRIPT_USER}' />
                <method_environment>
                    <envvar name='PATH' value='${PATH}' />
                </method_environment>
            </method_context>
        </exec_method>

        <exec_method type='method' name='stop' exec=':kill' timeout_seconds='60' />

        <property_group name='startd' type='framework'>
            <!-- sub-process core dumps/signals should not restart session -->
            <propval name='ignore_error' type='astring' value='core,signal' />
        </property_group>

        <!-- Description -->
        <template>
            <common_name>
                <loctext xml:lang='C'>VirtualBox TestBox Script</loctext>
            </common_name>
        </template>
    </service>
</service_bundle>
EOF

    if test "${MY_SOLARIS_VER}" -lt 11; then
      # Install the service, replacing old stuff.
      if "${MY_SVCCFG}" "export" "${MY_SVC_FMRI}" > /dev/null 2>&1; then
        "${MY_SVCCFG}" "delete" "${MY_SVC_FMRI}"
      fi
      "${MY_SVCCFG}" "import" "${MY_SVC}"

      # only for solaris version less than 11
      rm -f "${MY_SVC}"
    else
      "${MY_CHGRP}" "sys" "${MY_SVC}"
      "${MY_SVCADM}" "restart" "manifest-import"

      # Do not remove the xml file in Solaris versions 11 and higher.
      # The service will be removed automatically, if the command
      # svcadm restart manifest-import
      # will be executed

    fi
    return 0;
}

os_enable_service() {
    "${MY_SVCADM}" "enable" "${MY_SVC_FMRI}"
    return 0;
}

os_disable_service() {
    if "${MY_SVCCFG}" "export" "${MY_SVC_FMRI}" > /dev/null 2>&1; then
        "${MY_SVCADM}" "disable" "${MY_SVC_FMRI}"
        sleep 1
    fi
    return 0;
}

os_add_user() {
    useradd -m -s /usr/bin/bash -G staff "${TESTBOXSCRIPT_USER}"
    passwd "${TESTBOXSCRIPT_USER}" # This sucker prompts, seemingly no way around that.
    return 0;
}


maybe_hush_up_root_login() {
    # We don't want /etc/profile to display /etc/motd, quotas and mail status
    # every time we do sudo -i... It may screw up serious if we parse the
    # output of the command we sudid.
    > ~root/.hushlogin
    return 0;
}

os_final_message() {
    cat <<EOF

Additional things to do:"
    1. Configure NTP:
         a) echo "server wei01-time.de.oracle.com" >  /etc/inet/ntp.conf
            echo "driftfile /var/ntp/ntp.drift"    >> /etc/inet/ntp.conf
         b) Enable the service: svcadm enable ntp
         c) Sync once in case of big diff: ntpdate wei01-time.de.oracle.com
         d) Check that it works: ntpq -p

Enjoy!
EOF
}

