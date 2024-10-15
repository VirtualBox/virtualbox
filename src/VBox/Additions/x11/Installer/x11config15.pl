#!/usr/bin/perl -w
# $Id: x11config15.pl 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Guest Additions X11 config update script for X.org 1.5
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

# What this script does: X.org 1.5 introduces full hardware autodetection
# and no longer requires the user to provide an X.org configuration file.
# However, if such a file is provided, it will override autodetection of
# the graphics card (not of vboxmouse as far as I can see).  Although this
# would normally be the user's business, at least Fedora 9 still generates
# a configuration file by default, so we have to rewrite it if we want
# the additions to work on a default guest installation.  So we simply go
# through any configuration files we may find on the system and replace
# references to VESA or framebuffer drivers (which might be autodetected
# for use on a VirtualBox guest) and replace them with vboxvideo.

use File::Copy;

my $temp="/tmp/xorg.conf";
# The list of possible names of X.org configuration files
my @cfg_files = ("/etc/X11/xorg.conf-4", "/etc/X11/xorg.conf", "/etc/X11/.xorg.conf", "/etc/xorg.conf",
                 "/usr/etc/X11/xorg.conf-4", "/usr/etc/X11/xorg.conf", "/usr/lib/X11/xorg.conf-4",
                 "/usr/lib/X11/xorg.conf");
my $CFG;
my $TMP;

# Subroutine to roll back after a partial installation
sub do_fail {
    foreach $cfg (@cfg_files) {
        move $cfg.".vbox", $cfg;
        unlink $cfg.".vbox";
    }
    die $1;
}

# Perform the substitution on any configuration file we may find.
foreach $cfg (@cfg_files) {

    if (open(CFG, $cfg)) {
        open(TMP, ">$temp")
            or &do_fail("Can't create $TMP: $!\n");

        while (defined ($line = <CFG>)) {
            if ($line =~ /^\s*Section\s*"([a-zA-Z]+)"/i) {
                my $section = lc($1);
                if ($section eq "device") {
                    $in_section = 1;
                }
            } else {
                if ($line =~ /^\s*EndSection/i) {
                    $in_section = 0;
                }
            }

            if ($in_section) {
                if ($line =~ /^\s*driver\s+\"(fbdev|vga|vesa|vboxvideo|ChangeMe)\"/i) {
                    $line =~ s/(fbdev|vga|vesa|vboxvideo|ChangeMe)/vboxvideo/i;
                }
            }
            print TMP $line;
        }
        close(TMP);

        # We do not overwrite existing $cfg.".vbox" files because that will
        # likely ruin any future attempts to uninstall the additions
        copy $cfg, $cfg.".bak";
        if (! -e $cfg.".vbox") {
            rename $cfg, $cfg.".vbox";
        }
        copy $temp, $cfg
            or &do_fail("Could not overwrite configuration file $cfg!  Exiting...");
        unlink $temp;
    }
}
