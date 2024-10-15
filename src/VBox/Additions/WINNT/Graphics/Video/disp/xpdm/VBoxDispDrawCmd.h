/* $Id: VBoxDispDrawCmd.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox XPDM Display driver
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_xpdm_VBoxDispDrawCmd_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_xpdm_VBoxDispDrawCmd_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#define VBVA_DECL_OP(__fn, __args) \
    void vbvaDrv##__fn __args;     \
    void vrdpDrv##__fn __args;

VBVA_DECL_OP(BitBlt, (                 \
    SURFOBJ  *psoTrg,                  \
    SURFOBJ  *psoSrc,                  \
    SURFOBJ  *psoMask,                 \
    CLIPOBJ  *pco,                     \
    XLATEOBJ *pxlo,                    \
    RECTL    *prclTrg,                 \
    POINTL   *pptlSrc,                 \
    POINTL   *pptlMask,                \
    BRUSHOBJ *pbo,                     \
    POINTL   *pptlBrush,               \
    ROP4      rop4                     \
    ));

VBVA_DECL_OP(TextOut, (                \
    SURFOBJ  *pso,                     \
    STROBJ   *pstro,                   \
    FONTOBJ  *pfo,                     \
    CLIPOBJ  *pco,                     \
    RECTL    *prclExtra,               \
    RECTL    *prclOpaque,              \
    BRUSHOBJ *pboFore,                 \
    BRUSHOBJ *pboOpaque,               \
    POINTL   *pptlOrg,                 \
    MIX       mix                      \
    ));

VBVA_DECL_OP(LineTo, (                 \
    SURFOBJ   *pso,                    \
    CLIPOBJ   *pco,                    \
    BRUSHOBJ  *pbo,                    \
    LONG       x1,                     \
    LONG       y1,                     \
    LONG       x2,                     \
    LONG       y2,                     \
    RECTL     *prclBounds,             \
    MIX        mix                     \
    ));

VBVA_DECL_OP(StretchBlt, (             \
    SURFOBJ         *psoDest,          \
    SURFOBJ         *psoSrc,           \
    SURFOBJ         *psoMask,          \
    CLIPOBJ         *pco,              \
    XLATEOBJ        *pxlo,             \
    COLORADJUSTMENT *pca,              \
    POINTL          *pptlHTOrg,        \
    RECTL           *prclDest,         \
    RECTL           *prclSrc,          \
    POINTL          *pptlMask,         \
    ULONG            iMode             \
    ));

VBVA_DECL_OP(CopyBits, (               \
    SURFOBJ  *psoDest,                 \
    SURFOBJ  *psoSrc,                  \
    CLIPOBJ  *pco,                     \
    XLATEOBJ *pxlo,                    \
    RECTL    *prclDest,                \
    POINTL   *pptlSrc                  \
    ));

VBVA_DECL_OP(Paint, (                  \
    SURFOBJ  *pso,                     \
    CLIPOBJ  *pco,                     \
    BRUSHOBJ *pbo,                     \
    POINTL   *pptlBrushOrg,            \
    MIX       mix                      \
    ));

VBVA_DECL_OP(FillPath, (               \
    SURFOBJ  *pso,                     \
    PATHOBJ  *ppo,                     \
    CLIPOBJ  *pco,                     \
    BRUSHOBJ *pbo,                     \
    POINTL   *pptlBrushOrg,            \
    MIX       mix,                     \
    FLONG     flOptions                \
    ));

VBVA_DECL_OP(StrokePath, (             \
    SURFOBJ   *pso,                    \
    PATHOBJ   *ppo,                    \
    CLIPOBJ   *pco,                    \
    XFORMOBJ  *pxo,                    \
    BRUSHOBJ  *pbo,                    \
    POINTL    *pptlBrushOrg,           \
    LINEATTRS *plineattrs,             \
    MIX        mix                     \
    ));

VBVA_DECL_OP(StrokeAndFillPath, (      \
    SURFOBJ   *pso,                    \
    PATHOBJ   *ppo,                    \
    CLIPOBJ   *pco,                    \
    XFORMOBJ  *pxo,                    \
    BRUSHOBJ  *pboStroke,              \
    LINEATTRS *plineattrs,             \
    BRUSHOBJ  *pboFill,                \
    POINTL    *pptlBrushOrg,           \
    MIX        mixFill,                \
    FLONG      flOptions               \
    ))

VBVA_DECL_OP(SaveScreenBits, (         \
    SURFOBJ  *pso,                     \
    ULONG    iMode,                    \
    ULONG_PTR ident,                   \
    RECTL    *prcl                     \
    ))

#undef VBVA_DECL_OP

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_xpdm_VBoxDispDrawCmd_h */
