#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <iostream>
#include <windows.h>
#include <fstream>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
#define _CRT_SECURE_NO_WARNINGS
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"  // Include stb image write for saving as PNG
#include <vector>  // For std::vector



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
ComPtr<IDXGISwapChain> g_swapChain;
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> g_leftSRV;
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> g_rightSRV;
Microsoft::WRL::ComPtr<ID3D11RenderTargetView> g_leftRenderTargetView;
Microsoft::WRL::ComPtr<ID3D11RenderTargetView> g_rightRenderTargetView;
Microsoft::WRL::ComPtr<ID3D11SamplerState> g_sampler;




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


void CreateTextures() {
    // Initialize the textures here (left and right)
    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = 1920; // Example width, adjust as needed
    textureDesc.Height = 1080; // Example height, adjust as needed
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    textureDesc.CPUAccessFlags = 0;

    // Create left texture
    HRESULT hr = g_device->CreateTexture2D(&textureDesc, nullptr, &g_leftTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to create left texture." << std::endl;
        return;
    }

    // Create right texture
    hr = g_device->CreateTexture2D(&textureDesc, nullptr, &g_rightTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to create right texture." << std::endl;
        return;
    }

    std::cout << "Textures created successfully!" << std::endl;
}


/*void RenderTextures() {
    // Set the input layout and primitive topology
    g_context->IASetInputLayout(g_inputLayout.Get());  // Make sure to call Get() for ComPtr
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    // Set the vertex buffer (quad vertices)
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    g_context->IASetVertexBuffers(0, 1, &g_vertexBuffer, &stride, &offset);

    // Set the pixel shader
    g_context->PSSetShader(g_pixelShader.Get(), nullptr, 0);  // Get() for ComPtr

    // Bind the left texture SRV to the pixel shader (slot 0)
    g_context->PSSetShaderResources(0, 1, g_leftSRV.GetAddressOf());  // g_leftSRV is a ComPtr<ID3D11ShaderResourceView>

    // Bind the sampler state to the pixel shader
    g_context->PSSetSamplers(0, 1, g_sampler.GetAddressOf());  // g_sampler is a ComPtr<ID3D11SamplerState>

    // Draw the quad with the left texture
    g_context->Draw(4, 0);  // Draw the left texture quad

    // Bind the right texture SRV to the pixel shader (slot 1)
    g_context->PSSetShaderResources(1, 1, g_rightSRV.GetAddressOf());  // g_rightSRV is a ComPtr<ID3D11ShaderResourceView>

    // Draw the quad with the right texture
    g_context->Draw(4, 0);  // Draw the right texture quad
}*/



void CaptureBackbufferAndSave() {
    // Get the backbuffer from the swap chain
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) {
        std::cerr << "Failed to get swap chain back buffer! HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
        return;
    }

    // Create a staging texture to copy the backbuffer content into
    D3D11_TEXTURE2D_DESC desc;
    backBuffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;  // Allow CPU access to the texture data

    // Ensure we set the format to one that supports CPU access
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // Common format for staging textures
    // You can add a check here if desc.Width or desc.Height is too large

    // Create the staging texture
    ComPtr<ID3D11Texture2D> stagingTexture;
    hr = g_device->CreateTexture2D(&desc, nullptr, &stagingTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to create staging texture! HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
        return;
    }

    // Copy the backbuffer into the staging texture
    g_context->CopyResource(stagingTexture.Get(), backBuffer.Get());

    // Map the staging texture to read the data
    D3D11_MAPPED_SUBRESOURCE mappedData;
    hr = g_context->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedData);
    if (FAILED(hr)) {
        std::cerr << "Failed to map staging texture! HRESULT: 0x" << std::hex << hr << std::dec << std::endl;
        return;
    }

    // Access the pixel data in mappedData.pData
    // Each pixel is 4 bytes (RGBA)
    unsigned char* pixels = reinterpret_cast<unsigned char*>(mappedData.pData);

    // Save the image as PNG
    int width = desc.Width;
    int height = desc.Height;
    int pitch = mappedData.RowPitch;

    // Create a temporary buffer for the image data (flipping row order if necessary)
    std::vector<unsigned char> imageData(width * height * 4);
    for (int y = 0; y < height; ++y) {
        memcpy(&imageData[y * width * 4], &pixels[(height - y - 1) * pitch], width * 4);
    }

    // Use stb_image_write to save the image as a PNG
    if (stbi_write_png("captured_image.png", width, height, 4, imageData.data(), width * 4) == 0) {
        std::cerr << "Failed to save image as PNG!" << std::endl;
    }
    else {
        std::cout << "Image saved as 'captured_image.png'" << std::endl;
    }

    // Unmap the staging texture
    g_context->Unmap(stagingTexture.Get(), 0);
}


void RenderTextures() {
    // Set the input layout and primitive topology
    g_context->IASetInputLayout(g_inputLayout.Get());  // Ensure Get() for ComPtr
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    // Set the vertex buffer (quad vertices)
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    g_context->IASetVertexBuffers(0, 1, &g_vertexBuffer, &stride, &offset);

    // Set the pixel shader
    g_context->PSSetShader(g_pixelShader.Get(), nullptr, 0);  // Get() for ComPtr

    // Bind the left texture SRV to the pixel shader (slot 0)
    g_context->PSSetShaderResources(0, 1, g_leftSRV.GetAddressOf());  // g_leftSRV is a ComPtr<ID3D11ShaderResourceView>

    // Bind the sampler state to the pixel shader
    g_context->PSSetSamplers(0, 1, g_sampler.GetAddressOf());  // g_sampler is a ComPtr<ID3D11SamplerState>

    // Draw the quad with the left texture
    g_context->Draw(4, 0);  // Draw the left texture quad

    // Bind the right texture SRV to the pixel shader (slot 1)
    g_context->PSSetShaderResources(1, 1, g_rightSRV.GetAddressOf());  // g_rightSRV is a ComPtr<ID3D11ShaderResourceView>

    // Draw the quad with the right texture
    g_context->Draw(4, 0);  // Draw the right texture quad

    // Capture the backbuffer after rendering
    CaptureBackbufferAndSave();
}







void CreateSamplerState() {
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.BorderColor[0] = 0.0f;
    samplerDesc.BorderColor[1] = 0.0f;
    samplerDesc.BorderColor[2] = 0.0f;
    samplerDesc.BorderColor[3] = 0.0f;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    HRESULT hr = g_device->CreateSamplerState(&samplerDesc, &g_sampler);
    if (FAILED(hr)) {
        // Handle error (you might want to log or exit)
        std::cerr << "Failed to create sampler state." << std::endl;
    }

    std::cout << "SamplerState created Sucessfully." << std::endl;
}



void CreateRenderTargetViews() {

    // Check if the textures are created before creating RTVs
    if (!g_leftTexture || !g_rightTexture) {
        std::cerr << "Error: Textures are not created correctly!" << std::endl;
        return;
    }
    // Create the render target view for the left texture
    HRESULT hr = g_device->CreateRenderTargetView(g_leftTexture.Get(), nullptr, &g_leftRenderTargetView);
    if (FAILED(hr)) {
        // Handle error (you might want to log or exit)
        std::cerr << "Failed to create render target view for left texture." << std::endl;
    }

    // Create the render target view for the right texture
    hr = g_device->CreateRenderTargetView(g_rightTexture.Get(), nullptr, &g_rightRenderTargetView);
    if (FAILED(hr)) {
        // Handle error (you might want to log or exit)
        std::cerr << "Failed to create render target view for right texture." << std::endl;
    }

    std::cout << "Left and right renderTargetview created from left and right textures!" << std::endl;
}


void CreateSRVs() {
    // Create Shader Resource Views (SRVs) for the textures
    g_device->CreateShaderResourceView(g_leftTexture.Get(), nullptr, &g_leftSRV);
    g_device->CreateShaderResourceView(g_rightTexture.Get(), nullptr, &g_rightSRV);
    std::cout << "Left and right SRVs created from g_left and right textures!" << std::endl;
}

bool CreateSwapChain(HWND hwnd) {
    // Step 1: Create the DXGI_SWAP_CHAIN_DESC
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = 1;                         // Number of buffers (for double buffering)
    swapChainDesc.BufferDesc.Width = 800;                   // Width of the window (adjust as needed)
    swapChainDesc.BufferDesc.Height = 600;                  // Height of the window (adjust as needed)
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // Color format
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;    // Refresh rate (adjust as needed)
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // The swap chain will be used for rendering
    swapChainDesc.OutputWindow = hwnd;                       // Window handle
    swapChainDesc.SampleDesc.Count = 1;                      // No multi-sampling (you can enable it later)
    swapChainDesc.Windowed = TRUE;                           // Windowed mode (set to FALSE for full screen)

    // Step 2: Create the swap chain
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,                      // Default adapter
        D3D_DRIVER_TYPE_HARDWARE,      // Use hardware rendering
        nullptr,                      // No external software rasterizer
        0,                             // Flags (none)
        nullptr,                      // Feature levels (use default)
        0,                             // Count of feature levels
        D3D11_SDK_VERSION,            // SDK version
        &swapChainDesc,               // Swap chain description
        &g_swapChain,                 // Out parameter for the swap chain
        &g_device,                    // Out parameter for the device
        nullptr,                      // Out parameter for the feature level
        &g_context                    // Out parameter for the device context
    );

    if (FAILED(hr)) {
        std::cerr << "Failed to create swap chain. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    std::cout << "Swap chain created successfully." << std::endl;
    return true;
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


    //create Fleft and g_right textures
    CreateTextures();


    //CreateVertexBuffer 
    if (!CreateVertexBuffer()) {
        return -1;
    }


    //Loadshaders calls createinputlayout
    if (!LoadShaders()) {
        return -1;
    }


    //Loadshaders calls createinputlayout
    if (!CreateSwapChain(g_hwnd)) {
        return -1;
    }

    
    //Create Sampler State
    CreateSamplerState();
    //Create the renderrtargetview
    CreateRenderTargetViews();

    while (true) {
        if (!CaptureFrame()) {
            std::cerr << "Error capturing frame." << std::endl;
            break;
        }

        CreateSRVs();
        RenderTextures();
    }

    return 0;
}
