#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
#include <iostream>
#include <wrl/client.h>
#include<dxgi1_2.h>

using Microsoft::WRL::ComPtr;

// Global variables
ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<IDXGISwapChain> g_swapChain;
ComPtr<ID3D11RenderTargetView> g_renderTargetView;
ComPtr<IDXGIOutputDuplication> g_outputDuplication;
ComPtr<ID3D11RenderTargetView> g_dupRenderTargetView;

// Create D3D11 device, swap chain, and output duplication
void CreateDeviceAndSwapChain(HWND hwnd) {
    HRESULT hr = S_OK;

    // Swap chain description
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = 1;
    swapChainDesc.BufferDesc.Width = 800;  // Window width
    swapChainDesc.BufferDesc.Height = 600; // Window height
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = hwnd;  // Window handle
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.Windowed = TRUE;

    // Create device and swap chain
    hr = D3D11CreateDeviceAndSwapChain(
        nullptr,                    // Default adapter
        D3D_DRIVER_TYPE_HARDWARE,   // Hardware rendering
        nullptr,                    // No software rasterizer
        0,                          // No flags
        nullptr,                    // No feature levels
        0,                          // No feature levels
        D3D11_SDK_VERSION,          // SDK version
        &swapChainDesc,             // Swap chain description
        &g_swapChain,               // Output swap chain
        &g_device,                  // Output device
        nullptr,                    // Output feature level (optional)
        &g_context                  // Output device context
    );

    if (FAILED(hr)) {
        std::cerr << "Failed to create D3D11 device and swap chain. HRESULT: " << std::hex << hr << std::endl;
        return;
    }
    std::cout << "Device and swap chain created successfully." << std::endl;
}

void CreateOutputDuplication() {
    // Get the DXGI device from the D3D11 device
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = g_device.As(&dxgiDevice);
    if (FAILED(hr)) {
        std::cerr << "Failed to get IDXGIDevice. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    // Get the DXGI adapter (GPU) from the DXGI device
    ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    if (FAILED(hr)) {
        std::cerr << "Failed to get IDXGIAdapter. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    // Get the output (monitor) from the adapter
    ComPtr<IDXGIOutput> dxgiOutput;
    hr = dxgiAdapter->EnumOutputs(0, &dxgiOutput); // Use the first monitor (index 0)
    if (FAILED(hr)) {
        std::cerr << "Failed to get IDXGIOutput. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    // Create the output duplication object
    hr = dxgiOutput->DuplicateOutput(dxgiDevice.Get(), &g_outputDuplication);
    if (FAILED(hr)) {
        std::cerr << "Failed to create output duplication object. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    std::cout << "Output duplication created successfully." << std::endl;
}

void CreateRenderTargetView() {
    // Get the back buffer from the swap chain
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) {
        std::cerr << "Failed to get back buffer from swap chain. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    // Create render target view (RTV) from the back buffer
    hr = g_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &g_renderTargetView);
    if (FAILED(hr)) {
        std::cerr << "Failed to create render target view. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    // Set the render target view
    g_context->OMSetRenderTargets(1, g_renderTargetView.GetAddressOf(), nullptr);
}

void CaptureAndPresentFrame(HWND hwnd) {
    // Get the current frame from the output duplication object
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    ComPtr<IDXGIResource> pDxgiResource;
    HRESULT hr = g_outputDuplication->AcquireNextFrame(0, &frameInfo, &pDxgiResource);
    if (FAILED(hr)) {
        std::cerr << "Failed to acquire next frame from duplication. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    // Get the texture from the acquired resource
    ComPtr<ID3D11Texture2D> texture;
    hr = pDxgiResource.As(&texture);
    if (FAILED(hr)) {
        std::cerr << "Failed to cast resource to texture. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    // Create a render target view for the texture
    hr = g_device->CreateRenderTargetView(texture.Get(), nullptr, &g_dupRenderTargetView);
    if (FAILED(hr)) {
        std::cerr << "Failed to create render target view. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    // Set the render target view (RTV) to the context
    g_context->OMSetRenderTargets(1, g_dupRenderTargetView.GetAddressOf(), nullptr);

    // Set up the viewport to match the window size
    D3D11_VIEWPORT viewport = {};
    viewport.Width = 800;  // Match the window size
    viewport.Height = 600;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_context->RSSetViewports(1, &viewport);

    // Clear the render target to black (optional)
    float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };  // Black color
    g_context->ClearRenderTargetView(g_dupRenderTargetView.Get(), clearColor);

    // Present the duplicated texture to the new window
    g_swapChain->Present(1, 0);

    // Release the frame after processing
    g_outputDuplication->ReleaseFrame();
}

void RenderLoop(HWND hwnd) {
    // Set up the render target
    CreateRenderTargetView();

    // Set the viewport (this should match the window size)
    D3D11_VIEWPORT viewport = {};
    viewport.Width = 800; // Match window size
    viewport.Height = 600;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_context->RSSetViewports(1, &viewport);

    // Clear the render target to a color (optional)
    float clearColor[] = { 0.0f, 0.0f, 1.0f, 1.0f };  // Blue color
    g_context->ClearRenderTargetView(g_renderTargetView.Get(), clearColor);

    // Now capture and present the frame to the new window
    CaptureAndPresentFrame(hwnd);
}

void Cleanup() {
    // Cleanup resources here, if necessary
    // No need to manually release COM objects, they will be released automatically
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_SIZE:
        if (g_swapChain) {
            // Handle resizing (optional)
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

int main() {
    // Initialize Win32 window (this should be done before creating the device)
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"Direct3D11WindowClass";
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, wc.lpszClassName, L"Direct3D 11 Window", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, wc.hInstance, nullptr
    );
    if (!hwnd) {
        std::cerr << "Failed to create window." << std::endl;
        return -1;
    }

    // Create device, swap chain, and output duplication
    CreateDeviceAndSwapChain(hwnd);
    CreateOutputDuplication();

    // Show window
    ShowWindow(hwnd, SW_SHOW);

    // Main loop
    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            RenderLoop(hwnd);
        }
    }

    Cleanup();
    return 0;
}
