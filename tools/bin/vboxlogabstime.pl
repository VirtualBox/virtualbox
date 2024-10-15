#!/usr/bin/perl
# $Id: vboxlogabstime.pl 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# ???
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
use Time::Local;

if ($#ARGV != 0) { die "Give the VirtualBox log file in the command line\n"; }
open(LOG, $ARGV[0]) or die "Unable to open $ARGV[0] ($!)\n";

# extract log timestamp from VBox.log
my $line = 0;
my ($dummy, $start);
my $continuation = 0;
while (<LOG>)
{
  chomp;
  $line++;
  next if not /^.*Log opened|started.*/;
  if ($line ge 3) { die "Cannot find timestamp in $ARGV[0]\n"; }
  ($dummy,$start)=split(/.*?Log opened|started /);
  $continuation = 1 if /^.*Log started.*/;
  last;
}

# compute perl time value corresponding to timestamp
my ($year,$month,$day,$hh,$mm,$ss,$frac);
if ($start =~ s/(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})\.(\d+)Z/ /) {
  ($year,$month,$day,$hh,$mm,$ss,$frac) = ($1,$2-1,$3,$4,$5,$6,$7);
  $frac = "0.$frac";
}
else
{
  die "Timestamp $start cannot be parsed\n";
}
my $logstamp = timegm($ss,$mm,$hh,$day,$month,$year)+$frac;

# print entire log with absolute timestamps in local time
seek(LOG, 0, 0);
my $firstrel;
# Note that for continuations we're slightly inaccurate, as we have no idea
# about the time difference between the start of the process and the start of
# logging as documented by the timestamp. Usually a couple milliseconds.
if ($continuation) { $firstrel = 0; }
while (<LOG>)
{
  my ($time,$msg) = split('(?<=\s)(.*)');
  my ($h,$m,$s,$ms) = split(':|\.', $time);
  my $reltime = $h * 3600 + $m * 60 + $s + "0.$ms";
  if (!defined $firstrel) { $firstrel = $reltime; }
  $reltime -= $firstrel;
  my $abstime = $logstamp + $reltime;
  $ms = int(($abstime - int($abstime)) * 1000);
  # msec rounding paranoia
  if ($ms gt 999) { $ms = 999 };
  $abstime = int($abstime);
  my ($sec,$min,$hour,$mday,$mon,$year,$wday,$yday,$isdst) = localtime($abstime);
  printf("%04d-%02d-%02d %02d:%02d:%02d.%03d %s\n", $year + 1900, $mon + 1, $mday, $hour, $min, $sec, $ms, $msg);
}
close(LOG);
