#!/usr/bin/perl -w
# $Id: x11restore.pl 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Restore xorg.conf while removing Guest Additions.
#

#
# Copyright (C) 2008-2024 Oracle and/or its affiliates.
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


my $os_type=`uname -s`;
my @cfg_files = ("/etc/X11/xorg.conf-4", "/etc/X11/xorg.conf", "/etc/X11/.xorg.conf", "/etc/xorg.conf",
                 "/usr/etc/X11/xorg.conf-4", "/usr/etc/X11/xorg.conf", "/usr/lib/X11/xorg.conf-4",
                 "/usr/lib/X11/xorg.conf", "/etc/X11/XF86Config-4", "/etc/X11/XF86Config",
                 "/etc/XF86Config", "/usr/X11R6/etc/X11/XF86Config-4", "/usr/X11R6/etc/X11/XF86Config",
                 "/usr/X11R6/lib/X11/XF86Config-4", "/usr/X11R6/lib/X11/XF86Config");
my $CFG;
my $BAK;

my $config_count = 0;
my $vboxpresent = "vboxvideo";

foreach $cfg (@cfg_files)
{
    if (($os_type =~ 'SunOS') && (defined $ENV{PKG_INSTALL_ROOT}))
    {
        $cfg = $ENV{PKG_INSTALL_ROOT}.$cfg;
    }
    if (open(CFG, $cfg))
    {
        @array=<CFG>;
        close(CFG);

        foreach $line (@array)
        {
            if ($line =~ /$vboxpresent/)
            {
                if (open(BAK, $cfg.".bak"))
                {
                    close(BAK);
                    print("Restoring $cfg.back to $cfg.\n");
                    rename $cfg.".bak", $cfg;
                }
                else
                {
                    # On Solaris just delete existing conf if backup is not found (Possible on distros like Indiana)
                    if ($os_type =~ 'SunOS')
                    {
                        unlink $cfg
                    }
                    else
                    {
                        die "Failed to restore xorg.conf! Your existing config. still uses VirtualBox drivers!!";
                    }
                }
            }
        }
        $config_count++;
    }
}
