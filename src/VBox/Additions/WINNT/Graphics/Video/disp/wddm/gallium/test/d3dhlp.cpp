/* $Id: d3dhlp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Gallium D3D testcase. Various D3D helpers.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

#include "d3dhlp.h"
#include <math.h>

/*
 * D3D vector and matrix math helpers.
 */
void d3dMatrixTranspose(D3DMATRIX *pM)
{
    int j; /* Row. Only first 3 because diagonal elements are not swapped, i.e. no need to process [3][3]. */
    for (j = 0; j < 3; ++j)
    {
        int i; /* Column, upper right elements. Skip diagonal element [j][j]. */
        for (i = j + 1; i < 4; ++i)
        {
            /* Swap with corresponding bottom left element. */
            const float tmp = pM->m[j][i];
            pM->m[j][i] = pM->m[i][j];
            pM->m[i][j] = tmp;
        }
    }
}

void d3dMatrixIdentity(D3DMATRIX *pM)
{
    int j; /* Row. */
    for (j = 0; j < 4; ++j)
    {
        int i; /* Column. */
        for (i = 0; i < 4; ++i)
        {
            pM->m[j][i] = j == i ? 1.0f : 0.0f;
        }
    }
}

void d3dMatrixScaleTranslation(D3DMATRIX *pM, const float s, const float dx, const float dy, const float dz)
{
    /*
     * Translation matrix:
     * | s  0  0  0 |
     * | 0  s  0  0 |
     * | 0  0  s  0 |
     * | dx dy dz 1 |
     */
    pM->m[0][0] = s;
    pM->m[0][1] = 0;
    pM->m[0][2] = 0;
    pM->m[0][3] = 0;

    pM->m[1][0] = 0;
    pM->m[1][1] = s;
    pM->m[1][2] = 0;
    pM->m[1][3] = 0;

    pM->m[2][0] = 0;
    pM->m[2][1] = 0;
    pM->m[2][2] = s;
    pM->m[2][3] = 0;

    pM->m[3][0] = dx;
    pM->m[3][1] = dy;
    pM->m[3][2] = dz;
    pM->m[3][3] = 1;
}

void d3dMatrixRotationAxis(D3DMATRIX *pM, const D3DVECTOR *pV, float angle)
{
    /*
     * Rotation matrix:
     * | c+x^2*(1-c)    x*y*(1-c)+z*s  x*z*(1-c)-y*s  0 |
     * | x*y*(1-c)-z*s  c+y^2*(1-c)    y*z*(1-c)+x*s  0 |
     * | x*z*(1-c)+y*s  y*z*(1-c)-x*s  c+z^2*(1-c)    0 |
     * | 0              0              0              1 |
     */
    const float c = cos(angle);
    const float s = sin(angle);
    const float x = pV->x;
    const float y = pV->y;
    const float z = pV->z;

    pM->m[0][0] = c + x*x*(1-c);
    pM->m[0][1] = x*y*(1-c)+z*s;
    pM->m[0][2] = x*z*(1-c)-y*s;
    pM->m[0][3] = 0;

    pM->m[1][0] = x*y*(1-c)-z*s;
    pM->m[1][1] = c+y*y*(1-c);
    pM->m[1][2] = y*z*(1-c)+x*s;
    pM->m[1][3] = 0;

    pM->m[2][0] = x*z*(1-c)+y*s;
    pM->m[2][1] = y*z*(1-c)-x*s;
    pM->m[2][2] = c+z*z*(1-c);
    pM->m[2][3] = 0;

    pM->m[3][0] = 0;
    pM->m[3][1] = 0;
    pM->m[3][2] = 0;
    pM->m[3][3] = 1;
}

void d3dMatrixView(D3DMATRIX *pM, const D3DVECTOR *pR, const D3DVECTOR *pU, const D3DVECTOR *pL, const D3DVECTOR *pP)
{
    /*
     * Camera coordinate system vectors:
     * *pR = r = right = x
     * *pU = u = up    = y
     * *pL = l = look  = z
     * *pP = p = position
     *
     * View matrix:
     * |  r.x  u.x  l.x 0 |
     * |  r.y  u.y  l.y 0 |
     * |  r.z  u.z  l.z 0 |
     * | -pr  -pu  -pl  1 |
     *
     */

    pM->m[0][0] = pR->x;
    pM->m[0][1] = pU->x;
    pM->m[0][2] = pL->x;
    pM->m[0][3] = 0.0f;

    pM->m[1][0] = pR->y;
    pM->m[1][1] = pU->y;
    pM->m[1][2] = pL->y;
    pM->m[1][3] = 0.0f;

    pM->m[2][0] = pR->z;
    pM->m[2][1] = pU->z;
    pM->m[2][2] = pL->z;
    pM->m[2][3] = 0.0f;

    pM->m[3][0] = -d3dVectorDot(pP, pR);
    pM->m[3][1] = -d3dVectorDot(pP, pU);
    pM->m[3][2] = -d3dVectorDot(pP, pL);
    pM->m[3][3] = 1.0f;
}

void d3dMatrixPerspectiveProjection(D3DMATRIX *pM, float verticalFoV, float aspectRatio, float zNear, float zFar)
{
    /*
     * Perspective projection matrix.
     *
     * a = verticalFoV = vertical field of view angle.
     * R = aspectRatio = width / height of the view window.
     * n = zNear = near Z plane
     * f = zFar = far Z plane
     *
     * | 1/(R*tan(a/2)) 0          0          0 |
     * | 0              1/tan(a/2) 0          0 |
     * | 0              0          f/(f-n)    1 |
     * | 0              0          -f*n/(f-n) 0 |
     */
    const float reciprocalTan2 = 1.0f / tan(verticalFoV / 2.0f);
    const float zRange = zFar - zNear;

    pM->m[0][0] = reciprocalTan2 / aspectRatio;
    pM->m[0][1] = 0;
    pM->m[0][2] = 0;
    pM->m[0][3] = 0;

    pM->m[1][0] = 0;
    pM->m[1][1] = reciprocalTan2;
    pM->m[1][2] = 0;
    pM->m[1][3] = 0;

    pM->m[2][0] = 0;
    pM->m[2][1] = 0;
    pM->m[2][2] = zFar / zRange;
    pM->m[2][3] = 1;

    pM->m[3][0] = 0;
    pM->m[3][1] = 0;
    pM->m[3][2] = - zNear * zFar / zRange;
    pM->m[3][3] = 0;
}

void d3dMatrixMultiply(D3DMATRIX *pM, const D3DMATRIX *pM1, const D3DMATRIX *pM2)
{
    /* *pM = *pM1 * *pM2 */
    int j; /* Row. */
    for (j = 0; j < 4; ++j)
    {
        int i; /* Column. */
        for (i = 0; i < 4; ++i)
        {
            /* Row by Column */
            pM->m[j][i] =   pM1->m[j][0] * pM2->m[0][i]
                          + pM1->m[j][1] * pM2->m[1][i]
                          + pM1->m[j][2] * pM2->m[2][i]
                          + pM1->m[j][3] * pM2->m[3][i];
        }
    }
}

void d3dVectorMatrixMultiply(D3DVECTOR *pR, const D3DVECTOR *pV, float w, const D3DMATRIX *pM)
{
    /* Vector row by matrix columns */
    const float x = pV->x;
    const float y = pV->y;
    const float z = pV->z;
    pR->x = x * pM->m[0][0] + y * pM->m[1][0] + z * pM->m[2][0] + w * pM->m[3][0];
    pR->y = x * pM->m[0][1] + y * pM->m[1][1] + z * pM->m[2][1] + w * pM->m[3][1];
    pR->z = x * pM->m[0][2] + y * pM->m[1][2] + z * pM->m[2][2] + w * pM->m[3][2];
}

void d3dVectorNormalize(D3DVECTOR *pV)
{
    const float Length = sqrt(pV->x * pV->x + pV->y * pV->y + pV->z * pV->z);
    if (Length > 0.0f)
    {
        pV->x /= Length;
        pV->y /= Length;
        pV->z /= Length;
    }
}

void d3dVectorCross(D3DVECTOR *pC, const D3DVECTOR *pV1, const D3DVECTOR *pV2)
{
    /*
     * | i    j    k    |
     * | v1.x v1.y v1.z |
     * | v2.x v2.y v2.z |
     */
    pC->x =  pV1->y * pV2->z - pV2->y  * pV1->z;
    pC->y = -pV1->x * pV2->z + pV2->x  * pV1->z;
    pC->z =  pV1->x * pV2->y - pV2->x  * pV1->y;
}


float d3dVectorDot(const D3DVECTOR *pV1, const D3DVECTOR *pV2)
{
    return pV1->x * pV2->x + pV1->y * pV2->y + pV1->z * pV2->z;
}

void d3dVectorInit(D3DVECTOR *pV, float x, float y, float z)
{
    pV->x = x;
    pV->y = y;
    pV->z = z;
}


/*
 * Helper to compute view and projection matrices for a camera.
 */
D3DCamera::D3DCamera()
{
    d3dMatrixIdentity(&mView);
    d3dMatrixIdentity(&mProjection);
    d3dMatrixIdentity(&mViewProjection);

    d3dVectorInit(&mPosition, 0.0f, 0.0f, 0.0f);
    d3dVectorInit(&mRight,    1.0f, 0.0f, 0.0f);
    d3dVectorInit(&mUp,       0.0f, 1.0f, 0.0f);
    d3dVectorInit(&mLook,     0.0f, 0.0f, 1.0f);

    mTime = 0.0f;
}

const D3DMATRIX *D3DCamera::ViewProjection(void)
{
    return &mViewProjection;
}

void D3DCamera::SetupAt(const D3DVECTOR *pPos, const D3DVECTOR *pAt, const D3DVECTOR *pUp)
{
    mLook.x = pAt->x - pPos->x;
    mLook.y = pAt->y - pPos->y;
    mLook.z = pAt->z - pPos->z;
    d3dVectorNormalize(&mLook);

    d3dVectorCross(&mRight, pUp, &mLook);
    d3dVectorNormalize(&mRight);

    d3dVectorCross(&mUp, &mLook, &mRight);
    d3dVectorNormalize(&mUp);

    mPosition = *pPos;

    computeView();
    d3dMatrixMultiply(&mViewProjection, &mView, &mProjection);
}

void D3DCamera::SetProjection(float verticalFoV, float aspectRatio, float zNear, float zFar)
{
    d3dMatrixPerspectiveProjection(&mProjection, verticalFoV, aspectRatio, zNear, zFar);
    d3dMatrixMultiply(&mViewProjection, &mView, &mProjection);
}

void D3DCamera::TimeAdvance(float dt)
{
    mTime += dt;

    const float xAngleCam = 3.14f / 4.0f * sin(mTime * 3.14f / 9.0f); /* About camera X axis. */
    const float yAngleW = mTime * 3.14f / 4.0f; /* Rotate about world Y axis. */

    const D3DVECTOR xAxis = { 1.0f, 0.0f, 0.0f };
    const D3DVECTOR yAxis = { 0.0f, 1.0f, 0.0f };
    const D3DVECTOR zAxis = { 0.0f, 0.0f, 1.0f };

    /* Start from scratch. */
    mRight = xAxis;
    mUp    = yAxis;
    mLook  = zAxis;

    D3DMATRIX R;

    /* Rotate the camera up and look vectors about the right vector. */
    d3dMatrixRotationAxis(&R, &mRight, xAngleCam);
    d3dVectorMatrixMultiply(&mUp,   &mUp,   0.0f, &R);
    d3dVectorMatrixMultiply(&mLook, &mLook, 0.0f, &R);

    /* Rotate camera axes about the world Y axis. */
    d3dMatrixRotationAxis(&R, &yAxis, yAngleW);
    d3dVectorMatrixMultiply(&mRight, &mRight, 0.0f, &R);
    d3dVectorMatrixMultiply(&mUp,    &mUp,    0.0f, &R);
    d3dVectorMatrixMultiply(&mLook,  &mLook,  0.0f, &R);

    computeView();
    d3dMatrixMultiply(&mViewProjection, &mView, &mProjection);
}

void D3DCamera::computeView(void)
{
    /* Vectors of the ñamera coordinate system must be orthonormal. */
    d3dVectorNormalize(&mLook);

    d3dVectorCross(&mUp, &mLook, &mRight);
    d3dVectorNormalize(&mUp);

    d3dVectorCross(&mRight, &mUp, &mLook);
    d3dVectorNormalize(&mRight);

    d3dMatrixView(&mView, &mRight, &mUp, &mLook, &mPosition);
}

/* Create and initialize IDirect3DCubeTexture9. */
HRESULT d3dCreateCubeTexture(IDirect3DDevice9 *pDevice, IDirect3DCubeTexture9 **ppCubeTexture)
{
    HRESULT hr = S_OK;

    /* Create a texture in memory. Test transfer D3DPOOL_SYSTEMMEM -> D3DPOOL_DEFAULT */
    UINT      EdgeLength = 256;
    UINT      Levels = 8; /* Greater than number of faces. */
    DWORD     Usage = 0;
    D3DFORMAT Format = D3DFMT_A8R8G8B8;
    D3DPOOL   Pool = D3DPOOL_SYSTEMMEM;

    IDirect3DCubeTexture9 *pMemTex = NULL;
    HTEST(pDevice->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, &pMemTex, NULL));

    /* Initialize texture content. */
    int iFace;
    for (iFace = 0; iFace < 6; ++iFace)
    {
        DWORD Color;
        D3DCUBEMAP_FACES Face;
        switch (iFace)
        {
        case 0: Face = D3DCUBEMAP_FACE_POSITIVE_X; Color = 0xfff0f0f0; break; /* Almost white */
        case 1: Face = D3DCUBEMAP_FACE_NEGATIVE_X; Color = 0xff7f7f7f; break; /* Gray */
        case 2: Face = D3DCUBEMAP_FACE_POSITIVE_Y; Color = 0xff0000ff; break; /* Blue */
        case 3: Face = D3DCUBEMAP_FACE_NEGATIVE_Y; Color = 0xff00007f; break; /* Darker blue */
        case 4: Face = D3DCUBEMAP_FACE_POSITIVE_Z; Color = 0xff00ff00; break; /* Green */
        default:
        case 5: Face = D3DCUBEMAP_FACE_NEGATIVE_Z; Color = 0xff007f00; break; /* Darker green */
        }

        UINT Level;
        for (Level = 0; Level < Levels; ++Level)
        {
            IDirect3DSurface9 *pCubeMapSurface = NULL;
            HTEST(pMemTex->GetCubeMapSurface(Face, Level, &pCubeMapSurface));

            D3DSURFACE_DESC Desc;
            HTEST(pCubeMapSurface->GetDesc(&Desc));

            D3DLOCKED_RECT LockedRect;
            HTEST(pCubeMapSurface->LockRect(&LockedRect, NULL, D3DLOCK_DISCARD));

            UINT y;
            BYTE *pb = (BYTE *)LockedRect.pBits;
            for (y = 0; y < Desc.Height; ++y)
            {
                UINT x;
                for (x = 0; x < Desc.Width; ++x)
                {
                    DWORD *pdw = (DWORD *)pb;
                    pdw[x] = Color;
                }
                pb += LockedRect.Pitch;
            }

            HTEST(pCubeMapSurface->UnlockRect());

            pCubeMapSurface->Release();
        }
    }

    /* Create actual texture. */
    Pool = D3DPOOL_DEFAULT;

    IDirect3DCubeTexture9 *pCubeTex = NULL;
    HTEST(pDevice->CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, &pCubeTex, NULL));

    /* Copy texture content. */
    HTEST(pDevice->UpdateTexture(pMemTex, pCubeTex));

    /* Do not need the bounce texture anymore. */
    pMemTex->Release();

    *ppCubeTexture = pCubeTex;
    return hr;
}

/* Create IDirect3DVertexBuffer9 with vertices for a cube. */
HRESULT d3dCreateCubeVertexBuffer(IDirect3DDevice9 *pDevice, float EdgeLength, IDirect3DVertexBuffer9 **ppVertexBuffer)
{
    /* Create vertices of a cube (no indexing). Layout: 3x float.
     * Arbitrary winding order, will use D3DRS_CULLMODE = D3DCULL_NONE.
     */

    static float aVertices[6 * 6 * 3] =
    {
        /* POSITIVE_X */
        /* Triangle 1 */
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        /* Triangle 2 */
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f, -1.0f,

        /* NEGATIVE_X */
        /* Triangle 1 */
        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        /* Triangle 2 */
        -1.0f, -1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,

        /* POSITIVE_Y */
        /* Triangle 1 */
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        /* Triangle 2 */
        -1.0f,  1.0f, -1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f, -1.0f,

        /* NEGATIVE_Y */
        /* Triangle 1 */
        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
        /* Triangle 2 */
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f, -1.0f, -1.0f,

        /* POSITIVE_Z */
        /* Triangle 1 */
         1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        /* Triangle 2 */
         1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,

        /* NEGATIVE_Z */
        /* Triangle 1 */
         1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        /* Triangle 2 */
         1.0f, -1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
    };

    HRESULT hr = S_OK;

    IDirect3DVertexBuffer9 *pVB = NULL;
    HTEST(pDevice->CreateVertexBuffer(6 * 6 * 3 * sizeof(float), D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &pVB, 0));

    void *pv = 0;
    HTEST(pVB->Lock(0, 0, &pv, 0));

    const int cFloats = sizeof(aVertices)/sizeof(aVertices[0]);
    float *p = (float *)pv;
    int i;
    for (i = 0; i < cFloats; ++i)
    {
        p[i] = aVertices[i] * EdgeLength / 2.0f;
    }

    HTEST(pVB->Unlock());

    *ppVertexBuffer = pVB;
    return S_OK;
}
