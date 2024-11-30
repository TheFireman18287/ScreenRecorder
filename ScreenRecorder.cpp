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



void CreateSwapChainForMonitor(
    ComPtr<IDXGIOutput> output,
    ComPtr<IDXGISwapChain>& swapChain,
    const wchar_t* windowTitle
);


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
    return true;
}

struct Vertex {
    float x, y, z;
    float u, v;
};

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
}


void CreateShaderResourceViews() {
    HRESULT hr;

    // Create shader resource view for the left texture
    hr = g_device->CreateShaderResourceView(g_leftTexture.Get(), nullptr, &g_leftTextureSRV);
    if (FAILED(hr)) {
        std::cerr << "Failed to create SRV for left texture. HRESULT: " << std::hex << hr << std::endl;
    }

    // Create shader resource view for the right texture
    hr = g_device->CreateShaderResourceView(g_rightTexture.Get(), nullptr, &g_rightTextureSRV);
    if (FAILED(hr)) {
        std::cerr << "Failed to create SRV for right texture. HRESULT: " << std::hex << hr << std::endl;
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
        WS_OVERLAPPEDWINDOW,  // Allow resizing and moving
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

void InspectTexture(ID3D11Texture2D* texture) {
    // Create a staging texture
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.BindFlags = 0;

    ComPtr<ID3D11Texture2D> stagingTexture;
    HRESULT hr = g_device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture);
    if (FAILED(hr)) {
        std::cerr << "Failed to create staging texture. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    // Copy the data
    g_context->CopyResource(stagingTexture.Get(), texture);

    // Map the texture
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = g_context->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(hr)) {
        std::cerr << "Failed to map staging texture. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    // Log the first pixel value
    auto* data = static_cast<uint32_t*>(mappedResource.pData);
    std::cout << "First pixel RGBA: " << std::hex << *data << std::endl;

    g_context->Unmap(stagingTexture.Get(), 0);
}


void RenderToMonitors(ComPtr<IDXGISwapChain> leftSwapChain, ComPtr<IDXGISwapChain> rightSwapChain) {
    HRESULT hr;

    const float clearColor[4] = { 0.2f, 0.2f, 0.2f, 1.0f }; // Gray

    // Render the left texture
    ComPtr<ID3D11Texture2D> leftBackBuffer;
    hr = leftSwapChain->GetBuffer(0, IID_PPV_ARGS(&leftBackBuffer));
    if (SUCCEEDED(hr)) {
        ComPtr<ID3D11RenderTargetView> leftRTV;
        g_device->CreateRenderTargetView(leftBackBuffer.Get(), nullptr, &leftRTV);
        g_context->OMSetRenderTargets(1, leftRTV.GetAddressOf(), nullptr);

        // Bind the vertex buffer
        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        g_context->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

        // Bind the left texture as a shader resource
        g_context->PSSetShaderResources(0, 1, g_leftTextureSRV.GetAddressOf());

        // Clear the render target
        g_context->ClearRenderTargetView(leftRTV.Get(), clearColor);

        // Draw the full-screen quad
        g_context->Draw(4, 0);

        // Present the frame
        leftSwapChain->Present(1, 0);
        std::cout << "Left texture presented." << std::endl;
    }
    else {
        std::cerr << "Failed to get left back buffer. HRESULT: " << std::hex << hr << std::endl;
    }

    // Render the right texture
    ComPtr<ID3D11Texture2D> rightBackBuffer;
    hr = rightSwapChain->GetBuffer(0, IID_PPV_ARGS(&rightBackBuffer));
    if (SUCCEEDED(hr)) {
        ComPtr<ID3D11RenderTargetView> rightRTV;
        g_device->CreateRenderTargetView(rightBackBuffer.Get(), nullptr, &rightRTV);
        g_context->OMSetRenderTargets(1, rightRTV.GetAddressOf(), nullptr);

        // Bind the vertex buffer
        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        g_context->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

        // Bind the right texture as a shader resource
        g_context->PSSetShaderResources(0, 1, g_rightTextureSRV.GetAddressOf());

        // Clear the render target
        g_context->ClearRenderTargetView(rightRTV.Get(), clearColor);

        // Draw the full-screen quad
        g_context->Draw(4, 0);

        // Present the frame
        rightSwapChain->Present(1, 0);
        std::cout << "Right texture presented." << std::endl;
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
    /*
    static int frameIndex = 0;
    wchar_t leftFilename[128];
    wchar_t rightFilename[128];
    swprintf_s(leftFilename, L"left_frame_%d.png", frameIndex);
    swprintf_s(rightFilename, L"right_frame_%d.png", frameIndex);

    SaveTextureAsPNGStandalone(g_device.Get(), g_context.Get(), g_leftTexture.Get(), leftFilename);
    SaveTextureAsPNGStandalone(g_device.Get(), g_context.Get(), g_rightTexture.Get(), rightFilename);

    frameIndex++;
    */


    
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

    InitializeShaders(); //To Initilize shaders and compile them

    // Initialize monitors and swap chains
    InitializeMonitorsAndSwapChains(g_leftSwapChain, g_rightSwapChain);

    //Initalize Vertex Texture
    CreateFullScreenQuad();

    std::cout << "Initialization complete. Capturing frames..." << std::endl;

    while (true) {
        if (!CaptureFrame()) {
            break;
        }

        // Render the split textures to monitors
        RenderToMonitors(g_leftSwapChain, g_rightSwapChain);
    }

    CoUninitialize();
    return 0;
}
