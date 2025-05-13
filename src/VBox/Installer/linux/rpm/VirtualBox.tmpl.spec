# $Id: VirtualBox.tmpl.spec 109520 2025-05-13 15:06:05Z vadim.galitsyn@oracle.com $
## @file
# Spec file for creating VirtualBox rpm packages
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

%define %SPEC% 1
%define %OSE% 1
%define %PYTHON% 1
%define VBOXDOCDIR %{_defaultdocdir}/%NAME%
%global __requires_exclude_from ^/usr/lib/virtualbox/VBoxPython.*$|^/usr/lib/python.*$|^.*\\.py$
%{!?python_sitelib: %define python_sitelib %(%{__python} -c "from distutils.sysconfig import get_python_lib; print(get_python_lib())")}

Summary:   Oracle VirtualBox
Name:      %NAME%
Version:   %BUILDVER%_%BUILDREL%
Release:   1
URL:       http://www.virtualbox.org/
Source:    VirtualBox.tar
License:   GPLv3
Group:     Applications/System
Vendor:    Oracle Corporation
BuildRoot: %BUILDROOT%
Requires:  %INITSCRIPTS% %LIBASOUND% %NETTOOLS% %LIBVULKAN%

%if %{?rpm_suse:1}%{!?rpm_suse:0}
%debug_package
%endif

%MACROSPYTHON%
%if %{?__python3:1}%{!?__python3:0}
%define vbox_python %{__python3}
%define vbox_python_sitelib %{python3_sitelib}
%else
%define vbox_python %{__python}
%{?rpm_suse: %define vbox_python_sitelib %{py_sitedir}}
%{!?rpm_suse: %define vbox_python_sitelib %{python_sitelib}}
%endif

# Remove source code from debuginfo package, needed for Fedora 27 and later
# as we build the binaries before creating the RPMs.
%if 0%{?fedora} >= 27
%undefine _debugsource_packages
%undefine _debuginfo_subpackages
%endif
%if 0%{?rhel} >= 8
%undefine _debugsource_packages
%undefine _debuginfo_subpackages
%endif

%description
VirtualBox is a powerful PC virtualization solution allowing
you to run a wide range of PC operating systems on your Linux
system. This includes Windows, Linux, FreeBSD, DOS, OpenBSD
and others. VirtualBox comes with a broad feature set and
excellent performance, making it the premier virtualization
software solution on the market.


%prep
%setup -q
DESTDIR=""
unset DESTDIR


%build


%install
# Mandriva: prevent replacing 'echo' by 'gprintf'
export DONT_GPRINTIFY=1
rm -rf $RPM_BUILD_ROOT
install -m 755 -d $RPM_BUILD_ROOT/sbin
install -m 755 -d $RPM_BUILD_ROOT%{_initrddir}
install -m 755 -d $RPM_BUILD_ROOT/lib/modules
install -m 755 -d $RPM_BUILD_ROOT/etc/vbox
install -m 755 -d $RPM_BUILD_ROOT/usr/bin
install -m 755 -d $RPM_BUILD_ROOT/usr/src
install -m 755 -d $RPM_BUILD_ROOT/usr/share/applications
install -m 755 -d $RPM_BUILD_ROOT/usr/share/pixmaps
install -m 755 -d $RPM_BUILD_ROOT/usr/share/icons/hicolor
install -m 755 -d $RPM_BUILD_ROOT%{VBOXDOCDIR}
install -m 755 -d $RPM_BUILD_ROOT/usr/lib/virtualbox
install -m 755 -d $RPM_BUILD_ROOT/usr/share/virtualbox
install -m 755 -d $RPM_BUILD_ROOT/usr/share/mime/packages
%if %{?with_python:1}%{!?with_python:0}
(export VBOX_INSTALL_PATH=/usr/lib/virtualbox && \
  cd ./sdk/installer/python && \
  %{vbox_python} ./vboxapisetup.py install --prefix %{_prefix} --root $RPM_BUILD_ROOT)
%endif
rm -rf sdk/installer
mv UnattendedTemplates $RPM_BUILD_ROOT/usr/share/virtualbox
mv nls $RPM_BUILD_ROOT/usr/share/virtualbox
cp -a src $RPM_BUILD_ROOT/usr/share/virtualbox
mv VBox.sh $RPM_BUILD_ROOT/usr/bin/VBox
mv VBoxSysInfo.sh $RPM_BUILD_ROOT/usr/share/virtualbox
cp icons/128x128/virtualbox.png $RPM_BUILD_ROOT/usr/share/pixmaps/virtualbox.png
cd icons
  for i in *; do
    if [ -f $i/virtualbox.* ]; then
      install -d $RPM_BUILD_ROOT/usr/share/icons/hicolor/$i/apps
      mv $i/virtualbox.* $RPM_BUILD_ROOT/usr/share/icons/hicolor/$i/apps
    fi
    install -d $RPM_BUILD_ROOT/usr/share/icons/hicolor/$i/mimetypes
    mv $i/* $RPM_BUILD_ROOT/usr/share/icons/hicolor/$i/mimetypes || true
    rmdir $i
  done
cd -
rmdir icons
mv virtualbox.xml $RPM_BUILD_ROOT/usr/share/mime/packages
%if %{?is_ose:0}%{!?is_ose:1}
%if "%BUILDREL%" == "el7"
# For el7 we use gcc from devtoolset-4, which is not suitable for kernel work.
# See the PATH trickery in src/VBox/Installer/linux/rpm/rules.
old_path="$PATH"
PATH=${PATH#/opt/rh/devtoolset-4/root/usr/bin:}
%endif
%if "%BUILDREL%" == "openSUSE156"
# For openSUSE156 we need GCC 10+ to build Qt 6.8 related code. However,
# kernel modules need be built with older GCC.
VBOX_EXTRA_FLAGS="CC=gcc-7"
%endif
for d in /lib/modules/*; do
  if [ -L $d/build ]; then
    rm -f /tmp/vboxdrv-Module.symvers
    ./src/vboxhost/build_in_tmp \
      --save-module-symvers /tmp/vboxdrv-Module.symvers \
      --module-source `pwd`/src/vboxhost/vboxdrv \
      KBUILD_VERBOSE= KERN_VER=$(basename $d) INSTALL_MODULE_PATH=$RPM_BUILD_ROOT $VBOX_EXTRA_FLAGS -j4 \
      %INSTMOD%
    ./src/vboxhost/build_in_tmp \
      --use-module-symvers /tmp/vboxdrv-Module.symvers \
      --module-source `pwd`/src/vboxhost/vboxnetflt \
      KBUILD_VERBOSE= KERN_VER=$(basename $d) INSTALL_MODULE_PATH=$RPM_BUILD_ROOT $VBOX_EXTRA_FLAGS -j4 \
      %INSTMOD%
    ./src/vboxhost/build_in_tmp \
      --use-module-symvers /tmp/vboxdrv-Module.symvers \
      --module-source `pwd`/src/vboxhost/vboxnetadp \
      KBUILD_VERBOSE= KERN_VER=$(basename $d) INSTALL_MODULE_PATH=$RPM_BUILD_ROOT $VBOX_EXTRA_FLAGS -j4 \
      %INSTMOD%
    if [ -e `pwd`/src/vboxhost/vboxpci ]; then
      ./src/vboxhost/build_in_tmp \
        --use-module-symvers /tmp/vboxdrv-Module.symvers \
        --module-source `pwd`/src/vboxhost/vboxpci \
        KBUILD_VERBOSE= KERN_VER=$(basename $d) INSTALL_MODULE_PATH=$RPM_BUILD_ROOT $VBOX_EXTRA_FLAGS -j4 \
        %INSTMOD%
    fi
  fi
done
%if "%BUILDREL%" == "el7"
# For el7 restore PATH, see above.
PATH="$old_path"
unset old_path
%endif
%if "%BUILDREL%" == "openSUSE156"
unset VBOX_EXTRA_FLAGS
%endif
rm -r src
%endif
for i in additions/VBoxGuestAdditions.iso; do
  mv $i $RPM_BUILD_ROOT/usr/share/virtualbox; done
ln -s VBox $RPM_BUILD_ROOT/usr/bin/VirtualBox
ln -s VBox $RPM_BUILD_ROOT/usr/bin/virtualbox
ln -s VBox $RPM_BUILD_ROOT/usr/bin/VirtualBoxVM
ln -s VBox $RPM_BUILD_ROOT/usr/bin/virtualboxvm
ln -s VBox $RPM_BUILD_ROOT/usr/bin/VBoxManage
ln -s VBox $RPM_BUILD_ROOT/usr/bin/vboxmanage
test -f VBoxSDL && ln -s VBox $RPM_BUILD_ROOT/usr/bin/VBoxSDL
test -f VBoxSDL && ln -s VBox $RPM_BUILD_ROOT/usr/bin/vboxsdl
ln -s VBox $RPM_BUILD_ROOT/usr/bin/VBoxVRDP
ln -s VBox $RPM_BUILD_ROOT/usr/bin/VBoxHeadless
ln -s VBox $RPM_BUILD_ROOT/usr/bin/vboxheadless
ln -s VBox $RPM_BUILD_ROOT/usr/bin/VBoxDTrace
ln -s VBox $RPM_BUILD_ROOT/usr/bin/VBoxAudioTest
ln -s VBox $RPM_BUILD_ROOT/usr/bin/vboxdtrace
ln -s VBox $RPM_BUILD_ROOT/usr/bin/vboxaudiotest
test -f VBoxBugReport   && ln -s VBox $RPM_BUILD_ROOT/usr/bin/VBoxBugReport
test -f VBoxBugReport   && ln -s VBox $RPM_BUILD_ROOT/usr/bin/vboxbugreport
test -f VBoxBalloonCtrl && ln -s VBox $RPM_BUILD_ROOT/usr/bin/VBoxBalloonCtrl
test -f VBoxBalloonCtrl && ln -s VBox $RPM_BUILD_ROOT/usr/bin/vboxballoonctrl
test -f VBoxAutostart   && ln -s VBox $RPM_BUILD_ROOT/usr/bin/VBoxAutostart
test -f VBoxAutostart   && ln -s VBox $RPM_BUILD_ROOT/usr/bin/vboxautostart
test -f vboxwebsrv && ln -s VBox $RPM_BUILD_ROOT/usr/bin/vboxwebsrv
ln -s /usr/lib/virtualbox/vbox-img $RPM_BUILD_ROOT/usr/bin/vbox-img
ln -s /usr/lib/virtualbox/vboximg-mount $RPM_BUILD_ROOT/usr/bin/vboximg-mount
ln -s /usr/share/virtualbox/src/vboxhost $RPM_BUILD_ROOT/usr/src/vboxhost-%VER%
mv virtualbox.desktop $RPM_BUILD_ROOT/usr/share/applications/virtualbox.desktop
mv VBox.png $RPM_BUILD_ROOT/usr/share/pixmaps/VBox.png
%{!?is_ose: mv LICENSE $RPM_BUILD_ROOT%{VBOXDOCDIR}}
mv UserManual*.pdf UserManual*.qch UserManual*.qhc $RPM_BUILD_ROOT%{VBOXDOCDIR}
install -m 755 -d $RPM_BUILD_ROOT/usr/lib/debug/usr/lib/virtualbox
%if %{?rpm_suse:1}%{!?rpm_suse:0}
rm *.debug
%else
mv *.debug $RPM_BUILD_ROOT/usr/lib/debug/usr/lib/virtualbox
%endif
mv * $RPM_BUILD_ROOT/usr/lib/virtualbox
if [ -f $RPM_BUILD_ROOT/usr/lib/virtualbox/libQt6CoreVBox.so.6 ]; then
  $RPM_BUILD_ROOT/usr/lib/virtualbox/chrpath --keepgoing --replace /usr/lib/virtualbox \
    $RPM_BUILD_ROOT/usr/lib/virtualbox/*.so.6 \
    $RPM_BUILD_ROOT/usr/lib/virtualbox/plugins/platforms/*.so \
    $RPM_BUILD_ROOT/usr/lib/virtualbox/plugins/platformthemes/*.so \
    $RPM_BUILD_ROOT/usr/lib/virtualbox/plugins/sqldrivers/*.so \
    $RPM_BUILD_ROOT/usr/lib/virtualbox/plugins/styles/*.so || true
  echo "[Paths]" > $RPM_BUILD_ROOT/usr/lib/virtualbox/qt.conf
  echo "Plugins = /usr/lib/virtualbox/plugins" >> $RPM_BUILD_ROOT/usr/lib/virtualbox/qt.conf
fi
rm -f $RPM_BUILD_ROOT/usr/lib/virtualbox/chrpath
ln -s ../VBoxVMM.so $RPM_BUILD_ROOT/usr/lib/virtualbox/components/VBoxVMM.so
for i in VirtualBoxVM VBoxHeadless VBoxNetDHCP VBoxNetNAT VBoxNetAdpCtl; do
  chmod 4511 $RPM_BUILD_ROOT/usr/lib/virtualbox/$i; done
if [ -f $RPM_BUILD_ROOT/usr/lib/virtualbox/VBoxVolInfo ]; then
  chmod 4511 $RPM_BUILD_ROOT/usr/lib/virtualbox/VBoxVolInfo
fi
test -f $RPM_BUILD_ROOT/usr/lib/virtualbox/VBoxSDL && \
  chmod 4511 $RPM_BUILD_ROOT/usr/lib/virtualbox/VBoxSDL
%if %{?with_python:1}%{!?with_python:0}
if [ -x /usr/bin/pathfix.py ]; then
  /usr/bin/pathfix.py -pni "%{__python3} %{py3_shbang_opts}" $RPM_BUILD_ROOT/usr/lib/virtualbox/vboxshell.py
fi
%endif


%pre
# defaults
[ -r /etc/default/virtualbox ] && . /etc/default/virtualbox

# check for old installation
if [ -r /etc/vbox/vbox.cfg ]; then
  . /etc/vbox/vbox.cfg
  if [ "x$INSTALL_DIR" != "x" -a -d "$INSTALL_DIR" ]; then
    echo "An old installation of VirtualBox was found. To install this package the"
    echo "old package has to be removed first. Have a look at /etc/vbox/vbox.cfg to"
    echo "determine the installation directory of the previous installation. After"
    echo "uninstalling the old package remove the file /etc/vbox/vbox.cfg."
    exit 1
  fi
fi

# check for active VMs of the installed (old) package
# Execute the installed packages pre-uninstaller if present.
/usr/lib/virtualbox/prerm-common.sh 2>/dev/null
# Stop services from older versions without pre-uninstaller.
/etc/init.d/vboxballoonctrl-service stop 2>/dev/null
/etc/init.d/vboxautostart-service stop 2>/dev/null
/etc/init.d/vboxweb-service stop 2>/dev/null
VBOXSVC_PID=`pidof VBoxSVC 2>/dev/null || true`
if [ -n "$VBOXSVC_PID" ]; then
  # Ask VBoxSVC to terminate gracefully if it is not
  # busy with handling client requests.
  kill -USR1 $VBOXSVC_PID
  # Wait for VBoxSVC to terminate.
  for attempt in 1 2 3 4 5 6 7 8 9 10; do
    [ -n "$(pidof VBoxSVC 2> /dev/null)" ] && sleep 1
  done
  # Still running?
  if pidof VBoxSVC > /dev/null 2>&1; then
    echo "A copy of VirtualBox is currently running.  Please close it and try again."
    echo "Please note that it can take up to ten seconds for VirtualBox (in particular"
    echo "the VBoxSVC daemon) to finish running."
    exit 1
  fi
fi


%post
LOG="/var/log/vbox-install.log"

# defaults
[ -r /etc/default/virtualbox ] && . /etc/default/virtualbox

# remove old cruft
if [ -f /etc/init.d/vboxdrv.sh ]; then
  echo "Found old version of /etc/init.d/vboxdrv.sh, removing."
  rm /etc/init.d/vboxdrv.sh
fi
if [ -f /etc/vbox/vbox.cfg ]; then
  echo "Found old version of /etc/vbox/vbox.cfg, removing."
  rm /etc/vbox/vbox.cfg
fi
rm -f /etc/vbox/module_not_compiled

# create users groups (disable with INSTALL_NO_GROUP=1 in /etc/default/virtualbox)
if [ "$INSTALL_NO_GROUP" != "1" ]; then
  echo
  echo "Creating group 'vboxusers'. VM users must be member of that group!"
  echo
  groupadd -r -f vboxusers 2> /dev/null
fi

%if %{?rpm_mdv:1}%{!?rpm_mdv:0}
/sbin/ldconfig
%update_menus
%endif
update-mime-database /usr/share/mime &> /dev/null || :
update-desktop-database -q > /dev/null 2>&1 || :
touch --no-create /usr/share/icons/hicolor
gtk-update-icon-cache -q /usr/share/icons/hicolor 2> /dev/null || :

# Disable module compilation with INSTALL_NO_VBOXDRV=1 in /etc/default/virtualbox
if test "${INSTALL_NO_VBOXDRV}" = 1; then
  POSTINST_START=--nostart
else
  POSTINST_START=
fi
# Install and start the new service scripts.
/usr/lib/virtualbox/prerm-common.sh || true
/usr/lib/virtualbox/postinst-common.sh ${POSTINST_START} > /dev/null || true


%preun
# Called before the package is removed, or during upgrade after (not before)
# the new version's "post" scriptlet.
# $1==0: remove the last version of the package
# $1>=1: upgrade
if [ "$1" = 0 ]; then
  /usr/lib/virtualbox/prerm-common.sh || exit 1
  rm -f /etc/udev/rules.d/60-vboxdrv.rules
  rm -f /etc/vbox/license_agreed
  rm -f /etc/vbox/module_not_compiled
fi

%postun
%if %{?rpm_mdv:1}%{!?rpm_mdv:0}
/sbin/ldconfig
%{clean_desktop_database}
%clean_menus
%endif
update-mime-database /usr/share/mime &> /dev/null || :
update-desktop-database -q > /dev/null 2>&1 || :
touch --no-create /usr/share/icons/hicolor
gtk-update-icon-cache -q /usr/share/icons/hicolor 2> /dev/null || :
rm -rf /usr/lib/virtualbox/ExtensionPacks


%clean
rm -rf $RPM_BUILD_ROOT


%changelog
* %DATE% Oracle Corporation <info@virtualbox.org>
- rebuild RPM package, see https://www.virtualbox.org/wiki/Changelog


%files
%defattr(-,root,root)
%doc %{VBOXDOCDIR}/*
%if %{?with_python:1}%{!?with_python:0}
%{vbox_python_sitelib}/*
%endif
/etc/vbox
/usr/bin/*
/usr/src/vbox*
/usr/lib/virtualbox
/usr/share/applications/*
/usr/share/icons/hicolor/*/apps/*
/usr/share/icons/hicolor/*/mimetypes/*
/usr/share/mime/packages/*
/usr/share/pixmaps/*
/usr/share/virtualbox
