/* $Id: Debug.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox storage devices: debug helpers
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */


#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/log.h>
#include <iprt/types.h>
#include <iprt/string.h>
#include <VBox/scsi.h>
#include <VBox/ata.h>

#ifdef LOG_ENABLED

/**
 * ATA command codes
 */
static const char * const g_apszATACmdNames[256] =
{
    "NOP",                                 /* 0x00 */
    "",                                    /* 0x01 */
    "",                                    /* 0x02 */
    "CFA REQUEST EXTENDED ERROR CODE",     /* 0x03 */
    "",                                    /* 0x04 */
    "",                                    /* 0x05 */
    "DATA SET MANAGEMENT",                 /* 0x06 */
    "",                                    /* 0x07 */
    "DEVICE RESET",                        /* 0x08 */
    "",                                    /* 0x09 */
    "",                                    /* 0x0a */
    "",                                    /* 0x0b */
    "",                                    /* 0x0c */
    "",                                    /* 0x0d */
    "",                                    /* 0x0e */
    "",                                    /* 0x0f */
    "RECALIBRATE",                         /* 0x10 */
    "",                                    /* 0x11 */
    "",                                    /* 0x12 */
    "",                                    /* 0x13 */
    "",                                    /* 0x14 */
    "",                                    /* 0x15 */
    "",                                    /* 0x16 */
    "",                                    /* 0x17 */
    "",                                    /* 0x18 */
    "",                                    /* 0x19 */
    "",                                    /* 0x1a */
    "",                                    /* 0x1b */
    "",                                    /* 0x1c */
    "",                                    /* 0x1d */
    "",                                    /* 0x1e */
    "",                                    /* 0x1f */
    "READ SECTORS",                        /* 0x20 */
    "READ SECTORS WITHOUT RETRIES",        /* 0x21 */
    "READ LONG",                           /* 0x22 */
    "READ LONG WITHOUT RETRIES",           /* 0x23 */
    "READ SECTORS EXT",                    /* 0x24 */
    "READ DMA EXT",                        /* 0x25 */
    "READ DMA QUEUED EXT",                 /* 0x26 */
    "READ NATIVE MAX ADDRESS EXT",         /* 0x27 */
    "",                                    /* 0x28 */
    "READ MULTIPLE EXT",                   /* 0x29 */
    "READ STREAM DMA EXT",                 /* 0x2a */
    "READ STREAM EXT",                     /* 0x2b */
    "",                                    /* 0x2c */
    "",                                    /* 0x2d */
    "",                                    /* 0x2e */
    "READ LOG EXT",                        /* 0x2f */
    "WRITE SECTORS",                       /* 0x30 */
    "WRITE SECTORS WITHOUT RETRIES",       /* 0x31 */
    "WRITE LONG",                          /* 0x32 */
    "WRITE LONG WITHOUT RETRIES",          /* 0x33 */
    "WRITE SECTORS EXT",                   /* 0x34 */
    "WRITE DMA EXT",                       /* 0x35 */
    "WRITE DMA QUEUED EXT",                /* 0x36 */
    "SET MAX ADDRESS EXT",                 /* 0x37 */
    "CFA WRITE SECTORS WITHOUT ERASE",     /* 0x38 */
    "WRITE MULTIPLE EXT",                  /* 0x39 */
    "WRITE STREAM DMA EXT",                /* 0x3a */
    "WRITE STREAM EXT",                    /* 0x3b */
    "WRITE VERIFY",                        /* 0x3c */
    "WRITE DMA FUA EXT",                   /* 0x3d */
    "WRITE DMA QUEUED FUA EXT",            /* 0x3e */
    "WRITE LOG EXT",                       /* 0x3f */
    "READ VERIFY SECTORS",                 /* 0x40 */
    "READ VERIFY SECTORS WITHOUT RETRIES", /* 0x41 */
    "READ VERIFY SECTORS EXT",             /* 0x42 */
    "",                                    /* 0x43 */
    "",                                    /* 0x44 */
    "WRITE UNCORRECTABLE EXT",             /* 0x45 */
    "",                                    /* 0x46 */
    "READ LOG DMA EXT",                    /* 0x47 */
    "",                                    /* 0x48 */
    "",                                    /* 0x49 */
    "",                                    /* 0x4a */
    "",                                    /* 0x4b */
    "",                                    /* 0x4c */
    "",                                    /* 0x4d */
    "",                                    /* 0x4e */
    "",                                    /* 0x4f */
    "FORMAT TRACK",                        /* 0x50 */
    "CONFIGURE STREAM",                    /* 0x51 */
    "",                                    /* 0x52 */
    "",                                    /* 0x53 */
    "",                                    /* 0x54 */
    "",                                    /* 0x55 */
    "",                                    /* 0x56 */
    "WRITE LOG DMA EXT",                   /* 0x57 */
    "",                                    /* 0x58 */
    "",                                    /* 0x59 */
    "",                                    /* 0x5a */
    "",                                    /* 0x5b */
    "TRUSTED RECEIVE",                     /* 0x5c */
    "TRUSTED RECEIVE DMA",                 /* 0x5d */
    "TRUSTED SEND",                        /* 0x5e */
    "TRUSTED SEND DMA",                    /* 0x5f */
    "READ FPDMA QUEUED",                   /* 0x60 */
    "WRITE FPDMA QUEUED",                  /* 0x61 */
    "",                                    /* 0x62 */
    "",                                    /* 0x63 */
    "",                                    /* 0x64 */
    "",                                    /* 0x65 */
    "",                                    /* 0x66 */
    "",                                    /* 0x67 */
    "",                                    /* 0x68 */
    "",                                    /* 0x69 */
    "",                                    /* 0x6a */
    "",                                    /* 0x6b */
    "",                                    /* 0x6c */
    "",                                    /* 0x6d */
    "",                                    /* 0x6e */
    "",                                    /* 0x6f */
    "SEEK",                                /* 0x70 */
    "",                                    /* 0x71 */
    "",                                    /* 0x72 */
    "",                                    /* 0x73 */
    "",                                    /* 0x74 */
    "",                                    /* 0x75 */
    "",                                    /* 0x76 */
    "",                                    /* 0x77 */
    "",                                    /* 0x78 */
    "",                                    /* 0x79 */
    "",                                    /* 0x7a */
    "",                                    /* 0x7b */
    "",                                    /* 0x7c */
    "",                                    /* 0x7d */
    "",                                    /* 0x7e */
    "",                                    /* 0x7f */
    "",                                    /* 0x80 */
    "",                                    /* 0x81 */
    "",                                    /* 0x82 */
    "",                                    /* 0x83 */
    "",                                    /* 0x84 */
    "",                                    /* 0x85 */
    "",                                    /* 0x86 */
    "CFA TRANSLATE SECTOR",                /* 0x87 */
    "",                                    /* 0x88 */
    "",                                    /* 0x89 */
    "",                                    /* 0x8a */
    "",                                    /* 0x8b */
    "",                                    /* 0x8c */
    "",                                    /* 0x8d */
    "",                                    /* 0x8e */
    "",                                    /* 0x8f */
    "EXECUTE DEVICE DIAGNOSTIC",           /* 0x90 */
    "INITIALIZE DEVICE PARAMETERS",        /* 0x91 */
    "DOWNLOAD MICROCODE",                  /* 0x92 */
    "",                                    /* 0x93 */
    "STANDBY IMMEDIATE  ALT",              /* 0x94 */
    "IDLE IMMEDIATE  ALT",                 /* 0x95 */
    "STANDBY  ALT",                        /* 0x96 */
    "IDLE  ALT",                           /* 0x97 */
    "CHECK POWER MODE  ALT",               /* 0x98 */
    "SLEEP  ALT",                          /* 0x99 */
    "",                                    /* 0x9a */
    "",                                    /* 0x9b */
    "",                                    /* 0x9c */
    "",                                    /* 0x9d */
    "",                                    /* 0x9e */
    "",                                    /* 0x9f */
    "PACKET",                              /* 0xa0 */
    "IDENTIFY PACKET DEVICE",              /* 0xa1 */
    "SERVICE",                             /* 0xa2 */
    "",                                    /* 0xa3 */
    "",                                    /* 0xa4 */
    "",                                    /* 0xa5 */
    "",                                    /* 0xa6 */
    "",                                    /* 0xa7 */
    "",                                    /* 0xa8 */
    "",                                    /* 0xa9 */
    "",                                    /* 0xaa */
    "",                                    /* 0xab */
    "",                                    /* 0xac */
    "",                                    /* 0xad */
    "",                                    /* 0xae */
    "",                                    /* 0xaf */
    "SMART",                               /* 0xb0 */
    "DEVICE CONFIGURATION OVERLAY",        /* 0xb1 */
    "",                                    /* 0xb2 */
    "",                                    /* 0xb3 */
    "",                                    /* 0xb4 */
    "",                                    /* 0xb5 */
    "NV CACHE",                            /* 0xb6 */
    "",                                    /* 0xb7 */
    "",                                    /* 0xb8 */
    "",                                    /* 0xb9 */
    "",                                    /* 0xba */
    "",                                    /* 0xbb */
    "",                                    /* 0xbc */
    "",                                    /* 0xbd */
    "",                                    /* 0xbe */
    "",                                    /* 0xbf */
    "CFA ERASE SECTORS",                   /* 0xc0 */
    "",                                    /* 0xc1 */
    "",                                    /* 0xc2 */
    "",                                    /* 0xc3 */
    "READ MULTIPLE",                       /* 0xc4 */
    "WRITE MULTIPLE",                      /* 0xc5 */
    "SET MULTIPLE MODE",                   /* 0xc6 */
    "READ DMA QUEUED",                     /* 0xc7 */
    "READ DMA",                            /* 0xc8 */
    "READ DMA WITHOUT RETRIES",            /* 0xc9 */
    "WRITE DMA",                           /* 0xca */
    "WRITE DMA WITHOUT RETRIES",           /* 0xcb */
    "WRITE DMA QUEUED",                    /* 0xcc */
    "CFA WRITE MULTIPLE WITHOUT ERASE",    /* 0xcd */
    "WRITE MULTIPLE FUA EXT",              /* 0xce */
    "",                                    /* 0xcf */
    "",                                    /* 0xd0 */
    "CHECK MEDIA CARD TYPE",               /* 0xd1 */
    "",                                    /* 0xd2 */
    "",                                    /* 0xd3 */
    "",                                    /* 0xd4 */
    "",                                    /* 0xd5 */
    "",                                    /* 0xd6 */
    "",                                    /* 0xd7 */
    "",                                    /* 0xd8 */
    "",                                    /* 0xd9 */
    "GET MEDIA STATUS",                    /* 0xda */
    "ACKNOWLEDGE MEDIA CHANGE",            /* 0xdb */
    "BOOT POST BOOT",                      /* 0xdc */
    "BOOT PRE BOOT",                       /* 0xdd */
    "MEDIA LOCK",                          /* 0xde */
    "MEDIA UNLOCK",                        /* 0xdf */
    "STANDBY IMMEDIATE",                   /* 0xe0 */
    "IDLE IMMEDIATE",                      /* 0xe1 */
    "STANDBY",                             /* 0xe2 */
    "IDLE",                                /* 0xe3 */
    "READ BUFFER",                         /* 0xe4 */
    "CHECK POWER MODE",                    /* 0xe5 */
    "SLEEP",                               /* 0xe6 */
    "FLUSH CACHE",                         /* 0xe7 */
    "WRITE BUFFER",                        /* 0xe8 */
    "WRITE SAME",                          /* 0xe9 */
    "FLUSH CACHE EXT",                     /* 0xea */
    "",                                    /* 0xeb */
    "IDENTIFY DEVICE",                     /* 0xec */
    "MEDIA EJECT",                         /* 0xed */
    "IDENTIFY DMA",                        /* 0xee */
    "SET FEATURES",                        /* 0xef */
    "",                                    /* 0xf0 */
    "SECURITY SET PASSWORD",               /* 0xf1 */
    "SECURITY UNLOCK",                     /* 0xf2 */
    "SECURITY ERASE PREPARE",              /* 0xf3 */
    "SECURITY ERASE UNIT",                 /* 0xf4 */
    "SECURITY FREEZE LOCK",                /* 0xf5 */
    "SECURITY DISABLE PASSWORD",           /* 0xf6 */
    "",                                    /* 0xf7 */
    "READ NATIVE MAX ADDRESS",             /* 0xf8 */
    "SET MAX",                             /* 0xf9 */
    "",                                    /* 0xfa */
    "",                                    /* 0xfb */
    "",                                    /* 0xfc */
    "",                                    /* 0xfd */
    "",                                    /* 0xfe */
    ""                                     /* 0xff */
};

#endif /* LOG_ENABLED */

#if defined(LOG_ENABLED) || defined(RT_STRICT)

/**
 * SCSI command codes.
 */
static const char * const g_apszSCSICmdNames[256] =
{
    "TEST UNIT READY",                     /* 0x00 */
    "REZERO UNIT",                         /* 0x01 */
    "",                                    /* 0x02 */
    "REQUEST SENSE",                       /* 0x03 */
    "FORMAT UNIT",                         /* 0x04 */
    "READ BLOCK LIMITS",                   /* 0x05 */
    "",                                    /* 0x06 */
    "REASSIGN BLOCKS",                     /* 0x07 */
    "READ (6)",                            /* 0x08 */
    "",                                    /* 0x09 */
    "WRITE (6)",                           /* 0x0a */
    "SEEK (6)",                            /* 0x0b */
    "",                                    /* 0x0c */
    "",                                    /* 0x0d */
    "",                                    /* 0x0e */
    "READ REVERSE (6)",                    /* 0x0f */
    "READ FILEMARKS (6)",                  /* 0x10 */
    "SPACE (6)",                           /* 0x11 */
    "INQUIRY",                             /* 0x12 */
    "VERIFY (6)",                          /* 0x13 */
    "RECOVER BUFFERED DATA",               /* 0x14 */
    "MODE SELECT (6)",                     /* 0x15 */
    "RESERVE (6)",                         /* 0x16 */
    "RELEASE (6)",                         /* 0x17 */
    "COPY",                                /* 0x18 */
    "ERASE (6)",                           /* 0x19 */
    "MODE SENSE (6)",                      /* 0x1a */
    "START STOP UNIT",                     /* 0x1b */
    "RECEIVE DIAGNOSTIC RESULTS",          /* 0x1c */
    "SEND DIAGNOSTIC",                     /* 0x1d */
    "PREVENT ALLOW MEDIUM REMOVAL",        /* 0x1e */
    "",                                    /* 0x1f */
    "",                                    /* 0x20 */
    "",                                    /* 0x21 */
    "",                                    /* 0x22 */
    "READ FORMAT CAPACITIES",              /* 0x23 */
    "SET WINDOW",                          /* 0x24 */
    "READ CAPACITY",                       /* 0x25 */
    "",                                    /* 0x26 */
    "",                                    /* 0x27 */
    "READ (10)",                           /* 0x28 */
    "READ GENERATION",                     /* 0x29 */
    "WRITE (10)",                          /* 0x2a */
    "SEEK (10)",                           /* 0x2b */
    "ERASE (10)",                          /* 0x2c */
    "READ UPDATED BLOCK",                  /* 0x2d */
    "WRITE AND VERIFY (10)",               /* 0x2e */
    "VERIFY (10)",                         /* 0x2f */
    "SEARCH DATA HIGH (10)",               /* 0x30 */
    "SEARCH DATA EQUAL (10)",              /* 0x31 */
    "SEARCH DATA LOW (10)",                /* 0x32 */
    "SET LIMITS (10)",                     /* 0x33 */
    "PRE-FETCH (10)",                      /* 0x34 */
    "SYNCHRONIZE CACHE (10)",              /* 0x35 */
    "LOCK UNLOCK CACHE (10)",              /* 0x36 */
    "READ DEFECT DATA (10)",               /* 0x37 */
    "MEDIUM SCAN",                         /* 0x38 */
    "COMPARE",                             /* 0x39 */
    "COPY AND VERIFY",                     /* 0x3a */
    "WRITE BUFFER",                        /* 0x3b */
    "READ BUFFER",                         /* 0x3c */
    "UPDATE BLOCK",                        /* 0x3d */
    "READ LONG (10)",                      /* 0x3e */
    "WRITE LONG (10)",                     /* 0x3f */
    "CHANGE DEFINITION",                   /* 0x40 */
    "WRITE SAME (10)",                     /* 0x41 */
    "READ SUBCHANNEL",                     /* 0x42 */
    "READ TOC/PMA/ATIP",                   /* 0x43 */
    "REPORT DENSITY SUPPORT",              /* 0x44 */
    "PLAY AUDIO (10)",                     /* 0x45 */
    "GET CONFIGURATION",                   /* 0x46 */
    "PLAY AUDIO MSF",                      /* 0x47 */
    "",                                    /* 0x48 */
    "",                                    /* 0x49 */
    "GET EVENT STATUS NOTIFICATION",       /* 0x4a */
    "PAUSE/RESUME",                        /* 0x4b */
    "LOG SELECT",                          /* 0x4c */
    "LOG SENSE",                           /* 0x4d */
    "STOP PLAY/SCAN",                      /* 0x4e */
    "",                                    /* 0x4f */
    "XDWRITE (10)",                        /* 0x50 */
    "READ DISC INFORMATION",               /* 0x51 */
    "READ TRACK INFORMATION",              /* 0x52 */
    "RESERVE TRACK",                       /* 0x53 */
    "SEND OPC INFORMATION",                /* 0x54 */
    "MODE SELECT (10)",                    /* 0x55 */
    "RESERVE (10)",                        /* 0x56 */
    "RELEASE (10)",                        /* 0x57 */
    "REPAIR TRACK",                        /* 0x58 */
    "",                                    /* 0x59 */
    "MODE SENSE (10)",                     /* 0x5a */
    "CLOSE TRACK/SESSION",                 /* 0x5b */
    "READ BUFFER CAPACITY",                /* 0x5c */
    "SEND CUE SHEET",                      /* 0x5d */
    "PERSISTENT RESERVE IN",               /* 0x5e */
    "PERSISTENT RESERVE OUT",              /* 0x5f */
    "",                                    /* 0x60 */
    "",                                    /* 0x61 */
    "",                                    /* 0x62 */
    "",                                    /* 0x63 */
    "",                                    /* 0x64 */
    "",                                    /* 0x65 */
    "",                                    /* 0x66 */
    "",                                    /* 0x67 */
    "",                                    /* 0x68 */
    "",                                    /* 0x69 */
    "",                                    /* 0x6a */
    "",                                    /* 0x6b */
    "",                                    /* 0x6c */
    "",                                    /* 0x6d */
    "",                                    /* 0x6e */
    "",                                    /* 0x6f */
    "",                                    /* 0x70 */
    "",                                    /* 0x71 */
    "",                                    /* 0x72 */
    "",                                    /* 0x73 */
    "",                                    /* 0x74 */
    "",                                    /* 0x75 */
    "",                                    /* 0x76 */
    "",                                    /* 0x77 */
    "",                                    /* 0x78 */
    "",                                    /* 0x79 */
    "",                                    /* 0x7a */
    "",                                    /* 0x7b */
    "",                                    /* 0x7c */
    "",                                    /* 0x7d */
    "",                                    /* 0x7e */
    "",                                    /* 0x7f */
    "WRITE FILEMARKS (16)",                /* 0x80 */
    "READ REVERSE (16)",                   /* 0x81 */
    "REGENERATE (16)",                     /* 0x82 */
    "EXTENDED COPY",                       /* 0x83 */
    "RECEIVE COPY RESULTS",                /* 0x84 */
    "ATA COMMAND PASS THROUGH (16)",       /* 0x85 */
    "ACCESS CONTROL IN",                   /* 0x86 */
    "ACCESS CONTROL OUT",                  /* 0x87 */
    "READ (16)",                           /* 0x88 */
    "",                                    /* 0x89 */
    "WRITE(16)",                           /* 0x8a */
    "",                                    /* 0x8b */
    "READ ATTRIBUTE",                      /* 0x8c */
    "WRITE ATTRIBUTE",                     /* 0x8d */
    "WRITE AND VERIFY (16)",               /* 0x8e */
    "VERIFY (16)",                         /* 0x8f */
    "PRE-FETCH (16)",                      /* 0x90 */
    "SYNCHRONIZE CACHE (16)",              /* 0x91 */
    "LOCK UNLOCK CACHE (16)",              /* 0x92 */
    "WRITE SAME (16)",                     /* 0x93 */
    "",                                    /* 0x94 */
    "",                                    /* 0x95 */
    "",                                    /* 0x96 */
    "",                                    /* 0x97 */
    "",                                    /* 0x98 */
    "",                                    /* 0x99 */
    "",                                    /* 0x9a */
    "",                                    /* 0x9b */
    "",                                    /* 0x9c */
    "",                                    /* 0x9d */
    "SERVICE ACTION IN (16)",              /* 0x9e */
    "SERVICE ACTION OUT (16)",             /* 0x9f */
    "REPORT LUNS",                         /* 0xa0 */
    "BLANK",                               /* 0xa1 */
    "SEND EVENT",                          /* 0xa2 */
    "SEND KEY",                            /* 0xa3 */
    "REPORT KEY",                          /* 0xa4 */
    "PLAY AUDIO (12)",                     /* 0xa5 */
    "LOAD/UNLOAD MEDIUM",                  /* 0xa6 */
    "SET READ AHEAD",                      /* 0xa7 */
    "READ (12)",                           /* 0xa8 */
    "SERVICE ACTION OUT (12)",             /* 0xa9 */
    "WRITE (12)",                          /* 0xaa */
    "SERVICE ACTION IN (12)",              /* 0xab */
    "GET PERFORMANCE",                     /* 0xac */
    "READ DVD STRUCTURE",                  /* 0xad */
    "WRITE AND VERIFY (12)",               /* 0xae */
    "VERIFY (12)",                         /* 0xaf */
    "SEARCH DATA HIGH (12)",               /* 0xb0 */
    "SEARCH DATA EQUAL (12)",              /* 0xb1 */
    "SEARCH DATA LOW (12)",                /* 0xb2 */
    "SET LIMITS (12)",                     /* 0xb3 */
    "READ ELEMENT STATUS ATTACHED",        /* 0xb4 */
    "REQUEST VOLUME ELEMENT ADDRESS",      /* 0xb5 */
    "SET STREAMING",                       /* 0xb6 */
    "READ DEFECT DATA (12)",               /* 0xb7 */
    "READ ELEMENT STATUS",                 /* 0xb8 */
    "READ CD MSF",                         /* 0xb9 */
    "SCAN",                                /* 0xba */
    "SET CD SPEED",                        /* 0xbb */
    "SPARE (IN)",                          /* 0xbc */
    "MECHANISM STATUS",                    /* 0xbd */
    "READ CD",                             /* 0xbe */
    "SEND DVD STRUCTURE",                  /* 0xbf */
    "",                                    /* 0xc0 */
    "",                                    /* 0xc1 */
    "",                                    /* 0xc2 */
    "",                                    /* 0xc3 */
    "",                                    /* 0xc4 */
    "",                                    /* 0xc5 */
    "",                                    /* 0xc6 */
    "",                                    /* 0xc7 */
    "",                                    /* 0xc8 */
    "",                                    /* 0xc9 */
    "",                                    /* 0xca */
    "",                                    /* 0xcb */
    "",                                    /* 0xcc */
    "",                                    /* 0xcd */
    "",                                    /* 0xce */
    "",                                    /* 0xcf */
    "",                                    /* 0xd0 */
    "",                                    /* 0xd1 */
    "",                                    /* 0xd2 */
    "",                                    /* 0xd3 */
    "",                                    /* 0xd4 */
    "",                                    /* 0xd5 */
    "",                                    /* 0xd6 */
    "",                                    /* 0xd7 */
    "",                                    /* 0xd8 */
    "",                                    /* 0xd9 */
    "",                                    /* 0xda */
    "",                                    /* 0xdb */
    "",                                    /* 0xdc */
    "",                                    /* 0xdd */
    "",                                    /* 0xde */
    "",                                    /* 0xdf */
    "",                                    /* 0xe0 */
    "",                                    /* 0xe1 */
    "",                                    /* 0xe2 */
    "",                                    /* 0xe3 */
    "",                                    /* 0xe4 */
    "",                                    /* 0xe5 */
    "",                                    /* 0xe6 */
    "",                                    /* 0xe7 */
    "",                                    /* 0xe8 */
    "",                                    /* 0xe9 */
    "",                                    /* 0xea */
    "",                                    /* 0xeb */
    "",                                    /* 0xec */
    "",                                    /* 0xed */
    "",                                    /* 0xee */
    "",                                    /* 0xef */
    "",                                    /* 0xf0 */
    "",                                    /* 0xf1 */
    "",                                    /* 0xf2 */
    "",                                    /* 0xf3 */
    "",                                    /* 0xf4 */
    "",                                    /* 0xf5 */
    "",                                    /* 0xf6 */
    "",                                    /* 0xf7 */
    "",                                    /* 0xf8 */
    "",                                    /* 0xf9 */
    "",                                    /* 0xfa */
    "",                                    /* 0xfb */
    "",                                    /* 0xfc */
    "",                                    /* 0xfd */
    "",                                    /* 0xfe */
    ""                                     /* 0xff */
};

static const char * const g_apszSCSISenseNames[] =
{
    "NO SENSE",
    "RECOVERED ERROR",
    "NOT READY",
    "MEDIUM ERROR",
    "HARDWARE ERROR",
    "ILLEGAL REQUEST",
    "UNIT ATTENTION",
    "DATA PROTECT",
    "BLANK CHECK",
    "VENDOR-SPECIFIC",
    "COPY ABORTED",
    "ABORTED COMMAND",
    "(obsolete)",
    "VOLUME OVERFLOW",
    "MISCOMPARE",
    "(reserved)"
};

static struct
{
    uint8_t uStatus;
    const char * const pszStatusText;
} g_aSCSIStatusText[]
=
{
    { 0x00, "GOOD" },
    { 0x02, "CHECK CONDITION" },
    { 0x04, "CONDITION MET" },
    { 0x08, "BUSY" },
    { 0x10, "INTERMEDIATE"},
    { 0x14, "CONDITION MET" },
    { 0x18, "RESERVATION CONFLICT" },
    { 0x22, "COMMAND TERMINATED" },
    { 0x28, "TASK SET FULL" },
    { 0x30, "ACA ACTIVE" },
    { 0x40, "TASK ABORTED" },
};

/**
 * SCSI Sense text
 */
static struct
{
    uint8_t uASC;
    uint8_t uASCQ;
    const char * const pszSenseText;
} g_aSCSISenseText[]
=
{
    { 0x67, 0x02, "A ADD LOGICAL UNIT FAILED" },
    { 0x13, 0x00, "ADDRESS MARK NOT FOUND FOR DATA FIELD" },
    { 0x12, 0x00, "ADDRESS MARK NOT FOUND FOR ID FIELD" },
    { 0x27, 0x03, "ASSOCIATED WRITE PROTECT" },
    { 0x67, 0x06, "ATTACHMENT OF LOGICAL UNIT FAILED" },
    { 0x00, 0x11, "AUDIO PLAY OPERATION IN PROGRESS" },
    { 0x00, 0x12, "AUDIO PLAY OPERATION PAUSED" },
    { 0x00, 0x14, "AUDIO PLAY OPERATION STOPPED DUE TO ERROR" },
    { 0x00, 0x13, "AUDIO PLAY OPERATION SUCCESSFULLY COMPLETED" },
    { 0x66, 0x00, "AUTOMATIC DOCUMENT FEEDER COVER UP" },
    { 0x66, 0x01, "AUTOMATIC DOCUMENT FEEDER LIFT UP" },
    { 0x00, 0x04, "BEGINNING-OF-PARTITION/MEDIUM DETECTED" },
    { 0x0C, 0x06, "BLOCK NOT COMPRESSIBLE" },
    { 0x14, 0x04, "BLOCK SEQUENCE ERROR" },
    { 0x29, 0x03, "BUS DEVICE RESET FUNCTION OCCURRED" },
    { 0x11, 0x0E, "CANNOT DECOMPRESS USING DECLARED ALGORITHM" },
    { 0x30, 0x06, "CANNOT FORMAT MEDIUM - INCOMPATIBLE MEDIUM" },
    { 0x30, 0x02, "CANNOT READ MEDIUM - INCOMPATIBLE FORMAT" },
    { 0x30, 0x01, "CANNOT READ MEDIUM - UNKNOWN FORMAT" },
    { 0x30, 0x08, "CANNOT WRITE - APPLICATION CODE MISMATCH" },
    { 0x30, 0x05, "CANNOT WRITE MEDIUM - INCOMPATIBLE FORMAT" },
    { 0x30, 0x04, "CANNOT WRITE MEDIUM - UNKNOWN FORMAT" },
    { 0x52, 0x00, "CARTRIDGE FAULT" },
    { 0x73, 0x00, "CD CONTROL ERROR" },
    { 0x3F, 0x02, "CHANGED OPERATING DEFINITION" },
    { 0x11, 0x06, "CIRC UNRECOVERED ERROR" },
    { 0x30, 0x03, "CLEANING CARTRIDGE INSTALLED" },
    { 0x30, 0x07, "CLEANING FAILURE" },
    { 0x00, 0x17, "CLEANING REQUESTED" },
    { 0x4A, 0x00, "COMMAND PHASE ERROR" },
    { 0x2C, 0x00, "COMMAND SEQUENCE ERROR" },
    { 0x6E, 0x00, "COMMAND TO LOGICAL UNIT FAILED" },
    { 0x2F, 0x00, "COMMANDS CLEARED BY ANOTHER INITIATOR" },
    { 0x0C, 0x04, "COMPRESSION CHECK MISCOMPARE ERROR" },
    { 0x67, 0x00, "CONFIGURATION FAILURE" },
    { 0x67, 0x01, "CONFIGURATION OF INCAPABLE LOGICAL UNITS FAILED" },
    { 0x2B, 0x00, "COPY CANNOT EXECUTE SINCE HOST CANNOT DISCONNECT" },
    { 0x67, 0x07, "CREATION OF LOGICAL UNIT FAILED" },
    { 0x2C, 0x04, "CURRENT PROGRAM AREA IS EMPTY" },
    { 0x2C, 0x03, "CURRENT PROGRAM AREA IS NOT EMPTY" },
    { 0x30, 0x09, "CURRENT SESSION NOT FIXATED FOR APPEND" },
    { 0x0C, 0x05, "DATA EXPANSION OCCURRED DURING COMPRESSION" },
    { 0x69, 0x00, "DATA LOSS ON LOGICAL UNIT" },
    { 0x41, 0x00, "DATA PATH FAILURE (SHOULD USE 40 NN)" },
    { 0x4B, 0x00, "DATA PHASE ERROR" },
    { 0x11, 0x07, "DATA RE-SYNCHRONIZATION ERROR" },
    { 0x16, 0x03, "DATA SYNC ERROR - DATA AUTO-REALLOCATED" },
    { 0x16, 0x01, "DATA SYNC ERROR - DATA REWRITTEN" },
    { 0x16, 0x04, "DATA SYNC ERROR - RECOMMEND REASSIGNMENT" },
    { 0x16, 0x02, "DATA SYNC ERROR - RECOMMEND REWRITE" },
    { 0x16, 0x00, "DATA SYNCHRONIZATION MARK ERROR" },
    { 0x11, 0x0D, "DE-COMPRESSION CRC ERROR" },
    { 0x71, 0x00, "DECOMPRESSION EXCEPTION LONG ALGORITHM ID" },
    { 0x70, 0xFF, "DECOMPRESSION EXCEPTION SHORT ALGORITHM ID OF NN" },
    { 0x19, 0x00, "DEFECT LIST ERROR" },
    { 0x19, 0x03, "DEFECT LIST ERROR IN GROWN LIST" },
    { 0x19, 0x02, "DEFECT LIST ERROR IN PRIMARY LIST" },
    { 0x19, 0x01, "DEFECT LIST NOT AVAILABLE" },
    { 0x1C, 0x00, "DEFECT LIST NOT FOUND" },
    { 0x32, 0x01, "DEFECT LIST UPDATE FAILURE" },
    { 0x29, 0x04, "DEVICE INTERNAL RESET" },
    { 0x40, 0xFF, "DIAGNOSTIC FAILURE ON COMPONENT NN (80H-FFH)" },
    { 0x66, 0x02, "DOCUMENT JAM IN AUTOMATIC DOCUMENT FEEDER" },
    { 0x66, 0x03, "DOCUMENT MISS FEED AUTOMATIC IN DOCUMENT FEEDER" },
    { 0x72, 0x04, "EMPTY OR PARTIALLY WRITTEN RESERVED TRACK" },
    { 0x34, 0x00, "ENCLOSURE FAILURE" },
    { 0x35, 0x00, "ENCLOSURE SERVICES FAILURE" },
    { 0x35, 0x03, "ENCLOSURE SERVICES TRANSFER FAILURE" },
    { 0x35, 0x04, "ENCLOSURE SERVICES TRANSFER REFUSED" },
    { 0x35, 0x02, "ENCLOSURE SERVICES UNAVAILABLE" },
    { 0x3B, 0x0F, "END OF MEDIUM REACHED" },
    { 0x63, 0x00, "END OF USER AREA ENCOUNTERED ON THIS TRACK" },
    { 0x00, 0x05, "END-OF-DATA DETECTED" },
    { 0x14, 0x03, "END-OF-DATA NOT FOUND" },
    { 0x00, 0x02, "END-OF-PARTITION/MEDIUM DETECTED" },
    { 0x51, 0x00, "ERASE FAILURE" },
    { 0x0A, 0x00, "ERROR LOG OVERFLOW" },
    { 0x11, 0x10, "ERROR READING ISRC NUMBER" },
    { 0x11, 0x0F, "ERROR READING UPC/EAN NUMBER" },
    { 0x11, 0x02, "ERROR TOO LONG TO CORRECT" },
    { 0x03, 0x02, "EXCESSIVE WRITE ERRORS" },
    { 0x67, 0x04, "EXCHANGE OF LOGICAL UNIT FAILED" },
    { 0x3B, 0x07, "FAILED TO SENSE BOTTOM-OF-FORM" },
    { 0x3B, 0x06, "FAILED TO SENSE TOP-OF-FORM" },
    { 0x5D, 0x00, "FAILURE PREDICTION THRESHOLD EXCEEDED" },
    { 0x5D, 0xFF, "FAILURE PREDICTION THRESHOLD EXCEEDED (FALSE)" },
    { 0x00, 0x01, "FILEMARK DETECTED" },
    { 0x14, 0x02, "FILEMARK OR SETMARK NOT FOUND" },
    { 0x09, 0x02, "FOCUS SERVO FAILURE" },
    { 0x31, 0x01, "FORMAT COMMAND FAILED" },
    { 0x58, 0x00, "GENERATION DOES NOT EXIST" },
    { 0x1C, 0x02, "GROWN DEFECT LIST NOT FOUND" },
    { 0x27, 0x01, "HARDWARE WRITE PROTECTED" },
    { 0x09, 0x04, "HEAD SELECT FAULT" },
    { 0x00, 0x06, "I/O PROCESS TERMINATED" },
    { 0x10, 0x00, "ID CRC OR ECC ERROR" },
    { 0x5E, 0x03, "IDLE CONDITION ACTIVATED BY COMMAND" },
    { 0x5E, 0x01, "IDLE CONDITION ACTIVATED BY TIMER" },
    { 0x22, 0x00, "ILLEGAL FUNCTION (USE 20 00, 24 00, OR 26 00)" },
    { 0x64, 0x00, "ILLEGAL MODE FOR THIS TRACK" },
    { 0x28, 0x01, "IMPORT OR EXPORT ELEMENT ACCESSED" },
    { 0x30, 0x00, "INCOMPATIBLE MEDIUM INSTALLED" },
    { 0x11, 0x08, "INCOMPLETE BLOCK READ" },
    { 0x6A, 0x00, "INFORMATIONAL, REFER TO LOG" },
    { 0x48, 0x00, "INITIATOR DETECTED ERROR MESSAGE RECEIVED" },
    { 0x3F, 0x03, "INQUIRY DATA HAS CHANGED" },
    { 0x44, 0x00, "INTERNAL TARGET FAILURE" },
    { 0x3D, 0x00, "INVALID BITS IN IDENTIFY MESSAGE" },
    { 0x2C, 0x02, "INVALID COMBINATION OF WINDOWS SPECIFIED" },
    { 0x20, 0x00, "INVALID COMMAND OPERATION CODE" },
    { 0x21, 0x01, "INVALID ELEMENT ADDRESS" },
    { 0x24, 0x00, "INVALID FIELD IN CDB" },
    { 0x26, 0x00, "INVALID FIELD IN PARAMETER LIST" },
    { 0x49, 0x00, "INVALID MESSAGE ERROR" },
    { 0x64, 0x01, "INVALID PACKET SIZE" },
    { 0x26, 0x04, "INVALID RELEASE OF ACTIVE PERSISTENT RESERVATION" },
    { 0x11, 0x05, "L-EC UNCORRECTABLE ERROR" },
    { 0x60, 0x00, "LAMP FAILURE" },
    { 0x5B, 0x02, "LOG COUNTER AT MAXIMUM" },
    { 0x5B, 0x00, "LOG EXCEPTION" },
    { 0x5B, 0x03, "LOG LIST CODES EXHAUSTED" },
    { 0x2A, 0x02, "LOG PARAMETERS CHANGED" },
    { 0x21, 0x00, "LOGICAL BLOCK ADDRESS OUT OF RANGE" },
    { 0x08, 0x03, "LOGICAL UNIT COMMUNICATION CRC ERROR (ULTRA-DMA/32)" },
    { 0x08, 0x00, "LOGICAL UNIT COMMUNICATION FAILURE" },
    { 0x08, 0x02, "LOGICAL UNIT COMMUNICATION PARITY ERROR" },
    { 0x08, 0x01, "LOGICAL UNIT COMMUNICATION TIME-OUT" },
    { 0x05, 0x00, "LOGICAL UNIT DOES NOT RESPOND TO SELECTION" },
    { 0x4C, 0x00, "LOGICAL UNIT FAILED SELF-CONFIGURATION" },
    { 0x3E, 0x01, "LOGICAL UNIT FAILURE" },
    { 0x3E, 0x00, "LOGICAL UNIT HAS NOT SELF-CONFIGURED YET" },
    { 0x04, 0x01, "LOGICAL UNIT IS IN PROCESS OF BECOMING READY" },
    { 0x68, 0x00, "LOGICAL UNIT NOT CONFIGURED" },
    { 0x04, 0x00, "LOGICAL UNIT NOT READY, CAUSE NOT REPORTABLE" },
    { 0x04, 0x04, "LOGICAL UNIT NOT READY, FORMAT IN PROGRESS" },
    { 0x04, 0x02, "LOGICAL UNIT NOT READY, INITIALIZING CMD. REQUIRED" },
    { 0x04, 0x08, "LOGICAL UNIT NOT READY, LONG WRITE IN PROGRESS" },
    { 0x04, 0x03, "LOGICAL UNIT NOT READY, MANUAL INTERVENTION REQUIRED" },
    { 0x04, 0x07, "LOGICAL UNIT NOT READY, OPERATION IN PROGRESS" },
    { 0x04, 0x05, "LOGICAL UNIT NOT READY, REBUILD IN PROGRESS" },
    { 0x04, 0x06, "LOGICAL UNIT NOT READY, RECALCULATION IN PROGRESS" },
    { 0x25, 0x00, "LOGICAL UNIT NOT SUPPORTED" },
    { 0x27, 0x02, "LOGICAL UNIT SOFTWARE WRITE PROTECTED" },
    { 0x5E, 0x00, "LOW POWER CONDITION ON" },
    { 0x15, 0x01, "MECHANICAL POSITIONING ERROR" },
    { 0x53, 0x00, "MEDIA LOAD OR EJECT FAILED" },
    { 0x3B, 0x0D, "MEDIUM DESTINATION ELEMENT FULL" },
    { 0x31, 0x00, "MEDIUM FORMAT CORRUPTED" },
    { 0x3B, 0x13, "MEDIUM MAGAZINE INSERTED" },
    { 0x3B, 0x14, "MEDIUM MAGAZINE LOCKED" },
    { 0x3B, 0x11, "MEDIUM MAGAZINE NOT ACCESSIBLE" },
    { 0x3B, 0x12, "MEDIUM MAGAZINE REMOVED" },
    { 0x3B, 0x15, "MEDIUM MAGAZINE UNLOCKED" },
    { 0x3A, 0x00, "MEDIUM NOT PRESENT" },
    { 0x3A, 0x01, "MEDIUM NOT PRESENT - TRAY CLOSED" },
    { 0x3A, 0x02, "MEDIUM NOT PRESENT - TRAY OPEN" },
    { 0x53, 0x02, "MEDIUM REMOVAL PREVENTED" },
    { 0x3B, 0x0E, "MEDIUM SOURCE ELEMENT EMPTY" },
    { 0x43, 0x00, "MESSAGE ERROR" },
    { 0x3F, 0x01, "MICROCODE HAS BEEN CHANGED" },
    { 0x1D, 0x00, "MISCOMPARE DURING VERIFY OPERATION" },
    { 0x11, 0x0A, "MISCORRECTED ERROR" },
    { 0x2A, 0x01, "MODE PARAMETERS CHANGED" },
    { 0x67, 0x03, "MODIFICATION OF LOGICAL UNIT FAILED" },
    { 0x69, 0x01, "MULTIPLE LOGICAL UNIT FAILURES" },
    { 0x07, 0x00, "MULTIPLE PERIPHERAL DEVICES SELECTED" },
    { 0x11, 0x03, "MULTIPLE READ ERRORS" },
    { 0x00, 0x00, "NO ADDITIONAL SENSE INFORMATION" },
    { 0x00, 0x15, "NO CURRENT AUDIO STATUS TO RETURN" },
    { 0x32, 0x00, "NO DEFECT SPARE LOCATION AVAILABLE" },
    { 0x11, 0x09, "NO GAP FOUND" },
    { 0x01, 0x00, "NO INDEX/SECTOR SIGNAL" },
    { 0x06, 0x00, "NO REFERENCE POSITION FOUND" },
    { 0x02, 0x00, "NO SEEK COMPLETE" },
    { 0x03, 0x01, "NO WRITE CURRENT" },
    { 0x28, 0x00, "NOT READY TO READY CHANGE, MEDIUM MAY HAVE CHANGED" },
    { 0x00, 0x16, "OPERATION IN PROGRESS" },
    { 0x5A, 0x01, "OPERATOR MEDIUM REMOVAL REQUEST" },
    { 0x5A, 0x00, "OPERATOR REQUEST OR STATE CHANGE INPUT" },
    { 0x5A, 0x03, "OPERATOR SELECTED WRITE PERMIT" },
    { 0x5A, 0x02, "OPERATOR SELECTED WRITE PROTECT" },
    { 0x61, 0x02, "OUT OF FOCUS" },
    { 0x4E, 0x00, "OVERLAPPED COMMANDS ATTEMPTED" },
    { 0x2D, 0x00, "OVERWRITE ERROR ON UPDATE IN PLACE" },
    { 0x63, 0x01, "PACKET DOES NOT FIT IN AVAILABLE SPACE" },
    { 0x3B, 0x05, "PAPER JAM" },
    { 0x1A, 0x00, "PARAMETER LIST LENGTH ERROR" },
    { 0x26, 0x01, "PARAMETER NOT SUPPORTED" },
    { 0x26, 0x02, "PARAMETER VALUE INVALID" },
    { 0x2A, 0x00, "PARAMETERS CHANGED" },
    { 0x69, 0x02, "PARITY/DATA MISMATCH" },
    { 0x1F, 0x00, "PARTIAL DEFECT LIST TRANSFER" },
    { 0x03, 0x00, "PERIPHERAL DEVICE WRITE FAULT" },
    { 0x27, 0x05, "PERMANENT WRITE PROTECT" },
    { 0x27, 0x04, "PERSISTENT WRITE PROTECT" },
    { 0x50, 0x02, "POSITION ERROR RELATED TO TIMING" },
    { 0x3B, 0x0C, "POSITION PAST BEGINNING OF MEDIUM" },
    { 0x3B, 0x0B, "POSITION PAST END OF MEDIUM" },
    { 0x15, 0x02, "POSITIONING ERROR DETECTED BY READ OF MEDIUM" },
    { 0x73, 0x01, "POWER CALIBRATION AREA ALMOST FULL" },
    { 0x73, 0x03, "POWER CALIBRATION AREA ERROR" },
    { 0x73, 0x02, "POWER CALIBRATION AREA IS FULL" },
    { 0x29, 0x01, "POWER ON OCCURRED" },
    { 0x29, 0x00, "POWER ON, RESET, OR BUS DEVICE RESET OCCURRED" },
    { 0x42, 0x00, "POWER-ON OR SELF-TEST FAILURE (SHOULD USE 40 NN)" },
    { 0x1C, 0x01, "PRIMARY DEFECT LIST NOT FOUND" },
    { 0x73, 0x05, "PROGRAM MEMORY AREA IS FULL" },
    { 0x73, 0x04, "PROGRAM MEMORY AREA UPDATE FAILURE" },
    { 0x40, 0x00, "RAM FAILURE (SHOULD USE 40 NN)" },
    { 0x15, 0x00, "RANDOM POSITIONING ERROR" },
    { 0x11, 0x11, "READ ERROR - LOSS OF STREAMING" },
    { 0x3B, 0x0A, "READ PAST BEGINNING OF MEDIUM" },
    { 0x3B, 0x09, "READ PAST END OF MEDIUM" },
    { 0x11, 0x01, "READ RETRIES EXHAUSTED" },
    { 0x6C, 0x00, "REBUILD FAILURE OCCURRED" },
    { 0x6D, 0x00, "RECALCULATE FAILURE OCCURRED" },
    { 0x14, 0x01, "RECORD NOT FOUND" },
    { 0x14, 0x06, "RECORD NOT FOUND - DATA AUTO-REALLOCATED" },
    { 0x14, 0x05, "RECORD NOT FOUND - RECOMMEND REASSIGNMENT" },
    { 0x14, 0x00, "RECORDED ENTITY NOT FOUND" },
    { 0x18, 0x02, "RECOVERED DATA - DATA AUTO-REALLOCATED" },
    { 0x18, 0x05, "RECOVERED DATA - RECOMMEND REASSIGNMENT" },
    { 0x18, 0x06, "RECOVERED DATA - RECOMMEND REWRITE" },
    { 0x17, 0x05, "RECOVERED DATA USING PREVIOUS SECTOR ID" },
    { 0x18, 0x03, "RECOVERED DATA WITH CIRC" },
    { 0x18, 0x07, "RECOVERED DATA WITH ECC - DATA REWRITTEN" },
    { 0x18, 0x01, "RECOVERED DATA WITH ERROR CORR. & RETRIES APPLIED" },
    { 0x18, 0x00, "RECOVERED DATA WITH ERROR CORRECTION APPLIED" },
    { 0x18, 0x04, "RECOVERED DATA WITH L-EC" },
    { 0x17, 0x03, "RECOVERED DATA WITH NEGATIVE HEAD OFFSET" },
    { 0x17, 0x00, "RECOVERED DATA WITH NO ERROR CORRECTION APPLIED" },
    { 0x17, 0x02, "RECOVERED DATA WITH POSITIVE HEAD OFFSET" },
    { 0x17, 0x01, "RECOVERED DATA WITH RETRIES" },
    { 0x17, 0x04, "RECOVERED DATA WITH RETRIES AND/OR CIRC APPLIED" },
    { 0x17, 0x06, "RECOVERED DATA WITHOUT ECC - DATA AUTO-REALLOCATED" },
    { 0x17, 0x09, "RECOVERED DATA WITHOUT ECC - DATA REWRITTEN" },
    { 0x17, 0x07, "RECOVERED DATA WITHOUT ECC - RECOMMEND REASSIGNMENT" },
    { 0x17, 0x08, "RECOVERED DATA WITHOUT ECC - RECOMMEND REWRITE" },
    { 0x1E, 0x00, "RECOVERED ID WITH ECC CORRECTION" },
    { 0x6B, 0x01, "REDUNDANCY LEVEL GOT BETTER" },
    { 0x6B, 0x02, "REDUNDANCY LEVEL GOT WORSE" },
    { 0x67, 0x05, "REMOVE OF LOGICAL UNIT FAILED" },
    { 0x3B, 0x08, "REPOSITION ERROR" },
    { 0x2A, 0x03, "RESERVATIONS PREEMPTED" },
    { 0x36, 0x00, "RIBBON, INK, OR TONER FAILURE" },
    { 0x37, 0x00, "ROUNDED PARAMETER" },
    { 0x5C, 0x00, "RPL STATUS CHANGE" },
    { 0x39, 0x00, "SAVING PARAMETERS NOT SUPPORTED" },
    { 0x62, 0x00, "SCAN HEAD POSITIONING ERROR" },
    { 0x29, 0x02, "SCSI BUS RESET OCCURRED" },
    { 0x47, 0x00, "SCSI PARITY ERROR" },
    { 0x54, 0x00, "SCSI TO HOST SYSTEM INTERFACE FAILURE" },
    { 0x45, 0x00, "SELECT OR RESELECT FAILURE" },
    { 0x3B, 0x00, "SEQUENTIAL POSITIONING ERROR" },
    { 0x72, 0x00, "SESSION FIXATION ERROR" },
    { 0x72, 0x03, "SESSION FIXATION ERROR - INCOMPLETE TRACK IN SESSION" },
    { 0x72, 0x01, "SESSION FIXATION ERROR WRITING LEAD-IN" },
    { 0x72, 0x02, "SESSION FIXATION ERROR WRITING LEAD-OUT" },
    { 0x00, 0x03, "SETMARK DETECTED" },
    { 0x3B, 0x04, "SLEW FAILURE" },
    { 0x09, 0x03, "SPINDLE SERVO FAILURE" },
    { 0x5C, 0x02, "SPINDLES NOT SYNCHRONIZED" },
    { 0x5C, 0x01, "SPINDLES SYNCHRONIZED" },
    { 0x5E, 0x04, "STANDBY CONDITION ACTIVATED BY COMMAND" },
    { 0x5E, 0x02, "STANDBY CONDITION ACTIVATED BY TIMER" },
    { 0x6B, 0x00, "STATE CHANGE HAS OCCURRED" },
    { 0x1B, 0x00, "SYNCHRONOUS DATA TRANSFER ERROR" },
    { 0x55, 0x01, "SYSTEM BUFFER FULL" },
    { 0x55, 0x00, "SYSTEM RESOURCE FAILURE" },
    { 0x4D, 0xFF, "TAGGED OVERLAPPED COMMANDS (NN = QUEUE TAG)" },
    { 0x33, 0x00, "TAPE LENGTH ERROR" },
    { 0x3B, 0x03, "TAPE OR ELECTRONIC VERTICAL FORMS UNIT NOT READY" },
    { 0x3B, 0x01, "TAPE POSITION ERROR AT BEGINNING-OF-MEDIUM" },
    { 0x3B, 0x02, "TAPE POSITION ERROR AT END-OF-MEDIUM" },
    { 0x3F, 0x00, "TARGET OPERATING CONDITIONS HAVE CHANGED" },
    { 0x5B, 0x01, "THRESHOLD CONDITION MET" },
    { 0x26, 0x03, "THRESHOLD PARAMETERS NOT SUPPORTED" },
    { 0x3E, 0x02, "TIMEOUT ON LOGICAL UNIT" },
    { 0x2C, 0x01, "TOO MANY WINDOWS SPECIFIED" },
    { 0x09, 0x00, "TRACK FOLLOWING ERROR" },
    { 0x09, 0x01, "TRACKING SERVO FAILURE" },
    { 0x61, 0x01, "UNABLE TO ACQUIRE VIDEO" },
    { 0x57, 0x00, "UNABLE TO RECOVER TABLE-OF-CONTENTS" },
    { 0x53, 0x01, "UNLOAD TAPE FAILURE" },
    { 0x11, 0x00, "UNRECOVERED READ ERROR" },
    { 0x11, 0x04, "UNRECOVERED READ ERROR - AUTO REALLOCATE FAILED" },
    { 0x11, 0x0B, "UNRECOVERED READ ERROR - RECOMMEND REASSIGNMENT" },
    { 0x11, 0x0C, "UNRECOVERED READ ERROR - RECOMMEND REWRITE THE DATA" },
    { 0x46, 0x00, "UNSUCCESSFUL SOFT RESET" },
    { 0x35, 0x01, "UNSUPPORTED ENCLOSURE FUNCTION" },
    { 0x59, 0x00, "UPDATED BLOCK READ" },
    { 0x61, 0x00, "VIDEO ACQUISITION ERROR" },
    { 0x65, 0x00, "VOLTAGE FAULT" },
    { 0x0B, 0x00, "WARNING" },
    { 0x0B, 0x02, "WARNING - ENCLOSURE DEGRADED" },
    { 0x0B, 0x01, "WARNING - SPECIFIED TEMPERATURE EXCEEDED" },
    { 0x50, 0x00, "WRITE APPEND ERROR" },
    { 0x50, 0x01, "WRITE APPEND POSITION ERROR" },
    { 0x0C, 0x00, "WRITE ERROR" },
    { 0x0C, 0x02, "WRITE ERROR - AUTO REALLOCATION FAILED" },
    { 0x0C, 0x09, "WRITE ERROR - LOSS OF STREAMING" },
    { 0x0C, 0x0A, "WRITE ERROR - PADDING BLOCKS ADDED" },
    { 0x0C, 0x03, "WRITE ERROR - RECOMMEND REASSIGNMENT" },
    { 0x0C, 0x01, "WRITE ERROR - RECOVERED WITH AUTO REALLOCATION" },
    { 0x0C, 0x08, "WRITE ERROR - RECOVERY FAILED" },
    { 0x0C, 0x07, "WRITE ERROR - RECOVERY NEEDED" },
    { 0x27, 0x00, "WRITE PROTECTED" },
};

#endif /* LOG_ENABLED || RT_STRICT */

#ifdef LOG_ENABLED

/**
 * Return the plain text of an ATA command for debugging purposes.
 * Don't allocate the string as we use this function in Log() statements.
 */
const char * ATACmdText(uint8_t uCmd)
{
    AssertCompile(RT_ELEMENTS(g_apszATACmdNames) == (1 << (8*sizeof(uCmd))));
    return g_apszATACmdNames[uCmd];
}

#endif

#if defined(LOG_ENABLED) || defined(RT_STRICT)

/**
 * Return the plain text of a SCSI command for debugging purposes.
 * Don't allocate the string as we use this function in Log() statements.
 */
const char * SCSICmdText(uint8_t uCmd)
{
    AssertCompile(RT_ELEMENTS(g_apszSCSICmdNames) == (1 << (8*sizeof(uCmd))));
    return g_apszSCSICmdNames[uCmd];
}

/**
 * Return the plain text of a SCSI sense code.
 * Don't allocate the string as we use this function in Log() statements.
 */
const char * SCSISenseText(uint8_t uSense)
{
    if (uSense < RT_ELEMENTS(g_apszSCSISenseNames))
        return g_apszSCSISenseNames[uSense];

    return "(SCSI sense out of range)";
}

const char * SCSIStatusText(uint8_t uStatus)
{
    unsigned iIdx;

    /* Linear search. Doesn't hurt as we don't call this function very frequently */
    for (iIdx = 0; iIdx < RT_ELEMENTS(g_aSCSISenseText); iIdx++)
    {
        if (g_aSCSIStatusText[iIdx].uStatus  == uStatus)
            return g_aSCSIStatusText[iIdx].pszStatusText;
    }
    return "(Unknown extended status code)";
}

/**
 * Return the plain text of an extended SCSI sense key.
 * Don't allocate the string as we use this function in Log() statements.
 */
const char * SCSISenseExtText(uint8_t uASC, uint8_t uASCQ)
{
    unsigned iIdx;

    /* Linear search. Doesn't hurt as we don't call this function very frequently */
    for (iIdx = 0; iIdx < RT_ELEMENTS(g_aSCSISenseText); iIdx++)
    {
        if (   g_aSCSISenseText[iIdx].uASC  == uASC
            && (   g_aSCSISenseText[iIdx].uASCQ == uASCQ
                || g_aSCSISenseText[iIdx].uASCQ == 0xff))
            return g_aSCSISenseText[iIdx].pszSenseText;
    }
    return "(Unknown extended sense code)";
}

/**
 * Log the write parameters mode page into a given buffer.
 */
static int scsiLogWriteParamsModePage(char *pszBuffer, size_t cchBuffer, uint8_t *pbModePage, size_t cbModePage)
{
    RT_NOREF(cbModePage);
    size_t cch = 0;
    const char *pcsz = NULL;

    switch (pbModePage[2] & 0x0f)
    {
        case 0x00: pcsz = "Packet/Incremental"; break;
        case 0x01: pcsz = "Track At Once"; break;
        case 0x02: pcsz = "Session At Once"; break;
        case 0x03: pcsz = "RAW"; break;
        case 0x04: pcsz = "Layer Jump Recording"; break;
        default : pcsz = "Unknown/Reserved Write Type"; break;
    }

    cch = RTStrPrintf(pszBuffer, cchBuffer, "BUFE=%d LS_V=%d TestWrite=%d WriteType=%s\n",
                      pbModePage[2] & RT_BIT(6) ? 1 : 0,
                      pbModePage[2] & RT_BIT(5) ? 1 : 0,
                      pbModePage[2] & RT_BIT(4) ? 1 : 0,
                      pcsz);
    pszBuffer += cch;
    cchBuffer -= cch;
    if (!cchBuffer)
        return VERR_BUFFER_OVERFLOW;

    switch ((pbModePage[3] & 0xc0) >> 6)
    {
        case 0x00: pcsz = "No B0 pointer, no next session"; break;
        case 0x01: pcsz = "B0 pointer=FF:FF:FF, no next session"; break;
        case 0x02: pcsz = "Reserved"; break;
        case 0x03: pcsz = "Next session allowed"; break;
        default: pcsz = "Impossible multi session field value"; break;
    }

    cch = RTStrPrintf(pszBuffer, cchBuffer, "MultiSession=%s FP=%d Copy=%d TrackMode=%d\n",
                      pcsz,
                      pbModePage[3] & RT_BIT(5) ? 1 : 0,
                      pbModePage[3] & RT_BIT(4) ? 1 : 0,
                      pbModePage[3] & 0x0f);
    pszBuffer += cch;
    cchBuffer -= cch;
    if (!cchBuffer)
        return VERR_BUFFER_OVERFLOW;

    switch (pbModePage[4] & 0x0f)
    {
        case  0: pcsz = "Raw data (2352)"; break;
        case  1: pcsz = "Raw data with P and Q Sub-channel (2368)"; break;
        case  2: pcsz = "Raw data with P-W Sub-channel (2448)"; break;
        case  3: pcsz = "Raw data with raw P-W Sub-channel (2448)"; break;
        case  8: pcsz = "Mode 1 (ISO/IEC 10149) (2048)"; break;
        case  9: pcsz = "Mode 2 (ISO/IEC 10149) (2336)"; break;
        case 10: pcsz = "Mode 2 (CD-ROM XA, form 1) (2048)"; break;
        case 11: pcsz = "Mode 2 (CD-ROM XA, form 1) (2056)"; break;
        case 12: pcsz = "Mode 2 (CD-ROM XA, form 2) (2324)"; break;
        case 13: pcsz = "Mode 2 (CD-ROM XA, form 1, form 2 or mixed form) (2332)"; break;
        default: pcsz = "Reserved or vendor specific Data Block Type Code"; break;
    }

    cch = RTStrPrintf(pszBuffer, cchBuffer, "DataBlockType=%d (%s)\n",
                      pbModePage[4] & 0x0f,
                      pcsz);
    pszBuffer += cch;
    cchBuffer -= cch;
    if (!cchBuffer)
        return VERR_BUFFER_OVERFLOW;

    cch = RTStrPrintf(pszBuffer, cchBuffer, "LinkSize=%d\n", pbModePage[5]);
    pszBuffer += cch;
    cchBuffer -= cch;
    if (!cchBuffer)
        return VERR_BUFFER_OVERFLOW;

    cch = RTStrPrintf(pszBuffer, cchBuffer, "HostApplicationCode=%d\n",
                      pbModePage[7] & 0x3f);
    pszBuffer += cch;
    cchBuffer -= cch;
    if (!cchBuffer)
        return VERR_BUFFER_OVERFLOW;

    switch (pbModePage[8])
    {
        case 0x00: pcsz = "CD-DA or CD-ROM or other data discs"; break;
        case 0x10: pcsz = "CD-I Disc"; break;
        case 0x20: pcsz = "CD-ROM XA Disc"; break;
        default: pcsz = "Reserved"; break;
    }

    cch = RTStrPrintf(pszBuffer, cchBuffer, "SessionFormat=%d (%s)\n",
                      pbModePage[8], pcsz);
    pszBuffer += cch;
    cchBuffer -= cch;
    if (!cchBuffer)
        return VERR_BUFFER_OVERFLOW;

    return VINF_SUCCESS;
}

/**
 * Log a mode page in a human readable form.
 *
 * @returns VBox status code.
 * @retval VERR_BUFFER_OVERFLOW if the given buffer is not large enough.
 *         The buffer might contain valid data though.
 * @param  pszBuffer     The buffer to log into.
 * @param  cchBuffer     Size of the buffer in characters.
 * @param  pbModePage    The mode page buffer.
 * @param  cbModePage    Size of the mode page buffer in bytes.
 */
int SCSILogModePage(char *pszBuffer, size_t cchBuffer, uint8_t *pbModePage,
                    size_t cbModePage)
{
    int rc = VINF_SUCCESS;
    uint8_t uModePage;
    const char *pcszModePage = NULL;
    size_t cch = 0;

    uModePage = pbModePage[0] & 0x3f;
    switch (uModePage)
    {
        case 0x05: pcszModePage = "Write Parameters"; break;
        default:
            pcszModePage = "Unknown mode page";
    }

    cch = RTStrPrintf(pszBuffer, cchBuffer, "Byte 0: PS=%d, Page code=%d (%s)\n",
                      pbModePage[0] & 0x80 ? 1 : 0, uModePage, pcszModePage);
    pszBuffer += cch;
    cchBuffer -= cch;
    if (!cchBuffer)
        return VERR_BUFFER_OVERFLOW;

    cch = RTStrPrintf(pszBuffer, cchBuffer, "Byte 1: Page length=%u\n", pbModePage[1]);
    pszBuffer += cch;
    cchBuffer -= cch;
    if (!cchBuffer)
        return VERR_BUFFER_OVERFLOW;

    switch (uModePage)
    {
        case 0x05:
            rc = scsiLogWriteParamsModePage(pszBuffer, cchBuffer, pbModePage, cbModePage);
            break;
        default:
            break;
    }

    return rc;
}

/**
 * Log a cue sheet in a human readable form.
 *
 * @returns VBox status code.
 * @retval VERR_BUFFER_OVERFLOW if the given buffer is not large enough.
 *         The buffer might contain valid data though.
 * @param  pszBuffer     The buffer to log into.
 * @param  cchBuffer     Size of the buffer in characters.
 * @param  pbCueSheet    The cue sheet buffer.
 * @param  cbCueSheet    Size of the cue sheet buffer in bytes.
 */
int SCSILogCueSheet(char *pszBuffer, size_t cchBuffer, uint8_t *pbCueSheet, size_t cbCueSheet)
{
    int rc = VINF_SUCCESS;
    size_t cch = 0;
    size_t cCueSheetEntries = cbCueSheet / 8;

    AssertReturn(cbCueSheet % 8 == 0, VERR_INVALID_PARAMETER);

    for (size_t i = 0; i < cCueSheetEntries; i++)
    {
        cch = RTStrPrintf(pszBuffer, cchBuffer,
                          "CTL/ADR=%#x TNO=%#x INDEX=%#x DATA=%#x SCMS=%#x TIME=%u:%u:%u\n",
                          pbCueSheet[0], pbCueSheet[1], pbCueSheet[2], pbCueSheet[3],
                          pbCueSheet[4], pbCueSheet[5], pbCueSheet[6], pbCueSheet[7]);
        pszBuffer += cch;
        cchBuffer -= cch;
        if (!cchBuffer)
        {
            rc = VERR_BUFFER_OVERFLOW;
            break;
        }

        pbCueSheet += 8;
        cbCueSheet -= 8;
    }

    return rc;
}

#endif /* LOG_ENABLED || RT_STRICT */
