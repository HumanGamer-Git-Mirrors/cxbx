// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   Cxbx->Win32->CxbxKrnl->EmuD3D8.cpp
// *
// *  This file is part of the Cxbx project.
// *
// *  Cxbx and Cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file COPYING.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2002-2003 Aaron Robinson <caustik@caustik.com>
// *
// *  All rights reserved
// *
// ******************************************************************
#define _CXBXKRNL_INTERNAL
#define _XBOXKRNL_LOCAL_

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace xboxkrnl
{
    #include <xboxkrnl/xboxkrnl.h>
};

#include "Emu.h"
#include "EmuFS.h"
#include "EmuKrnl.h"
#include "EmuDInput.h"
#include "EmuShared.h"

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace xd3d8
{
    #include "xd3d8.h"
};

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace xg
{
    #include "EmuXG.h"
};

#include "EmuD3D8.h"

#include "ResCxbxDll.h"

#include <process.h>
#include <locale.h>

// ******************************************************************
// * Global(s)
// ******************************************************************
HWND g_hEmuWindow = NULL;   // Rendering Window

// ******************************************************************
// * Static Function(s)
// ******************************************************************
static LRESULT WINAPI EmuMsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
static DWORD WINAPI   EmuRenderWindow(LPVOID);
static DWORD WINAPI   EmuUpdateTickCount(LPVOID);

// ******************************************************************
// * Static Variable(s)
// ******************************************************************
static xd3d8::LPDIRECT3D8       g_pD3D8         = NULL;   // Direct3D8
static xd3d8::LPDIRECT3DDEVICE8 g_pD3DDevice8   = NULL;   // Direct3D8 Device
static DWORD                    g_VertexShaderUsage = 0;  // Vertex Shader Usage Param
static Xbe::Header             *g_XbeHeader     = NULL;   // XbeHeader
static uint32                   g_XbeHeaderSize = 0;      // XbeHeaderSize
static xd3d8::D3DCAPS8          g_D3DCaps;                // Direct3D8 Caps
static HBRUSH                   g_hBgBrush      = NULL;   // Background Brush
static volatile bool            g_ThreadInitialized = false;
static XBVideo                  g_XBVideo;

// ******************************************************************
// * EmuD3DDefferedRenderState
// ******************************************************************
DWORD *xd3d8::EmuD3DDefferedRenderState;

// ******************************************************************
// * func: EmuD3DInit
// ******************************************************************
VOID EmuD3DInit(Xbe::Header *XbeHeader, uint32 XbeHeaderSize)
{
    g_EmuShared->GetXBVideo(&g_XBVideo);

    // ******************************************************************
    // * store XbeHeader and XbeHeaderSize for further use
    // ******************************************************************
    g_XbeHeader     = XbeHeader;
    g_XbeHeaderSize = XbeHeaderSize;

    g_ThreadInitialized = false;

    // ******************************************************************
    // * Create a thread dedicated to timing
    // ******************************************************************
    {
        DWORD dwThreadId;

        CreateThread(NULL, NULL, EmuUpdateTickCount, NULL, NULL, &dwThreadId);
    }

    // ******************************************************************
    // * spark up a new thread to handle window message processing
    // ******************************************************************
    {
        DWORD dwThreadId;

        CreateThread(NULL, NULL, EmuRenderWindow, NULL, NULL, &dwThreadId);

        while(!g_ThreadInitialized)
            Sleep(10);

        Sleep(50);
    }

    // ******************************************************************
    // * create Direct3D8 and retrieve caps
    // ******************************************************************
    {
        using namespace xd3d8;

        // xbox Direct3DCreate8 returns "1" always, so we need our own ptr
        g_pD3D8 = Direct3DCreate8(D3D_SDK_VERSION);

        if(g_pD3D8 == NULL)
            EmuCleanup("Could not initialize Direct3D8!");

        D3DDEVTYPE DevType = (g_XBVideo.GetDirect3DDevice() == 0) ? D3DDEVTYPE_HAL : D3DDEVTYPE_REF;

        g_pD3D8->GetDeviceCaps(g_XBVideo.GetDisplayAdapter(), DevType, &g_D3DCaps);
    }
}

// ******************************************************************
// * func: EmuD3DCleanup
// ******************************************************************
VOID EmuD3DCleanup()
{
    EmuDInputCleanup();

    return;
}

// ******************************************************************
// * func: EmuUpdateTickCount
// ******************************************************************
DWORD WINAPI EmuUpdateTickCount(LPVOID)
{
    timeBeginPeriod(0);

    while(true)
    {
        xboxkrnl::KeTickCount = timeGetTime();
        Sleep(1);
    }

    timeEndPeriod(0);
}

// ******************************************************************
// * func: EmuRenderWindow
// ******************************************************************
DWORD WINAPI EmuRenderWindow(LPVOID)
{
    // ******************************************************************
    // * register window class
    // ******************************************************************
    {
#ifdef _DEBUG
        HMODULE hCxbxDll = GetModuleHandle("CxbxKrnl.dll");
#else
        HMODULE hCxbxDll = GetModuleHandle("Cxbx.dll");
#endif

        LOGBRUSH logBrush = {BS_SOLID, RGB(0,0,0)};

        g_hBgBrush = CreateBrushIndirect(&logBrush);

        WNDCLASSEX wc =
        {
            sizeof(WNDCLASSEX),
            CS_CLASSDC,
            EmuMsgProc,
            0, 0, GetModuleHandle(NULL),
            LoadIcon(hCxbxDll, MAKEINTRESOURCE(IDI_CXBX)),
            LoadCursor(NULL, IDC_ARROW), 
            (HBRUSH)(g_hBgBrush), NULL,
            "CxbxRender",
            NULL
        };

        RegisterClassEx(&wc);
    }

    // ******************************************************************
    // * create the window
    // ******************************************************************
    {
        char AsciiTitle[50];

        // ******************************************************************
        // * retrieve xbe title (if possible)
        // ******************************************************************
        {
            char tAsciiTitle[40] = "Unknown";

            uint32 CertAddr = g_XbeHeader->dwCertificateAddr - g_XbeHeader->dwBaseAddr;

            if(CertAddr + 0x0C + 40 < g_XbeHeaderSize)
            {
                Xbe::Certificate *XbeCert = (Xbe::Certificate*)((uint32)g_XbeHeader + CertAddr);

                setlocale( LC_ALL, "English" );

                wcstombs(tAsciiTitle, XbeCert->wszTitleName, 40);
            }

            sprintf(AsciiTitle, "Cxbx : Emulating %s", tAsciiTitle);
        }

        // ******************************************************************
        // * Create Window
        // ******************************************************************
        {
            DWORD dwStyle = WS_OVERLAPPEDWINDOW;

            int x = 100, y = 100, nWidth = 640, nHeight = 480;

            sscanf(g_XBVideo.GetVideoResolution(), "%d x %d", &nWidth, &nHeight);

            if(g_XBVideo.GetFullscreen())
            {
                x = y = nWidth = nHeight = 0;
                dwStyle = WS_POPUP;
            }

            g_hEmuWindow = CreateWindow
            (
                "CxbxRender", AsciiTitle,
                dwStyle, x, y, nWidth, nHeight,
                GetDesktopWindow(), NULL, GetModuleHandle(NULL), NULL
            );
        }
    }

    ShowWindow(g_hEmuWindow, SW_SHOWDEFAULT);
    UpdateWindow(g_hEmuWindow);

    // ******************************************************************
    // * initialize direct input
    // ******************************************************************
    if(!EmuDInputInit())
        EmuCleanup("Could not initialize DirectInput!");

    // ******************************************************************
    // * message processing loop
    // ******************************************************************
    {
        MSG msg;

        ZeroMemory(&msg, sizeof(msg));

        while(msg.message != WM_QUIT)
        {
            if(PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
            {
                g_ThreadInitialized = true;

                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            else
                Sleep(10);
        }

        EmuCleanup(NULL);
    }

    return 0;
}

// ******************************************************************
// * func: EmuMsgProc
// ******************************************************************
LRESULT WINAPI EmuMsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg)
    {
        case WM_DESTROY:
            DeleteObject(g_hBgBrush);
            PostQuitMessage(0);
            return 0;

        case WM_KEYDOWN:
            if(wParam == VK_ESCAPE)
                PostMessage(hWnd, WM_CLOSE, 0, 0);
            break;

        case WM_CLOSE:
            DestroyWindow(hWnd);
            break;
        case WM_SETCURSOR:
            if(g_XBVideo.GetFullscreen())
            {
                SetCursor(NULL);
                return 0;
            }
            return DefWindowProc(hWnd, msg, wParam, lParam);
        default:
            return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    return 0;
}

// ******************************************************************
// * func: EmuIDirect3D8_CreateDevice
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3D8_CreateDevice
(
    UINT                        Adapter,
    D3DDEVTYPE                  DeviceType,
    HWND                        hFocusWindow,
    DWORD                       BehaviorFlags,
    X_D3DPRESENT_PARAMETERS    *pPresentationParameters,
    IDirect3DDevice8          **ppReturnedDeviceInterface
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3D8_CreateDevice\n"
               "(\n"
               "   Adapter                   : 0x%.08X\n"
               "   DeviceType                : 0x%.08X\n"
               "   hFocusWindow              : 0x%.08X\n"
               "   BehaviorFlags             : 0x%.08X\n"
               "   pPresentationParameters   : 0x%.08X\n"
               "   ppReturnedDeviceInterface : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Adapter, DeviceType, hFocusWindow,
               BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
    }
    #endif

    // ******************************************************************
    // * verify no ugly circumstances
    // ******************************************************************
    if(pPresentationParameters->BufferSurfaces[0] != NULL || pPresentationParameters->DepthStencilSurface != NULL)
        EmuCleanup("EmuIDirect3D8_CreateDevice: BufferSurfaces or DepthStencilSurface != NULL");

    // ******************************************************************
    // * make adjustments to parameters to make sense with windows d3d
    // ******************************************************************
    {
        DeviceType =(g_XBVideo.GetDirect3DDevice() == 0) ? D3DDEVTYPE_HAL : D3DDEVTYPE_REF;
        Adapter    = g_XBVideo.GetDisplayAdapter();

        pPresentationParameters->Windowed = !g_XBVideo.GetFullscreen();

        if(g_XBVideo.GetVSync())
            pPresentationParameters->SwapEffect = D3DSWAPEFFECT_COPY_VSYNC;

        hFocusWindow = g_hEmuWindow;

        pPresentationParameters->BackBufferFormat       = EmuXB2PC_D3DFormat(pPresentationParameters->BackBufferFormat);
        pPresentationParameters->AutoDepthStencilFormat = EmuXB2PC_D3DFormat(pPresentationParameters->AutoDepthStencilFormat);

        // TODO: This should be detected from D3DCAPS8 ? (FrameSkip?)
        pPresentationParameters->FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

        // ******************************************************************
        // * Retrieve Resolution from Configuration
        // ******************************************************************
        if(pPresentationParameters->Windowed)
        {
            D3DDISPLAYMODE D3DDisplayMode;

            sscanf(g_XBVideo.GetVideoResolution(), "%d x %d", &pPresentationParameters->BackBufferWidth, &pPresentationParameters->BackBufferHeight);

            g_pD3D8->GetAdapterDisplayMode(g_XBVideo.GetDisplayAdapter(), &D3DDisplayMode);

            pPresentationParameters->BackBufferFormat = D3DDisplayMode.Format;
            pPresentationParameters->FullScreen_RefreshRateInHz = 0;
        }
        else
        {
            char szBackBufferFormat[16];

            sscanf(g_XBVideo.GetVideoResolution(), "%d x %d %*dbit %s (%d hz)", 
                &pPresentationParameters->BackBufferWidth, 
                &pPresentationParameters->BackBufferHeight,
                szBackBufferFormat,
                &pPresentationParameters->FullScreen_RefreshRateInHz);

            if(strcmp(szBackBufferFormat, "x1r5g5b5") == 0)
                pPresentationParameters->BackBufferFormat = D3DFMT_X1R5G5B5;
            else if(strcmp(szBackBufferFormat, "r5g6r5") == 0)
                pPresentationParameters->BackBufferFormat = D3DFMT_R5G6B5;
            else if(strcmp(szBackBufferFormat, "x8r8g8b8") == 0)
                pPresentationParameters->BackBufferFormat = D3DFMT_X8R8G8B8;
            else if(strcmp(szBackBufferFormat, "a8r8g8b8") == 0)
                pPresentationParameters->BackBufferFormat = D3DFMT_A8R8G8B8;
        }
    }

    // ******************************************************************
    // * Detect vertex processing capabilities
    // ******************************************************************
    if((g_D3DCaps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) && DeviceType == D3DDEVTYPE_HAL)
    {
        #ifdef _DEBUG_TRACE
        printf("EmuD3D8 (0x%X): Using hardware vertex processing\n", GetCurrentThreadId());
        #endif
        BehaviorFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING;
        g_VertexShaderUsage = 0;
    }
    else
    {
        #ifdef _DEBUG_TRACE
        printf("EmuD3D8 (0x%X): Using software vertex processing\n", GetCurrentThreadId());
        #endif
        BehaviorFlags = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
        g_VertexShaderUsage = D3DUSAGE_SOFTWAREPROCESSING;
    }

    // ******************************************************************
    // * redirect to windows d3d
    // ******************************************************************
    HRESULT hRet = g_pD3D8->CreateDevice
    (
        Adapter,
        DeviceType,
        hFocusWindow,
        BehaviorFlags,
        (xd3d8::D3DPRESENT_PARAMETERS*)pPresentationParameters,
        ppReturnedDeviceInterface
    );

    // ******************************************************************
    // * it is necessary to store this pointer globally for emulation
    // ******************************************************************
    g_pD3DDevice8 = *ppReturnedDeviceInterface;

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3D8_GetAdapterDisplayMode
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3D8_GetAdapterDisplayMode
(
    UINT                        Adapter,
    X_D3DDISPLAYMODE           *pMode
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3D8_GetAdapterDisplayMode\n"
               "(\n"
               "   Adapter                   : 0x%.08X\n"
               "   pMode                     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Adapter, pMode);
    }
    #endif

    // ******************************************************************
    // * redirect to windows d3d
    // ******************************************************************
    HRESULT hRet = g_pD3D8->GetAdapterDisplayMode
    (
        g_XBVideo.GetDisplayAdapter(),
        (D3DDISPLAYMODE*)pMode
    );

    // ******************************************************************
    // * make adjustments to parameters to make sense with windows d3d
    // ******************************************************************
    {
        D3DDISPLAYMODE *pPCMode = (D3DDISPLAYMODE*)pMode;

        // Convert Format (PC->Xbox)
        pMode->Format = EmuPC2XB_D3DFormat(pPCMode->Format);

        // TODO: Make this configurable in the future?
        pMode->Flags  = 0x000000A1; // D3DPRESENTFLAG_FIELD | D3DPRESENTFLAG_INTERLACED | D3DPRESENTFLAG_LOCKABLE_BACKBUFFER
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateVertexShader
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DDevice8_CreateVertexShader
(
    CONST DWORD    *pDeclaration,
    CONST DWORD    *pFunction,
    DWORD          *pHandle,
    DWORD           Usage
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreateVertexShader\n"
               "(\n"
               "   pDeclaration        : 0x%.08X\n"
               "   pFunction           : 0x%.08X\n"
               "   pHandle             : 0x%.08X\n"
               "   Usage               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pDeclaration, pFunction, pHandle, Usage);
    }
    #endif

    // ******************************************************************
    // * redirect to windows d3d
    // ******************************************************************
    HRESULT hRet = g_pD3DDevice8->CreateVertexShader
    (
        pDeclaration,
        pFunction,
        pHandle,
        g_VertexShaderUsage // TODO: HACK: Xbox has extensions!
    );

    // hey look, we lied
    hRet = D3D_OK;

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexShaderConstant
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DDevice8_SetVertexShaderConstant
(
    INT         Register,
    CONST PVOID pConstantData,
    DWORD       ConstantCount
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexShaderConstant\n"
               "(\n"
               "   Register            : 0x%.08X\n"
               "   pConstantData       : 0x%.08X\n"
               "   ConstantCount       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Register, pConstantData, ConstantCount);
    }
    #endif

    // ******************************************************************
    // * redirect to windows d3d
    // ******************************************************************
    HRESULT hRet = g_pD3DDevice8->SetVertexShaderConstant
    (
        Register,
        pConstantData,
        ConstantCount
    );

    // hey look, we lied
    hRet = D3D_OK;

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreatePixelShader
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DDevice8_CreatePixelShader
(
    CONST DWORD    *pFunction,
    DWORD          *pHandle
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreatePixelShader\n"
               "(\n"
               "   pFunction           : 0x%.08X\n"
               "   pHandle             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pFunction, pHandle);
    }
    #endif

    // ******************************************************************
    // * redirect to windows d3d
    // ******************************************************************
    HRESULT hRet = g_pD3DDevice8->CreatePixelShader
    (
        pFunction,
        pHandle
    );

    // hey look, we lied
    hRet = D3D_OK;

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateTexture2
// ******************************************************************
xd3d8::X_D3DResource * WINAPI xd3d8::EmuIDirect3DDevice8_CreateTexture2
(
    UINT                Width,
    UINT                Height,
    UINT                Depth,
    UINT                Levels,
    DWORD               Usage,
    D3DFORMAT           Format,
    D3DRESOURCETYPE     D3DResource
)
{
    // TODO: Verify this function's parameters ^

    X_D3DResource *pTexture;

    EmuIDirect3DDevice8_CreateTexture(Width, Height, Levels, Usage, Format, D3DPOOL_MANAGED, &pTexture);

    return pTexture;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateTexture
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DDevice8_CreateTexture
(
    UINT            Width,
    UINT            Height,
    UINT            Levels,
    DWORD           Usage,
    D3DFORMAT       Format,
    D3DPOOL         Pool,
    X_D3DResource **ppTexture
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreateTexture\n"
               "(\n"
               "   Width               : 0x%.08X\n"
               "   Height              : 0x%.08X\n"
               "   Levels              : 0x%.08X\n"
               "   Usage               : 0x%.08X\n"
               "   Format              : 0x%.08X\n"
               "   Pool                : 0x%.08X\n"
               "   ppTexture           : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Width, Height, Levels, Usage, Format, Pool, ppTexture);
    }
    #endif

    // Convert Format (Xbox->PC)
    Format = EmuXB2PC_D3DFormat(Format);

    // Create emulated resource datatype (free'd during intercepted release)
    *ppTexture = new X_D3DResource();

    // ******************************************************************
    // * redirect to windows d3d
    // ******************************************************************
    HRESULT hRet = g_pD3DDevice8->CreateTexture
    (
        Width, Height, Levels, 
        0,   // TODO: Xbox Allows a border to be drawn (maybe hack this in software ;[)
        Format, D3DPOOL_MANAGED, &((*ppTexture)->EmuTexture8)
    );

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTexture
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DDevice8_SetTexture
(
    DWORD           Stage,
    X_D3DResource  *pTexture
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetTexture\n"
               "(\n"
               "   Stage               : 0x%.08X\n"
               "   pTexture            : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Stage, pTexture);
    }
    #endif

    IDirect3DBaseTexture8 *pBaseTexture8 = pTexture->EmuBaseTexture8;
 
    HRESULT hRet = g_pD3DDevice8->SetTexture(Stage, pBaseTexture8);

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_GetDisplayMode
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DDevice8_GetDisplayMode
(
    X_D3DDISPLAYMODE         *pMode
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_GetDisplayMode\n"
               "(\n"
               "   pMode               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pMode);
    }
    #endif

    HRESULT hRet;

    // ******************************************************************
    // * make adjustments to parameters to make sense with windows d3d
    // ******************************************************************
    {
        D3DDISPLAYMODE *pPCMode = (D3DDISPLAYMODE*)pMode;

        hRet = g_pD3DDevice8->GetDisplayMode(pPCMode);

        // Convert Format (PC->Xbox)
        pMode->Format = EmuPC2XB_D3DFormat(pPCMode->Format);

        // TODO: Make this configurable in the future?
        pMode->Flags  = 0x000000A1; // D3DPRESENTFLAG_FIELD | D3DPRESENTFLAG_INTERLACED | D3DPRESENTFLAG_LOCKABLE_BACKBUFFER
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_Clear
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DDevice8_Clear
(
    DWORD           Count,
    CONST D3DRECT  *pRects,
    DWORD           Flags,
    D3DCOLOR        Color,
    float           Z,
    DWORD           Stencil
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_Clear\n"
               "(\n"
               "   Count               : 0x%.08X\n"
               "   pRects              : 0x%.08X\n"
               "   Flags               : 0x%.08X\n"
               "   Color               : 0x%.08X\n"
               "   Z                   : 0x%.08X\n"
               "   Stencil             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Count, pRects, Flags,
               Color, Z, Stencil);
    }
    #endif
    
    // ******************************************************************
    // * make adjustments to parameters to make sense with windows d3d
    // ******************************************************************
    {
        // TODO: D3DCLEAR_TARGET_A, *R, *G, *B don't exist on windows
        DWORD newFlags = 0;

        if(Flags & 0x000000f0l)
            newFlags |= D3DCLEAR_TARGET;

        if(Flags & 0x00000001l)
            newFlags |= D3DCLEAR_ZBUFFER;

        if(Flags & 0x00000002l)
            newFlags |= D3DCLEAR_STENCIL;

        Flags = newFlags;
    }

    HRESULT ret = g_pD3DDevice8->Clear(Count, pRects, Flags, Color, Z, Stencil);

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_Present
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DDevice8_Present
(
    CONST RECT* pSourceRect,
    CONST RECT* pDestRect,
    PVOID       pDummy1,
    PVOID       pDummy2
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_Present\n"
               "(\n"
               "   pSourceRect         : 0x%.08X\n"
               "   pDestRect           : 0x%.08X\n"
               "   pDummy1             : 0x%.08X\n"
               "   pDummy2             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pSourceRect, pDestRect, pDummy1, pDummy2);
    }
    #endif

    HRESULT ret = g_pD3DDevice8->Present(pSourceRect, pDestRect, (HWND)pDummy1, (CONST RGNDATA*)pDummy2);

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_Swap
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DDevice8_Swap
(
    DWORD Flags
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_Swap\n"
               "(\n"
               "   Flags               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Flags);
    }
    #endif

    // TODO: Ensure this flag is always the same across library versions
    if(Flags != 0)
        EmuCleanup("xd3d8::EmuIDirect3DDevice8_Swap: Flags != 0");

    // Swap(0) is equivalent to present(0,0,0,0)
    HRESULT ret = g_pD3DDevice8->Present(0, 0, 0, 0);

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuIDirect3DResource8_Register
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DResource8_Register
(
    X_D3DResource      *pThis,
    PVOID               pBase
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DResource8_Register\n"
               "(\n"
               "   pThis               : 0x%.08X\n"
               "   pBase               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pBase);
    }
    #endif

    HRESULT hRet;

    X_D3DResource *pResource = (X_D3DResource*)pThis;

    // ******************************************************************
    // * Determine the resource type, and initialize
    // ******************************************************************
    switch(pResource->Common & X_D3DCOMMON_TYPE_MASK)
    {
        case X_D3DCOMMON_TYPE_TEXTURE:
        {
            X_D3DPixelContainer *pPixelContainer = (X_D3DPixelContainer*)pResource;

            D3DFORMAT Format;

            // ******************************************************************
            // * Obtain PC D3D Format
            // ******************************************************************
            {
                X_D3DFORMAT XFormat = (X_D3DFORMAT)((pPixelContainer->Format & X_D3DFORMAT_FORMAT_MASK) >> X_D3DFORMAT_FORMAT_SHIFT);

                Format = EmuXB2PC_D3DFormat(XFormat);
            }

            // ******************************************************************
            // * Unswizzle texture, if necessary
            // ******************************************************************
            if(Format != D3DFMT_X8R8G8B8)
                EmuCleanup("Temporarily Unrecognized Format (0x%.08X)", Format);
            else
            {
                // Deswizzle, mofo!
            }

            // ******************************************************************
            // * Create the happy little texture
            // ******************************************************************
            hRet = g_pD3DDevice8->CreateTexture
            (
                // TODO: HACK: These are temporarily FIXED until we deswizzle
                256, 256, 1, 0, Format,
                D3DPOOL_MANAGED, &pResource->EmuTexture8
            );

            // ******************************************************************
            // * Deswizzlorama
            // ******************************************************************
            {
                D3DLOCKED_RECT LockedRect;

                pResource->EmuTexture8->LockRect(0, &LockedRect, NULL, 0);

                RECT  iRect  = {0,0,0,0};
                POINT iPoint = {0,0};

                xg::EmuXGUnswizzleRect
                (
                    pBase, 256, 256, 1, LockedRect.pBits, 
                    LockedRect.Pitch, iRect, iPoint, 4
                );

                pResource->EmuTexture8->UnlockRect(0);
            }
        }
        break;

        default:
            EmuCleanup("IDirect3DResource8::Register Unknown Common Type (%d)", pResource->Common & X_D3DCOMMON_TYPE_MASK);
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DResource8_Release
// ******************************************************************
ULONG WINAPI xd3d8::EmuIDirect3DResource8_Release
(
    X_D3DResource      *pThis
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DResource8_Release\n"
               "(\n"
               "   pThis               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis);
    }
    #endif

    IDirect3DResource8 *pResource8 = pThis->EmuResource8;

    ULONG uRet = pResource8->Release();

    if(uRet == 0)
    {
        printf("Cleaned up a Resource!\n");
        delete pThis;
    }

    EmuSwapFS();   // XBox FS

    return uRet;
}

// ******************************************************************
// * func: EmuIDirect3DSurface8_GetDesc
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DSurface8_GetDesc
(
    X_D3DResource      *pThis,
    X_D3DSURFACE_DESC  *pDesc
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DSurface8_GetDesc\n"
               "(\n"
               "   pThis               : 0x%.08X\n"
               "   pDesc               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pDesc);
    }
    #endif

	D3DSURFACE_DESC SurfaceDesc;

    IDirect3DSurface8 *pSurface8 = pThis->EmuSurface8;

	HRESULT hRet = pSurface8->GetDesc(&SurfaceDesc);

    // ******************************************************************
    // * Rearrange into windows format (remove D3DPool)
    // ******************************************************************
    {
        // Convert Format (PC->Xbox)
        pDesc->Format = EmuPC2XB_D3DFormat(SurfaceDesc.Format);
        pDesc->Type   = SurfaceDesc.Type;

        if(pDesc->Type > 7)
            EmuCleanup("EmuIDirect3DSurface8_GetDesc: pDesc->Type > 7");

        pDesc->Usage  = SurfaceDesc.Usage;
        pDesc->Size   = SurfaceDesc.Size;

        // TODO: Convert from Xbox to PC!!
        if(SurfaceDesc.MultiSampleType == D3DMULTISAMPLE_NONE)
            pDesc->MultiSampleType = (xd3d8::D3DMULTISAMPLE_TYPE)0x0011;
        else
            EmuCleanup("EmuIDirect3DSurface8_GetDesc Unknown Multisample format! (%d)", SurfaceDesc.MultiSampleType);

        pDesc->Width  = SurfaceDesc.Width;
        pDesc->Height = SurfaceDesc.Height;
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DSurface8_LockRect
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DSurface8_LockRect
(
    X_D3DResource      *pThis,
    D3DLOCKED_RECT     *pLockedRect,
    CONST RECT         *pRect,
    DWORD               Flags
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DSurface8_LockRect\n"
               "(\n"
               "   pThis               : 0x%.08X\n"
               "   pLockedRect         : 0x%.08X\n"
               "   pRect               : 0x%.08X\n"
               "   Flags               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pLockedRect, pRect, Flags);
    }
    #endif

    IDirect3DSurface8 *pSurface8 = pThis->EmuSurface8;

    DWORD NewFlags = 0;

    if(Flags & 0x40)
        printf("EmuIDirect3DSurface8_LockRect: *Warning* D3DLOCK_TILED ignored!\n");

    if(Flags & 0x80)
        NewFlags |= D3DLOCK_READONLY;

    if(Flags & !(0x80 | 0x40))
        EmuCleanup("EmuIDirect3DSurface8_LockRect: Unknown Flags! (0x%.08X)", Flags);

    // This is lame, but we have no choice (afaik)
    pSurface8->UnlockRect();

    HRESULT hRet = pSurface8->LockRect(pLockedRect, pRect, NewFlags);

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DBaseTexture8_GetLevelCount
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DBaseTexture8_GetLevelCount
(
    X_D3DBaseTexture   *pThis
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DBaseTexture8_GetLevelCount\n"
               "(\n"
               "   pThis               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis);
    }
    #endif

    IDirect3DBaseTexture8 *pBaseTexture8 = pThis->EmuBaseTexture8;

    HRESULT hRet = pBaseTexture8->GetLevelCount();

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DTexture8_GetSurfaceLevel2
// ******************************************************************
xd3d8::X_D3DResource * WINAPI xd3d8::EmuIDirect3DTexture8_GetSurfaceLevel2
(
    X_D3DTexture   *pThis,
    UINT            Level
)
{
    X_D3DSurface *pSurfaceLevel;

    EmuIDirect3DTexture8_GetSurfaceLevel(pThis, Level, &pSurfaceLevel);

    return pSurfaceLevel;
}

// ******************************************************************
// * func: EmuIDirect3DTexture8_GetSurfaceLevel
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DTexture8_GetSurfaceLevel
(
    X_D3DTexture       *pThis,
    UINT                Level,
    X_D3DSurface      **ppSurfaceLevel
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DTexture8_GetSurfaceLevel\n"
               "(\n"
               "   pThis               : 0x%.08X\n"
               "   Level               : 0x%.08X\n"
               "   ppSurfaceLevel      : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, Level, ppSurfaceLevel);
    }
    #endif

    IDirect3DTexture8 *pTexture8 = pThis->EmuTexture8;

    *ppSurfaceLevel = new X_D3DSurface();

    HRESULT hRet = pTexture8->GetSurfaceLevel(Level, &((*ppSurfaceLevel)->EmuSurface8));

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateVertexBuffer
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DDevice8_CreateVertexBuffer
(
    UINT                Length,
    DWORD               Usage,
    DWORD               FVF,
    D3DPOOL             Pool,
    X_D3DVertexBuffer **ppVertexBuffer
)
{
    *ppVertexBuffer = EmuIDirect3DDevice8_CreateVertexBuffer2(Length);

    return D3D_OK;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_CreateVertexBuffer2
// ******************************************************************
xd3d8::X_D3DVertexBuffer* WINAPI xd3d8::EmuIDirect3DDevice8_CreateVertexBuffer2
(
    UINT Length
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_CreateVertexBuffer2\n"
               "(\n"
               "   Length              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Length);
    }
    #endif

    // Create emulated resource datatype (free'd during intercepted release)
    X_D3DVertexBuffer *pD3DVertexBuffer = new X_D3DVertexBuffer();

    IDirect3DVertexBuffer8 *ppVertexBuffer=NULL;

    HRESULT hRet = g_pD3DDevice8->CreateVertexBuffer
    (
        Length, 
        D3DUSAGE_WRITEONLY,
        0,
        D3DPOOL_DEFAULT, 
        &pD3DVertexBuffer->EmuVertexBuffer8
    );

    EmuSwapFS();   // XBox FS

    return pD3DVertexBuffer;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_CullMode
// ******************************************************************
VOID WINAPI xd3d8::EmuIDirect3DDevice8_SetRenderState_CullMode
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_CullMode\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    // ******************************************************************
    // * Convert from Xbox D3D to PC D3D enumeration
    // ******************************************************************
    // TODO: XDK-Specific Tables? So far they are the same
    switch(Value)
    {
        case 0:
            Value = D3DCULL_NONE;
            break;
        case 0x900:
            Value = D3DCULL_CW;
            break;
        case 0x901:
            Value = D3DCULL_CCW;
            break;
        default:
            EmuCleanup("EmuIDirect3DDevice8_SetRenderState_CullMode: Unknown Cullmode (%d)", Value);
    }

    g_pD3DDevice8->SetRenderState(D3DRS_CULLMODE, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_ZEnable
// ******************************************************************
VOID WINAPI xd3d8::EmuIDirect3DDevice8_SetRenderState_ZEnable
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_ZEnable\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    g_pD3DDevice8->SetRenderState(D3DRS_ZENABLE, Value);

    EmuSwapFS();   // XBox FS

    return;
}
// ******************************************************************
// * func: EmuIDirect3DDevice8_SetRenderState_MultiSampleAntiAlias
// ******************************************************************
VOID WINAPI xd3d8::EmuIDirect3DDevice8_SetRenderState_MultiSampleAntiAlias
(
    DWORD Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetRenderState_MultiSampleAntiAlias\n"
               "(\n"
               "   Value               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Value);
    }
    #endif

    g_pD3DDevice8->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, Value);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetTransform
// ******************************************************************
VOID WINAPI xd3d8::EmuIDirect3DDevice8_SetTransform
(
    D3DTRANSFORMSTATETYPE State,
    CONST D3DMATRIX      *pMatrix
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetTransform\n"
               "(\n"
               "   State               : 0x%.08X\n"
               "   pMatrix             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), State, pMatrix);
    }
    #endif

    // ******************************************************************
    // * Convert from Xbox D3D to PC D3D enumeration
    // ******************************************************************
    // TODO: XDK-Specific Tables? So far they are the same
    if((uint32)State < 2)
        State = (D3DTRANSFORMSTATETYPE)(State + 2);
    else if((uint32)State < 6)
        State = (D3DTRANSFORMSTATETYPE)(State + 14);
    else if((uint32)State < 9)
        State = D3DTS_WORLDMATRIX(State-6);

    g_pD3DDevice8->SetTransform(State, pMatrix);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DVertexBuffer8_Lock
// ******************************************************************
VOID WINAPI xd3d8::EmuIDirect3DVertexBuffer8_Lock
(
    X_D3DVertexBuffer  *ppVertexBuffer,
    UINT                OffsetToLock,
    UINT                SizeToLock,
    BYTE              **ppbData,
    DWORD               Flags
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DVertexBuffer8_Lock\n"
               "(\n"
               "   ppVertexBuffer      : 0x%.08X\n"
               "   OffsetToLock        : 0x%.08X\n"
               "   SizeToLock          : 0x%.08X\n"
               "   ppbData             : 0x%.08X\n"
               "   Flags               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), ppVertexBuffer, OffsetToLock, SizeToLock, ppbData, Flags);
    }
    #endif

    IDirect3DVertexBuffer8 *pVertexBuffer8 = ppVertexBuffer->EmuVertexBuffer8;

    HRESULT hRet = pVertexBuffer8->Lock(OffsetToLock, SizeToLock, ppbData, Flags);

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuIDirect3DVertexBuffer8_Lock2
// ******************************************************************
BYTE* WINAPI xd3d8::EmuIDirect3DVertexBuffer8_Lock2
(
    X_D3DVertexBuffer  *ppVertexBuffer,
    DWORD               Flags
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DVertexBuffer8_Lock2\n"
               "(\n"
               "   ppVertexBuffer      : 0x%.08X\n"
               "   Flags               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), ppVertexBuffer, Flags);
    }
    #endif

    IDirect3DVertexBuffer8 *pVertexBuffer8 = ppVertexBuffer->EmuVertexBuffer8;

    BYTE *pbData = NULL;

    HRESULT hRet = pVertexBuffer8->Lock(0, 0, &pbData, Flags);

    EmuSwapFS();   // XBox FS

    return pbData;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetStreamSource
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DDevice8_SetStreamSource
(
    UINT                StreamNumber,
    X_D3DVertexBuffer  *pStreamData,
    UINT                Stride
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetStreamSource\n"
               "(\n"
               "   StreamNumber        : 0x%.08X\n"
               "   pStreamData         : 0x%.08X\n"
               "   Stride              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), StreamNumber, pStreamData, Stride);
    }
    #endif

    IDirect3DVertexBuffer8 *pVertexBuffer8 = pStreamData->EmuVertexBuffer8;

    pVertexBuffer8->Unlock();

    HRESULT hRet = g_pD3DDevice8->SetStreamSource(StreamNumber, pVertexBuffer8, Stride);

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetVertexShader
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DDevice8_SetVertexShader
(
    DWORD Handle
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetVertexShader\n"
               "(\n"
               "   Handle              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Handle);
    }
    #endif

    HRESULT hRet = g_pD3DDevice8->SetVertexShader(Handle);

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_DrawVertices
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DDevice8_DrawVertices
(
    D3DPRIMITIVETYPE PrimitiveType,
    UINT             StartVertex,
    UINT             VertexCount
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_DrawVertices\n"
               "(\n"
               "   PrimitiveType       : 0x%.08X\n"
               "   StartVertex         : 0x%.08X\n"
               "   VertexCount         : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), PrimitiveType, StartVertex, VertexCount);
    }
    #endif

    // Certain D3DRS values need to be checked on each Draw[Indexed]Vertices
    g_pD3DDevice8->SetRenderState(D3DRS_LIGHTING,              xd3d8::EmuD3DDefferedRenderState[10]);
    g_pD3DDevice8->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE, xd3d8::EmuD3DDefferedRenderState[20]);
    g_pD3DDevice8->SetRenderState(D3DRS_AMBIENT,               xd3d8::EmuD3DDefferedRenderState[23]);

    UINT PrimitiveCount = D3DVertex2PrimitiveCount(PrimitiveType, VertexCount);

    // Convert from Xbox to PC enumeration
    PrimitiveType = EmuPrimitiveType(PrimitiveType);

    HRESULT hRet = g_pD3DDevice8->DrawPrimitive
    (
        PrimitiveType,
        StartVertex,
        PrimitiveCount
    );

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetLight
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DDevice8_SetLight
(
    DWORD            Index,
    CONST D3DLIGHT8 *pLight
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetLight\n"
               "(\n"
               "   Index               : 0x%.08X\n"
               "   pLight              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Index, pLight);
    }
    #endif

    HRESULT hRet = g_pD3DDevice8->SetLight(Index, pLight);

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_SetMaterial
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DDevice8_SetMaterial
(
    CONST D3DMATERIAL8 *pMaterial
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_SetMaterial\n"
               "(\n"
               "   pMaterial           : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pMaterial);
    }
    #endif

    HRESULT hRet = g_pD3DDevice8->SetMaterial(pMaterial);

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirect3DDevice8_LightEnable
// ******************************************************************
HRESULT WINAPI xd3d8::EmuIDirect3DDevice8_LightEnable
(
    DWORD            Index,
    BOOL             bEnable
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuD3D8 (0x%X): EmuIDirect3DDevice8_LightEnable\n"
               "(\n"
               "   Index               : 0x%.08X\n"
               "   bEnable             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Index, bEnable);
    }
    #endif

    HRESULT hRet = g_pD3DDevice8->LightEnable(Index, bEnable);

    EmuSwapFS();   // XBox FS
    
    return hRet;
}
