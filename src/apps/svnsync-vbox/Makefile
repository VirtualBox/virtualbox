# $Id: Makefile 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Makefile
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

CC = gcc
CFLAGS = -O2 -g -Wall -pedantic -Wno-long-long -Wno-trigraphs \
  -Wmissing-prototypes -Wstrict-prototypes -Wmissing-declarations
DEFS = -DVBOX -D_FILE_OFFSET_BITS=64
INCS = -I. -I/usr/include/subversion-1 -I/usr/include/apr-1.0
LIBS = -lsvn_ra-1 -lsvn_ra_local-1 \
  -lsvn_repos-1 -lsvn_fs-1 -lsvn_delta-1 \
  -lsvn_subr-1 -lapr-1 -laprutil-1 \
  -ldb

all: svnsync-vbox

svnsync-vbox: main.c
	$(CC) $(CFLAGS) $(DEFS) $(INCS) $(LIBS) -o $@ $<

svnsync-vbox: svn_private_config.h

svnsync-vbox: Makefile

