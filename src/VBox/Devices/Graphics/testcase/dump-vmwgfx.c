/* $Id: dump-vmwgfx.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * dump-vmwgfx.c - Dumps parameters and capabilities of vmwgfx.ko.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define DRM_IOCTL_BASE              'd'
#define DRM_COMMAND_BASE            0x40
#define DRM_VMW_GET_PARAM           0
#define DRM_VMW_GET_3D_CAP          13
#define DRM_IOCTL_VMW_GET_PARAM     _IOWR(DRM_IOCTL_BASE, DRM_COMMAND_BASE + DRM_VMW_GET_PARAM, struct drm_vmw_getparam_arg)
#define DRM_IOCTL_VMW_GET_3D_CAP    _IOW(DRM_IOCTL_BASE, DRM_COMMAND_BASE + DRM_VMW_GET_3D_CAP, struct drm_vmw_get_3d_cap_arg)

#define SVGA3DCAPS_RECORD_DEVCAPS   0x100

#define DRM_VMW_PARAM_NUM_STREAMS       0
#define DRM_VMW_PARAM_FREE_STREAMS      1
#define DRM_VMW_PARAM_3D                2
#define DRM_VMW_PARAM_HW_CAPS           3
#define DRM_VMW_PARAM_FIFO_CAPS         4
#define DRM_VMW_PARAM_MAX_FB_SIZE       5
#define DRM_VMW_PARAM_FIFO_HW_VERSION   6
#define DRM_VMW_PARAM_MAX_SURF_MEMORY   7
#define DRM_VMW_PARAM_3D_CAP_SIZE       8
#define DRM_VMW_PARAM_MAX_MOB_MEMORY    9
#define DRM_VMW_PARAM_MAX_MOB_SIZE     10


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
struct drm_vmw_get_3d_cap_arg
{
    uint64_t buffer;
    uint32_t max_size;
    uint32_t pad64;
};


struct SVGA3dCapsRecordHeader
{
    uint32_t length;
    uint32_t type;
};

struct SVGA3dCapsRecord
{
    /* Skipped if DRM_VMW_PARAM_MAX_MOB_MEMORY is read. */
    struct SVGA3dCapsRecordHeader header;
    uint32_t data[1];
};

struct drm_vmw_getparam_arg
{
    uint64_t value;
    uint32_t param;
    uint32_t pad64;
};


typedef struct FLAGDESC
{
    uint32_t fMask;
    const char *pszName;
} FLAGDESC;
typedef FLAGDESC const *PCFLAGDESC;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The size of the 3D capabilities. */
static uint32_t     g_cb3dCaps;
/** Set if the driver will return the new 3D capability format. */
static int          g_fNew3dCapFormat = 0;
/** The SVGA_CAP_XXX mask for the card. */
static uint64_t     g_fHwCaps = 0;

/** Names for the vmsvga 3d capabilities, prefixed with format type hint char. (Copied from DevSVGA.cpp.) */
static const char * const g_apszVmSvgaDevCapNames[] =
{
    "x3D",                           /* = 0 */
    "xMAX_LIGHTS",
    "xMAX_TEXTURES",
    "xMAX_CLIP_PLANES",
    "xVERTEX_SHADER_VERSION",
    "xVERTEX_SHADER",
    "xFRAGMENT_SHADER_VERSION",
    "xFRAGMENT_SHADER",
    "xMAX_RENDER_TARGETS",
    "xS23E8_TEXTURES",
    "xS10E5_TEXTURES",
    "xMAX_FIXED_VERTEXBLEND",
    "xD16_BUFFER_FORMAT",
    "xD24S8_BUFFER_FORMAT",
    "xD24X8_BUFFER_FORMAT",
    "xQUERY_TYPES",
    "xTEXTURE_GRADIENT_SAMPLING",
    "rMAX_POINT_SIZE",
    "xMAX_SHADER_TEXTURES",
    "xMAX_TEXTURE_WIDTH",
    "xMAX_TEXTURE_HEIGHT",
    "xMAX_VOLUME_EXTENT",
    "xMAX_TEXTURE_REPEAT",
    "xMAX_TEXTURE_ASPECT_RATIO",
    "xMAX_TEXTURE_ANISOTROPY",
    "xMAX_PRIMITIVE_COUNT",
    "xMAX_VERTEX_INDEX",
    "xMAX_VERTEX_SHADER_INSTRUCTIONS",
    "xMAX_FRAGMENT_SHADER_INSTRUCTIONS",
    "xMAX_VERTEX_SHADER_TEMPS",
    "xMAX_FRAGMENT_SHADER_TEMPS",
    "xTEXTURE_OPS",
    "xSURFACEFMT_X8R8G8B8",
    "xSURFACEFMT_A8R8G8B8",
    "xSURFACEFMT_A2R10G10B10",
    "xSURFACEFMT_X1R5G5B5",
    "xSURFACEFMT_A1R5G5B5",
    "xSURFACEFMT_A4R4G4B4",
    "xSURFACEFMT_R5G6B5",
    "xSURFACEFMT_LUMINANCE16",
    "xSURFACEFMT_LUMINANCE8_ALPHA8",
    "xSURFACEFMT_ALPHA8",
    "xSURFACEFMT_LUMINANCE8",
    "xSURFACEFMT_Z_D16",
    "xSURFACEFMT_Z_D24S8",
    "xSURFACEFMT_Z_D24X8",
    "xSURFACEFMT_DXT1",
    "xSURFACEFMT_DXT2",
    "xSURFACEFMT_DXT3",
    "xSURFACEFMT_DXT4",
    "xSURFACEFMT_DXT5",
    "xSURFACEFMT_BUMPX8L8V8U8",
    "xSURFACEFMT_A2W10V10U10",
    "xSURFACEFMT_BUMPU8V8",
    "xSURFACEFMT_Q8W8V8U8",
    "xSURFACEFMT_CxV8U8",
    "xSURFACEFMT_R_S10E5",
    "xSURFACEFMT_R_S23E8",
    "xSURFACEFMT_RG_S10E5",
    "xSURFACEFMT_RG_S23E8",
    "xSURFACEFMT_ARGB_S10E5",
    "xSURFACEFMT_ARGB_S23E8",
    "xMISSING62",
    "xMAX_VERTEX_SHADER_TEXTURES",
    "xMAX_SIMULTANEOUS_RENDER_TARGETS",
    "xSURFACEFMT_V16U16",
    "xSURFACEFMT_G16R16",
    "xSURFACEFMT_A16B16G16R16",
    "xSURFACEFMT_UYVY",
    "xSURFACEFMT_YUY2",
    "xMULTISAMPLE_NONMASKABLESAMPLES",
    "xMULTISAMPLE_MASKABLESAMPLES",
    "xALPHATOCOVERAGE",
    "xSUPERSAMPLE",
    "xAUTOGENMIPMAPS",
    "xSURFACEFMT_NV12",
    "xSURFACEFMT_AYUV",
    "xMAX_CONTEXT_IDS",
    "xMAX_SURFACE_IDS",
    "xSURFACEFMT_Z_DF16",
    "xSURFACEFMT_Z_DF24",
    "xSURFACEFMT_Z_D24S8_INT",
    "xSURFACEFMT_BC4_UNORM",
    "xSURFACEFMT_BC5_UNORM", /* 83 */
    "xVGPU10",
    "xVIDEO_DECODE",
    "xVIDEO_PROCESS",
    "xLINE_AA",
    "xLINE_STRIPPLE",
    "fMAX_LINE_WIDTH",
    "fMAX_AA_LINE_WIDTH",   /* 90 */
    "xSURFACEFMT_YV12",
    "xLOGICOPS",
    "xSCREENTARGETS",
    "xTS_COLOR_KEY",
    "xDX",                  /* 95 */
};

/** SVGA_CAP flag descriptors. */
static FLAGDESC const g_aVmSvgaCapFlags[] =
{
    { UINT32_C(0x00000001), "unknown-bit-0" },
    { UINT32_C(0x00000002), "SVGA_CAP_RECT_COPY" },
    { UINT32_C(0x00000004), "unknown-bit-2" },
    { UINT32_C(0x00000008), "unknown-bit-3" },
    { UINT32_C(0x00000010), "unknown-bit-4" },
    { UINT32_C(0x00000020), "SVGA_CAP_CURSOR" },
    { UINT32_C(0x00000040), "SVGA_CAP_CURSOR_BYPASS" },
    { UINT32_C(0x00000080), "SVGA_CAP_CURSOR_BYPASS_2" },
    { UINT32_C(0x00000100), "SVGA_CAP_8BIT_EMULATION" },
    { UINT32_C(0x00000200), "SVGA_CAP_ALPHA_CURSOR" },
    { UINT32_C(0x00000400), "unknown-bit-10" },
    { UINT32_C(0x00000800), "unknown-bit-11" },
    { UINT32_C(0x00001000), "unknown-bit-12" },
    { UINT32_C(0x00002000), "unknown-bit-13" },
    { UINT32_C(0x00004000), "SVGA_CAP_3D" },
    { UINT32_C(0x00008000), "SVGA_CAP_EXTENDED_FIFO" },
    { UINT32_C(0x00010000), "SVGA_CAP_MULTIMON" },
    { UINT32_C(0x00020000), "SVGA_CAP_PITCHLOCK" },
    { UINT32_C(0x00040000), "SVGA_CAP_IRQMASK" },
    { UINT32_C(0x00080000), "SVGA_CAP_DISPLAY_TOPOLOGY" },
    { UINT32_C(0x00100000), "SVGA_CAP_GMR" },
    { UINT32_C(0x00200000), "SVGA_CAP_TRACES" },
    { UINT32_C(0x00400000), "SVGA_CAP_GMR2" },
    { UINT32_C(0x00800000), "SVGA_CAP_SCREEN_OBJECT_2" },
    { UINT32_C(0x01000000), "SVGA_CAP_COMMAND_BUFFERS" },
    { UINT32_C(0x02000000), "SVGA_CAP_DEAD1" },
    { UINT32_C(0x04000000), "SVGA_CAP_CMD_BUFFERS_2" },
    { UINT32_C(0x08000000), "SVGA_CAP_GBOBJECTS" },
    { UINT32_C(0x10000000), "unknown-bit-28" },
    { UINT32_C(0x20000000), "unknown-bit-29" },
    { UINT32_C(0x40000000), "unknown-bit-30" },
    { UINT32_C(0x80000000), "unknown-bit-31" },
};

/** SVGA_FIFO_CAP flag descriptors. */
static FLAGDESC const g_aVmSvgaFifoCapFlags[] =
{
    { UINT32_C(0x00000001), "SVGA_FIFO_CAP_FENCE" },
    { UINT32_C(0x00000002), "SVGA_FIFO_CAP_ACCELFRONT" },
    { UINT32_C(0x00000004), "SVGA_FIFO_CAP_PITCHLOCK" },
    { UINT32_C(0x00000008), "SVGA_FIFO_CAP_VIDEO" },
    { UINT32_C(0x00000010), "SVGA_FIFO_CAP_CURSOR_BYPASS_3" },
    { UINT32_C(0x00000020), "SVGA_FIFO_CAP_ESCAPE" },
    { UINT32_C(0x00000040), "SVGA_FIFO_CAP_RESERVE" },
    { UINT32_C(0x00000080), "SVGA_FIFO_CAP_SCREEN_OBJECT" },
    { UINT32_C(0x00000100), "SVGA_FIFO_CAP_GMR2/SVGA_FIFO_CAP_3D_HWVERSION_REVISED" },
    { UINT32_C(0x00000200), "SVGA_FIFO_CAP_SCREEN_OBJECT_2" },
    { UINT32_C(0x00000400), "SVGA_FIFO_CAP_DEAD" },
    { UINT32_C(0x00000800), "unknown-bit-11" },
    { UINT32_C(0x00001000), "unknown-bit-12" },
    { UINT32_C(0x00002000), "unknown-bit-13" },
    { UINT32_C(0x00004000), "unknown-bit-14" },
    { UINT32_C(0x00008000), "unknown-bit-15" },
    { UINT32_C(0x00010000), "unknown-bit-16" },
    { UINT32_C(0x00020000), "unknown-bit-17" },
    { UINT32_C(0x00040000), "unknown-bit-18" },
    { UINT32_C(0x00080000), "unknown-bit-19" },
    { UINT32_C(0x00100000), "unknown-bit-20" },
    { UINT32_C(0x00200000), "unknown-bit-21" },
    { UINT32_C(0x00400000), "unknown-bit-22" },
    { UINT32_C(0x00800000), "unknown-bit-23" },
    { UINT32_C(0x01000000), "unknown-bit-24" },
    { UINT32_C(0x02000000), "unknown-bit-25" },
    { UINT32_C(0x04000000), "unknown-bit-26" },
    { UINT32_C(0x08000000), "unknown-bit-27" },
    { UINT32_C(0x10000000), "unknown-bit-28" },
    { UINT32_C(0x20000000), "unknown-bit-29" },
    { UINT32_C(0x40000000), "unknown-bit-30" },
    { UINT32_C(0x80000000), "unknown-bit-31" },
};


static void DisplayFlags(PCFLAGDESC paFlagDescs, uint32_t fFlags, unsigned cchIndent)
{
    uint32_t i;
    for (i = 0; i < 32; i++)
    {
        assert(paFlagDescs[i].fMask == (UINT32_C(1) << i));
        if (paFlagDescs[i].fMask & fFlags)
            printf("%*s%s\n", cchIndent, "", paFlagDescs[i].pszName);
    }
}


static int QueryParam(int fd, uint32_t uParam, const char *pszParam)
{
    struct drm_vmw_getparam_arg Arg;
    int rc;

    Arg.value = 0;
    Arg.param = uParam;
    Arg.pad64 = 0;
    rc = ioctl(fd, DRM_IOCTL_VMW_GET_PARAM, &Arg);
    if (rc >= 0)
    {
        switch (uParam)
        {
            case DRM_VMW_PARAM_3D:
                printf("%30s: %#llx -- enabled: %s\n", pszParam, Arg.value,
                       Arg.value == 0 ? "no" : Arg.value == 1 ? "yes" : "huh?");
                break;

            case DRM_VMW_PARAM_FIFO_HW_VERSION:
                printf("%30s: %#llx -- major=%llu minor=%llu\n", pszParam, Arg.value, Arg.value >> 16, Arg.value & 0xffff);
                break;

            case DRM_VMW_PARAM_HW_CAPS:
                printf("%30s: %#llx\n", pszParam, Arg.value);
                DisplayFlags(g_aVmSvgaCapFlags, (uint32_t)Arg.value, 32);
                g_fHwCaps = Arg.value;
                break;

            case DRM_VMW_PARAM_FIFO_CAPS:
                printf("%30s: %#llx\n", pszParam, Arg.value);
                DisplayFlags(g_aVmSvgaFifoCapFlags, (uint32_t)Arg.value, 32);
                break;

            case DRM_VMW_PARAM_3D_CAP_SIZE:
                printf("%30s: %#llx (%lld) [bytes]\n", pszParam, Arg.value, Arg.value);
                g_cb3dCaps = (uint32_t)Arg.value;
                break;

            default:
                printf("%30s: %#llx (%lld)\n", pszParam, Arg.value, Arg.value);
                break;
        }
    }
    else
        printf("%32s: failed: rc=%d errno=%d (%s)\n", pszParam, rc, errno, strerror(errno));
    return rc;
}


static int Dump3DParameters(int fd, int rcExit)
{
    int rc;
    printf("\n**** vmwgfx parameters *****\n");
#define QUERY_PARAM(nm) QueryParam(fd, nm, #nm)
    rc = QUERY_PARAM(DRM_VMW_PARAM_HW_CAPS);
    if (rc < 0)
        rcExit = 1;
    QUERY_PARAM(DRM_VMW_PARAM_FIFO_CAPS);
    QUERY_PARAM(DRM_VMW_PARAM_FIFO_HW_VERSION);
    QUERY_PARAM(DRM_VMW_PARAM_3D);
    QUERY_PARAM(DRM_VMW_PARAM_NUM_STREAMS);
    QUERY_PARAM(DRM_VMW_PARAM_FREE_STREAMS);
    QUERY_PARAM(DRM_VMW_PARAM_MAX_FB_SIZE);
    QUERY_PARAM(DRM_VMW_PARAM_MAX_SURF_MEMORY);
    QUERY_PARAM(DRM_VMW_PARAM_3D_CAP_SIZE);
    rc = QUERY_PARAM(DRM_VMW_PARAM_MAX_MOB_MEMORY);
    if (rc >= 0)
        g_fNew3dCapFormat = g_fHwCaps & UINT32_C(0x08000000) /*SVGA_CAP_GBOBJECTS */;
    QUERY_PARAM(DRM_VMW_PARAM_MAX_MOB_SIZE);
    return rcExit;
}


static void PrintOne3DCapability(uint32_t iCap, uint32_t uValue)
{
    union
    {
        float       rValue;
        uint32_t    u32Value;
    } u;
    u.u32Value = uValue;
    if (iCap < sizeof(g_apszVmSvgaDevCapNames) / sizeof(g_apszVmSvgaDevCapNames[0]))
    {
        const char *pszName = g_apszVmSvgaDevCapNames[iCap];
        if (pszName[0] == 'x')
            printf("    cap[%u]=%#010x {%s}\n", iCap, u.u32Value, pszName + 1);
        else
            printf("    cap[%u]=%d.%04u {%s}\n", iCap, (int)u.rValue, (unsigned)(u.rValue * 1000) % 10000, pszName + 1);
    }
    else
        printf("    cap[%u]=%#010x\n", iCap, u.u32Value);
}


static void DumpOld3dCapabilityRecords(struct SVGA3dCapsRecord *pCur)
{
    for (;;)
    {
        printf("    SVGA3dCapsRecordHeader: length=%#x (%d) type=%d\n",
               pCur->header.length, pCur->header.length, pCur->header.type);
        if (pCur->header.length == 0)
            break;

        uint32_t i;
        for (i = 0; i < pCur->header.length - 2; i += 2)
            PrintOne3DCapability(pCur->data[i], pCur->data[i + 1]);
        pCur = (struct SVGA3dCapsRecord *)((uint32_t *)pCur + pCur->header.length);
    }
}


static int Dump3DCapabilities(int fd, int rcExit)
{
    struct SVGA3dCapsRecord        *pBuf;
    struct drm_vmw_get_3d_cap_arg   Caps3D;
    int rc;


    printf("\n**** 3D capabilities *****\n");
    Caps3D.pad64    = 0;
    Caps3D.max_size = 1024 * sizeof(uint32_t);
    pBuf = (struct SVGA3dCapsRecord *)calloc(Caps3D.max_size, 1);
    Caps3D.buffer   = (uintptr_t)pBuf;

    errno = 0;
    rc = ioctl(fd, DRM_IOCTL_VMW_GET_3D_CAP, &Caps3D);
    if (rc >= 0)
    {
        printf("DRM_IOCTL_VMW_GET_3D_CAP: rc=%d\n", rc);
        if (!g_fNew3dCapFormat)
            DumpOld3dCapabilityRecords(pBuf);
        else
        {
            uint32_t const *pau32Data = (uint32_t const *)pBuf;
            uint32_t cCaps = g_cb3dCaps / sizeof(uint32_t);
            uint32_t iCap;
            for (iCap = 0; iCap < cCaps; iCap++)
                PrintOne3DCapability(iCap, pau32Data[iCap]);
        }
    }
    else
    {
        fprintf(stderr, "DRM_IOCTL_VMW_GET_3D_CAP failed: %d - %s\n", errno, strerror(errno));
        rcExit = 1;
    }

    free(pBuf);
    return rcExit;
}


static int FindAndMapFifo(uint32_t const **ppau32Fifo, uint32_t *pcbFifo, int rcExit)
{
    const char g_szDir[] = "/sys/bus/pci/devices";
    DIR *pDir = opendir(g_szDir);
    if (pDir)
    {
        struct dirent  *pEntry;
        char            szPath[4096];
        size_t          offPath = sizeof(g_szDir);
        memcpy(szPath, g_szDir, sizeof(g_szDir));
        szPath[offPath - 1] = '/';

        while ((pEntry = readdir(pDir)) != NULL)
        {
            struct stat st;
            size_t cchName = strlen(pEntry->d_name);
            memcpy(&szPath[offPath], pEntry->d_name, cchName);
            strcpy(&szPath[offPath + cchName], "/boot_vga");
            if (stat(szPath, &st) >= 0)
            {
                /* Found something that looks like the VGA device.  Try map resource2. */
                strcpy(&szPath[offPath + cchName], "/resource2");
                if (stat(szPath, &st) >= 0)
                {
                    int fdFifo = open(szPath, O_RDONLY);
                    if (fdFifo >= 0)
                    {
                        *pcbFifo   = (uint32_t)st.st_size;
                        *ppau32Fifo = (uint32_t *)mmap(NULL, *pcbFifo, PROT_READ, MAP_SHARED | MAP_FILE, fdFifo, 0);
                        if (*ppau32Fifo != MAP_FAILED)
                        {
                            printf("info: Mapped %s at %p LB %#x\n", szPath, *ppau32Fifo, *pcbFifo);
                            close(fdFifo);
                            closedir(pDir);
                            return rcExit;
                        }

                        fprintf(stderr, "error: failed to mmap '%s': %d (%s)\n", szPath, errno, strerror(errno));
                        close(fdFifo);
                    }
                    else
                        fprintf(stderr, "error: failed to open '%s': %d (%s)\n", g_szDir, errno, strerror(errno));
                }
                else
                    fprintf(stderr, "error: boot_vga devices doesn't have '%s'. (%d [%s])\n", szPath, errno, strerror(errno));
            }
        } /* for each directory entry */

        closedir(pDir);
    }
    else
        fprintf(stderr, "error: failed to open '%s': %d (%s)\n", g_szDir, errno, strerror(errno));
    return 1;
}


static int DumpFifoStuff(uint32_t const *pau32Fifo, uint32_t cbFifo, int rcExit)
{
    uint32_t cMax = cbFifo / sizeof(uint32_t);
    uint32_t i, iMin, iMax;

    printf("\n***** FIFO - %u bytes (%#x) *****\n", cbFifo, cbFifo);
    if (cMax >= 4)
    {
        iMin = pau32Fifo[0] / sizeof(uint32_t);
        printf("                 FIFO_MIN: %#09x --     iMin=%#08x\n", pau32Fifo[0], iMin);
        iMax = pau32Fifo[1] / sizeof(uint32_t);
        printf("                 FIFO_MAX: %#09x --     iMax=%#08x\n", pau32Fifo[1], iMax);
        printf("            FIFO_NEXT_CMD: %#09x -- iNextCmd=%#08x\n", pau32Fifo[2], (uint32_t)(pau32Fifo[2] / sizeof(uint32_t)));
        printf("                FIFO_STOP: %#09x --    iStop=%#08x\n", pau32Fifo[3], (uint32_t)(pau32Fifo[3] / sizeof(uint32_t)));
    }
    else
    {
        fprintf(stderr, "error: cbFifo=%#x is too small\n", cbFifo);
        return 1;
    }
    if (iMin > 4)
    {
        printf("        FIFO_CAPABILITIES: %#x (%d)\n", pau32Fifo[4], pau32Fifo[4]);
        DisplayFlags(g_aVmSvgaFifoCapFlags, pau32Fifo[4], 28);
    }
    if (iMin > 5)
        printf("               FIFO_FLAGS: %#x (%d)\n", pau32Fifo[5], pau32Fifo[5]);
    if (iMin > 6)
        printf("               FIFO_FENCE: %#x (%d)\n", pau32Fifo[6], pau32Fifo[6]);
    if (iMin > 7)
        printf("          FIFO_3D_VERSION: %#x -- %u.%u\n", pau32Fifo[7], pau32Fifo[7] >> 16, pau32Fifo[7] & 0xffff);
    if (iMin > 8)
        printf("          FIFO_PITCH_LOCK: %#x (%d)\n", pau32Fifo[8], pau32Fifo[8]);
    if (iMin > 9)
        printf("           FIFO_CURSOR_ON: %#x (%d)\n", pau32Fifo[9], pau32Fifo[9]);
    if (iMin > 10)
        printf("            FIFO_CURSOR_X: %#x (%d)\n", pau32Fifo[10], pau32Fifo[10]);
    if (iMin > 11)
        printf("            FIFO_CURSOR_Y: %#x (%d)\n", pau32Fifo[11], pau32Fifo[11]);
    if (iMin > 12)
        printf("        FIFO_CURSOR_COUNT: %#x (%d)\n", pau32Fifo[12], pau32Fifo[12]);
    if (iMin > 13)
        printf(" FIFO_CURSOR_LAST_UPDATED: %#x (%d)\n", pau32Fifo[13], pau32Fifo[13]);
    if (iMin > 14)
        printf("            FIFO_RESERVED: %#x (%d)\n", pau32Fifo[14], pau32Fifo[14]);
    if (iMin > 15)
        printf("    FIFO_CURSOR_SCREEN_ID: %#x (%d)\n", pau32Fifo[15], pau32Fifo[15]);
    if (iMin > 16)
        printf("                FIFO_DEAD: %#x (%d)\n", pau32Fifo[16], pau32Fifo[16]);
    if (iMin > 17)
        printf("FIFO_3D_HWVERSION_REVISED: %#x -- %u.%u\n", pau32Fifo[17], pau32Fifo[17] >> 16, pau32Fifo[7] & 0xffff);

    for (i = 18; i < 32 && i < iMin; i++)
        if (pau32Fifo[i] != 0)
            printf("FIFO_UNKNOWN_%u: %#x (%d)\n", i, pau32Fifo[i], pau32Fifo[i]);

    if (iMin >= 32+64)
    {
        if (pau32Fifo[32])
        {
            printf("            FIFO_3D_CAPS:\n");
            DumpOld3dCapabilityRecords((struct SVGA3dCapsRecord *)&pau32Fifo[32]);
        }
        else
            printf("warning: 3D capabilities not present?\n");
    }


    if (iMin > 288)
        printf("  FIFO_GUEST_3D_HWVERSION: %#x -- %u.%u\n", pau32Fifo[288], pau32Fifo[288] >> 16, pau32Fifo[288] & 0xffff);
    if (iMin > 289)
        printf("          FIFO_FENCE_GOAL: %#x (%d)\n", pau32Fifo[289], pau32Fifo[289]);
    if (iMin > 290)
        printf("                FIFO_BUSY: %#x (%d)\n", pau32Fifo[290], pau32Fifo[290]);

    for (i = 291; i < iMin; i++)
        if (pau32Fifo[i] != 0)
            printf("FIFO_UNKNOWN_%u: %#x (%d)\n", i, pau32Fifo[i], pau32Fifo[i]);

    return rcExit;
}





int main(int argc, char **argv)
{
    int rcExit = 0;
    const char *pszDev = "/dev/dri/card0";
    if (argc == 2)
        pszDev = argv[1];

    int fd = open(pszDev, O_RDWR);
    if (fd != -1)
    {
        uint32_t const *pau32Fifo = NULL;
        uint32_t        cbFifo = 0;

        /*
         * Parameters.
         */
        rcExit = Dump3DParameters(fd, rcExit);

        /*
         * 3D capabilities.
         */
        rcExit = Dump3DCapabilities(fd, rcExit);

        /*
         * Map and dump the FIFO registers.
         */
        rcExit = FindAndMapFifo(&pau32Fifo, &cbFifo, rcExit);
        if (pau32Fifo && cbFifo)
            rcExit = DumpFifoStuff(pau32Fifo, cbFifo, rcExit);
    }
    else
    {
        fprintf(stderr, "error opening '%s': %d\n", pszDev, errno);
        rcExit = 1;
    }

    return rcExit;
}
