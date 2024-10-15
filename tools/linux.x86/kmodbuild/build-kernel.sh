#!/bin/bash
# $Id: build-kernel.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Script for build a linux kernel with a default configuration.
#
# This script assumes gcc-6, gcc-4.9 and gcc-3.3 are available on the system.
#
# For really old kernels make 3.80 and 3.76 will need to be built and put in
# a specific place relative to the kernel sources.
#
# This script may patch the kernel source a little to work around issues with
# newere binutils, perl, glibc and maybe compilers.
#
# It is recommended to use a overlayfs setup and kDeDup the kernel sources to
# save disk space.
#

#
# Copyright (C) 2019-2024 Oracle and/or its affiliates.
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

#
# /etc/apt/sources.list clues:
#
# # for gcc-4.8
# deb http://deb.debian.org/debian/ oldstable main contrib non-free
# deb-src http://deb.debian.org/debian/ oldstable main contrib non-free
#
# # for gcc-6
# deb http://deb.debian.org/debian/ stable main contrib non-free
# deb-src http://deb.debian.org/debian/ stable main contrib non-free
#
# # for gcc 3.4.x
# deb [ allow-insecure=yes ] http://archive.debian.org/debian/ lenny main contrib non-free
# deb-src [ allow-insecure=yes ] http://archive.debian.org/debian/ lenny main contrib non-free
#
# # for gcc 3.3.x
# deb [ allow-insecure=yes ] http://archive.debian.org/debian/ etch main contrib non-free
# deb-src [ allow-insecure=yes ] http://archive.debian.org/debian/ etch main contrib non-free
#
# # for gcc 3.2.x
# deb [ allow-insecure=yes  arch=i386] http://archive.debian.org/debian/ woody main contrib non-free
# deb-src [ allow-insecure=yes arch=i386 ] http://archive.debian.org/debian/ woody main contrib non-free
#
#
# Clue for /etc/fstab:
# overlay /mnt/bldlnx/amd64 overlay lowerdir=/mnt/big/virgin-lnx/,upperdir=/mnt/big/bldlnx/amd64,workdir=/mnt/big/workdir/bldlnx-amd64,noauto 0 0
#

if [ -z "${JOBS}" ]; then JOBS=42; fi

#
# The path argument.
#
if [ "$#" -lt "1" ]; then
    echo "usage: build.sh <dir> [clean]"
    exit 2
fi

set -e
echo "********************************************************************************"
echo "*   $1"
echo "********************************************************************************"
set -x
shopt -s extglob

# Enter it.
cd "$1"

# Change the terminal title (ASSUMES xterm-like TERM).
KERN_SUBDIR=`basename $1`
export PS1="\$ ";
echo -ne "\033]0;build.sh - ${KERN_SUBDIR}\007"

# Derive the version from it.
KERN_VER=`echo $1 | sed -e 's/^.*linux-\([0-9][0-9.]*\).*$/\1/'`
case "${KERN_VER}" in
    [0-9].[0-9]|[0-9].[0-9][0-9]|[0-9][0-9].[0-9]|[0-9][0-9].[0-9][0-9])
        KERN_VER_3PLUS_DOTS="${KERN_VER}.0";;
    *)
        KERN_VER_3PLUS_DOTS=${KERN_VER};;
esac
echo "debug: KERN_VER=${KERN_VER}  --> KERN_VER_3PLUS_DOTS=${KERN_VER_3PLUS_DOTS}"

# Determin tool overrides.
OVERRIDES=
MAKE=/usr/bin/make
case "${KERN_VER_3PLUS_DOTS}" in
    4.9.*|4.1[0-7].*)
        OVERRIDES="CC=gcc-6 CXX=g++-6"
        ;;
    2.6.3[789]*|3.*|4.[0-8].*)
        OVERRIDES="CC=gcc-4.9 CXX=g++-4.9"
        ;;
    2.6.29*|2.6.3[0-9]*)
        OVERRIDES="CC=gcc-3.3 CXX=g++-3.3"
        ;;
    2.6.[89]*|2.6.12[0-9]*|2.6.2[0-8]*)
        OVERRIDES="CC=gcc-3.3 CXX=g++-3.3"
        MAKE=../../make-3.80/installed/bin/make
        ;;
    2.6.*)
        OVERRIDES="CC=gcc-3.3 CXX=g++-3.3"
        MAKE=../../make-3.80/installed/bin/make
        ;;
esac
echo "debug: OVERRIDES=${OVERRIDES}  MAKE=${MAKE}"

echo "${OVERRIDES}" > .bird-overrides
ln -sf "${MAKE}" .bird-make

# Done with arg #1.
shift


#
# Apply patches for newer tools and stuff.
#

# perl --annoying
if [ -f kernel/timeconst.pl ]; then
    if patch --output /tmp/build.$$ -Np1 <<EOF
--- a/kernel/timeconst.pl       2019-04-15 13:44:55.434946090 +0200
+++ b/kernel/timeconst.pl       2019-04-15 13:57:29.330140587 +0200
@@ -372,5 +372,5 @@
        @val = @{\$canned_values{\$hz}};
-       if (!defined(@val)) {
+       if (!@val) {
                @val = compute_values(\$hz);
        }
        output(\$hz, @val);
EOF
    then
        cp /tmp/build.$$ kernel/timeconst.pl
    fi
fi

# binutils PLT32
case "${KERN_VER_3PLUS_DOTS}" in
    4.10.*|4.11.*|4.12.*|4.13.*|4.14.*|4.15.*|4.16.*)
        if patch --output /tmp/build.$$ -Np1 <<EOF
diff --git a/arch/x86/kernel/machine_kexec_64.c b/arch/x86/kernel/machine_kexec_64.c
index 1f790cf9d38f..3b7427aa7d85 100644
--- a/arch/x86/kernel/machine_kexec_64.c
+++ b/arch/x86/kernel/machine_kexec_64.c
@@ -542,6 +542,7 @@ int arch_kexec_apply_relocations_add(const Elf64_Ehdr *ehdr,
                                goto overflow;
                        break;
                case R_X86_64_PC32:
+               case R_X86_64_PLT32:
                        value -= (u64)address;
                        *(u32 *)location = value;
                        break;
EOF
then
            cp /tmp/build.$$ arch/x86/kernel/machine_kexec_64.c
        fi
        case "${KERN_VER}" in
            4.10.*|4.11.*|4.12.*|4.13.*)
                if patch --output /tmp/build.$$ -Np1 <<EOF
diff --git a/arch/x86/kernel/module.c b/arch/x86/kernel/module.c
index da0c160e5589..f58336af095c 100644
--- a/arch/x86/kernel/module.c
+++ b/arch/x86/kernel/module.c
@@ -191,6 +191,7 @@ int apply_relocate_add(Elf64_Shdr *sechdrs,
                                goto overflow;
                        break;
                case R_X86_64_PC32:
+               case R_X86_64_PLT32:
                        val -= (u64)loc;
                        *(u32 *)loc = val;
 #if 0
EOF
                then
                    cp /tmp/build.$$ arch/x86/kernel/module.c
                fi;;
            **)
                if patch --output /tmp/build.$$ -Np1 <<EOF
diff --git a/arch/x86/kernel/module.c b/arch/x86/kernel/module.c
index da0c160e5589..f58336af095c 100644
--- a/arch/x86/kernel/module.c
+++ b/arch/x86/kernel/module.c
@@ -191,6 +191,7 @@ int apply_relocate_add(Elf64_Shdr *sechdrs,
                                goto overflow;
                        break;
                case R_X86_64_PC32:
+               case R_X86_64_PLT32:
                        if (*(u32 *)loc != 0)
                                goto invalid_relocation;
                        val -= (u64)loc;
EOF
                then
                    cp /tmp/build.$$ arch/x86/kernel/module.c
                fi;;
        esac
        if patch --output /tmp/build.$$ -Np1 <<EOF
diff --git a/arch/x86/tools/relocs.c b/arch/x86/tools/relocs.c
index 5d73c443e778..220e97841e49 100644
--- a/arch/x86/tools/relocs.c
+++ b/arch/x86/tools/relocs.c
@@ -770,9 +770,12 @@ static int do_reloc64(struct section *sec, Elf_Rel *rel, ElfW(Sym) *sym,
                break;

        case R_X86_64_PC32:
+       case R_X86_64_PLT32:
                /*
                 * PC relative relocations don't need to be adjusted unless
                 * referencing a percpu symbol.
+                *
+                * NB: R_X86_64_PLT32 can be treated as R_X86_64_PC32.
                 */
                if (is_percpu_sym(sym, symname))
                        add_reloc(&relocs32neg, offset);
EOF
        then
            cp /tmp/build.$$ arch/x86/tools/relocs.c
        fi
        if patch --output /tmp/build.$$ -Np1 <<EOF
--- linux-4.15/tools/lib/subcmd/pager.c 2017-11-12 19:46:13.000000000 +0100
+++ linux-4.17/tools/lib/subcmd/pager.c 2018-06-03 23:15:21.000000000 +0200
@@ -30,10 +30,13 @@
         * have real input
         */
        fd_set in;
+       fd_set exception;

        FD_ZERO(&in);
+       FD_ZERO(&exception);
        FD_SET(0, &in);
-       select(1, &in, NULL, &in, NULL);
+       FD_SET(0, &exception);
+       select(1, &in, NULL, &exception, NULL);

        setenv("LESS", "FRSX", 0);
 }
EOF
        then
            cp /tmp/build.$$ tools/lib/subcmd/pager.c
        fi
        if patch --output /tmp/build.$$ -Np1 <<EOF
--- linux-4.16/tools/lib/str_error_r.c  2019-04-15 06:04:50.978464217 +0200
+++ linux-4.17/tools/lib/str_error_r.c  2018-06-03 23:15:21.000000000 +0200
@@ -22,6 +22,6 @@
 {
        int err = strerror_r(errnum, buf, buflen);
        if (err)
-               snprintf(buf, buflen, "INTERNAL ERROR: strerror_r(%d, %p, %zd)=%d", errnum, buf, buflen, err);
+               snprintf(buf, buflen, "INTERNAL ERROR: strerror_r(%d, [buf], %zd)=%d", errnum, buflen, err);
        return buf;
 }
EOF
        then
            cp /tmp/build.$$ tools/lib/str_error_r.c
        fi
        ;;
esac

# Undefined ____ilog2_NaN symbol:
if [ -f include/linux/log2.h ]; then
    case "${KERN_VER_3PLUS_DOTS}" in
        4.10.*|4.[9].*)
        if patch --output /tmp/build.$$ -Np1 <<EOF
--- linux-4.10/include/linux/log2.h     2017-02-19 23:34:00.000000000 +0100
+++ linux-4.11/include/linux/log2.h     2017-11-12 19:46:13.000000000 +0100
@@ -15,14 +15,8 @@
 #include <linux/types.h>
 #include <linux/bitops.h>

 /*
- * deal with unrepresentable constant logarithms
- */
-extern __attribute__((const, noreturn))
-int ____ilog2_NaN(void);
-
-/*
  * non-constant log of base 2 calculators
  * - the arch may override these in asm/bitops.h if they can be implemented
  *   more efficiently than using fls() and fls64()
  * - the arch is not required to handle n==0 if implementing the fallback
@@ -84,9 +78,9 @@
  */
 #define ilog2(n)                               \\
 (                                              \\
        __builtin_constant_p(n) ? (             \\
-               (n) < 1 ? ____ilog2_NaN() :     \\
+               (n) < 2 ? 0 :                   \\
                (n) & (1ULL << 63) ? 63 :       \\
                (n) & (1ULL << 62) ? 62 :       \\
                (n) & (1ULL << 61) ? 61 :       \\
                (n) & (1ULL << 60) ? 60 :       \\
@@ -147,12 +141,9 @@
                (n) & (1ULL <<  5) ?  5 :       \\
                (n) & (1ULL <<  4) ?  4 :       \\
                (n) & (1ULL <<  3) ?  3 :       \\
                (n) & (1ULL <<  2) ?  2 :       \\
-               (n) & (1ULL <<  1) ?  1 :       \\
-               (n) & (1ULL <<  0) ?  0 :       \\
-               ____ilog2_NaN()                 \\
-                                  ) :          \\
+               1 ) :                           \\
        (sizeof(n) <= 4) ?                      \\
        __ilog2_u32(n) :                        \\
        __ilog2_u64(n)                          \\
  )
EOF
        then
            cp /tmp/build.$$ include/linux/log2.h
        fi
        ;;
    esac
fi

# extern then static current_menu.
if [ -f scripts/kconfig/lkc.h  -a  -f scripts/kconfig/mconf.c ]; then
    case "${KERN_VER_3PLUS_DOTS}" in
        2.6.1[0-9]*|2.6.2[0-9]*|2.6.3[0-9]*|2.6.4[0-9]*)
            ;;
        2.5.*|2.6.[012345678])
            if patch --output /tmp/build.$$ -Np1 <<EOF
--- linux-2.6.8/scripts/kconfig/mconf.c 2004-08-14 07:36:32.000000000 +0200
+++ linux-2.6.8/scripts/kconfig/mconf.c 2019-04-15 15:52:42.143587966 +0200
@@ -88,5 +88,5 @@
 static struct termios ios_org;
 static int rows = 0, cols = 0;
-static struct menu *current_menu;
+struct menu *current_menu;
 static int child_count;
 static int do_resize;
EOF
            then
                cp /tmp/build.$$ scripts/kconfig/mconf.c
            fi
            ;;
    esac
fi

# Incorrect END label in arch/x86/lib/copy_user_64.S
case "${KERN_VER_3PLUS_DOTS}" in
    2.6.2[456]*)
        if patch --output /tmp/build.$$ -Np1 <<EOF
--- linux-2.6.26/arch/x86/lib/copy_user_64.S    2019-04-15 16:21:49.475846822 +0200
+++ linux-2.6.26/arch/x86/lib/copy_user_64.S    2019-04-15 16:21:50.883863141 +0200
@@ -341,7 +341,7 @@
 11:    pop %rax
 7:     ret
        CFI_ENDPROC
-END(copy_user_generic_c)
+END(copy_user_generic_string)

        .section __ex_table,"a"
        .quad 1b,3b
EOF
        then
            cp /tmp/build.$$ arch/x86/lib/copy_user_64.S
        fi
        ;;
    2.6.2[0123]*|2.6.19*)
        if patch --output /tmp/build.$$ -Np1 <<EOF
--- linux-2.6.23/arch/x86_64/lib/copy_user.S    2019-04-15 16:42:16.898006203 +0200
+++ linux-2.6.23/arch/x86_64/lib/copy_user.S    2019-04-15 16:42:25.906109885 +0200
@@ -344,7 +344,7 @@
 11:    pop %rax
 7:     ret
        CFI_ENDPROC
-END(copy_user_generic_c)
+END(copy_user_generic_string)

        .section __ex_table,"a"
        .quad 1b,3b
EOF
        then
            cp /tmp/build.$$ arch/x86_64/lib/copy_user.S
        fi
        ;;
esac

# Increase vdso text segment limit as newer tools/whatever causes it to be too large.
if [ -f arch/x86_64/vdso/vdso.lds.S ]; then
    if patch --output /tmp/build.$$ -Np1 <<EOF
--- linux-2.6.23/arch/x86_64/vdso/vdso.lds.S    2019-04-15 17:20:27.567440594 +0200
+++ linux-2.6.23/arch/x86_64/vdso/vdso.lds.S    2019-04-15 17:20:29.635463886 +0200
@@ -28,5 +28,5 @@
   .text           : { *(.text) }               :text
   .text.ptr       : { *(.text.ptr) }           :text
-  . = VDSO_PRELINK + 0x900;
+  . = VDSO_PRELINK + 0xa00;
   .data           : { *(.data) }               :text
   .bss            : { *(.bss) }                        :text
EOF
    then
        cp /tmp/build.$$ arch/x86_64/vdso/vdso.lds.S
    fi
fi

# glibc PATH_MAX cleanup affect 2.6.21 and earlier:
if [ -f scripts/mod/sumversion.c ]; then
    case "${KERN_VER_3PLUS_DOTS}" in
        2.6.[0-9]!([0-9])*|2.6.1[0-9]*|2.6.2[01]*)
            if patch --output /tmp/build.$$ -Np1 <<EOF
--- linux-2.6.21/scripts/mod/sumversion.c       2007-02-04 19:44:54.000000000 +0100
+++ linux-2.6.21/scripts/mod/sumversion.c       2019-02-15 16:10:12.956678862 +0100
@@ -7,4 +7,5 @@
 #include <ctype.h>
 #include <errno.h>
 #include <string.h>
+#include <linux/limits.h>
 #include "modpost.h"
EOF
        then
            cp /tmp/build.$$ scripts/mod/sumversion.c
        fi
    esac
fi

# Problem with "System too big" messages in 2.6.17 and earlier:
if [ -f arch/x86_64/boot/tools/build.c ]; then
    case "${KERN_VER_3PLUS_DOTS}" in
        2.6.[0-9]!([0-9])*|2.6.1[0-7]*)
            if patch --output /tmp/build.$$ -Np1 <<EOF
--- linux-2.6.17/arch/x86_64/boot/tools/build.c 2006-01-03 04:21:10.000000000 +0100
+++ linux-2.6.18/arch/x86_64/boot/tools/build.c 2007-02-04 19:44:54.000000000 +0100
@@ -149,9 +149,7 @@
        sz = sb.st_size;
        fprintf (stderr, "System is %d kB\n", sz/1024);
        sys_size = (sz + 15) / 16;
-       /* 0x40000*16 = 4.0 MB, reasonable estimate for the current maximum */
-       if (sys_size > (is_big_kernel ? 0x40000 : DEF_SYSSIZE))
-               die("System is too big. Try using %smodules.",
-                       is_big_kernel ? "" : "bzImage or ");
+       if (!is_big_kernel && sys_size > DEF_SYSSIZE)
+               die("System is too big. Try using bzImage or modules.");
        while (sz > 0) {
                int l, n;
EOF
        then
            cp /tmp/build.$$ arch/x86_64/boot/tools/build.c
        fi
    esac
fi

# Problem with incorrect mov sizes for segments in 2.6.11 and earlier:
if [ -f arch/x86_64/kernel/process.c ]; then
    case "${KERN_VER_3PLUS_DOTS}" in
        2.6.[0-9]!([0-9])*|2.6.1[01]*)
            if patch --output /tmp/build.$$ -lNp1 <<EOF
--- linux-2.6.11/arch/x86_64/kernel/process.c   2005-03-02 08:38:10.000000000 +0100
+++ linux-2.6.11/arch/x86_64/kernel/process.c   2019-02-15 16:57:47.653585327 +0100
@@ -390,10 +390,10 @@
        p->thread.fs = me->thread.fs;
        p->thread.gs = me->thread.gs;

-       asm("movl %%gs,%0" : "=m" (p->thread.gsindex));
-       asm("movl %%fs,%0" : "=m" (p->thread.fsindex));
-       asm("movl %%es,%0" : "=m" (p->thread.es));
-       asm("movl %%ds,%0" : "=m" (p->thread.ds));
+       asm("movw %%gs,%0" : "=m" (p->thread.gsindex));
+       asm("movw %%fs,%0" : "=m" (p->thread.fsindex));
+       asm("movw %%es,%0" : "=m" (p->thread.es));
+       asm("movw %%ds,%0" : "=m" (p->thread.ds));

        if (unlikely(me->thread.io_bitmap_ptr != NULL)) {
                p->thread.io_bitmap_ptr = kmalloc(IO_BITMAP_BYTES, GFP_KERNEL);
@@ -456,11 +456,11 @@
         * Switch DS and ES.
         * This won't pick up thread selector changes, but I guess that is ok.
         */
-       asm volatile("movl %%es,%0" : "=m" (prev->es));
+       asm volatile("movw %%es,%0" : "=m" (prev->es));
        if (unlikely(next->es | prev->es))
                loadsegment(es, next->es);

-       asm volatile ("movl %%ds,%0" : "=m" (prev->ds));
+       asm volatile ("movw %%ds,%0" : "=m" (prev->ds));
        if (unlikely(next->ds | prev->ds))
                loadsegment(ds, next->ds);
EOF
        then
            cp /tmp/build.$$ arch/x86_64/kernel/process.c
        fi
    esac
fi


#
# Other arguments.
#
while [ "$#" -gt 0 ];
do
    case "$1" in
        clean)
            time ./.bird-make ${OVERRIDES} -j ${JOBS} clean
            ;;

        *)
            echo "syntax error: $1" 1>&2
            ;;
    esac
    shift
done

#
# Configure.
#
if [ -f .config ]; then
    mv -f .config .bird-previous-config
fi
nice ./.bird-make ${OVERRIDES} -j ${JOBS} defconfig
case "${KERN_VER_3PLUS_DOTS}" in
    2.[012345].*|2.6.[0-9]!([0-9])*|2.6.[12][0-9]*)
        ;;
    *)
        echo CONFIG_DRM_TTM=m >> .config;
        echo CONFIG_DRM_RADEON=m >> .config
        echo CONFIG_DRM_RADEON_UMS=y >> .config
        echo CONFIG_DRM_RADEON_USERPTR=y >> .config
        echo CONFIG_DRM_RADEON_KMS=y >> .config
        ;;
esac
case "${KERN_VER_3PLUS_DOTS}" in
    2.4.*)  ;;
    4.2[0-9].*|4.1[789].*|[5-9].*)
        nice ./.bird-make ${OVERRIDES} syncconfig;;
    *)  nice ./.bird-make ${OVERRIDES} silentoldconfig;;
esac
if [ -f .bird-previous-config ]; then
    if cmp -s .config .bird-previous-config; then
        mv -f .bird-previous-config .config
    fi
fi

#
# Build all.
#
if time nice ./.bird-make ${OVERRIDES} -j ${JOBS} all -k; then
    rm -f .bird-failed
    echo -ne "\033]0;build.sh - ${KERN_SUBDIR} - done\007"
else
    touch .bird-failed
    echo -ne "\033]0;build.sh - ${KERN_SUBDIR} - failed\007"
    exit 1
fi

