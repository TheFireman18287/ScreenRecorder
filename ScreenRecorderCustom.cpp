#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dxgi.h>
#include <wrl.h>
#include <chrono>
#include <iostream>
#include <vector>
#include <fstream> // For redirecting std::cout and std::cerr
#include <wincodec.h> // For Windows Imaging Component
#include <d3dcompiler.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#include <chrono>
#include <thread>



//CustomProcedure for SalarWindows
// 
// 
//Window1
LRESULT CALLBACK wndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CLOSE:
        std::cout << "Window closed: " << hWnd << std::endl;
        PostQuitMessage(69);
        break;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}


int CALLBACK WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nCmdShow
) {
     Attach a console for logging
    AllocConsole();
    FILE* consoleOutput;
    freopen_s(&consoleOutput, "CONOUT$", "w", stdout);
    freopen_s(&consoleOutput, "CONOUT$", "w", stderr);

    std::cout << "Console attached. Starting application..." << std::endl;

    const auto pClassName = L"SalarWindowClass";
    //Register window class
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = nullptr;
    wc.hCursor = nullptr;
    wc.hbrBackground = nullptr;
    wc.lpfnWndProc = wndProc;
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = pClassName;
    wc.hIconSm = nullptr;
    RegisterClassEx(&wc);

    //Create window Instance 1
    HWND hWnd1 = CreateWindowEx(
        0, pClassName,
        L"WindowInstance1",
        WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
        200, 200, 2560, 1440,
        nullptr, nullptr, hInstance, nullptr
    );

    //Create window Instance 2
    HWND hWnd2 = CreateWindowEx(
        0, pClassName,
        L"WindowInstance2",
        WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
        2560, 200, 2560, 1440,
        nullptr, nullptr, hInstance, nullptr
    );
    //Show window 1
    ShowWindow(hWnd1, SW_SHOW);


    //Show window 2
    ShowWindow(hWnd2, SW_SHOW);


    //MessageStructure for windows
    MSG msg;
    BOOL gResult;
    while ((gResult = GetMessage(&msg, nullptr, 0, 0)) > 0) {
        std::cout << "Received message for hWnd: " << msg.hwnd << std::endl;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (gResult == 69)
    {
        break;
    }
    if (gResult == -1)
    {
        return -1;
    }
    else
    {
        return msg.wParam;
    }
}
