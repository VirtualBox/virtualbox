#!/usr/bin/perl -w
# $Id: x11config15suse.pl 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
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

# Versions of (open)SUSE which ship X.Org Server 1.5 still do not enable
# mouse autodetection, so on these systems we have to enable vboxmouse in the
# X.Org configuration file as well as vboxvideo.  When uninstalling, we enable
# the fbdev driver, which SUSE prefers over vesa, and we leave the references
# to vboxmouse in place, as without the driver they are harmless.

use File::Copy;

# This is the file name for the temporary file we write the new configuration
# to.
# @todo: perl must have an API for generating this
my $temp="/tmp/xorg.conf";
# The list of possible names of X.org configuration files
my @cfg_files = ("/etc/X11/xorg.conf-4", "/etc/X11/xorg.conf", "/etc/X11/.xorg.conf", "/etc/xorg.conf",
                 "/usr/etc/X11/xorg.conf-4", "/usr/etc/X11/xorg.conf", "/usr/lib/X11/xorg.conf-4",
                 "/usr/lib/X11/xorg.conf");
# File descriptor of the old configuration file
my $CFG;
# File descriptor of the temporary file
my $TMP;

# The name of the mouse driver we are enabling
my $mousedrv = 'vboxmouse';
# The name of the video driver we are enabling
my $videodrv=  'vboxvideo';

# If we are uninstalling, restore the old video driver
if (@ARGV && "$ARGV[0]" eq 'uninstall')
{
    $videodrv = 'fbdev'  # SUSE prefers this one
}

# How many different configuration files have we found?
my $config_count = 0;

# Subroutine to roll back after a partial installation
sub do_fail {
    foreach $cfg (@cfg_files) {
        move "$cfg.vbox", $cfg;
        unlink "$cfg.vbox";
    }
    die $_[0];
}

# Perform the substitution on any configuration file we may find.
foreach $cfg (@cfg_files) {

    if (open(CFG, $cfg)) {
        open(TMP, ">$temp")
            or &do_fail("Can't create $TMP: $!\n");

        my $have_mouse = 0;
        my $in_section = 0;
        my $in_layout = 0;

        # Go through the configuration file line by line
        while (defined ($line = <CFG>)) {
            # Look for the start of sections
            if ($line =~ /^\s*Section\s*"([a-zA-Z]+)"/i) {
                my $section = lc($1);
                # And see if they are device or input device sections
                if (($section eq "inputdevice") || $section eq "device") {
                    $in_section = 1;
                }
                # Or server layout sections
                if ($section eq "serverlayout")
                {
                    $in_section = 1;
                    $in_layout = 1;
                }
            } else {
                if ($line =~ /^\s*EndSection/i && $in_layout) {
                    # We always add this to the end of the server layout.
                    print TMP "  InputDevice  \"VBoxMouse\"\n"
                }
                if ($line =~ /^\s*EndSection/i) {
                    $in_section = 0;
                    $in_layout = 0;
                }
            }

            if ($in_section) {
                # Inside sections, look for any graphics drivers and replace
                # them with our one.
                if ($line =~ /^\s*driver\s+\"(fbdev|vga|vesa|vboxvideo|ChangeMe)\"/i) {
                    $line =~ s/(fbdev|vga|vesa|vboxvideo|ChangeMe)/$videodrv/i;
                }
                # Also keep track of whether this configuration file contains
                # an input device section for vboxmouse.  If it does, we don't
                # need to add one later.
                if ($line =~ /^\s*driver\s+\"(?:vboxmouse)\"/i)
                {
                    $have_mouse = 1
                }

                # We add vboxmouse to the server layout section ourselves, so
                # remove any existing references to it.
                if (   $line =~ /^\s*inputdevice.*\"vboxmouse\"/i)
                {
                    $line = "";
                }
            }
            print TMP $line;
        }

        # We always add a vboxmouse section at the end for SUSE guests using
        # X.Org 1.5 if vboxmouse is not referenced anywhere else in the file,
        # and we do not remove it when we uninstall the additions, as it will
        # not do any harm if it is left.
        if (!$have_mouse) {
            print TMP "\n";
            print TMP "Section \"InputDevice\"\n";
            print TMP "        Identifier  \"VBoxMouse\"\n";
            print TMP "        Driver      \"$mousedrv\"\n";
            print TMP "        Option      \"Device\"     \"\/dev\/vboxguest\"\n";
            print TMP "        Option      \"SendCoreEvents\"  \"on\"\n";
            print TMP "EndSection\n";
        }
        close(TMP);

        # We do not overwrite existing "$cfg.vbox" files in order to keep a
        # record of what the configuration looked like before the very first
        # installation of the additions.
        copy $cfg, "$cfg.bak";
        if (! -e "$cfg.vbox") {
            rename $cfg, "$cfg.vbox";
        }
        copy $temp, $cfg
            or &do_fail("Could not overwrite configuration file $cfg!  Exiting...");
        unlink $temp;

        $config_count++;
    }
}

# Warn if we did not find any configuration files
$config_count != 0 or die "Could not find any X11 configuration files";

