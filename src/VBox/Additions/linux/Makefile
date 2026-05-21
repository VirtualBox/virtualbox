#
# Makefile for the VirtualBox Linux Guest Drivers.
#

#
# Copyright (C) 2009-2025 Oracle and/or its affiliates.
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

ifneq ($(KERNELRELEASE),)

# Building from kBuild (make -C <kernel_directory> M=`pwd`)
# or inside a kernel source tree.

obj-m = vboxguest/ vboxsf/ vboxvideo/

else # ! KERNELRELEASE

KBUILD_VERBOSE =
 ifeq ($(KBUILD_VERBOSE),)
VBOX_QUIET    := @
VBOX_QUIET_SH := @
 else
VBOX_QUIET    :=
VBOX_QUIET_SH := set -x;
 endif

KERN_MAJ = $(shell uname -r | cut -d . -f1)

all: vboxguest vboxsf $(if $(shell [ "$(KERN_MAJ)" -lt 7 ] && echo y),vboxvideo,)

vboxguest:
	@echo "=== Building 'vboxguest' module ==="
	+ $(VBOX_QUIET)$(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C vboxguest
	$(VBOX_QUIET_SH)if [ -f vboxguest/vboxguest.ko ]; then \
	    cp vboxguest/vboxguest.ko .; \
	 else \
	    cp vboxguest/vboxguest.o .; \
	 fi
	@echo

vboxsf: vboxguest
	+ $(VBOX_QUIET_SH)if [ -d vboxsf ]; then \
	    if [ -f vboxguest/Module.symvers ]; then \
	        cp vboxguest/Module.symvers vboxsf; \
	    fi; \
	    echo "=== Building 'vboxsf' module ==="; \
	    $(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) KBUILD_EXTRA_SYMBOLS=$(abspath vboxsf/Module.symvers) -C vboxsf || exit 1; \
	    if [ -f vboxsf/vboxsf.ko ]; then \
	        cp vboxsf/vboxsf.ko .; \
	    else \
	        cp vboxsf/vboxsf.o .; \
	    fi; \
	    echo; \
	fi

vboxvideo:
	+ $(VBOX_QUIET_SH)if [ -d vboxvideo ]; then \
	    echo "=== Building 'vboxvideo' module ==="; \
	    $(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C vboxvideo || exit 1; \
	    if [ -f vboxvideo/vboxvideo.ko ]; then \
	        cp vboxvideo/vboxvideo.ko .; \
	    elif [ -f vboxvideo/vboxvideo.o ]; then \
	        cp vboxvideo/vboxvideo.o .; \
	    fi; \
	    echo; \
	fi

install-vboxguest:
	+ $(VBOX_QUIET)$(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C vboxguest install

install-vboxsf:
	+ $(VBOX_QUIET_SH)if [ -d vboxsf ]; then \
	    $(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C vboxsf install; \
	fi

install-vboxvideo:
	+ $(VBOX_QUIET_SH)if [ -d vboxvideo ]; then \
	    $(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C vboxvideo install; \
	fi

install: install-vboxguest install-vboxsf $(if $(shell [ "$(KERN_MAJ)" -lt 7 ] && echo y),install-vboxvideo,)

clean-vboxguest:
	+ $(VBOX_QUIET)$(MAKE) -C vboxguest clean
	rm -f vboxguest.*o

clean-vboxsf:
	+ $(VBOX_QUIET_SH)if [ -d vboxsf ]; then \
	    $(MAKE) -C vboxsf clean; \
	fi
	rm -f vboxsf.*o

clean-vboxvideo:
	+ $(VBOX_QUIET_SH)if [ -d vboxvideo ]; then \
	    $(MAKE) -C vboxvideo clean; \
	fi
	rm -f vboxvideo.*o

clean: clean-vboxguest clean-vboxsf $(if $(shell [ "$(KERN_MAJ)" -lt 7 ] && echo y),clean-vboxvideo,)

check:
	$(VBOX_QUIET)$(MAKE) KBUILD_VERBOSE=$(KBUILD_VERBOSE) -C vboxguest check

unload:
	$(VBOX_QUIET)/sbin/rmmod vboxvideo || true
	$(VBOX_QUIET)/sbin/rmmod vboxvfs || true
	$(VBOX_QUIET)/sbin/rmmod vboxsf || true
	$(VBOX_QUIET)/sbin/rmmod vboxguest || true

load: unload
	$(VBOX_QUIET)/sbin/insmod vboxguest.ko
	$(VBOX_QUIET)if [ -f vboxsf.ko ]; then /sbin/insmod vboxsf.ko; fi
	$(VBOX_QUIET)if [ -f vboxvideo.ko ]; then /sbin/insmod vboxvideo.ko; fi

.PHONY: all install clean check unload load \
    vboxguest vboxsf vboxvideo \
    install-vboxguest install-vboxsf install-vboxvideo \
    clean-vboxguest clean-vboxsf clean-vboxvideo

endif # ! KERNELRELEASE
