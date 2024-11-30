#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl.h>
#include <iostream>
#include <io.h>
#include <fcntl.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")






// Add this to enable console logging
void EnableDebugConsole() {
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$", "r", stdin);

    std::cout << "Debug console attached." << std::endl;
}
using Microsoft::WRL::ComPtr;

// Global Variables
HWND g_hwnd;
int g_windowWidth = 1280;
int g_windowHeight = 720;
ComPtr<ID3D11Texture2D> g_resizedTexture; // For resizing
ComPtr<ID3D11Texture2D> g_intermediateTexture; // For intermediate texture
ComPtr<ID3D11Texture2D> leftTexture;
ComPtr<ID3D11Texture2D> rightTexture;
ComPtr<IDXGISwapChain> leftSwapChain;
ComPtr<IDXGISwapChain> rightSwapChain;

// Use DXGI to enumerate displays and create separate swap chains for each

HWND CreateRenderWindow(HMONITOR monitor, const wchar_t* title) {
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"DirectXWindow";

    RegisterClass(&wc);

    // Create a window on the specific monitor
    RECT monitorRect;
    MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
    if (GetMonitorInfo(monitor, &monitorInfo)) {
        monitorRect = monitorInfo.rcMonitor;
    }
    else {
        monitorRect = { 0, 0, 2560, 1440 }; // Default to 2560x1440
    }

    HWND hwnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        title,
        WS_OVERLAPPEDWINDOW,
        monitorRect.left,
        monitorRect.top,
        monitorRect.right - monitorRect.left,
        monitorRect.bottom - monitorRect.top,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr
    );

    ShowWindow(hwnd, SW_SHOW);
    return hwnd;
}


void CreateSwapChainForMonitor(
    ComPtr<IDXGIOutput> output,
    ComPtr<IDXGISwapChain>& swapChain,
    const wchar_t* windowTitle
) {
    // Get the output's description
    DXGI_OUTPUT_DESC desc;
    output->GetDesc(&desc);

    // Create a simple window for this display
    HWND hwnd = CreateRenderWindow(desc.Monitor, windowTitle);

    // Define the swap chain description
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = 1;
    swapChainDesc.BufferDesc.Width = 2560;  // Half the width
    swapChainDesc.BufferDesc.Height = 1440; // Full height
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = hwnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = TRUE; // Fullscreen can be configured if needed
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    // Create the swap chain
    HRESULT hr = factory->CreateSwapChain(g_device.Get(), &swapChainDesc, &swapChain);
    if (FAILED(hr)) {
        std::cerr << "Failed to create swap chain for monitor. HRESULT: " << std::hex << hr << std::endl;
    }
    else {
        std::wcout << L"Swap chain created for monitor: " << desc.DeviceName << std::endl;
    }
}



void InitializeSwapChains() {
    // Create a DXGI Factory
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        std::cerr << "Failed to create DXGI factory. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    // Enumerate adapters
    ComPtr<IDXGIAdapter> adapter;
    for (UINT adapterIndex = 0; factory->EnumAdapters(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND; ++adapterIndex) {
        ComPtr<IDXGIOutput> output;

        // Enumerate outputs (monitors)
        for (UINT outputIndex = 0; adapter->EnumOutputs(outputIndex, &output) != DXGI_ERROR_NOT_FOUND; ++outputIndex) {
            DXGI_OUTPUT_DESC desc;
            output->GetDesc(&desc);
            std::wcout << L"Monitor found: " << desc.DeviceName << std::endl;

            // For simplicity, we'll just use the first two monitors
            if (outputIndex == 0) {
                CreateSwapChainForMonitor(output, leftSwapChain, L"Left Display");
            }
            else if (outputIndex == 1) {
                CreateSwapChainForMonitor(output, rightSwapChain, L"Right Display");
            }
        }
    }
}









// Forward declarations
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
bool InitializeWindow(HINSTANCE hInstance, int nCmdShow);
bool InitializeDirect3D11();
bool InitializeCaptureResources();
void RenderFrame();
void UpdateTexture(ID3D11Texture2D* frame);

// Direct3D 11 Components
ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<IDXGISwapChain> g_swapChain;
ComPtr<ID3D11RenderTargetView> g_renderTargetView;
ComPtr<ID3D11Texture2D> g_gpuTexture; // GPU-compatible texture
ComPtr<IDXGIOutputDuplication> g_duplication;

// Initialize the window
bool InitializeWindow(HINSTANCE hInstance, int nCmdShow) {
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"FrameCaptureWindow";

    if (!RegisterClass(&wc)) {
        std::cerr << "Failed to register window class." << std::endl;
        return false;
    }

    RECT rect = { 0, 0, g_windowWidth, g_windowHeight };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    g_hwnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        L"Frame Capture Renderer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!g_hwnd) {
        std::cerr << "Failed to create window." << std::endl;
        return false;
    }

    ShowWindow(g_hwnd, nCmdShow);
    return true;
}

// Initialize Direct3D 11
bool InitializeDirect3D11() {
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = 1;
    swapChainDesc.BufferDesc.Width = g_windowWidth;
    swapChainDesc.BufferDesc.Height = g_windowHeight;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = g_hwnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.Windowed = TRUE;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &swapChainDesc,
        g_swapChain.GetAddressOf(),
        g_device.GetAddressOf(),
        &featureLevel,
        g_context.GetAddressOf()
    );

    if (FAILED(hr)) {
        std::cerr << "Failed to create Direct3D 11 device and swap chain. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    // Create render target view
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) {
        std::cerr << "Failed to get back buffer. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    hr = g_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &g_renderTargetView);
    if (FAILED(hr)) {
        std::cerr << "Failed to create render target view. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    g_context->OMSetRenderTargets(1, g_renderTargetView.GetAddressOf(), nullptr);
    return true;
}

// Initialize capture resources
bool InitializeCaptureResources() {
    HRESULT hr; // Declare once at the top
    ComPtr<IDXGIDevice> dxgiDevice;
    g_device.As(&dxgiDevice);

    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetParent(IID_PPV_ARGS(&adapter));

    ComPtr<IDXGIOutput> output;
    adapter->EnumOutputs(0, &output);

    ComPtr<IDXGIOutput1> output1;
    output.As(&output1);

    hr = output1->DuplicateOutput(g_device.Get(), &g_duplication);
    if (FAILED(hr)) {
        std::cerr << "Failed to duplicate output. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }
=




    // Create a GPU-compatible texture for rendering
    D3D11_TEXTURE2D_DESC resizedDesc = {};
    resizedDesc.Width = g_windowWidth; // Match window size
    resizedDesc.Height = g_windowHeight;
    resizedDesc.MipLevels = 1;
    resizedDesc.ArraySize = 1;
    resizedDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // Match captured frame format
    resizedDesc.SampleDesc.Count = 1;
    resizedDesc.Usage = D3D11_USAGE_DEFAULT;
    resizedDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    resizedDesc.CPUAccessFlags = 0;
    resizedDesc.MiscFlags = 0;




    hr = g_device->CreateTexture2D(&resizedDesc, nullptr, &g_resizedTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to create resized texture. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }


    std::cout << "Resized texture created successfully." << std::endl;
    return true;


    D3D11_TEXTURE2D_DESC halfDesc = {};
    halfDesc.Width = 2560;               // Half the width of the original frame
    halfDesc.Height = 1440;              // Full height
    halfDesc.MipLevels = 1;
    halfDesc.ArraySize = 1;
    halfDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    halfDesc.SampleDesc.Count = 1;
    halfDesc.Usage = D3D11_USAGE_DEFAULT;
    halfDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    hr = g_device->CreateTexture2D(&halfDesc, nullptr, &leftTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to create left texture. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    hr = g_device->CreateTexture2D(&halfDesc, nullptr, &rightTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to create right texture. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    std::cout << "Textures for splitting created successfully." << std::endl;

}

// Update the texture with the captured frame
void UpdateTexture(ID3D11Texture2D* frame) {

    D3D11_TEXTURE2D_DESC capturedDesc;
    frame->GetDesc(&capturedDesc);
    std::cout << "Captured frame format: " << capturedDesc.Format << std::endl;

    if (capturedDesc.Format == 10) {
        std::cerr << "Captured frame format 10 is unsupported. Skipping." << std::endl;
        return;
    }

    // Handle frame formats and copy to the intermediate texture
    if (capturedDesc.Format == DXGI_FORMAT_NV12) {
        // Format conversion logic here if required
        std::cerr << "NV12 format detected; handle accordingly." << std::endl;
    }

    // Copy intermediate texture to resized texture
    g_context->CopyResource(g_resizedTexture.Get(), g_intermediateTexture.Get());
    std::cout << "Intermediate texture copied to resized texture." << std::endl;


    // Copy frame to intermediate texture
    g_context->CopyResource(g_intermediateTexture.Get(), frame);
    std::cout << "Captured frame copied to intermediate texture." << std::endl;



    if (!frame) {
        std::cerr << "No frame to update. Skipping texture update." << std::endl;
        return;
    }

    // Scale the captured frame to the resized texture
    g_context->CopySubresourceRegion(
        g_resizedTexture.Get(), // Destination texture
        0,                     // Destination subresource
        0,                     // X offset
        0,                     // Y offset
        0,                     // Z offset
        frame,                 // Source texture
        0,                     // Source subresource
        nullptr                // Copy entire source
    );

    std::cout << "Frame scaled and copied to resized texture." << std::endl;

    // Inspect the first pixel of the captured frame
    D3D11_TEXTURE2D_DESC desc;
    frame->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ComPtr<ID3D11Texture2D> stagingTexture;
    HRESULT hr = g_device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to create staging texture. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    g_context->CopyResource(stagingTexture.Get(), frame);

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = g_context->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(hr)) {
        std::cerr << "Failed to map staging texture. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    // Inspect the first pixel
    auto* data = static_cast<uint32_t*>(mappedResource.pData);
    std::cout << "First pixel RGBA: " << std::hex << *data << std::endl;

    g_context->Unmap(stagingTexture.Get(), 0);
}

// Render the frame
void RenderFrame() {
    // Split the frame
    SplitFrame(); // Perform the splitting logic

    // Present each half to the respective display
    PresentTexture(leftTexture, leftSwapChain);
    PresentTexture(rightTexture, rightSwapChain);

    // Define a clear color (RGBA)
    const float testColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f }; // Red color for testing
    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; // Black with full opacity


    // Clear the render target
    g_context->ClearRenderTargetView(g_renderTargetView.Get(), clearColor);

    // Get the back buffer
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) {
        std::cerr << "Failed to get back buffer. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    // Copy the resized texture to the back buffer
    g_context->CopyResource(backBuffer.Get(), g_resizedTexture.Get());
    std::cout << "Frame copied to back buffer." << std::endl;

    // Inspect resized texture contents
    D3D11_TEXTURE2D_DESC desc;
    g_resizedTexture->GetDesc(&desc);
    std::cout << "Resized texture dimensions: " << desc.Width << "x" << desc.Height
        << " Format: " << desc.Format << std::endl;

    D3D11_TEXTURE2D_DESC stagingDesc = {};
    stagingDesc.Width = desc.Width;
    stagingDesc.Height = desc.Height;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = desc.Format; // Match resized texture format
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0; // Must be 0 for staging
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; // Enable CPU read access
    stagingDesc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> stagingTexture;


    hr = g_device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to create staging texture for resized texture. HRESULT: " << std::hex << hr << std::endl;
        return;
    }


    g_context->CopyResource(stagingTexture.Get(), g_resizedTexture.Get());

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = g_context->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(hr)) {
        std::cerr << "Failed to map resized texture. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    auto* data = static_cast<uint32_t*>(mappedResource.pData);
    std::cout << "First pixel in resized texture RGBA: " << std::hex << *data << std::endl;

    g_context->Unmap(stagingTexture.Get(), 0);

    // Present the frame
    hr = g_swapChain->Present(1, 0);
    if (FAILED(hr)) {
        std::cerr << "Failed to present the frame. HRESULT: " << std::hex << hr << std::endl;
    }
    else {
        std::cout << "Frame presented to the screen." << std::endl;
    }
}


int APIENTRY WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow
) {
    EnableDebugConsole(); // Attach the console at the start

    if (!InitializeWindow(hInstance, nCmdShow)) return -1;
    if (!InitializeDirect3D11()) return -1;
    if (!InitializeCaptureResources()) return -1;

    MSG msg = {};
    while (true) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            ComPtr<IDXGIResource> resource;
            DXGI_OUTDUPL_FRAME_INFO frameInfo;

            HRESULT hr = g_duplication->AcquireNextFrame(500, &frameInfo, &resource);
            if (SUCCEEDED(hr)) {
                ComPtr<ID3D11Texture2D> frame;
                resource.As(&frame);

                UpdateTexture(frame.Get());
                RenderFrame();

                g_duplication->ReleaseFrame();
            }
        }
    }

    void PresentTexture(ComPtr<ID3D11Texture2D> texture, ComPtr<IDXGISwapChain> swapChain) {
        // Get the back buffer
        ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr)) {
            std::cerr << "Failed to get back buffer. HRESULT: " << std::hex << hr << std::endl;
            return;
        }

        // Copy the texture to the back buffer
        g_context->CopyResource(backBuffer.Get(), texture.Get());

        // Present the frame
        hr = swapChain->Present(1, 0);
        if (FAILED(hr)) {
            std::cerr << "Failed to present the frame. HRESULT: " << std::hex << hr << std::endl;
        }
    };


    return 0;

}


LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}
