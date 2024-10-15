#!/usr/bin/perl
# $Id: x11config15sol.pl 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
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

use strict;
use warnings;

my $temp="/tmp/xorg.conf";
my $os_type=`uname -s`;
my @cfg_files = ("/etc/X11/xorg.conf", "/etc/X11/.xorg.conf", "/etc/X11/xorg.conf-4", "/etc/xorg.conf",
                 "/usr/etc/X11/xorg.conf-4", "/usr/etc/X11/xorg.conf", "/usr/lib/X11/xorg.conf-4",
                 "/usr/lib/X11/xorg.conf", "/etc/X11/XF86Config-4", "/etc/X11/XF86Config",
                 "/etc/XF86Config", "/usr/X11R6/etc/X11/XF86Config-4", "/usr/X11R6/etc/X11/XF86Config",
                 "/usr/X11R6/lib/X11/XF86Config-4", "/usr/X11R6/lib/X11/XF86Config");

## @todo: r=ramshankar: Hmm, why do we use the same variable name with upper/lower case for different variables?
my $cfg;
my $CFG;
my $TMP;
my $line;
my $config_count = 0;

# Command line options
if ($#ARGV < 0)
{
   die "x11config15sol.pl: Missing driver name argument to configure for X.org";
}
my $driver_name = $ARGV[0];

# Loop through all possible config files and change them. It's done this wasy for hysterical raisins
# as we didn't know what the correct config file is so we update all of them. However, for Solaris it's
# most likely -only- one of the 2 config files (/etc/X11/xorg.conf, /etc/X11/.xorg.conf).
foreach $cfg (@cfg_files)
{

    if (($os_type =~ 'SunOS') && (defined $ENV{PKG_INSTALL_ROOT}))
    {
        $cfg = $ENV{PKG_INSTALL_ROOT}.$cfg;
    }
    if (open(CFG, $cfg))
    {
        open(TMP, ">$temp") or die "Can't create $TMP: $!\n";

        my $in_section = 0;

        while (defined ($line = <CFG>))
        {
            if ($line =~ /^\s*Section\s*"([a-zA-Z]+)"/i)
            {
                my $section = lc($1);
                if ($section eq "device")
                {
                    $in_section = 1;
                }
            }
            else
            {
                if ($line =~ /^\s*EndSection/i)
                {
                    $in_section = 0;
                }
            }

            if ($in_section)
            {
                if ($line =~ /^\s*driver\s+\"(?:fbdev|vga|vesa|vboxvideo|ChangeMe)\"/i)
                {
                    $line = "    Driver      \"$driver_name\"\n";
                }
            }
            print TMP $line;
        }

        close(TMP);

        rename $cfg, $cfg.".bak";
        system("cp $temp $cfg");
        unlink $temp;

        # Solaris specific: Rename our modified .xorg.conf to xorg.conf for it to be used
        if ((defined $ENV{PKG_INSTALL_ROOT}) &&
            ($os_type =~ 'SunOS') && ($cfg =~ "$ENV{PKG_INSTALL_ROOT}/etc/X11/.xorg.conf"))
        {
                system("mv -f $cfg $ENV{PKG_INSTALL_ROOT}/etc/X11/xorg.conf");
        }
        else
        {
            if (($os_type =~ 'SunOS') && ($cfg =~ '/etc/X11/.xorg.conf'))
            {
                system("mv -f $cfg /etc/X11/xorg.conf");
            }
        }

        $config_count++;
    }
}

$config_count != 0 or die "Could not find any X11 configuration files.";

