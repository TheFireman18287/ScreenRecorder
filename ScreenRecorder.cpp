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




using Microsoft::WRL::ComPtr;

// Global variables
ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<IDXGIOutputDuplication> g_duplication;
ComPtr<ID3D11Texture2D> g_leftTexture;
ComPtr<ID3D11Texture2D> g_rightTexture;
ComPtr<IDXGISwapChain> g_leftSwapChain;
ComPtr<IDXGISwapChain> g_rightSwapChain;
ComPtr<IDXGISwapChain> swapChain;
ComPtr<ID3D11VertexShader> vertexShader;
ComPtr<ID3D11PixelShader> pixelShader;
ComPtr<ID3D11InputLayout> inputLayout;
ComPtr<ID3D11ShaderResourceView> g_leftTextureSRV;
ComPtr<ID3D11ShaderResourceView> g_rightTextureSRV;
ComPtr<ID3D11Texture2D> g_leftStagingTexture;
ComPtr<ID3D11Texture2D> g_rightStagingTexture;
ComPtr<ID3D11RenderTargetView> leftRTV; 
ComPtr<ID3D11RenderTargetView> rightRTV;
//Global variables continue - Split the captured frame into left and right halves
D3D11_BOX leftBox = { 0, 0, 0, 2560, 1440, 1 };
D3D11_BOX rightBox = { 2560, 0, 0, 5120, 1440, 1 }; 


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
// Function to handle window messages (message loop)
void WindowMessageLoop(HWND hWnd) {
    MSG msg;
    BOOL gResult;
    while ((gResult = GetMessage(&msg, hWnd, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void CreateAndShowWindows(HINSTANCE hInstance) {
    const auto pClassName = L"SalarWindowClass";

    // Register window class
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = nullptr;
    wc.hCursor = nullptr;
    wc.hbrBackground = nullptr;
    wc.lpfnWndProc = DefWindowProc; // Use the default window procedure
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = pClassName;
    wc.hIconSm = nullptr;
    RegisterClassEx(&wc);

    // Create window instance 1
    HWND hWnd1 = CreateWindowEx(
        0, pClassName,
        L"WindowInstance1",
        WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
        200, 200, 2560, 1440,
        nullptr, nullptr, hInstance, nullptr
    );

    // Create window instance 2
    HWND hWnd2 = CreateWindowEx(
        0, pClassName,
        L"WindowInstance2",
        WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
        2560, 200, 2560, 1440,
        nullptr, nullptr, hInstance, nullptr
    );

    // Show both windows
    ShowWindow(hWnd1, SW_SHOW);
    ShowWindow(hWnd2, SW_SHOW);
    return;
}
void CreateRenderTargetViewForSwapChain(ComPtr<IDXGISwapChain>& swapChain, ComPtr<ID3D11RenderTargetView>& rtv) {


    // Get the back buffer from the swap chain
    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) {
        std::cerr << "Failed to get the back buffer from swap chain. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    // Create the render target view (RTV)
    hr = g_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &rtv);
    if (FAILED(hr)) {
        std::cerr << "Failed to create render target view. HRESULT: " << std::hex << hr << std::endl;
        return;
    }
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

    //std::cout << "Creating window Instances..." << std::endl;
    //CreateAndShowWindows(hInstance);
   
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

void CreateSwapChainForWindow(
    HWND hwnd,
    ComPtr<IDXGISwapChain>& swapChain,
    UINT width,
    UINT height
) {
    // Define the swap chain description
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = 1;
    swapChainDesc.BufferDesc.Width = width;
    swapChainDesc.BufferDesc.Height = height;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = hwnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = TRUE; // Fullscreen can be configured if needed
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    // Create the Factory 
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        std::cerr << "Failed to create DXGI factory. HRESULT: " << std::hex << hr << std::endl;
        return;
    }
    // Create the swap chain
    hr = factory->CreateSwapChain(g_device.Get(), &swapChainDesc, &swapChain);
    if (FAILED(hr)) {
        std::cerr << "Failed to create swap chain. HRESULT: " << std::hex << hr << std::endl;
        return;
    }

    // Check if swapChain is valid after creation
    if (!swapChain) {
        std::cerr << "Error: swapChain is still null after creation!" << std::endl;
        return;
    }

    std::cout << "Swap chain created successfully! From inside CreateSwapChainForWindowFunction " << std::endl;
}


void CreateSwapChainsForWindows(
    ComPtr<IDXGISwapChain>& leftSwapChain,
    ComPtr<IDXGISwapChain>& rightSwapChain,
    ComPtr<ID3D11RenderTargetView>& leftRTV,
    ComPtr<ID3D11RenderTargetView>& rightRTV
) {
    // Create a window for the left and right render targets
    HWND hwndLeft = CreateRenderWindow(L"Left Window");
    HWND hwndRight = CreateRenderWindow(L"Right Window");

    std::cout << "Finished creating windows "<< std::endl;

    // Create the swap chains for the windows
    CreateSwapChainForWindow(hwndLeft, leftSwapChain, 2560, 1440);
    CreateSwapChainForWindow(hwndRight, rightSwapChain, 2560, 1440);

    std::cout << "Finished creating Swap Chains. " << std::endl;

    if (!leftSwapChain) {
        std::cerr << "Error: leftSwapChain is null!" << std::endl;
    }

    if (!rightSwapChain) {
        std::cerr << "Error: rightSwapChain is null!" << std::endl;
        return;
    }

    if (!g_device || !g_context) {
        std::cerr << "Device or context is null!" << std::endl;
        return;
    }


    std::cout << "Starting to create RenderView for Swapchains. " << std::endl;
    // Now create the render target views for the swap chains
    CreateRenderTargetViewForSwapChain(leftSwapChain, leftRTV);
    CreateRenderTargetViewForSwapChain(rightSwapChain, rightRTV);

    std::cout << "Finished creating RenderTargetView " << std::endl;
}





    void InspectTexture(ComPtr<ID3D11Texture2D> texture, ComPtr<ID3D11Texture2D> stagingTexture, const std::string & name) {
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
   
  

    InitializeShaders();
    CreateSwapChainsForWindows(g_leftSwapChain, g_rightSwapChain, leftRTV, rightRTV);

    //CreateShaderResourceViews(); // Call the function here
    //InitializeStagingTextures();
    //InitializeShaders();
    //CreateFullScreenQuad();
    //RenderToMonitors(g_leftSwapChain, g_rightSwapChain);

    std::cout << "Initialization complete. Capturing frames..." << std::endl;
    CaptureFrame();



    /*while (true) {
        if (!CaptureFrame()) {
            break;
        }
        CreateShaderResourceViews(); // Call the function here
        InitializeStagingTextures();
        RenderToMonitors(g_leftSwapChain, g_rightSwapChain);
        InspectTexture(g_leftTexture, g_leftStagingTexture, "Left Texture");


    } */

    
    //RenderToMonitors(g_leftSwapChain, g_rightSwapChain);



    //InspectTexture(g_leftTexture, g_leftStagingTexture, "Left Texture");
    std::cin.get(); // Wait for user input before exiting
    // Add this after rendering the first frame
    std::cout << "Pausing for 10 seconds to inspect the output..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(999));
    CoUninitialize();
    return 0;
}
