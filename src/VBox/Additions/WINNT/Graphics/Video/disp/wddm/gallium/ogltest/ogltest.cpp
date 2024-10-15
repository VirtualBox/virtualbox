/* $Id: ogltest.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * OpenGL testcase. Win32 application to run Gallium OpenGL tests.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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

#include "oglrender.h"
#include <iprt/string.h>

PFNGLBINDBUFFERPROC                             glBindBuffer;
PFNGLDELETEBUFFERSPROC                          glDeleteBuffers;
PFNGLGENBUFFERSPROC                             glGenBuffers;
PFNGLBUFFERDATAPROC                             glBufferData;
PFNGLMAPBUFFERPROC                              glMapBuffer;
PFNGLUNMAPBUFFERPROC                            glUnmapBuffer;
PFNGLENABLEVERTEXATTRIBARRAYPROC                glEnableVertexAttribArray;
PFNGLDISABLEVERTEXATTRIBARRAYPROC               glDisableVertexAttribArray;
PFNGLVERTEXATTRIBPOINTERPROC                    glVertexAttribPointer;
PFNGLCREATESHADERPROC                           glCreateShader;
PFNGLATTACHSHADERPROC                           glAttachShader;
PFNGLCOMPILESHADERPROC                          glCompileShader;
PFNGLCREATEPROGRAMPROC                          glCreateProgram;
PFNGLDELETEPROGRAMPROC                          glDeleteProgram;
PFNGLDELETESHADERPROC                           glDeleteShader;
PFNGLDETACHSHADERPROC                           glDetachShader;
PFNGLLINKPROGRAMPROC                            glLinkProgram;
PFNGLSHADERSOURCEPROC                           glShaderSource;
PFNGLUSEPROGRAMPROC                             glUseProgram;
PFNGLGETPROGRAMIVPROC                           glGetProgramiv;
PFNGLGETPROGRAMINFOLOGPROC                      glGetProgramInfoLog;
PFNGLGETSHADERIVPROC                            glGetShaderiv;
PFNGLGETSHADERINFOLOGPROC                       glGetShaderInfoLog;
PFNGLVERTEXATTRIBDIVISORPROC                    glVertexAttribDivisor;
PFNGLDRAWARRAYSINSTANCEDPROC                    glDrawArraysInstanced;

class OGLTest
{
public:
    OGLTest();
    ~OGLTest();

    HRESULT Init(HINSTANCE hInstance, int argc, char **argv, int nCmdShow);
    int Run();

private:
    HRESULT initWindow(HINSTANCE hInstance, int nCmdShow);
    HRESULT initOGL();
    void parseCmdLine(int argc, char **argv);
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void setCurrentGLCtx(HGLRC hGLRC);

    int miRenderId;
    int miRenderStep;

    HWND mHwnd;
    HGLRC mhGLRC;

    OGLRender *mpRender;
};

OGLTest::OGLTest()
    :
    miRenderId(0),
    miRenderStep(1),
    mHwnd(0),
    mhGLRC(0),
    mpRender(0)
{
}

OGLTest::~OGLTest()
{
    if (mpRender)
    {
        delete mpRender;
        mpRender = 0;
    }

    setCurrentGLCtx(NULL);
    wglDeleteContext(mhGLRC);
}

void OGLTest::setCurrentGLCtx(HGLRC hGLRC)
{
    if (hGLRC)
    {
        HDC const hDC = GetDC(mHwnd);
        wglMakeCurrent(hDC, mhGLRC);
        ReleaseDC(mHwnd, hDC);
    }
    else
    {
        wglMakeCurrent(NULL, NULL);
    }
}


LRESULT CALLBACK OGLTest::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

HRESULT OGLTest::initWindow(HINSTANCE hInstance,
                            int nCmdShow)
{
    HRESULT hr = S_OK;

    WNDCLASSA wc = { 0 };
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = wndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIcon(0, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName  = 0;
    wc.lpszClassName = "OGLTestWndClassName";

    if (RegisterClassA(&wc))
    {
       RECT r = {0, 0, 800, 600};
       AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, false);

       mHwnd = CreateWindowA("OGLTestWndClassName",
                             "OGL Test",
                             WS_OVERLAPPEDWINDOW,
                             100, 100, r.right, r.bottom,
                             0, 0, hInstance, 0);
       if (mHwnd)
       {
           ShowWindow(mHwnd, nCmdShow);
           UpdateWindow(mHwnd);
       }
       else
       {
           TestShowError(hr, "CreateWindow");
           hr = E_FAIL;
       }
    }
    else
    {
        TestShowError(hr, "RegisterClass");
        hr = E_FAIL;
    }

    return hr;
}

HRESULT OGLTest::initOGL()
{
    HRESULT hr = S_OK;

    HDC hDC = GetDC(mHwnd);

    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize      = sizeof(pfd);
    pfd.nVersion   = 1;
    pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;

    int pf = ChoosePixelFormat(hDC, &pfd);
    if (pf)
    {
        if (SetPixelFormat(hDC, pf, &pfd))
        {
            DescribePixelFormat(hDC, pf, sizeof(PIXELFORMATDESCRIPTOR), &pfd);

            mhGLRC = wglCreateContext(hDC);
            setCurrentGLCtx(mhGLRC);

/* Get a function address, return VERR_NOT_IMPLEMENTED on failure. */
#define GLGETPROC_(ProcType, ProcName, NameSuffix) do { \
    ProcName = (ProcType)wglGetProcAddress(#ProcName NameSuffix); \
    if (!ProcName) { TestShowError(E_FAIL, #ProcName NameSuffix " missing"); } \
} while(0)

            GLGETPROC_(PFNGLBINDBUFFERPROC                       , glBindBuffer, "");
            GLGETPROC_(PFNGLDELETEBUFFERSPROC                    , glDeleteBuffers, "");
            GLGETPROC_(PFNGLGENBUFFERSPROC                       , glGenBuffers, "");
            GLGETPROC_(PFNGLBUFFERDATAPROC                       , glBufferData, "");
            GLGETPROC_(PFNGLMAPBUFFERPROC                        , glMapBuffer, "");
            GLGETPROC_(PFNGLUNMAPBUFFERPROC                      , glUnmapBuffer, "");
            GLGETPROC_(PFNGLENABLEVERTEXATTRIBARRAYPROC          , glEnableVertexAttribArray, "");
            GLGETPROC_(PFNGLDISABLEVERTEXATTRIBARRAYPROC         , glDisableVertexAttribArray, "");
            GLGETPROC_(PFNGLVERTEXATTRIBPOINTERPROC              , glVertexAttribPointer, "");
            GLGETPROC_(PFNGLCREATESHADERPROC                     , glCreateShader, "");
            GLGETPROC_(PFNGLATTACHSHADERPROC                     , glAttachShader, "");
            GLGETPROC_(PFNGLCOMPILESHADERPROC                    , glCompileShader, "");
            GLGETPROC_(PFNGLCREATEPROGRAMPROC                    , glCreateProgram, "");
            GLGETPROC_(PFNGLDELETEPROGRAMPROC                    , glDeleteProgram, "");
            GLGETPROC_(PFNGLDELETESHADERPROC                     , glDeleteShader, "");
            GLGETPROC_(PFNGLDETACHSHADERPROC                     , glDetachShader, "");
            GLGETPROC_(PFNGLLINKPROGRAMPROC                      , glLinkProgram, "");
            GLGETPROC_(PFNGLSHADERSOURCEPROC                     , glShaderSource, "");
            GLGETPROC_(PFNGLUSEPROGRAMPROC                       , glUseProgram, "");
            GLGETPROC_(PFNGLGETPROGRAMIVPROC                     , glGetProgramiv, "");
            GLGETPROC_(PFNGLGETPROGRAMINFOLOGPROC                , glGetProgramInfoLog, "");
            GLGETPROC_(PFNGLGETSHADERIVPROC                      , glGetShaderiv, "");
            GLGETPROC_(PFNGLGETSHADERINFOLOGPROC                 , glGetShaderInfoLog, "");
            GLGETPROC_(PFNGLVERTEXATTRIBDIVISORPROC              , glVertexAttribDivisor, "");
            GLGETPROC_(PFNGLDRAWARRAYSINSTANCEDPROC              , glDrawArraysInstanced, "");

#undef GLGETPROC_

        }
        else
        {
            TestShowError(hr, "SetPixelFormat");
            hr = E_FAIL;
        }
    }
    else
    {
        TestShowError(hr, "ChoosePixelFormat");
        hr = E_FAIL;
    }

    ReleaseDC(mHwnd, hDC);

    return hr;
}

void OGLTest::parseCmdLine(int argc, char **argv)
{
    /* Very simple: test number followed by step flag.
     * Default is test 0, step mode: 1
     */

    /* First number is the render id. */
    if (argc >= 2)
        miRenderId = RTStrToInt32(argv[1]);

    /* Second number is the step mode. */
    if (argc >= 3)
        miRenderStep = RTStrToInt32(argv[2]);
}

HRESULT OGLTest::Init(HINSTANCE hInstance, int argc, char **argv, int nCmdShow)
{
    parseCmdLine(argc, argv);

    HRESULT hr = initWindow(hInstance, nCmdShow);
    if (SUCCEEDED(hr))
    {
        mpRender = CreateRender(miRenderId);
        if (mpRender)
        {
            hr = initOGL();
            if (SUCCEEDED(hr))
            {
                setCurrentGLCtx(mhGLRC);

                hr = mpRender->InitRender();
                if (FAILED(hr))
                {
                    TestShowError(hr, "InitRender");
                }

                setCurrentGLCtx(NULL);
            }
        }
        else
        {
            hr = E_FAIL;
        }
    }

    return hr;
}

int OGLTest::Run()
{
    bool fFirst = true;
    MSG msg;
    do
    {
        BOOL fGotMessage;
        if (miRenderStep)
        {
            fGotMessage = GetMessageA(&msg, 0, 0, 0);
        }
        else
        {
            fGotMessage = PeekMessageA(&msg, 0, 0, 0, PM_REMOVE);
        }

        if (fGotMessage)
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        float dt = 0.0f; /* Time in seconds since last render step. @todo Measure. */

        BOOL fDoRender = FALSE;
        if (miRenderStep)
        {
            if (msg.message == WM_CHAR)
            {
                if (msg.wParam == ' ')
                {
                    fDoRender = TRUE;
                    dt = fFirst ? 0.0f : 0.1f; /* 0.1 second increment per step. */
                }
            }
        }
        else
        {
             fDoRender = TRUE;
        }

        if (fDoRender)
        {
            if (mpRender)
            {
                setCurrentGLCtx(mhGLRC);

                mpRender->TimeAdvance(dt);
                mpRender->DoRender();

                setCurrentGLCtx(NULL);

                fFirst = false;
            }
        }
    } while (msg.message != WM_QUIT);

    return msg.wParam;
}

int main(int argc, char **argv)
{
    int      rcExit = RTEXITCODE_FAILURE;

    OGLTest test;
    HRESULT hr = test.Init(GetModuleHandleW(NULL), argc, argv, SW_SHOWDEFAULT);
    if (SUCCEEDED(hr))
        rcExit = test.Run();

    return rcExit;
}
