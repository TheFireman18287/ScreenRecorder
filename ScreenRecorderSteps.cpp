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
ComPtr<ID3D11Buffer> g_vertexBuffer;
ComPtr<ID3D11InputLayout> g_inputLayout;




D3D11_BOX leftBox = { 0, 0, 0, 2560, 1440, 1 };  // Adjust resolution as needed
D3D11_BOX rightBox = { 2560, 0, 0, 5120, 1440, 1 };

//Define vertex data for full screen quad
struct Vertex {
    float position[3];
    float texCoord[2];
};

Vertex vertices[] = {
    // Positions         // Texture Coordinates
    { {-1.0f,  1.0f, 0.0f}, {0.0f, 0.0f} }, // Top-left
    { { 1.0f,  1.0f, 0.0f}, {1.0f, 0.0f} }, // Top-right
    { {-1.0f, -1.0f, 0.0f}, {0.0f, 1.0f} }, // Bottom-left
    { { 1.0f, -1.0f, 0.0f}, {1.0f, 1.0f} }, // Bottom-right
};


//List of functions (latest first)


// Global variables
HINSTANCE g_hInstance;
HWND g_hwnd;

// Window procedure (this handles messages for the window)
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

bool CreateRenderWindow() {
    // Define the window class
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WindowProc;       // The window procedure function
    wc.hInstance = g_hInstance;
    wc.lpszClassName = L"ScreenRecorderClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    // Register the window class
    if (!RegisterClassEx(&wc)) {
        std::cerr << "Failed to register window class!" << std::endl;
        return false;
    }

    // Create the window
    g_hwnd = CreateWindowEx(
        0,                                  // Extended style
        wc.lpszClassName,                   // Window class
        L"WindowInstance1",                  // Window title
        WS_OVERLAPPEDWINDOW,                 // Window style
        CW_USEDEFAULT, CW_USEDEFAULT,       // Position
        2560, 1440,                            // Size
        nullptr,                            // Parent window
        nullptr,                            // Menu
        g_hInstance,                        // Instance handle
        nullptr                             // Additional application data
    );

    if (!g_hwnd) {
        std::cerr << "Failed to create window!" << std::endl;
        return false;
    }

    // Show the window
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    return true;
}


bool CreateInputLayout(ComPtr<ID3DBlob> vertexShaderBlob) {
    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    HRESULT hr = g_device->CreateInputLayout(
        layoutDesc,
        ARRAYSIZE(layoutDesc),
        vertexShaderBlob->GetBufferPointer(),
        vertexShaderBlob->GetBufferSize(),
        &g_inputLayout
    );
    if (FAILED(hr)) {
        std::cerr << "Failed to create input layout. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    return true;
}




bool CreateVertexBuffer() {
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(vertices);
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices;

    HRESULT hr = g_device->CreateBuffer(&bufferDesc, &initData, &g_vertexBuffer);
    if (FAILED(hr)) {
        std::cerr << "Failed to create vertex buffer. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    return true;
}


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

    // Create the input layout
    if (!CreateInputLayout(vertexShaderBlob)) {
        return false;  // If input layout creation fails, return false
    }

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

    // Now split the captured frame into left and right halves
    D3D11_TEXTURE2D_DESC desc;
    capturedTexture->GetDesc(&desc);

    // Ensure the texture dimensions are even to properly split
    if (desc.Width % 2 != 0) {
        desc.Width++;  // Adjust to even width
    }

    // Create the left and right textures only once (or resize them as needed)
    if (!g_leftTexture) {
        // Create the left texture (half the width of the captured frame)
        desc.Width /= 2;  // Set width to half
        hr = g_device->CreateTexture2D(&desc, nullptr, &g_leftTexture);
        if (FAILED(hr)) {
            std::cerr << "Failed to create left texture. HRESULT: " << std::hex << hr << std::endl;
            g_duplication->ReleaseFrame();
            return false;
        }
    }
    std::cout << "LeftTexture Created successfully" << std::endl;

    if (!g_rightTexture) {
        // Create the right texture (half the width of the captured frame)
        desc.Width *= 2;  // Restore full width
        desc.Width /= 2;  // Set width back to half for right texture
        hr = g_device->CreateTexture2D(&desc, nullptr, &g_rightTexture);
        if (FAILED(hr)) {
            std::cerr << "Failed to create right texture. HRESULT: " << std::hex << hr << std::endl;
            g_duplication->ReleaseFrame();
            return false;
        }
    }
    std::cout << "RightTexture Created successfully" << std::endl;
    // Split the captured frame and copy data to left and right textures
    D3D11_BOX leftBox = { 0, 0, 0, desc.Width, desc.Height, 1 };
    D3D11_BOX rightBox = { desc.Width, 0, 0, 2 * desc.Width, desc.Height, 1 };

    // Copy the left half to g_leftTexture
    g_context->CopySubresourceRegion(g_leftTexture.Get(), 0, 0, 0, 0, capturedTexture.Get(), 0, &leftBox);
    std::cout << "LeftTexture copied to g_leftTexture successfully" << std::endl;
    // Copy the right half to g_rightTexture
    g_context->CopySubresourceRegion(g_rightTexture.Get(), 0, 0, 0, 0, capturedTexture.Get(), 0, &rightBox);
    std::cout << "RightTexture copied to g_rightTexture successfully" << std::endl;
    g_duplication->ReleaseFrame();
    return true;
}

//Main function
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {

    // Create a console for logging
    CreateConsole();

    //LoadWinPixGpuCapturer.dll to be able to atatach
    LoadPixGpuCaptureDll();


    // Create the window
    if (!CreateRenderWindow()) {
        return -1;
    }

    //Initilize the devie and setup duplication
    if (!InitializeDeviceAndDuplication()) {
        return -1;
    }

    //CreateVertexBuffer 
    if (!CreateVertexBuffer()) {
        return -1;
    }


    //Loadshaders calls createinputlayout
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
