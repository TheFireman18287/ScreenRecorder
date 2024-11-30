#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl.h>
#include <chrono>
#include <iostream>
#include <vector>
#include <fstream> // For redirecting std::cout and std::cerr
#include <wincodec.h> // For Windows Imaging Component
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")



using Microsoft::WRL::ComPtr;

// Global variables
ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
ComPtr<IDXGIOutputDuplication> g_duplication;
ComPtr<ID3D11Texture2D> g_leftTexture;
ComPtr<ID3D11Texture2D> g_rightTexture;
ComPtr<IDXGISwapChain> g_leftSwapChain;
ComPtr<IDXGISwapChain> g_rightSwapChain;


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

void RenderToMonitors(ComPtr<IDXGISwapChain> leftSwapChain, ComPtr<IDXGISwapChain> rightSwapChain) {
    // Render left texture
    ComPtr<ID3D11Texture2D> leftBackBuffer;
    HRESULT hr = leftSwapChain->GetBuffer(0, IID_PPV_ARGS(&leftBackBuffer));
    if (SUCCEEDED(hr)) {
        g_context->CopyResource(leftBackBuffer.Get(), g_leftTexture.Get());
        leftSwapChain->Present(1, 0);
    }

    // Render right texture
    ComPtr<ID3D11Texture2D> rightBackBuffer;
    hr = rightSwapChain->GetBuffer(0, IID_PPV_ARGS(&rightBackBuffer));
    if (SUCCEEDED(hr)) {
        g_context->CopyResource(rightBackBuffer.Get(), g_rightTexture.Get());
        rightSwapChain->Present(1, 0);
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

    void CreateSwapChainForMonitor(
        ComPtr<IDXGIOutput> output,
        ComPtr<IDXGISwapChain>&swapChain,
        const wchar_t* windowTitle

    );

    // Initialize monitors and swap chains
    InitializeMonitorsAndSwapChains(g_leftSwapChain, g_rightSwapChain);

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
