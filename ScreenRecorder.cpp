#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
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

HWND CreateRenderWindow(const wchar_t* title);




using Microsoft::WRL::ComPtr;

// Global variables
ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<IDXGIOutputDuplication> g_duplication;
ComPtr<ID3D11Texture2D> g_leftTexture;
ComPtr<ID3D11Texture2D> g_rightTexture;
ComPtr<IDXGISwapChain> g_leftSwapChain;
ComPtr<IDXGISwapChain> g_rightSwapChain;
ComPtr<ID3D11VertexShader> vertexShader;
ComPtr<ID3D11PixelShader> pixelShader;
ComPtr<ID3D11InputLayout> inputLayout;
ComPtr<ID3D11ShaderResourceView> g_leftTextureSRV;
ComPtr<ID3D11ShaderResourceView> g_rightTextureSRV;
ComPtr<ID3D11Texture2D> g_leftStagingTexture;
ComPtr<ID3D11Texture2D> g_rightStagingTexture;


void CreateSwapChainForMonitor(
    ComPtr<IDXGIOutput> output,
    ComPtr<IDXGISwapChain>& swapChain,
    const wchar_t* windowTitle
);
void InspectTexture(ComPtr<ID3D11Texture2D> texture, ComPtr<ID3D11Texture2D> stagingTexture, const std::string& name) {
    if (!stagingTexture || !texture) {
        std::cerr << "One or both textures are null. Cannot inspect " << name << "." << std::endl;
        return;
    }

    // No HRESULT assignment, as CopyResource returns void
    g_context->CopyResource(stagingTexture.Get(), texture.Get());

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = g_context->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(hr)) {
        std::cerr << "Failed to map " << name << ". HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    uint32_t* data = static_cast<uint32_t*>(mappedResource.pData);
    std::cout << "First pixel in " << name << " RGBA: " << std::hex << *data << std::endl;

    g_context->Unmap(stagingTexture.Get(), 0);
}

bool SaveTextureAsPNGStandalone(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture, const wchar_t* filename) {
    // Get the texture description

    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    // Create a staging texture to copy GPU data to CPU
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.BindFlags = 0;

    ComPtr<ID3D11Texture2D> stagingTexture;
    HRESULT hr = device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);


    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        std::cerr << "Device lost. Reason: "
            << std::hex << g_device->GetDeviceRemovedReason() << std::endl;
    }
    if (FAILED(hr)) {
        std::cerr << "Failed to create staging texture. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }


    // Copy the data from the source texture to the staging texture
    context->CopyResource(stagingTexture.Get(), texture);



    // Map the staging texture to access its data
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = context->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(hr)) {
        std::cerr << "Failed to map staging texture. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    // Initialize WIC
    ComPtr<IWICImagingFactory> wicFactory;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr)) {
        std::cerr << "Failed to create WIC factory. HRESULT: " << std::hex << hr << std::endl;
        context->Unmap(stagingTexture.Get(), 0);
        return false;
    }

    // Create a PNG encoder
    ComPtr<IWICBitmapEncoder> encoder;
    hr = wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) {
        std::cerr << "Failed to create PNG encoder. HRESULT: " << std::hex << hr << std::endl;
        context->Unmap(stagingTexture.Get(), 0);
        return false;
    }

    // Create a stream for the output file
    ComPtr<IWICStream> stream;
    hr = wicFactory->CreateStream(&stream);
    if (FAILED(hr)) {
        std::cerr << "Failed to create WIC stream. HRESULT: " << std::hex << hr << std::endl;
        context->Unmap(stagingTexture.Get(), 0);
        return false;
    }

    hr = stream->InitializeFromFilename(filename, GENERIC_WRITE);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize WIC stream. HRESULT: " << std::hex << hr << std::endl;
        context->Unmap(stagingTexture.Get(), 0);
        return false;
    }

    // Initialize the encoder
    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize PNG encoder. HRESULT: " << std::hex << hr << std::endl;
        context->Unmap(stagingTexture.Get(), 0);
        return false;
    }

    // Create a frame for the PNG
    ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(&frame, nullptr);
    if (FAILED(hr)) {
        std::cerr << "Failed to create PNG frame. HRESULT: " << std::hex << hr << std::endl;
        context->Unmap(stagingTexture.Get(), 0);
        return false;
    }

    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize PNG frame. HRESULT: " << std::hex << hr << std::endl;
        context->Unmap(stagingTexture.Get(), 0);
        return false;
    }

    hr = frame->SetSize(desc.Width, desc.Height);
    if (FAILED(hr)) {
        std::cerr << "Failed to set PNG frame size. HRESULT: " << std::hex << hr << std::endl;
        context->Unmap(stagingTexture.Get(), 0);
        return false;
    }

    // Set the pixel format
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&format);
    if (FAILED(hr)) {
        std::cerr << "Failed to set PNG pixel format. HRESULT: " << std::hex << hr << std::endl;
        context->Unmap(stagingTexture.Get(), 0);
        return false;
    }

    // Write the pixel data to the PNG
    hr = frame->WritePixels(
        desc.Height,
        mappedResource.RowPitch,
        mappedResource.RowPitch * desc.Height,
        static_cast<BYTE*>(mappedResource.pData)
    );
    if (FAILED(hr)) {
        std::cerr << "Failed to write PNG pixels. HRESULT: " << std::hex << hr << std::endl;
        context->Unmap(stagingTexture.Get(), 0);
        return false;
    }

    hr = frame->Commit();
    if (FAILED(hr)) {
        std::cerr << "Failed to commit PNG frame. HRESULT: " << std::hex << hr << std::endl;
        context->Unmap(stagingTexture.Get(), 0);
        return false;
    }

    hr = encoder->Commit();
    if (FAILED(hr)) {
        std::cerr << "Failed to commit PNG encoder. HRESULT: " << std::hex << hr << std::endl;
        context->Unmap(stagingTexture.Get(), 0);
        return false;
    }

    std::cout << "Saved PNG: " << filename << std::endl;
    context->Unmap(stagingTexture.Get(), 0);
    return true;
}


// Redirect WinMain to main
int main();

int CALLBACK WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nCmdShow
) {
    // Attach a console for logging
    AllocConsole();
    FILE* consoleOutput;
    freopen_s(&consoleOutput, "CONOUT$", "w", stdout);
    freopen_s(&consoleOutput, "CONOUT$", "w", stderr);

    std::cout << "Console attached. Starting application..." << std::endl;

    return main();
}



// Function to initialize DirectX and Desktop Duplication API

void InitializeShaders() {
    // Compile the vertex shader
    ComPtr<ID3DBlob> vsBlob;
    HRESULT hr = D3DCompileFromFile(L"VertexShader.hlsl", nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, nullptr);
    if (FAILED(hr)) {
        std::cerr << "Failed to compile vertex shader. HRESULT: " << std::hex << hr << std::endl;
        return;
    }
    hr = g_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vertexShader);
    if (FAILED(hr)) {
        std::cerr << "Failed to create vertex shader. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    // Compile the pixel shader
    ComPtr<ID3DBlob> psBlob;
    hr = D3DCompileFromFile(L"PixelShader.hlsl", nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, nullptr);
    if (FAILED(hr)) {
        std::cerr << "Failed to compile pixel shader. HRESULT: " << std::hex << hr << std::endl;
        return;
    }
    hr = g_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pixelShader);
    if (FAILED(hr)) {
        std::cerr << "Failed to create pixel shader. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    // Define the input layout
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };



    hr = g_device->CreateInputLayout(layout, ARRAYSIZE(layout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout);
    if (FAILED(hr)) {
        std::cerr << "Failed to create input layout. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    std::cout << "Shaders initialized successfully." << std::endl;
}





bool InitializeCaptureResources() {
    HRESULT hr;

    // Create D3D11 device
    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &g_device,
        &featureLevel,
        &g_context
    );
    if (FAILED(hr)) {
        std::cerr << "Failed to create D3D11 device. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }
    std::cout << "D3D11 device created successfully." << std::endl;

    // Get DXGI device and output
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

    ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(0, &output);
    if (FAILED(hr)) {
        std::cerr << "Failed to get DXGI output. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (FAILED(hr)) {
        std::cerr << "Failed to get DXGI output1. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    // Create duplication
    hr = output1->DuplicateOutput(g_device.Get(), &g_duplication);
    if (FAILED(hr)) {
        std::cerr << "Failed to create desktop duplication. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }
    std::cout << "Desktop duplication created successfully." << std::endl;

    // Create left and right textures

    D3D11_TEXTURE2D_DESC halfDesc = {};
    halfDesc.Width = 2560;
    halfDesc.Height = 1440;
    halfDesc.MipLevels = 1;
    halfDesc.ArraySize = 1;
    halfDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    halfDesc.SampleDesc.Count = 1;
    halfDesc.Usage = D3D11_USAGE_DEFAULT;
    halfDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    hr = g_device->CreateTexture2D(&halfDesc, nullptr, &g_leftTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to create left texture. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    hr = g_device->CreateTexture2D(&halfDesc, nullptr, &g_rightTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to create right texture. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    std::cout << "Textures for splitting created successfully." << std::endl;
    std::cout << "Finished Creating Device, Duplicate, Texture Spliting (1 func: InitializeCaptureResources) " << std::endl;
    return true;
}

struct Vertex {
    float x, y, z;
    float u, v;
};


void InitializeStagingTextures() {
    HRESULT hr;
    D3D11_TEXTURE2D_DESC desc = {};
    g_leftTexture->GetDesc(&desc);

    // Create the staging texture for the left texture
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = g_device->CreateTexture2D(&stagingDesc, nullptr, &g_leftStagingTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to create left staging texture. HRESULT: " << std::hex << hr << std::endl;
    }

    // Create the staging texture for the right texture
    hr = g_device->CreateTexture2D(&stagingDesc, nullptr, &g_rightStagingTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to create right staging texture. HRESULT: " << std::hex << hr << std::endl;
    }

    std::cout << "Created Staging Textures " << std::endl;
}






ComPtr<ID3D11Buffer> vertexBuffer;
void CreateFullScreenQuad() {
    Vertex quadVertices[] = {
        { -1.0f, -1.0f, 0.0f, 0.0f, 1.0f },
        { -1.0f,  1.0f, 0.0f, 0.0f, 0.0f },
        {  1.0f, -1.0f, 0.0f, 1.0f, 1.0f },
        {  1.0f,  1.0f, 0.0f, 1.0f, 0.0f },
    };

    D3D11_BUFFER_DESC vertexBufferDesc = {};
    vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    vertexBufferDesc.ByteWidth = sizeof(quadVertices);
    vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vertexData = {};
    vertexData.pSysMem = quadVertices;

    HRESULT hr = g_device->CreateBuffer(&vertexBufferDesc, &vertexData, &vertexBuffer);
    if (FAILED(hr)) {
        std::cerr << "Failed to create vertex buffer. HRESULT: " << std::hex << hr << std::endl;

    }

    std::cout << "Created vertex buffer " << std::endl;
}


void CreateShaderResourceViews() {
    HRESULT hr;

    // Left texture
    hr = g_device->CreateShaderResourceView(g_leftTexture.Get(), nullptr, &g_leftTextureSRV);

    std::cout << "Calling Inspect Texture from CreateShaderResourceView:" << std::endl;
    InspectTexture(g_leftTexture, g_leftStagingTexture, "Left Texture");
    InspectTexture(g_rightTexture, g_rightStagingTexture, "Right Texture");
    if (FAILED(hr)) {
        std::cerr << "Failed to create SRV for left texture. HRESULT: " << std::hex << hr << std::endl;
    }
    else {
        std::cout << "Shader resource view created for left texture." << std::endl;
    }

    // Right texture
    hr = g_device->CreateShaderResourceView(g_rightTexture.Get(), nullptr, &g_rightTextureSRV);
    if (FAILED(hr)) {
        std::cerr << "Failed to create SRV for right texture. HRESULT: " << std::hex << hr << std::endl;
    }
    else {
        std::cout << "Shader resource view created for right texture." << std::endl;
    }
}

void CreateSwapChainForMonitor(
    ComPtr<IDXGIOutput> output,
    ComPtr<IDXGISwapChain>& swapChain,
    const wchar_t* windowTitle
) {
    // Create a window for this monitor
    HWND hwnd = CreateRenderWindow(windowTitle);

    // Define the swap chain description
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = 1;
    swapChainDesc.BufferDesc.Width = 2560;  // Match half width
    swapChainDesc.BufferDesc.Height = 1440; // Match height
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = hwnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = TRUE; // Fullscreen can be configured if needed
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    // Create the swap chain
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        std::cerr << "Failed to create DXGI factory. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    hr = factory->CreateSwapChain(g_device.Get(), &swapChainDesc, &swapChain);
    if (FAILED(hr)) {
        std::cerr << "Failed to create swap chain for monitor. HRESULT: " << std::hex << hr << std::endl;
    }
    else {
        std::wcout << L"Swap chain created for monitor: " << windowTitle << std::endl;
    }
}

HWND CreateRenderWindow(const wchar_t* title) {
    // Define a simple window class
    WNDCLASS wc = {};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"DirectXWindow";


    // Register the window class
    RegisterClass(&wc);

    // Create the window with a movable style
    HWND hwnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        title,
        WS_OVERLAPPEDWINDOW | WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,  // Allow resizing and moving
        CW_USEDEFAULT, CW_USEDEFAULT,
        2560, 1440,           // Default size
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr
    );

    // Show the window
    ShowWindow(hwnd, SW_SHOW);

    return hwnd;
}



void InitializeMonitorsAndSwapChains(ComPtr<IDXGISwapChain>& leftSwapChain, ComPtr<IDXGISwapChain>& rightSwapChain) {
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

            // Create swap chains for the first two monitors
            if (outputIndex == 0) {
                CreateSwapChainForMonitor(output, leftSwapChain, L"Left Monitor");
            }
            else if (outputIndex == 1) {
                CreateSwapChainForMonitor(output, rightSwapChain, L"Right Monitor");
            }
        }
    }
}





void RenderToMonitors(ComPtr<IDXGISwapChain> leftSwapChain, ComPtr<IDXGISwapChain> rightSwapChain, ComPtr<ID3D11ShaderResourceView> leftTextureSRV, ComPtr<ID3D11ShaderResourceView> rightTextureSRV) {
    HRESULT hr;
    // Check if the device and context are valid
    if (!g_device || !g_context) {
        std::cerr << "Device or context is null!" << std::endl;
        return;
    }

    // Make sure swap chains are valid
    if (!leftSwapChain || !rightSwapChain) {
        std::cerr << "One or both swap chains are null!" << std::endl;
        return;
    }

    // Optionally, log swap chain details to ensure they are correct
    std::cout << "Rendering to Left Swap Chain: " << leftSwapChain.Get() << std::endl;
    std::cout << "Rendering to Right Swap Chain: " << rightSwapChain.Get() << std::endl;
    std::cout << "\nCalling InspectTexture for left and right texture from RenderToMonotor!\n " << rightSwapChain.Get() << std::endl;
    InspectTexture(g_leftTexture, g_leftStagingTexture, "Left Texture");
    InspectTexture(g_rightTexture, g_rightStagingTexture, "Right Texture");



    // Render the left texture
    ComPtr<ID3D11Texture2D> leftBackBuffer;
    hr = leftSwapChain->GetBuffer(0, IID_PPV_ARGS(&leftBackBuffer));
    if (SUCCEEDED(hr)) {
        ComPtr<ID3D11RenderTargetView> leftRTV;
        hr = g_device->CreateRenderTargetView(leftBackBuffer.Get(), nullptr, &leftRTV);
        if (SUCCEEDED(hr)) {
            // Set the render target view for the left swap chain
            g_context->OMSetRenderTargets(1, leftRTV.GetAddressOf(), nullptr);

            // Optional: clear the render target before drawing
            const float clearColor[4] = { 0.2f, 0.2f, 0.2f, 1.0f }; // Gray
            g_context->ClearRenderTargetView(leftRTV.Get(), clearColor);

            // Additional rendering steps (binding shaders, drawing, etc.)
            // g_context->Draw(...);
            g_context->Draw(4, 0);  // Drawing a quad (4 vertices)
            // Present the left swap chain
            leftSwapChain->Present(1, 0);
            std::cout << "Left texture presented." << std::endl;
        }
        else {
            std::cerr << "Failed to create Render Target View for Left texture. HRESULT: " << std::hex << hr << std::endl;
        }
    }
    else {
        std::cerr << "Failed to get left back buffer. HRESULT: " << std::hex << hr << std::endl;
    }

    // Render the right texture
    ComPtr<ID3D11Texture2D> rightBackBuffer;
    hr = rightSwapChain->GetBuffer(0, IID_PPV_ARGS(&rightBackBuffer));
    if (SUCCEEDED(hr)) {
        ComPtr<ID3D11RenderTargetView> rightRTV;
        hr = g_device->CreateRenderTargetView(rightBackBuffer.Get(), nullptr, &rightRTV);
        if (SUCCEEDED(hr)) {
            // Set the render target view for the right swap chain
            g_context->OMSetRenderTargets(1, rightRTV.GetAddressOf(), nullptr);

            // Optional: clear the render target before drawing
            const float clearColor[4] = { 0.2f, 0.2f, 0.2f, 1.0f }; // Gray
            g_context->ClearRenderTargetView(rightRTV.Get(), clearColor);

            // Additional rendering steps (binding shaders, drawing, etc.)
            // g_context->Draw(...);
            g_context->Draw(4, 0);  // Drawing a quad (4 vertices)

            // Present the right swap chain
            rightSwapChain->Present(1, 0);
            std::cout << "Right texture presented." << std::endl;
        }
        else {
            std::cerr << "Failed to create Render Target View for Right texture. HRESULT: " << std::hex << hr << std::endl;
        }
    }
    else {
        std::cerr << "Failed to get right back buffer. HRESULT: " << std::hex << hr << std::endl;
    }
}








// Function to capture a frame
bool CaptureFrame() {
    ComPtr<IDXGIResource> desktopResource;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    HRESULT hr = g_duplication->AcquireNextFrame(500, &frameInfo, &desktopResource);
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) return true; // No new frame
        std::cerr << "Failed to acquire next frame. HRESULT: " << std::hex << hr << std::endl;
        return false;
    }

    // Get the captured texture
    ComPtr<ID3D11Texture2D> capturedTexture;
    hr = desktopResource.As(&capturedTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to get captured texture. HRESULT: " << std::hex << hr << std::endl;
        g_duplication->ReleaseFrame();
        return false;
    }

    // Split the captured frame into left and right halves
    D3D11_BOX leftBox = { 0, 0, 0, 2560, 1440, 1 };
    D3D11_BOX rightBox = { 2560, 0, 0, 5120, 1440, 1 };

    g_context->CopySubresourceRegion(g_leftTexture.Get(), 0, 0, 0, 0, capturedTexture.Get(), 0, &leftBox);
    g_context->CopySubresourceRegion(g_rightTexture.Get(), 0, 0, 0, 0, capturedTexture.Get(), 0, &rightBox);

    std::cout << "Frame split successfully into left and right textures." << std::endl;

    g_duplication->ReleaseFrame();

    // Save left and right textures as PNG files

    // Commenting out image creation for now

    static int frameIndex = 0;
    wchar_t leftFilename[128];
    wchar_t rightFilename[128];
    swprintf_s(leftFilename, L"left_frame_%d.png", frameIndex);
    swprintf_s(rightFilename, L"right_frame_%d.png", frameIndex);

    SaveTextureAsPNGStandalone(g_device.Get(), g_context.Get(), g_leftTexture.Get(), leftFilename);
    SaveTextureAsPNGStandalone(g_device.Get(), g_context.Get(), g_rightTexture.Get(), rightFilename);

    frameIndex++;




    return true;
}

// Main function
int main() {
    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize COM library. HRESULT: " << std::hex << hr << std::endl;
        return -1;
    }

    if (!InitializeCaptureResources()) {
        CoUninitialize();
        return -1;
    }

    CaptureFrame();
    //InitializeCaptureResources();
    //InitializeShaders();
    InitializeMonitorsAndSwapChains(g_leftSwapChain, g_rightSwapChain);
    CreateShaderResourceViews(); // Call the function here
    InitializeStagingTextures();
    InitializeShaders();
    CreateFullScreenQuad();
    //RenderToMonitors(g_leftSwapChain, g_rightSwapChain, g_leftTextureSRV, g_rightTextureSRV);

    std::cout << "Initialization complete. Capturing frames..." << std::endl;



    /*while (true) {
        if (!CaptureFrame()) {
            break;
        }
        CreateShaderResourceViews(); // Call the function here
        InitializeStagingTextures();
        RenderToMonitors(g_leftSwapChain, g_rightSwapChain);
        InspectTexture(g_leftTexture, g_leftStagingTexture, "Left Texture");


    } */

    //CaptureFrame();
    //RenderToMonitors(g_leftSwapChain, g_rightSwapChain);



    //InspectTexture(g_leftTexture, g_leftStagingTexture, "Left Texture");
    while(true);
    CoUninitialize();
    return 0;
}
