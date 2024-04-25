#include <pce-go/pce-go.h>
#if !PICO_ON_DEVICE
#include "MiniFB.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static WNDCLASS s_wc;
static HWND s_wnd;
static int s_close = 0;
static int s_width;
static int s_height;
static int s_scale = 1;
static HDC s_hdc;
static void* s_buffer;
BITMAPINFO* s_bitmapInfo;
static char key_status[512] = {};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    LRESULT res = 0;

    switch (message) {
        case WM_PAINT: {
            if (s_buffer) {
                StretchDIBits(s_hdc,
                    0, 0, s_width * s_scale, s_height * s_scale,
                    0, 0, s_width, s_height,
                    s_buffer,
                              s_bitmapInfo, DIB_RGB_COLORS, SRCCOPY);

                ValidateRect(hWnd, NULL);
            }

            break;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            key_status[wParam] = !(lParam >> 31 & 1);

            if ((wParam & 0xFF) == 27)
                s_close = 1;

            break;
        }

        case WM_CLOSE: {
            s_close = 1;
            break;
        }

        default: {
            res = DefWindowProc(hWnd, message, wParam, lParam);
        }
    }

    return res;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int mfb_open(const char* title, int width, int height, int scale) {
    RECT rect = { 0 };

    s_wc.style = CS_OWNDC | CS_VREDRAW | CS_HREDRAW ;
    s_wc.lpfnWndProc = WndProc;
    s_wc.hCursor = LoadCursor(0, IDC_ARROW);
    s_wc.lpszClassName = title;

    RegisterClass(&s_wc);

    rect.right = 256 * scale;
    rect.bottom = 240 * scale;

    AdjustWindowRect(&rect, WS_POPUP | WS_SYSMENU | WS_CAPTION , 0);

    rect.right -= rect.left;
    rect.bottom -= rect.top;

    s_width = 256;
    s_height = 240;
    s_scale = scale;

    s_wnd = CreateWindowEx(0,
                           title, title,
                           WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
                           (GetSystemMetrics(SM_CXSCREEN) - rect.right) / 2,
                           (GetSystemMetrics(SM_CYSCREEN) - rect.bottom + rect.top) / 2,
                           rect.right, rect.bottom,
                           0, 0, 0, 0);

    if (!s_wnd)
        return 0;

    ShowWindow(s_wnd, SW_NORMAL);

    s_bitmapInfo = (BITMAPINFO*)malloc(sizeof(BITMAPINFO) + sizeof(RGBQUAD) * 256);
    s_bitmapInfo->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    s_bitmapInfo->bmiHeader.biBitCount = 8;
    s_bitmapInfo->bmiHeader.biPlanes = 1;
    s_bitmapInfo->bmiHeader.biWidth = width;
    s_bitmapInfo->bmiHeader.biHeight = -height;
    s_bitmapInfo->bmiHeader.biCompression = 0;
    s_bitmapInfo->bmiHeader.biSizeImage = 0;
    // s_bitmapInfo->bmiHeader.biXPelsPerMeter = 14173;
    // s_bitmapInfo->bmiHeader.biYPelsPerMeter = 14173;
    s_bitmapInfo->bmiHeader.biClrUsed = 256;
    s_bitmapInfo->bmiHeader.biClrImportant = 1;

    RGBQUAD* palette = &s_bitmapInfo->bmiColors[0];
    // for (int i = 0; i < 256; ++i)
    // {
    //     RGBQUAD rgb = {0};
    //     rgb.rgbRed =  ~i;
    //     rgb.rgbGreen =  ~i;
    //     rgb.rgbBlue =  ~i;
    //     palette[i] = rgb;
    // }
    for (int i = 0; i < 256; i++) {
            uint8_t *ptr = &palette[i];
            *ptr++ = ((i & 0x03) << 2) * 16;
            *ptr++ = ((i & 0xe0) >> 4) * 16;
            *ptr = ((i & 0x1C) >> 1) * 16;
    }



/*
    s_bitmapInfo->bmiHeader.biBitCount = 16;
    s_bitmapInfo->bmiHeader.biCompression = BI_BITFIELDS;

    ((DWORD *)s_bitmapInfo->bmiColors)[0] = 0xF800;
    ((DWORD *)s_bitmapInfo->bmiColors)[1] = 0x07E0;
    ((DWORD *)s_bitmapInfo->bmiColors)[2] = 0x001F;
*/
    // ((DWORD *)s_bitmapInfo->bmiColors)[0] = 0x001F;
    // ((DWORD *)s_bitmapInfo->bmiColors)[1] = 0x001F;
    // ((DWORD *)s_bitmapInfo->bmiColors)[2] = 0x001F;
    s_hdc = GetDC(s_wnd);

    return 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int mfb_resize(int width, int height) {
    s_width = width;
    s_height = height;
    SetWindowPos(s_wnd,0,0,0,s_width * s_scale,s_height * s_scale,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
}
int mfb_update(void* buffer, int fps_limit) {
    static DWORD previousFrameTime = 0;
    MSG msg;

    s_buffer = buffer;

    InvalidateRect(s_wnd, NULL, TRUE);
    SendMessage(s_wnd, WM_PAINT, 0, 0);

    while (PeekMessage(&msg, s_wnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (s_close == 1)
        return -1;

    if (fps_limit) {
        const DWORD targetFrameTime = 1000 / fps_limit;
        const DWORD elapsedFrameTime = GetTickCount() - previousFrameTime;

        if (elapsedFrameTime < targetFrameTime) {
            Sleep(targetFrameTime - elapsedFrameTime);
        }

        previousFrameTime = GetTickCount();
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void mfb_close() {
    s_buffer = 0;
    free(s_bitmapInfo);
    ReleaseDC(s_wnd, s_hdc);
    DestroyWindow(s_wnd);
}

char * mfb_keystatus() {
    return key_status;
}
#endif