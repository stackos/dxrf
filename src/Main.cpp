#include <Windows.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>

#include "DeviceResources.h"

using namespace DX;

static int g_minimized = 0;
static int g_width = 0;
static int g_height = 0;
static DWORD g_time = 0;
static int g_frame_count = 0;
std::unique_ptr<DeviceResources> g_device;

class DeviceNotify : public DX::IDeviceNotify
{
    virtual void OnDeviceLost() override
    {
    
    }

    virtual void OnDeviceRestored() override
    {
        
    }
};
DeviceNotify g_device_notify;

static void Init(HWND hwnd, int width, int height)
{
    g_width = width;
    g_height = height;

    g_device = std::make_unique<DeviceResources>(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_UNKNOWN,
        3,
        D3D_FEATURE_LEVEL_11_0,
        // Sample shows handling of use cases with tearing support, which is OS dependent and has been supported since TH2.
        // Since the sample requires build 1809 (RS5) or higher, we don't need to handle non-tearing cases.
        DeviceResources::c_RequireTearingSupport,
        UINT_MAX);
    g_device->RegisterDeviceNotify(&g_device_notify);
    g_device->SetWindow(hwnd, width, height);
    g_device->InitializeDXGIAdapter();

    ThrowIfFalse(IsDirectXRaytracingSupported(g_device->GetAdapter()),
        "ERROR: DirectX Raytracing is not supported by your OS, GPU and/or driver.\n\n");

    g_device->CreateDeviceResources();
    g_device->CreateWindowSizeDependentResources();
}

static void Done()
{
    g_device.reset();
}

static void DrawFrame()
{
    
}

static LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED)
            {
                g_minimized = 1;
            }
            else
            {
                if (!g_minimized)
                {
                    int width = lParam & 0xffff;
                    int height = (lParam & 0xffff0000) >> 16;

                    if (g_width != width || g_height != height)
                    {
                        g_width = width;
                        g_height = height;
                    }
                }

                g_minimized = 0;
            }
            break;

        case WM_CLOSE:
            Done();
            DestroyWindow(hWnd);
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            break;
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nShowCmd)
{
    const char* name = "dx12_ray_tracing_framework";
    int window_width = 1280;
    int window_height = 720;

    WNDCLASSEX win_class;
    ZeroMemory(&win_class, sizeof(win_class));

    win_class.cbSize = sizeof(WNDCLASSEX);
    win_class.style = CS_HREDRAW | CS_VREDRAW;
    win_class.lpfnWndProc = WindowProc;
    win_class.cbClsExtra = 0;
    win_class.cbWndExtra = 0;
    win_class.hInstance = hInstance;
    win_class.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
    win_class.lpszMenuName = NULL;
    win_class.lpszClassName = name;
    win_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    win_class.hIcon = NULL;
    win_class.hIconSm = NULL;

    if (!RegisterClassEx(&win_class))
    {
        return 0;
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    DWORD style_ex = 0;

    RECT wr = { 0, 0, window_width, window_height };
    AdjustWindowRect(&wr, style, FALSE);

    HWND hwnd = NULL;

    {
        int x = (GetSystemMetrics(SM_CXSCREEN) - window_width) / 2 + wr.left;
        int y = (GetSystemMetrics(SM_CYSCREEN) - window_height) / 2 + wr.top;
        int w = wr.right - wr.left;
        int h = wr.bottom - wr.top;

        hwnd = CreateWindowEx(
            style_ex,   // window ex style
            name,       // class name
            name,       // app name
            style,      // window style
            x, y,       // x, y
            w, h,       // w, h
            NULL,    // handle to parent
            NULL,    // handle to menu
            hInstance,  // hInstance
            NULL);   // no extra parameters
    }

    if (!hwnd)
    {
        return 0;
    }

    Init(hwnd, window_width, window_height);

    ShowWindow(hwnd, SW_SHOW);

    int exit = 0;
    MSG msg;

    while (1)
    {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                exit = 1;
                break;
            }
            else
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        if (exit)
        {
            break;
        }

        if (!g_minimized)
        {
            DWORD t = timeGetTime();
            if (t - g_time > 1000)
            {
                int fps = g_frame_count;
                g_frame_count = 0;
                g_time = t;

                char title[1024];
                sprintf(title, "%s [w: %d h: %d] [fps: %d]", name, g_width, g_height, fps);
                SetWindowText(hwnd, title);
            }
            g_frame_count += 1;

            DrawFrame();
        }
    }

    return 0;
}
