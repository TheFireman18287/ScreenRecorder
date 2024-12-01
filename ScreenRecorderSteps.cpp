#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <iostream>
#include <windows.h>
#include <fstream>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

// Global Variables
ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<IDXGIOutputDuplication> g_duplication;
ComPtr<ID3D11Texture2D> g_leftTexture;
ComPtr<ID3D11Texture2D> g_rightTexture;
ComPtr<ID3D11VertexShader> g_vertexShader;
ComPtr<ID3D11PixelShader> g_pixelShader;


D3D11_BOX leftBox = { 0, 0, 0, 2560, 1440, 1 };  // Adjust resolution as needed
D3D11_BOX rightBox = { 2560, 0, 0, 5120, 1440, 1 };

bool LoadShaders() {
    HRESULT hr;

    // Load vertex shader
    ComPtr<ID3DBlob> vertexShaderBlob;
    hr = D3DReadFileToBlob(L"VertexShader.cso", &vertexShaderBlob);
    if (FAILED(hr)) {
        std::cerr << "Failed to load vertex shader. HRESULT: " << std::hex << hr << std::endl;
        return false;  // Return false on failure
    }
    g_device->CreateVertexShader(vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), nullptr, &g_vertexShader);

    // Load pixel shader
    ComPtr<ID3DBlob> pixelShaderBlob;
    hr = D3DReadFileToBlob(L"PixelShader.cso", &pixelShaderBlob);
    if (FAILED(hr)) {
        std::cerr << "Failed to load pixel shader. HRESULT: " << std::hex << hr << std::endl;
        return false;  // Return false on failure
    }
    g_device->CreatePixelShader(pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize(), nullptr, &g_pixelShader);

    return true;  // Return true if everything succeeded
}



void CreateConsole() {
    // Allocate a console for the application
    if (AllocConsole()) {
        FILE* fp;

        // Redirect standard output
        freopen_s(&fp, "CONOUT$", "w", stdout);

        // Redirect standard error
        freopen_s(&fp, "CONOUT$", "w", stderr);

        // Redirect standard input
        freopen_s(&fp, "CONIN$", "r", stdin);

        std::cout << "Console attached successfully." << std::endl;
    }
    else {
        std::cerr << "Failed to allocate console." << std::endl;
    }
}


void LoadPixGpuCaptureDll() {
    const wchar_t* pixDllPath = L"C:\\Program Files\\Microsoft PIX\\2409.23\\WinPixGpuCapturer.dll";

    if (!LoadLibrary(pixDllPath)) {
        std::cerr << "Failed to load WinPixGpuCapturer.dll." << std::endl;
    }
    else {
        std::cout << "WinPixGpuCapturer.dll loaded successfully." << std::endl;
    }
}


bool InitializeDeviceAndDuplication() {
    HRESULT hr;

    // Create D3D11 device
    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION, &g_device, &featureLevel, &g_context);
    if (FAILED(hr)) {
        std::cerr << "Failed to create D3D11 device. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    // Get DXGI adapter
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = g_device.As(&dxgiDevice);
    if (FAILED(hr)) {
        std::cerr << "Failed to get DXGI device. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) {
        std::cerr << "Failed to get DXGI adapter. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    // Get output and create duplication
    ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(0, &output);
    if (FAILED(hr)) {
        std::cerr << "Failed to get DXGI output. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (FAILED(hr)) {
        std::cerr << "Failed to query IDXGIOutput1 interface. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    hr = output1->DuplicateOutput(g_device.Get(), &g_duplication);
    if (FAILED(hr)) {
        std::cerr << "Failed to create desktop duplication. HRESULT: " << std::hex << hr << std::endl;
        if (hr == DXGI_ERROR_UNSUPPORTED) {
            std::cerr << "Desktop Duplication API is not supported on this system." << std::endl;
        }
        return false;
    }

    std::cout << "Device and duplication initialized successfully." << std::endl;
    return true;
}

bool InitializeTextures() {
    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = 2560;  // Half the screen width
    textureDesc.Height = 1440; // Screen height
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = g_device->CreateTexture2D(&textureDesc, nullptr, &g_leftTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to create left texture. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    hr = g_device->CreateTexture2D(&textureDesc, nullptr, &g_rightTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to create right texture. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    std::cout << "Left and right textures initialized successfully." << std::endl;
    return true;
}

bool CaptureFrame() {
    if (!g_duplication) {
        std::cerr << "Duplication interface is not initialized." << std::endl;
        return false;
    }

    ComPtr<IDXGIResource> desktopResource;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;

    HRESULT hr = g_duplication->AcquireNextFrame(500, &frameInfo, &desktopResource);
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            return true; // No new frame
        }
        std::cerr << "Failed to acquire next frame. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    // Process the captured frame
    ComPtr<ID3D11Texture2D> capturedTexture;
    hr = desktopResource.As(&capturedTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to get captured texture. HRESULT: " << std::hex << hr << std::endl;
        g_duplication->ReleaseFrame();
        return false;
    }

    std::cout << "Frame captured successfully." << std::endl;

    g_duplication->ReleaseFrame();
    return true;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Create a console for logging
    CreateConsole();

    //LoadWinPixGpuCapturer.dll to be able to atatach
    LoadPixGpuCaptureDll();

    //Start the main program
    if (!InitializeDeviceAndDuplication()) {
        return -1;
    }

    if (!InitializeTextures()) {
        return -1;
    }

    if (!LoadShaders()) {
        return -1;
    }

    while (true) {
        if (!CaptureFrame()) {
            std::cerr << "Error capturing frame." << std::endl;
            break;
        }
    }

    return 0;
}
