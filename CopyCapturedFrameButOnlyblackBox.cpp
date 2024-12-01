#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>
#include <iostream>

using Microsoft::WRL::ComPtr;

// Global variables
ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<IDXGISwapChain> g_swapChain;
ComPtr<ID3D11RenderTargetView> g_renderTargetView;
ComPtr<IDXGIOutputDuplication> g_outputDuplication;

// Initialize Device and Swap Chain
void CreateDeviceAndSwapChain(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = 1;
    swapChainDesc.BufferDesc.Width = 800;
    swapChainDesc.BufferDesc.Height = 600;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = hwnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.Windowed = TRUE;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &swapChainDesc, &g_swapChain, &g_device, nullptr, &g_context
    );

    if (FAILED(hr)) {
        std::cerr << "Failed to create D3D11 device and swap chain. HRESULT: " << std::hex << hr << std::endl;
        exit(-1);
    }
}

// Initialize Desktop Duplication
void InitializeDesktopDuplication() {
    ComPtr<IDXGIDevice> dxgiDevice;
    g_device.As(&dxgiDevice);

    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(&adapter);

    ComPtr<IDXGIOutput> output;
    adapter->EnumOutputs(0, &output);

    ComPtr<IDXGIOutput1> output1;
    output.As(&output1);

    HRESULT hr = output1->DuplicateOutput(g_device.Get(), &g_outputDuplication);
    if (FAILED(hr)) {
        std::cerr << "Failed to create desktop duplication. HRESULT: " << std::hex << hr << std::endl;
        exit(-1);
    }
}

// Create Render Target View
void CreateRenderTargetView() {
    ComPtr<ID3D11Texture2D> backBuffer;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));

    HRESULT hr = g_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &g_renderTargetView);
    if (FAILED(hr)) {
        std::cerr << "Failed to create render target view. HRESULT: " << std::hex << hr << std::endl;
        exit(-1);
    }

    g_context->OMSetRenderTargets(1, g_renderTargetView.GetAddressOf(), nullptr);
}

// Capture Frame
ComPtr<ID3D11Texture2D> CaptureFrame() {
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    ComPtr<IDXGIResource> desktopResource;

    HRESULT hr = g_outputDuplication->AcquireNextFrame(500, &frameInfo, &desktopResource);
    if (FAILED(hr)) {
        if (hr != DXGI_ERROR_WAIT_TIMEOUT) {
            std::cerr << "Failed to acquire next frame. HRESULT: " << std::hex << hr << std::endl;
        }
        return nullptr;
    }

    ComPtr<ID3D11Texture2D> capturedTexture;
    desktopResource.As(&capturedTexture);

    g_outputDuplication->ReleaseFrame();

    return capturedTexture;
}

// Render Captured Frame
void RenderCapturedFrame() {
    // Capture the frame
    ComPtr<ID3D11Texture2D> frameTexture = CaptureFrame();
    if (!frameTexture) {
        return; // No new frame or error
    }

    // Get the back buffer from the swap chain
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) {
        std::cerr << "Failed to get back buffer from swap chain. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    // Copy the captured frame to the back buffer
    g_context->CopyResource(backBuffer.Get(), frameTexture.Get());

    // Present the frame
    g_swapChain->Present(1, 0);
}


// Window Procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_SIZE:
        if (g_swapChain) {
            // Handle resizing
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

// Main Function
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"ScreenRecorderWindow";

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, wc.lpszClassName, L"Screen Recorder", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, wc.hInstance, nullptr);

    ShowWindow(hwnd, SW_SHOW);

    CreateDeviceAndSwapChain(hwnd);
    InitializeDesktopDuplication();
    CreateRenderTargetView();

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            RenderCapturedFrame();
        }
    }

    return 0;
}
