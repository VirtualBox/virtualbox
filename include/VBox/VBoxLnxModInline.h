/* $Id: VBoxLnxModInline.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * A common code for VirtualBox Linux kernel modules.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef VBOX_INCLUDED_VBoxLnxModInline_h
#define VBOX_INCLUDED_VBoxLnxModInline_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

# include <linux/module.h>
# include <linux/types.h>

/** Disable automatic module loading. */
static int g_fDisabled = -1;
MODULE_PARM_DESC(disabled, "Disable automatic module loading");
module_param_named(disabled, g_fDisabled, int, 0400);

/**
 * Check if module loading was explicitly disabled.
 *
 * Usually module loading can be disabled by
 * specifying mod_name.disabled=1 in kernel command line.
 *
 * @returns True if modules loading was disabled, False otherwise.
 */
static inline bool vbox_mod_should_load(void)
{
    bool fShouldLoad = (g_fDisabled != 1);

    /* Print message into dmesg log if module loading was disabled. */
    if (!fShouldLoad)
        printk(KERN_WARNING "%s: automatic module loading disabled in kernel command line\n",
               module_name(THIS_MODULE));

    return fShouldLoad;
}

#endif /* !VBOX_INCLUDED_VBoxLnxModInline_h */

