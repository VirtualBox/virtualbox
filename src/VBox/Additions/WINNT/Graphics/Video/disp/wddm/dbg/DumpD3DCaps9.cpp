/* $Id: DumpD3DCaps9.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * ???
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#include <iprt/win/windows.h>
#include <d3d9types.h>
#include <d3d9caps.h>
#include <d3d9.h>
#include <stdio.h>

#define MAX(_v1, _v2) ((_v1) > (_v2) ? (_v1) : (_v2))
#define MIN(_v1, _v2) ((_v1) < (_v2) ? (_v1) : (_v2))

#define MISSING_FLAGS(_dw1, _dw2) ((_dw2) & ((_dw1) ^ (_dw2)))

#define MyLog(_m) do { printf _m ; } while (0)

#define DUMP_STRCASE(_t) \
        case _t: { MyLog(("%s", #_t"")); break; }
#define DUMP_STRCASE_DEFAULT_INT(_dw) \
        default: { MyLog(("0x%08x", (_dw))); break; }

#define DUMP_STRIF_INIT(_ps, _t) \
        const char * _pSep = (_ps); \
        bool _fSep = false; \
        _t _fFlags = 0; \

#define DUMP_STRIF(_v, _t) do { \
        if ((_v) & _t) { \
            if (_fSep) { \
                MyLog(("%s%s", _pSep ,#_t"")); \
            } \
            else { \
                MyLog(("%s", #_t"")); \
                _fSep = !!_pSep; \
            } \
            _fFlags |= _t; \
        } \
    } while (0)

#define DUMP_STRIF_MISSED(_dw) do { \
        _fFlags = MISSING_FLAGS(_fFlags, _dw); \
        if (_fFlags) { \
            if (_fSep) { \
                MyLog(("%s0x%08lx", _pSep, (_fFlags))); \
            } \
            else { \
                MyLog(("0x%08lx", (_fFlags))); \
                _fSep = !!_pSep; \
            } \
        } \
        _fFlags = _dw & ~(_fFlags); /* revert the flags valus back */ \
    } while (0)

/*
#define DUMP_DIFF_CAPS_VAL(_f, _name, _c1, _c2) do { \
        DWORD dwTmp =  MISSING_FLAGS((_c1), (_c2)); \
        if (dwTmp) {  _f(_name " |= ", " | ", dwTmp, ";\n"); } \
        dwTmp =  MISSING_FLAGS((_c2), (_c1)); \
        if (dwTmp) {  _f("// " _name " &= ~(", " | ", dwTmp, ");\n"); } \
    } while (0)

#define DUMP_DIFF_CAPS_FIELD(_f, _field, _name, _c1, _c2) DUMP_DIFF_CAPS_VAL(_f, _name""_field, (_c1)->_field, (_c2)->_field)
*/
#define DUMP_DIFF_CAPS(_f, _field) do { \
        DWORD dwTmp =  MISSING_FLAGS((pCaps1->_field), (pCaps2->_field)); \
        if (dwTmp) {  _f("pCaps->" #_field " |= ", " | ", dwTmp, ";\n"); } \
        dwTmp =  MISSING_FLAGS((pCaps2->_field), (pCaps1->_field)); \
        if (dwTmp) {  _f("// pCaps->" #_field " &= ~(", " | ", dwTmp, ");\n"); } \
    } while (0)

#define DUMP_DIFF_VAL(_field, _format) do { \
        if (pCaps1->_field != pCaps2->_field) { MyLog(("pCaps->" #_field " = " _format "; // " _format " \n", pCaps2->_field, pCaps1->_field)); } \
    } while (0)

static void printDeviceType(const char* pszPrefix, D3DDEVTYPE DeviceType, const char* pszSuffix)
{
    MyLog(("%s", pszPrefix));
    switch(DeviceType)
    {
        DUMP_STRCASE(D3DDEVTYPE_HAL)
        DUMP_STRCASE(D3DDEVTYPE_REF)
        DUMP_STRCASE(D3DDEVTYPE_SW)
        DUMP_STRCASE(D3DDEVTYPE_NULLREF)
        DUMP_STRCASE_DEFAULT_INT(DeviceType)
    }
    MyLog(("%s", pszSuffix));
}

static void printCaps(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
//    DUMP_STRIF(Caps, D3DCAPS_OVERLAY);
    DUMP_STRIF(Caps, D3DCAPS_READ_SCANLINE);
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}


static void printCaps2(const char* pszPrefix, const char* pszSeparator, DWORD Caps2, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps2, D3DCAPS2_FULLSCREENGAMMA);
    DUMP_STRIF(Caps2, D3DCAPS2_CANCALIBRATEGAMMA);
    DUMP_STRIF(Caps2, D3DCAPS2_RESERVED);
    DUMP_STRIF(Caps2, D3DCAPS2_CANMANAGERESOURCE);
    DUMP_STRIF(Caps2, D3DCAPS2_DYNAMICTEXTURES);
    DUMP_STRIF(Caps2, D3DCAPS2_CANAUTOGENMIPMAP);
    DUMP_STRIF(Caps2, D3DCAPS2_CANSHARERESOURCE);
    DUMP_STRIF_MISSED(Caps2);
    MyLog(("%s", pszSuffix));
}

static void printCaps3(const char* pszPrefix, const char* pszSeparator, DWORD Caps3, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps3, D3DCAPS3_ALPHA_FULLSCREEN_FLIP_OR_DISCARD);
    DUMP_STRIF(Caps3, D3DCAPS3_LINEAR_TO_SRGB_PRESENTATION);
    DUMP_STRIF(Caps3, D3DCAPS3_COPY_TO_VIDMEM);
    DUMP_STRIF(Caps3, D3DCAPS3_COPY_TO_SYSTEMMEM);
//    DUMP_STRIF(Caps3, D3DCAPS3_DXVAHD);
    DUMP_STRIF_MISSED(Caps3);
    MyLog(("%s", pszSuffix));
}

static void printPresentationIntervals(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps, D3DPRESENT_INTERVAL_ONE);
    DUMP_STRIF(Caps, D3DPRESENT_INTERVAL_TWO);
    DUMP_STRIF(Caps, D3DPRESENT_INTERVAL_THREE);
    DUMP_STRIF(Caps, D3DPRESENT_INTERVAL_FOUR);
    DUMP_STRIF(Caps, D3DPRESENT_INTERVAL_IMMEDIATE);
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}

static void printCursorCaps(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps, D3DCURSORCAPS_COLOR);
    DUMP_STRIF(Caps, D3DCURSORCAPS_LOWRES);
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}

static void printDevCaps(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps, D3DDEVCAPS_EXECUTESYSTEMMEMORY);
    DUMP_STRIF(Caps, D3DDEVCAPS_EXECUTEVIDEOMEMORY);
    DUMP_STRIF(Caps, D3DDEVCAPS_TLVERTEXSYSTEMMEMORY);
    DUMP_STRIF(Caps, D3DDEVCAPS_TLVERTEXVIDEOMEMORY);
    DUMP_STRIF(Caps, D3DDEVCAPS_TEXTURESYSTEMMEMORY);
    DUMP_STRIF(Caps, D3DDEVCAPS_TEXTUREVIDEOMEMORY);
    DUMP_STRIF(Caps, D3DDEVCAPS_DRAWPRIMTLVERTEX);
    DUMP_STRIF(Caps, D3DDEVCAPS_CANRENDERAFTERFLIP);
    DUMP_STRIF(Caps, D3DDEVCAPS_TEXTURENONLOCALVIDMEM);
    DUMP_STRIF(Caps, D3DDEVCAPS_DRAWPRIMITIVES2);
    DUMP_STRIF(Caps, D3DDEVCAPS_SEPARATETEXTUREMEMORIES);
    DUMP_STRIF(Caps, D3DDEVCAPS_DRAWPRIMITIVES2EX);
    DUMP_STRIF(Caps, D3DDEVCAPS_HWTRANSFORMANDLIGHT);
    DUMP_STRIF(Caps, D3DDEVCAPS_CANBLTSYSTONONLOCAL);
    DUMP_STRIF(Caps, D3DDEVCAPS_HWRASTERIZATION);
    DUMP_STRIF(Caps, D3DDEVCAPS_PUREDEVICE);
    DUMP_STRIF(Caps, D3DDEVCAPS_QUINTICRTPATCHES);
    DUMP_STRIF(Caps, D3DDEVCAPS_RTPATCHES);
    DUMP_STRIF(Caps, D3DDEVCAPS_RTPATCHHANDLEZERO);
    DUMP_STRIF(Caps, D3DDEVCAPS_NPATCHES);
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}

static void printPrimitiveMiscCaps(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps, D3DPMISCCAPS_MASKZ);
    DUMP_STRIF(Caps, D3DPMISCCAPS_CULLNONE);
    DUMP_STRIF(Caps, D3DPMISCCAPS_CULLCW);
    DUMP_STRIF(Caps, D3DPMISCCAPS_CULLCCW);
    DUMP_STRIF(Caps, D3DPMISCCAPS_COLORWRITEENABLE);
    DUMP_STRIF(Caps, D3DPMISCCAPS_CLIPPLANESCALEDPOINTS);
    DUMP_STRIF(Caps, D3DPMISCCAPS_CLIPTLVERTS);
    DUMP_STRIF(Caps, D3DPMISCCAPS_TSSARGTEMP);
    DUMP_STRIF(Caps, D3DPMISCCAPS_BLENDOP);
    DUMP_STRIF(Caps, D3DPMISCCAPS_NULLREFERENCE);
    DUMP_STRIF(Caps, D3DPMISCCAPS_INDEPENDENTWRITEMASKS);
    DUMP_STRIF(Caps, D3DPMISCCAPS_PERSTAGECONSTANT);
    DUMP_STRIF(Caps, D3DPMISCCAPS_FOGANDSPECULARALPHA);
    DUMP_STRIF(Caps, D3DPMISCCAPS_SEPARATEALPHABLEND);
    DUMP_STRIF(Caps, D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS);
    DUMP_STRIF(Caps, D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING);
    DUMP_STRIF(Caps, D3DPMISCCAPS_FOGVERTEXCLAMPED);
    DUMP_STRIF(Caps, D3DPMISCCAPS_POSTBLENDSRGBCONVERT);
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}

static void printRasterCaps(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps, D3DPRASTERCAPS_DITHER);
    DUMP_STRIF(Caps, D3DPRASTERCAPS_ZTEST);
    DUMP_STRIF(Caps, D3DPRASTERCAPS_FOGVERTEX);
    DUMP_STRIF(Caps, D3DPRASTERCAPS_FOGTABLE);
    DUMP_STRIF(Caps, D3DPRASTERCAPS_MIPMAPLODBIAS);
    DUMP_STRIF(Caps, D3DPRASTERCAPS_ZBUFFERLESSHSR);
    DUMP_STRIF(Caps, D3DPRASTERCAPS_FOGRANGE);
    DUMP_STRIF(Caps, D3DPRASTERCAPS_ANISOTROPY);
    DUMP_STRIF(Caps, D3DPRASTERCAPS_WBUFFER);
    DUMP_STRIF(Caps, D3DPRASTERCAPS_WFOG);
    DUMP_STRIF(Caps, D3DPRASTERCAPS_ZFOG);
    DUMP_STRIF(Caps, D3DPRASTERCAPS_COLORPERSPECTIVE);
    DUMP_STRIF(Caps, D3DPRASTERCAPS_SCISSORTEST);
    DUMP_STRIF(Caps, D3DPRASTERCAPS_SLOPESCALEDEPTHBIAS);
    DUMP_STRIF(Caps, D3DPRASTERCAPS_DEPTHBIAS);
    DUMP_STRIF(Caps, D3DPRASTERCAPS_MULTISAMPLE_TOGGLE);
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}

static void printCmpCaps(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps, D3DPCMPCAPS_NEVER);
    DUMP_STRIF(Caps, D3DPCMPCAPS_LESS);
    DUMP_STRIF(Caps, D3DPCMPCAPS_EQUAL);
    DUMP_STRIF(Caps, D3DPCMPCAPS_LESSEQUAL);
    DUMP_STRIF(Caps, D3DPCMPCAPS_GREATER);
    DUMP_STRIF(Caps, D3DPCMPCAPS_NOTEQUAL);
    DUMP_STRIF(Caps, D3DPCMPCAPS_GREATEREQUAL);
    DUMP_STRIF(Caps, D3DPCMPCAPS_ALWAYS);
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}

static void printBlendCaps(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps, D3DPBLENDCAPS_ZERO);
    DUMP_STRIF(Caps, D3DPBLENDCAPS_ONE);
    DUMP_STRIF(Caps, D3DPBLENDCAPS_SRCCOLOR);
    DUMP_STRIF(Caps, D3DPBLENDCAPS_INVSRCCOLOR);
    DUMP_STRIF(Caps, D3DPBLENDCAPS_SRCALPHA);
    DUMP_STRIF(Caps, D3DPBLENDCAPS_INVSRCALPHA);
    DUMP_STRIF(Caps, D3DPBLENDCAPS_DESTALPHA);
    DUMP_STRIF(Caps, D3DPBLENDCAPS_INVDESTALPHA);
    DUMP_STRIF(Caps, D3DPBLENDCAPS_DESTCOLOR);
    DUMP_STRIF(Caps, D3DPBLENDCAPS_INVDESTCOLOR);
    DUMP_STRIF(Caps, D3DPBLENDCAPS_SRCALPHASAT);
    DUMP_STRIF(Caps, D3DPBLENDCAPS_BOTHSRCALPHA);
    DUMP_STRIF(Caps, D3DPBLENDCAPS_BOTHINVSRCALPHA);
    DUMP_STRIF(Caps, D3DPBLENDCAPS_BLENDFACTOR);
    DUMP_STRIF(Caps, D3DPBLENDCAPS_SRCCOLOR2);
    DUMP_STRIF(Caps, D3DPBLENDCAPS_INVSRCCOLOR2);
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}

static void printShadeCaps(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps, D3DPSHADECAPS_COLORGOURAUDRGB);
    DUMP_STRIF(Caps, D3DPSHADECAPS_SPECULARGOURAUDRGB);
    DUMP_STRIF(Caps, D3DPSHADECAPS_ALPHAGOURAUDBLEND);
    DUMP_STRIF(Caps, D3DPSHADECAPS_FOGGOURAUD);
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}

static void printTextureCaps(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps, D3DPTEXTURECAPS_PERSPECTIVE);
    DUMP_STRIF(Caps, D3DPTEXTURECAPS_POW2);
    DUMP_STRIF(Caps, D3DPTEXTURECAPS_ALPHA);
    DUMP_STRIF(Caps, D3DPTEXTURECAPS_SQUAREONLY);
    DUMP_STRIF(Caps, D3DPTEXTURECAPS_TEXREPEATNOTSCALEDBYSIZE);
    DUMP_STRIF(Caps, D3DPTEXTURECAPS_ALPHAPALETTE);
    DUMP_STRIF(Caps, D3DPTEXTURECAPS_NONPOW2CONDITIONAL);
    DUMP_STRIF(Caps, D3DPTEXTURECAPS_PROJECTED);
    DUMP_STRIF(Caps, D3DPTEXTURECAPS_CUBEMAP);
    DUMP_STRIF(Caps, D3DPTEXTURECAPS_VOLUMEMAP);
    DUMP_STRIF(Caps, D3DPTEXTURECAPS_MIPMAP);
    DUMP_STRIF(Caps, D3DPTEXTURECAPS_MIPVOLUMEMAP);
    DUMP_STRIF(Caps, D3DPTEXTURECAPS_MIPCUBEMAP);
    DUMP_STRIF(Caps, D3DPTEXTURECAPS_CUBEMAP_POW2);
    DUMP_STRIF(Caps, D3DPTEXTURECAPS_VOLUMEMAP_POW2);
    DUMP_STRIF(Caps, D3DPTEXTURECAPS_NOPROJECTEDBUMPENV);
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}

static void printFilterCaps(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps, D3DPTFILTERCAPS_MINFPOINT);
    DUMP_STRIF(Caps, D3DPTFILTERCAPS_MINFLINEAR);
    DUMP_STRIF(Caps, D3DPTFILTERCAPS_MINFANISOTROPIC);
    DUMP_STRIF(Caps, D3DPTFILTERCAPS_MINFPYRAMIDALQUAD);
    DUMP_STRIF(Caps, D3DPTFILTERCAPS_MINFGAUSSIANQUAD);
    DUMP_STRIF(Caps, D3DPTFILTERCAPS_MIPFPOINT);
    DUMP_STRIF(Caps, D3DPTFILTERCAPS_MIPFLINEAR);
    DUMP_STRIF(Caps, D3DPTFILTERCAPS_CONVOLUTIONMONO);
    DUMP_STRIF(Caps, D3DPTFILTERCAPS_MAGFPOINT);
    DUMP_STRIF(Caps, D3DPTFILTERCAPS_MAGFLINEAR);
    DUMP_STRIF(Caps, D3DPTFILTERCAPS_MAGFANISOTROPIC);
    DUMP_STRIF(Caps, D3DPTFILTERCAPS_MAGFPYRAMIDALQUAD);
    DUMP_STRIF(Caps, D3DPTFILTERCAPS_MAGFGAUSSIANQUAD);
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}

static void printTextureAddressCaps(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps, D3DPTADDRESSCAPS_WRAP);
    DUMP_STRIF(Caps, D3DPTADDRESSCAPS_MIRROR);
    DUMP_STRIF(Caps, D3DPTADDRESSCAPS_CLAMP);
    DUMP_STRIF(Caps, D3DPTADDRESSCAPS_BORDER);
    DUMP_STRIF(Caps, D3DPTADDRESSCAPS_INDEPENDENTUV);
    DUMP_STRIF(Caps, D3DPTADDRESSCAPS_MIRRORONCE);
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}

static void printLineCaps(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps, D3DLINECAPS_TEXTURE);
    DUMP_STRIF(Caps, D3DLINECAPS_ZTEST);
    DUMP_STRIF(Caps, D3DLINECAPS_BLEND);
    DUMP_STRIF(Caps, D3DLINECAPS_ALPHACMP);
    DUMP_STRIF(Caps, D3DLINECAPS_FOG);
    DUMP_STRIF(Caps, D3DLINECAPS_ANTIALIAS);
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}

static void printStencilCaps(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps, D3DSTENCILCAPS_KEEP);
    DUMP_STRIF(Caps, D3DSTENCILCAPS_ZERO);
    DUMP_STRIF(Caps, D3DSTENCILCAPS_REPLACE);
    DUMP_STRIF(Caps, D3DSTENCILCAPS_INCRSAT);
    DUMP_STRIF(Caps, D3DSTENCILCAPS_DECRSAT);
    DUMP_STRIF(Caps, D3DSTENCILCAPS_INVERT);
    DUMP_STRIF(Caps, D3DSTENCILCAPS_INCR);
    DUMP_STRIF(Caps, D3DSTENCILCAPS_DECR);
    DUMP_STRIF(Caps, D3DSTENCILCAPS_TWOSIDED);
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}

static void printFVFCaps(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps, D3DFVFCAPS_TEXCOORDCOUNTMASK);
    DUMP_STRIF(Caps, D3DFVFCAPS_DONOTSTRIPELEMENTS);
    DUMP_STRIF(Caps, D3DFVFCAPS_PSIZE);
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}

static void printTextureOpCaps(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps, D3DTEXOPCAPS_DISABLE);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_SELECTARG1);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_SELECTARG2);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_MODULATE);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_MODULATE2X);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_MODULATE4X);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_ADD);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_ADDSIGNED);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_ADDSIGNED2X);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_SUBTRACT);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_ADDSMOOTH);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_BLENDDIFFUSEALPHA);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_BLENDTEXTUREALPHA);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_BLENDFACTORALPHA);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_BLENDTEXTUREALPHAPM);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_BLENDCURRENTALPHA);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_PREMODULATE);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_MODULATECOLOR_ADDALPHA);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_MODULATEINVALPHA_ADDCOLOR);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_MODULATEINVCOLOR_ADDALPHA);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_BUMPENVMAP);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_BUMPENVMAPLUMINANCE);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_DOTPRODUCT3);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_MULTIPLYADD);
    DUMP_STRIF(Caps, D3DTEXOPCAPS_LERP);
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}

static void printVertexProcessingCaps(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps, D3DVTXPCAPS_TEXGEN);
    DUMP_STRIF(Caps, D3DVTXPCAPS_MATERIALSOURCE7);
    DUMP_STRIF(Caps, D3DVTXPCAPS_DIRECTIONALLIGHTS);
    DUMP_STRIF(Caps, D3DVTXPCAPS_POSITIONALLIGHTS);
    DUMP_STRIF(Caps, D3DVTXPCAPS_LOCALVIEWER);
    DUMP_STRIF(Caps, D3DVTXPCAPS_TWEENING);
    DUMP_STRIF(Caps, D3DVTXPCAPS_TEXGEN_SPHEREMAP);
    DUMP_STRIF(Caps, D3DVTXPCAPS_NO_TEXGEN_NONLOCALVIEWER);
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}

static void printDevCaps2(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps, D3DDEVCAPS2_STREAMOFFSET);
    DUMP_STRIF(Caps, D3DDEVCAPS2_DMAPNPATCH);
    DUMP_STRIF(Caps, D3DDEVCAPS2_ADAPTIVETESSRTPATCH);
    DUMP_STRIF(Caps, D3DDEVCAPS2_ADAPTIVETESSNPATCH);
    DUMP_STRIF(Caps, D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES);
    DUMP_STRIF(Caps, D3DDEVCAPS2_PRESAMPLEDDMAPNPATCH);
    DUMP_STRIF(Caps, D3DDEVCAPS2_VERTEXELEMENTSCANSHARESTREAMOFFSET);
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}

static void printDeclTypes(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps, D3DDTCAPS_UBYTE4);
    DUMP_STRIF(Caps, D3DDTCAPS_UBYTE4N);
    DUMP_STRIF(Caps, D3DDTCAPS_SHORT2N);
    DUMP_STRIF(Caps, D3DDTCAPS_SHORT4N);
    DUMP_STRIF(Caps, D3DDTCAPS_USHORT2N);
    DUMP_STRIF(Caps, D3DDTCAPS_USHORT4N);
    DUMP_STRIF(Caps, D3DDTCAPS_UDEC3);
    DUMP_STRIF(Caps, D3DDTCAPS_DEC3N);
    DUMP_STRIF(Caps, D3DDTCAPS_FLOAT16_2);
    DUMP_STRIF(Caps, D3DDTCAPS_FLOAT16_4);
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}

#if 0
static void printXxxCaps(const char* pszPrefix, const char* pszSeparator, DWORD Caps, const char* pszSuffix)
{
    DUMP_STRIF_INIT(pszSeparator, DWORD);
    MyLog(("%s", pszPrefix));
    DUMP_STRIF(Caps, );
    DUMP_STRIF(Caps, );
    DUMP_STRIF(Caps, );
    DUMP_STRIF(Caps, );
    DUMP_STRIF(Caps, );
    DUMP_STRIF(Caps, );
    DUMP_STRIF(Caps, );
    DUMP_STRIF(Caps, );
    DUMP_STRIF(Caps, );
    DUMP_STRIF(Caps, );
    DUMP_STRIF(Caps, );
    DUMP_STRIF(Caps, );
    DUMP_STRIF(Caps, );
    DUMP_STRIF(Caps, );
    DUMP_STRIF(Caps, );
    DUMP_STRIF(Caps, );
    DUMP_STRIF_MISSED(Caps);
    MyLog(("%s", pszSuffix));
}
#endif

static void diffCaps(D3DCAPS9 *pCaps1, D3DCAPS9 *pCaps2)
{
    if (!memcmp(pCaps1, pCaps2, sizeof (D3DCAPS9)))
    {
        MyLog(("caps are identical!\n"));
        return;
    }

    MyLog(("caps differ, doing detailed diff..\n"));

    if (pCaps1->DeviceType != pCaps2->DeviceType)
    {
        printDeviceType("pCaps->DeviceType = ", pCaps2->DeviceType, ";\n");
    }

    DUMP_DIFF_VAL(AdapterOrdinal, "%d");

    DUMP_DIFF_CAPS(printCaps, Caps);
    DUMP_DIFF_CAPS(printCaps2, Caps2);
    DUMP_DIFF_CAPS(printCaps3, Caps3);
    DUMP_DIFF_CAPS(printPresentationIntervals, PresentationIntervals);
    DUMP_DIFF_CAPS(printCursorCaps, CursorCaps);
    DUMP_DIFF_CAPS(printDevCaps, DevCaps);
    DUMP_DIFF_CAPS(printPrimitiveMiscCaps, PrimitiveMiscCaps);
    DUMP_DIFF_CAPS(printRasterCaps, RasterCaps);
    DUMP_DIFF_CAPS(printCmpCaps, ZCmpCaps);
    DUMP_DIFF_CAPS(printBlendCaps, SrcBlendCaps);
    DUMP_DIFF_CAPS(printBlendCaps, DestBlendCaps);
    DUMP_DIFF_CAPS(printCmpCaps, AlphaCmpCaps);
    DUMP_DIFF_CAPS(printShadeCaps, ShadeCaps);
    DUMP_DIFF_CAPS(printTextureCaps, TextureCaps);
    DUMP_DIFF_CAPS(printFilterCaps, TextureFilterCaps);
    DUMP_DIFF_CAPS(printFilterCaps, CubeTextureFilterCaps);
    DUMP_DIFF_CAPS(printFilterCaps, VolumeTextureFilterCaps);
    DUMP_DIFF_CAPS(printTextureAddressCaps, TextureAddressCaps);
    DUMP_DIFF_CAPS(printTextureAddressCaps, VolumeTextureAddressCaps);
    DUMP_DIFF_CAPS(printLineCaps, LineCaps);

    /* non-caps */
    DUMP_DIFF_VAL(MaxTextureWidth, "%ld");
    DUMP_DIFF_VAL(MaxTextureHeight, "%ld");
    DUMP_DIFF_VAL(MaxVolumeExtent, "%ld");
    DUMP_DIFF_VAL(MaxTextureRepeat, "%ld");
    DUMP_DIFF_VAL(MaxTextureAspectRatio, "%ld");
    DUMP_DIFF_VAL(MaxAnisotropy, "%ld");
    DUMP_DIFF_VAL(MaxVertexW, "%f");
    DUMP_DIFF_VAL(GuardBandLeft, "%f");
    DUMP_DIFF_VAL(GuardBandTop, "%f");
    DUMP_DIFF_VAL(GuardBandRight, "%f");
    DUMP_DIFF_VAL(GuardBandBottom, "%f");
    DUMP_DIFF_VAL(ExtentsAdjust, "%f");

    /* caps */
    DUMP_DIFF_CAPS(printStencilCaps, StencilCaps);
    DUMP_DIFF_CAPS(printFVFCaps, FVFCaps);
    DUMP_DIFF_CAPS(printTextureOpCaps, TextureOpCaps);

    /* non-caps */
    DUMP_DIFF_VAL(MaxTextureBlendStages, "%ld");
    DUMP_DIFF_VAL(MaxSimultaneousTextures, "%ld");

    /* caps */
    DUMP_DIFF_CAPS(printVertexProcessingCaps, VertexProcessingCaps);

    /* non-caps */
    DUMP_DIFF_VAL(MaxActiveLights, "%ld");
    DUMP_DIFF_VAL(MaxUserClipPlanes, "%ld");
    DUMP_DIFF_VAL(MaxVertexBlendMatrices, "%ld");
    DUMP_DIFF_VAL(MaxVertexBlendMatrixIndex, "%ld");
    DUMP_DIFF_VAL(MaxPointSize, "%f");
    DUMP_DIFF_VAL(MaxPrimitiveCount, "%ld");
    DUMP_DIFF_VAL(MaxVertexIndex, "%ld");
    DUMP_DIFF_VAL(MaxStreams, "%ld");
    DUMP_DIFF_VAL(MaxStreamStride, "%ld");
    DUMP_DIFF_VAL(VertexShaderVersion, "0x%lx");
    DUMP_DIFF_VAL(MaxVertexShaderConst, "%ld");
    DUMP_DIFF_VAL(PixelShaderVersion, "0x%lx");
    DUMP_DIFF_VAL(PixelShader1xMaxValue, "%f");

    /* D3D9 */
    /* caps */
    DUMP_DIFF_CAPS(printDevCaps2, DevCaps2);

    /* non-caps */
    DUMP_DIFF_VAL(MaxNpatchTessellationLevel, "%f");
    DUMP_DIFF_VAL(Reserved5, "%ld");
    DUMP_DIFF_VAL(MasterAdapterOrdinal, "%d");
    DUMP_DIFF_VAL(AdapterOrdinalInGroup, "%d");
    DUMP_DIFF_VAL(NumberOfAdaptersInGroup, "%d");

    /* caps */
    DUMP_DIFF_CAPS(printDeclTypes, DeclTypes);

    /* non-caps */
    DUMP_DIFF_VAL(NumSimultaneousRTs, "%ld");

    /* caps */
    DUMP_DIFF_CAPS(printFilterCaps, StretchRectFilterCaps);

    /* non-caps */
    DUMP_DIFF_VAL(VS20Caps.Caps, "0x%lx");
    DUMP_DIFF_VAL(VS20Caps.DynamicFlowControlDepth, "%d");
    DUMP_DIFF_VAL(VS20Caps.NumTemps, "%d");
    DUMP_DIFF_VAL(VS20Caps.StaticFlowControlDepth, "%d");

    DUMP_DIFF_VAL(PS20Caps.Caps, "0x%lx");
    DUMP_DIFF_VAL(PS20Caps.DynamicFlowControlDepth, "%d");
    DUMP_DIFF_VAL(PS20Caps.NumTemps, "%d");
    DUMP_DIFF_VAL(PS20Caps.StaticFlowControlDepth, "%d");
    DUMP_DIFF_VAL(PS20Caps.NumInstructionSlots, "%d");

    DUMP_DIFF_CAPS(printFilterCaps, VertexTextureFilterCaps);
    DUMP_DIFF_VAL(MaxVShaderInstructionsExecuted, "%ld");
    DUMP_DIFF_VAL(MaxPShaderInstructionsExecuted, "%ld");
    DUMP_DIFF_VAL(MaxVertexShader30InstructionSlots, "%ld");
    DUMP_DIFF_VAL(MaxPixelShader30InstructionSlots, "%ld");
}

static DWORD g_aCaps1[] = {
        0x00000001, 0x00000000, 0x00020000, 0xe0000000,
        0x00000320, 0x80000001, 0x00000003, 0x0019aff0,
        0x000f4ff2, 0x07736191, 0x000000ff, 0x00003fff,
        0x000023ff, 0x000000ff, 0x00084208, 0x0001ecc5,
        0x07030700, 0x07030700, 0x03030300, 0x0000003f,
        0x0000003f, 0x0000001f, 0x00001000, 0x00001000,
        0x00000100, 0x00008000, 0x00001000, 0x00000010,
        0x3f800000, 0xc6000000, 0xc6000000, 0x46000000,
        0x46000000, 0x00000000, 0x000001ff, 0x00100008,
        0x03feffff, 0x00000008, 0x00000008, 0x0000013b,
        0x00000008, 0x00000006, 0x00000000, 0x00000000,
        0x437f0000, 0x000fffff, 0x000fffff, 0x00000010,
        0x00000400, 0xfffe0200, 0x00000080, 0xffff0200,
        0x41000000, 0x00000051, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000001, 0x0000030f,
        0x00000001, 0x03000300, 0x00000000, 0x00000018,
        0x00000020, 0x00000001, 0x00000000, 0x00000018,
        0x00000020, 0x00000000, 0x00000060, 0x01000100,
        0x0000ffff, 0x00000200, 0x00000000, 0x00000000
};


static DWORD g_aCaps2[] = {
        0x00000001, 0x00000000, 0x00000000, 0x60020000,
        0x00000320, 0x80000001, 0x00000003, 0x0019aff0,
        0x000a0ff2, 0x07332191, 0x000000ff, 0x00003fff,
        0x000023ff, 0x000000ff, 0x00084208, 0x0001ec85,
        0x07030700, 0x07030700, 0x03030300, 0x0000001f,
        0x0000001f, 0x0000001f, 0x00001000, 0x00001000,
        0x00000100, 0x00008000, 0x00001000, 0x00000010,
        0x3f800000, 0x00000000, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x000001ff, 0x00100008,
        0x03feffff, 0x00000008, 0x00000008, 0x0000013b,
        0x00000008, 0x00000006, 0x00000000, 0x00000000,
        0x437f0000, 0x000fffff, 0x000fffff, 0x00000010,
        0x00000400, 0xfffe0200, 0x00000080, 0xffff0200,
        0x41000000, 0x00000051, 0x00000000, 0x00000000,
        0x00000000, 0x00000000, 0x00000001, 0x0000000f,
        0x00000001, 0x03000300, 0x00000000, 0x00000000,
        0x0000001f, 0x00000001, 0x00000000, 0x00000000,
        0x00000100, 0x00000000, 0x00000060, 0x00000000,
        0x0000ffff, 0x00000200, 0x00000000, 0x00000000
};


/* ogl stuff */
static const char * strNext(const char * pcszStr)
{
    pcszStr = strchr(pcszStr, ' ');
    if (!pcszStr)
        return NULL;

    do
    {
        ++pcszStr;
        if (*pcszStr == '\0')
            return NULL;
        else if (*pcszStr != ' ')
            return pcszStr;
    } while (1);

    MyLog(("WARNING: should NOT be here!\n"));
    return NULL;
}

static int strLength(const char * pcszStr, char sep)
{
    if (sep == '\0')
        return (int)strlen(pcszStr);
    const char * pcszNext = strchr(pcszStr, sep);
    if (pcszNext)
        return (int)(pcszNext - pcszStr);
    return (int)strlen(pcszStr);
}

static int strCmp(const char * pcszStr1, const char * pcszStr2, char sep)
{
    if (sep == '\0')
        return strcmp(pcszStr1, pcszStr2);

    int cStr1 = strLength(pcszStr1, sep);
    int cStr2 = strLength(pcszStr2, sep);
    int iCmp = strncmp(pcszStr1, pcszStr2, MIN(cStr1, cStr2));
    if (iCmp)
        return iCmp;
    return cStr1 - cStr2;
}

static char * strDupCur(const char * pcszStr, char sep)
{
    int cStr = strLength(pcszStr, sep);
    char * newStr = (char *)malloc(cStr+1);
    if (!newStr)
    {
        MyLog(("malloc failed!\n"));
        return NULL;
    }
    memcpy(newStr, pcszStr, cStr);
    newStr[cStr] = '\0';
    return newStr;
}

static char * strDupTotal(const char * pcszStr)
{
    int cStr = (int)strlen(pcszStr);
    char * newStr = (char *)malloc(cStr+1+1);
    if (!newStr)
    {
        MyLog(("malloc failed!\n"));
        return NULL;
    }
    memcpy(newStr, pcszStr, cStr);
    newStr[cStr] = '\0';
    newStr[cStr+1] = '\0';
    return newStr;
}

static char * strDupSort(const char * pcszStr)
{
    int cStr = (int)strlen(pcszStr);
    char * pNewStr = (char *)malloc(cStr+1+1+1);
    if (!pNewStr)
    {
        MyLog(("malloc failed!\n"));
        return NULL;
    }
    char *pCurNew = pNewStr;
    const char *pPrevCmp = NULL;
    const char * pCmp = "\001";
    const char * pCur;
    int cLength, cPrevLength = 0;

    do
    {
        cLength = 0;
        for (pCur = pcszStr; pCur; pCur = strNext(pCur))
        {
            int cCur = strLength(pCur, ' ');
            int cCmp = strLength(pCmp, ' ');
            int iCmp = strncmp(pCur, pCmp, MIN(cCur, cCmp));
            if (!iCmp)
                iCmp = cCur - cCmp;
            if (iCmp > 0)
            {
                if (!cLength)
                {
                    pCmp = pCur;
                    cLength = cCur;
                }
            }
            else if (iCmp < 0)
            {
                if (cLength)
                {
                    if (pPrevCmp)
                    {
                        iCmp = strncmp(pCur, pPrevCmp, MIN(cCur, cPrevLength));
                        if (!iCmp)
                            iCmp = cCur - cPrevLength;
                        if (iCmp > 0)
                        {
                            pCmp = pCur;
                            cLength = cCur;
                        }
                    }
                    else
                    {
                        pCmp = pCur;
                        cLength = cCur;
                    }
                }
            }
        }

        if (!cLength)
            break;

        pPrevCmp = pCmp;
        cPrevLength = cLength;
        memcpy(pCurNew, pCmp, cLength);
        pCurNew += cLength;
        *pCurNew = ' ';
        ++pCurNew;
    } while (1);

    *pCurNew = '\0';
    ++pCurNew;

    return pNewStr;
}


#define DUMP_DIFF_STR_ADDED(_pStr) do { \
        char * pszCopy = strDupCur(_pStr, ' '); \
        MyLog(("+ %s\n", pszCopy)); \
        if (pszCopy) free(pszCopy); \
    } while (0)

#define DUMP_DIFF_STR_REMOVED(_pStr) do { \
        char * pszCopy = strDupCur(_pStr, ' '); \
        MyLog(("- %s\n", pszCopy)); \
        if (pszCopy) free(pszCopy); \
    } while (0)

#define DIFF_STR_ADDED(_ppStr) do { \
        DUMP_DIFF_STR_ADDED(*(_ppStr)); \
        *(_ppStr) = strNext(*(_ppStr)); \
    } while (0)

#define DIFF_STR_REMOVED(_ppStr) do { \
        DUMP_DIFF_STR_REMOVED(*(_ppStr)); \
        *(_ppStr) = strNext(*(_ppStr)); \
    } while (0)

#define DIFF_STR_MATCHED(_ppStr1, _ppStr2) do { \
        *(_ppStr1) = strNext(*(_ppStr1)); \
        *(_ppStr2) = strNext(*(_ppStr2)); \
    } while (0)

static void diffStrOrderedLists(const char * pcszStr1, const char * pcszStr2)
{
    while (pcszStr1 || pcszStr2)
    {
        if (pcszStr1 && pcszStr2)
        {
            int iCmp = strCmp(pcszStr1, pcszStr2, ' ');
//            int cStr1 = strLength(pcszStr1, ' ');
//            int cStr2 = strLength(pcszStr2, ' ');
//            int iCmp = strncmp(pcszStr1, pcszStr2, MAX(cStr1, cStr2));
            if (iCmp > 0)
                DIFF_STR_ADDED(&pcszStr2);
            else if (iCmp < 0)
                DIFF_STR_REMOVED(&pcszStr1);
            else
                DIFF_STR_MATCHED(&pcszStr1, &pcszStr2);
        }
        else if (pcszStr1)
            DIFF_STR_REMOVED(&pcszStr1);
        else
            DIFF_STR_ADDED(&pcszStr2);
    }
}

static void diffGlExts(const char * pcszExts1, const char * pcszExts2)
{
    pcszExts1 = strDupSort(pcszExts1);
    pcszExts2 = strDupSort(pcszExts2);

    if (!strcmp(pcszExts1, pcszExts2))
    {
        MyLog(("GL Exts identical!\n"));
        MyLog(("%s\n", pcszExts1));
        return;
    }

    MyLog(("%s\n", pcszExts1));

    MyLog(("Diffing GL Exts..\n"));
    diffStrOrderedLists(pcszExts1, pcszExts2);
}

static char *g_GlExts1 =
        "GL_ARB_multisample GL_EXT_abgr GL_EXT_bgra GL_EXT_blend_color GL_EXT_blend_logic_op GL_EXT_blend_minmax GL_EXT_blend_subtract GL_EXT_copy_texture "
        "GL_EXT_polygon_offset GL_EXT_subtexture GL_EXT_texture_object GL_EXT_vertex_array GL_EXT_compiled_vertex_array GL_EXT_texture GL_EXT_texture3D "
        "GL_IBM_rasterpos_clip GL_ARB_point_parameters GL_EXT_draw_range_elements GL_EXT_packed_pixels GL_EXT_point_parameters GL_EXT_rescale_normal "
        "GL_EXT_separate_specular_color GL_EXT_texture_edge_clamp GL_SGIS_generate_mipmap GL_SGIS_texture_border_clamp GL_SGIS_texture_edge_clamp "
        "GL_SGIS_texture_lod GL_ARB_framebuffer_sRGB GL_ARB_multitexture GL_EXT_framebuffer_sRGB GL_IBM_multimode_draw_arrays GL_IBM_texture_mirrored_repeat "
        "GL_ARB_texture_cube_map GL_ARB_texture_env_add GL_ARB_transpose_matrix GL_EXT_blend_func_separate GL_EXT_fog_coord GL_EXT_multi_draw_arrays "
        "GL_EXT_secondary_color GL_EXT_texture_env_add GL_EXT_texture_filter_anisotropic GL_EXT_texture_lod_bias GL_INGR_blend_func_separate GL_NV_blend_square "
        "GL_NV_light_max_exponent GL_NV_texgen_reflection GL_NV_texture_env_combine4 GL_SUN_multi_draw_arrays GL_ARB_texture_border_clamp GL_ARB_texture_compression GL_EXT_framebuffer_object "
        "GL_EXT_texture_env_dot3 GL_MESA_window_pos GL_NV_packed_depth_stencil GL_NV_texture_rectangle GL_ARB_depth_texture GL_ARB_occlusion_query GL_ARB_shadow GL_ARB_texture_env_combine "
        "GL_ARB_texture_env_crossbar GL_ARB_texture_env_dot3 GL_ARB_texture_mirrored_repeat GL_ARB_window_pos GL_EXT_stencil_two_side GL_EXT_texture_cube_map GL_NV_depth_clamp GL_APPLE_packed_pixels "
        "GL_APPLE_vertex_array_object GL_ARB_draw_buffers GL_ARB_fragment_program GL_ARB_fragment_shader GL_ARB_shader_objects GL_ARB_vertex_program GL_ARB_vertex_shader GL_ATI_draw_buffers GL_ATI_texture_env_combine3 "
        "GL_EXT_shadow_funcs GL_EXT_stencil_wrap GL_MESA_pack_invert GL_NV_primitive_restart GL_ARB_depth_clamp GL_ARB_fragment_program_shadow GL_ARB_half_float_pixel GL_ARB_occlusion_query2 GL_ARB_point_sprite "
        "GL_ARB_shading_language_100 GL_ARB_sync GL_ARB_texture_non_power_of_two GL_ARB_vertex_buffer_object GL_ATI_blend_equation_separate GL_EXT_blend_equation_separate GL_OES_read_format GL_ARB_color_buffer_float "
        "GL_ARB_pixel_buffer_object GL_ARB_texture_compression_rgtc GL_ARB_texture_rectangle GL_EXT_packed_float GL_EXT_pixel_buffer_object GL_EXT_texture_compression_rgtc GL_EXT_texture_mirror_clamp GL_EXT_texture_rectangle "
        "GL_EXT_texture_sRGB GL_EXT_texture_shared_exponent GL_ARB_framebuffer_object GL_EXT_framebuffer_blit GL_EXT_framebuffer_multisample GL_EXT_packed_depth_stencil GL_ARB_vertex_array_object GL_ATI_separate_stencil "
        "GL_ATI_texture_mirror_once GL_EXT_draw_buffers2 GL_EXT_draw_instanced GL_EXT_gpu_program_parameters GL_EXT_texture_env_combine GL_EXT_texture_sRGB_decode GL_EXT_timer_query GL_OES_EGL_image GL_ARB_copy_buffer "
        "GL_ARB_draw_instanced GL_ARB_half_float_vertex GL_ARB_instanced_arrays GL_ARB_map_buffer_range GL_ARB_texture_rg GL_ARB_texture_swizzle GL_ARB_vertex_array_bgra GL_EXT_separate_shader_objects GL_EXT_texture_swizzle "
        "GL_EXT_vertex_array_bgra GL_NV_conditional_render GL_ARB_ES2_compatibility GL_ARB_draw_elements_base_vertex GL_ARB_explicit_attrib_location GL_ARB_fragment_coord_conventions GL_ARB_provoking_vertex "
        "GL_ARB_sampler_objects GL_ARB_shader_texture_lod GL_EXT_provoking_vertex GL_EXT_texture_snorm GL_MESA_texture_signed_rgba GL_NV_texture_barrier GL_ARB_robustness"
        ;
static char *g_GlExts2 = "GL_ARB_blend_func_extended GL_ARB_color_buffer_float GL_ARB_compatibility GL_ARB_copy_buffer GL_ARB_depth_buffer_float GL_ARB_depth_clamp GL_ARB_depth_texture GL_ARB_draw_buffers "
        "GL_ARB_draw_elements_base_vertex GL_ARB_draw_instanced GL_ARB_ES2_compatibility GL_ARB_explicit_attrib_location GL_ARB_fragment_coord_conventions GL_ARB_fragment_program GL_ARB_fragment_program_shadow "
        "GL_ARB_fragment_shader GL_ARB_framebuffer_object GL_ARB_framebuffer_sRGB GL_ARB_geometry_shader4 GL_ARB_get_program_binary GL_ARB_half_float_pixel GL_ARB_half_float_vertex GL_ARB_imaging GL_ARB_instanced_arrays "
        "GL_ARB_map_buffer_range GL_ARB_multisample GL_ARB_multitexture GL_ARB_occlusion_query GL_ARB_occlusion_query2 GL_ARB_pixel_buffer_object GL_ARB_point_parameters GL_ARB_point_sprite GL_ARB_provoking_vertex "
        "GL_ARB_robustness GL_ARB_sampler_objects GL_ARB_seamless_cube_map GL_ARB_separate_shader_objects GL_ARB_shader_bit_encoding GL_ARB_shader_objects GL_ARB_shading_language_100 GL_ARB_shading_language_include "
        "GL_ARB_shadow GL_ARB_sync GL_ARB_texture_border_clamp GL_ARB_texture_buffer_object GL_ARB_texture_compression GL_ARB_texture_compression_rgtc GL_ARB_texture_cube_map GL_ARB_texture_env_add GL_ARB_texture_env_combine "
        "GL_ARB_texture_env_crossbar GL_ARB_texture_env_dot3 GL_ARB_texture_float GL_ARB_texture_mirrored_repeat GL_ARB_texture_multisample GL_ARB_texture_non_power_of_two GL_ARB_texture_rectangle GL_ARB_texture_rg "
        "GL_ARB_texture_rgb10_a2ui GL_ARB_texture_swizzle GL_ARB_timer_query GL_ARB_transpose_matrix GL_ARB_uniform_buffer_object GL_ARB_vertex_array_bgra GL_ARB_vertex_array_object GL_ARB_vertex_buffer_object GL_ARB_vertex_program "
        "GL_ARB_vertex_shader GL_ARB_vertex_type_2_10_10_10_rev GL_ARB_viewport_array GL_ARB_window_pos GL_ATI_draw_buffers GL_ATI_texture_float GL_ATI_texture_mirror_once GL_S3_s3tc GL_EXT_texture_env_add GL_EXT_abgr GL_EXT_bgra "
        "GL_EXT_bindable_uniform GL_EXT_blend_color GL_EXT_blend_equation_separate GL_EXT_blend_func_separate GL_EXT_blend_minmax GL_EXT_blend_subtract GL_EXT_compiled_vertex_array GL_EXT_Cg_shader GL_EXT_depth_bounds_test "
        "GL_EXT_direct_state_access GL_EXT_draw_buffers2 GL_EXT_draw_instanced GL_EXT_draw_range_elements GL_EXT_fog_coord GL_EXT_framebuffer_blit GL_EXT_framebuffer_multisample GL_EXTX_framebuffer_mixed_formats "
        "GL_EXT_framebuffer_object GL_EXT_framebuffer_sRGB GL_EXT_geometry_shader4 GL_EXT_gpu_program_parameters GL_EXT_gpu_shader4 GL_EXT_multi_draw_arrays GL_EXT_packed_depth_stencil GL_EXT_packed_float GL_EXT_packed_pixels "
        "GL_EXT_pixel_buffer_object GL_EXT_point_parameters GL_EXT_provoking_vertex GL_EXT_rescale_normal GL_EXT_secondary_color GL_EXT_separate_shader_objects GL_EXT_separate_specular_color GL_EXT_shadow_funcs "
        "GL_EXT_stencil_two_side GL_EXT_stencil_wrap GL_EXT_texture3D GL_EXT_texture_array GL_EXT_texture_buffer_object GL_EXT_texture_compression_dxt1 GL_EXT_texture_compression_latc GL_EXT_texture_compression_rgtc "
        "GL_EXT_texture_compression_s3tc GL_EXT_texture_cube_map GL_EXT_texture_edge_clamp GL_EXT_texture_env_combine GL_EXT_texture_env_dot3 GL_EXT_texture_filter_anisotropic GL_EXT_texture_format_BGRA8888 GL_EXT_texture_integer "
        "GL_EXT_texture_lod GL_EXT_texture_lod_bias GL_EXT_texture_mirror_clamp GL_EXT_texture_object GL_EXT_texture_shared_exponent GL_EXT_texture_sRGB GL_EXT_texture_swizzle GL_EXT_texture_type_2_10_10_10_REV GL_EXT_timer_query "
        "GL_EXT_vertex_array GL_EXT_vertex_array_bgra GL_EXT_x11_sync_object GL_EXT_import_sync_object GL_IBM_rasterpos_clip GL_IBM_texture_mirrored_repeat GL_KTX_buffer_region GL_NV_alpha_test GL_NV_blend_minmax GL_NV_blend_square "
        "GL_NV_complex_primitives GL_NV_conditional_render GL_NV_copy_depth_to_color GL_NV_copy_image GL_NV_depth_buffer_float GL_NV_depth_clamp GL_NV_explicit_multisample GL_NV_fbo_color_attachments "
        "GL_NV_fence GL_NV_float_buffer GL_NV_fog_distance GL_NV_fragdepth GL_NV_fragment_program GL_NV_fragment_program_option GL_NV_fragment_program2 GL_NV_framebuffer_multisample_coverage GL_NV_geometry_shader4 "
        "GL_NV_gpu_program4 GL_NV_half_float GL_NV_light_max_exponent GL_NV_multisample_coverage GL_NV_multisample_filter_hint GL_NV_occlusion_query GL_NV_packed_depth_stencil GL_NV_parameter_buffer_object "
        "GL_NV_parameter_buffer_object2 GL_NV_path_rendering GL_NV_pixel_data_range GL_NV_point_sprite GL_NV_primitive_restart GL_NV_register_combiners GL_NV_register_combiners2 GL_NV_shader_buffer_load GL_NV_texgen_reflection "
        "GL_NV_texture_barrier GL_NV_texture_compression_vtc GL_NV_texture_env_combine4 GL_NV_texture_expand_normal GL_NV_texture_lod_clamp GL_NV_texture_multisample GL_NV_texture_rectangle GL_NV_texture_shader GL_NV_texture_shader2 "
        "GL_NV_texture_shader3 GL_NV_transform_feedback GL_NV_vdpau_interop GL_NV_vertex_array_range GL_NV_vertex_array_range2 GL_NV_vertex_buffer_unified_memory GL_NV_vertex_program GL_NV_vertex_program1_1 GL_NV_vertex_program2 "
        "GL_NV_vertex_program2_option GL_NV_vertex_program3 GL_NVX_conditional_render GL_NVX_gpu_memory_info GL_OES_depth24 GL_OES_depth32 GL_OES_depth_texture GL_OES_element_index_uint GL_OES_fbo_render_mipmap "
        "GL_OES_get_program_binary GL_OES_mapbuffer GL_OES_packed_depth_stencil GL_OES_rgb8_rgba8 GL_OES_standard_derivatives GL_OES_texture_3D GL_OES_texture_float GL_OES_texture_float_linear GL_OES_texture_half_float "
        "GL_OES_texture_half_float_linear GL_OES_texture_npot GL_OES_vertex_array_object GL_OES_vertex_half_float GL_SGIS_generate_mipmap GL_SGIS_texture_lod GL_SGIX_depth_texture GL_SGIX_shadow GL_SUN_slice_accum";

typedef enum
{
    D3DCAPSSOURCE_TYPE_UNDEFINED = 0,
    D3DCAPSSOURCE_TYPE_EMBEDDED1,
    D3DCAPSSOURCE_TYPE_EMBEDDED2,
    D3DCAPSSOURCE_TYPE_NULL,
    D3DCAPSSOURCE_TYPE_LOCAL,
    D3DCAPSSOURCE_TYPE_FILE,
    D3DCAPSSOURCE_TYPE_NONE
} D3DCAPSSOURCE_TYPE;

static D3DCAPS9* selectCaps(D3DCAPS9 *pLocalStorage, D3DCAPS9 *pLocalEmbedded1, D3DCAPS9 *pLocalEmbedded2, D3DCAPSSOURCE_TYPE enmCapsType)
{
    switch (enmCapsType)
    {
        case D3DCAPSSOURCE_TYPE_EMBEDDED1:
            return pLocalEmbedded1;
        case D3DCAPSSOURCE_TYPE_EMBEDDED2:
            return pLocalEmbedded2;
        case D3DCAPSSOURCE_TYPE_NULL:
            memset (pLocalStorage, 0, sizeof (*pLocalStorage));
            return pLocalStorage;
        case D3DCAPSSOURCE_TYPE_LOCAL:
        {
            LPDIRECT3D9EX pD3D = NULL;
            HRESULT hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &pD3D);
            if (FAILED(hr))
            {
                MyLog(("Direct3DCreate9Ex failed hr 0x%lx\n", hr));
                return NULL;
            }

            memset (pLocalStorage, 0, sizeof (*pLocalStorage));

            hr = pD3D->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, pLocalStorage);

            pD3D->Release();

            if (FAILED(hr))
            {
                MyLog(("GetDeviceCaps failed hr 0x%lx\n", hr));
                return NULL;
            }

            return pLocalStorage;
        }
        case D3DCAPSSOURCE_TYPE_FILE:
        {
            MyLog(("Loading caps from file not implemented yet!"));
            return NULL;
        }
        case D3DCAPSSOURCE_TYPE_NONE:
            return NULL;
        default:
        {
            MyLog(("Unsupported type %d", enmCapsType));
        }
    }

    return NULL;
}

static void vboxUmdDumpDword(DWORD *pvData, DWORD cData)
{
    char aBuf[16*4];
    DWORD dw1, dw2, dw3, dw4;
    for (UINT i = 0; i < (cData & (~3)); i+=4)
    {
        dw1 = *pvData++;
        dw2 = *pvData++;
        dw3 = *pvData++;
        dw4 = *pvData++;
        sprintf(aBuf, "0x%08lx, 0x%08lx, 0x%08lx, 0x%08lx,\n", dw1, dw2, dw3, dw4);
        MyLog(("%s", aBuf));
    }

    cData = cData % 4;
    switch (cData)
    {
        case 3:
            dw1 = *pvData++;
            dw2 = *pvData++;
            dw3 = *pvData++;
            sprintf(aBuf, "0x%08lx, 0x%08lx, 0x%08lx\n", dw1, dw2, dw3);
            MyLog(("%s", aBuf));
            break;
        case 2:
            dw1 = *pvData++;
            dw2 = *pvData++;
            sprintf(aBuf, "0x%08lx, 0x%08lx\n", dw1, dw2);
            MyLog(("%s", aBuf));
            break;
        case 1:
            dw1 = *pvData++;
            sprintf(aBuf, "0x%8lx\n", dw1);
            MyLog(("%s", aBuf));
            break;
        default:
            break;
    }
}

int main()
{
    diffGlExts(g_GlExts1, g_GlExts2);

    if (sizeof (g_aCaps1) != sizeof (D3DCAPS9))
    {
        MyLog(("incorrect caps 1 size (%zd), expected(%zd)\n", sizeof (g_aCaps1), sizeof (D3DCAPS9)));
        return 1;
    }

    if (sizeof (g_aCaps2) != sizeof (D3DCAPS9))
    {
        MyLog(("incorrect caps 2 size (%zd), expected(%zd)\n", sizeof (g_aCaps2), sizeof (D3DCAPS9)));
        return 1;
    }

    D3DCAPS9 Caps1, Caps2;
    D3DCAPS9 *pCaps1, *pCaps2;
    D3DCAPSSOURCE_TYPE enmCaps1 = D3DCAPSSOURCE_TYPE_EMBEDDED1;
    D3DCAPSSOURCE_TYPE enmCaps2 = D3DCAPSSOURCE_TYPE_EMBEDDED2;

    pCaps1 = selectCaps(&Caps1, (D3DCAPS9*)g_aCaps1, (D3DCAPS9*)g_aCaps2, enmCaps1);
    if (!pCaps1)
    {
        MyLog(("Failed to select Caps1"));
        return 1;
    }

    if (D3DCAPSSOURCE_TYPE_NONE != enmCaps2)
    {
        pCaps2 = selectCaps(&Caps2, (D3DCAPS9*)g_aCaps1, (D3DCAPS9*)g_aCaps2, enmCaps2);
        if (!pCaps2)
        {
            MyLog(("Failed to select Caps2"));
            return 1;
        }

        diffCaps((D3DCAPS9*)pCaps1, (D3DCAPS9*)pCaps2);
    }
    else
    {
        vboxUmdDumpDword((DWORD*)pCaps1, sizeof (*pCaps1) / sizeof (DWORD));
    }
    return 0;
}
