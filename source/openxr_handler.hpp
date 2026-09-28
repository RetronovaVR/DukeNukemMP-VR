#pragma once
#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi.h>
#include <vector>
#include <immintrin.h>
#include <omp.h>

static inline void CopyRowStreamingAVX2(BYTE* dst, const BYTE* src, size_t size) {
    size_t i = 0;
    for (; i + 32 <= size; i += 32) {
        __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
        _mm256_stream_si256(reinterpret_cast<__m256i*>(dst + i), chunk);
    }
    for (; i < size; i++) {
        dst[i] = src[i];
    }
}

static inline void CopyRowWithGammaLUT(BYTE* dst, const BYTE* src, size_t size, const uint8_t* lut) {
    const uint32_t* src32 = reinterpret_cast<const uint32_t*>(src);
    uint32_t* dst32 = reinterpret_cast<uint32_t*>(dst);
    size_t count = size / 4;
    for (size_t i = 0; i < count; i++) {
        uint32_t px = src32[i];
        uint32_t b = lut[px & 0xFF];
        uint32_t g = lut[(px >> 8) & 0xFF];
        uint32_t r = lut[(px >> 16) & 0xFF];
        uint32_t a = px & 0xFF000000;
        dst32[i] = a | (r << 16) | (g << 8) | b;
    }
}

#define XR_USE_GRAPHICS_API_D3D11
#define XR_NO_PROT_TYPES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "vr_config.hpp"
#include "cull_fix.hpp"

#define XR_LOADER_INFO_STRUCT_VERSION 1
#define XR_RUNTIME_INFO_STRUCT_VERSION 1

inline void LogXR(const char* fmt, ...) {
    FILE* f = nullptr;
    if (fopen_s(&f, "vr_openxr.log", "a") == 0 && f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fprintf(f, "\n");
        fclose(f);
    }
}

typedef struct XrNegotiateLoaderInfo {
    XrStructureType structType;
    const void* next;
    uint32_t structVersion;
    size_t structSize;
    uint32_t minInterfaceVersion;
    uint32_t maxInterfaceVersion;
    uint32_t minApiVersion;
    uint32_t maxApiVersion;
} XrNegotiateLoaderInfo;

typedef struct XrNegotiateRuntimeRequest {
    XrStructureType structType;
    void* next;
    uint32_t structVersion;
    size_t structSize;
    uint32_t runtimeInterfaceVersion;
    uint32_t runtimeApiVersion;
    PFN_xrGetInstanceProcAddr getInstanceProcAddr;
} XrNegotiateRuntimeRequest;

typedef XrResult (XRAPI_PTR *PFN_xrNegotiateLoaderRuntimeInterface)(
    const XrNegotiateLoaderInfo* loaderInfo,
    XrNegotiateRuntimeRequest* runtimeRequest);

class OpenXRHandler {
public:
    bool m_bInitialized = false;
    bool m_bSessionRunning = false;

    // Auto-IPD state
    bool m_bIPDInitialized = false;    // true once headset IPD has been latched
    bool m_bIPDManualOverride = false; // true if user pressed Numpad +/- this session
    bool m_bIn3DScene = false;         // true during active 3D gameplay, false in menus

    // Head tracking pose
    float m_headPos[3] = { 0.0f, 0.0f, 0.0f };
    float m_headRot[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; // Quaternion (x, y, z, w)

    // Recenter reference offset
    float m_recenterPos[3] = { 0.0f, 0.0f, 0.0f };
    float m_recenterRot[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    bool m_bInitialRecentered = false;

    // Game FOV tracking (radians)
    float m_gameFovY = 0.55f;
    float m_gameFovX = 0.55f;
    float m_nearZ = 0.5f;
    float m_farZ = 10000.0f;
    // Head tracking views stored from xrLocateViews
    XrView m_views[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
    bool m_bViewsLocated = false;

    // D3D11 bridge objects
    ID3D11Device* m_pD3D11Device = nullptr;
    ID3D11DeviceContext* m_pD3D11Context = nullptr;

    // Improvement #2: DXGI Shared Texture (D3D9 renders into this, D3D11 reads it — no CPU roundtrip)
    ID3D11Texture2D*     m_pD3D11SharedTex  = nullptr; // D3D11 side of shared texture
    IDirect3DTexture9*   m_pD3D9SharedTex   = nullptr; // D3D9 side (POOL_DEFAULT, shared handle)
    IDirect3DSurface9*   m_pD3D9SharedSurf  = nullptr; // Level-0 surface of D3D9 shared texture
    HANDLE               m_hSharedHandle    = NULL;
    UINT m_nStagingWidth   = 0;
    UINT m_nStagingHeight  = 0;
    UINT m_nTransferWidth  = 0;
    UINT m_nTransferHeight = 0;

    // Improvement #6: Hardware GPU Downscale Surface (4K SSAA -> 1440p transfer for high-FPS VR)
    IDirect3DSurface9*   m_pD3D9DownscaleSurf = nullptr;

    // Improvement #4: Dedicated D3D9 mirror surface (left-eye only, for clean monitor output)
    IDirect3DSurface9*   m_pD3D9MirrorSurf  = nullptr;
    IDirect3DQuery9*     m_pD3D9FlushQuery  = nullptr;

    // Improvement #1: Frame state stored across BeginFrame / RenderDirect
    XrFrameState m_frameState    = { XR_TYPE_FRAME_STATE };
    bool         m_bFrameStarted = false;

    // Improvement #3: Double-Buffered Asynchronous AVX2 Fallback Path (Zero GPU Stall + SIMD Streaming)
    IDirect3DSurface9* m_pCPUFallbackSurf[2] = { nullptr, nullptr };
    int                m_nFallbackIndex      = 0;
    bool               m_bFallbackPrimed     = false;
    ID3D11Texture2D*   m_pCPUFallbackTex     = nullptr;
    XrView             m_fallbackViews[2][2] = {}; // [bufferIdx 0..1][eye 0..1] Exact tracking pose per buffer

    // Improvement #5: Depth submission swapchains (XR_KHR_composition_layer_depth)
    XrSwapchain m_depthSwapchains[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
    std::vector<XrSwapchainImageD3D11KHR> m_depthSwapchainImages[2];
    std::vector<ID3D11DepthStencilView*> m_depthDSVs[2];
    bool m_bDepthSupported = false;

    // Real-time VR Gamma & Brightness LUT
    uint8_t m_gammaLUT[256] = {};
    float   m_cachedGamma = -1.0f;
    float   m_cachedBrightness = -1.0f;

    void UpdateGammaLUT(float gamma, float brightness) {
        if (gamma <= 0.1f) gamma = 1.0f;
        if (brightness <= 0.0f) brightness = 1.0f;
        m_cachedGamma = gamma;
        m_cachedBrightness = brightness;
        float invGamma = 1.0f / gamma;
        for (int i = 0; i < 256; i++) {
            float v = (float)i / 255.0f;
            float adj = powf(v, invGamma) * brightness;
            if (adj < 0.0f) adj = 0.0f;
            if (adj > 1.0f) adj = 1.0f;
            m_gammaLUT[i] = (uint8_t)(adj * 255.0f + 0.5f);
        }
        LogXR("Updated VR Gamma LUT: Gamma=%.2f, Brightness=%.2f", gamma, brightness);
    }

    // OpenXR function pointers
    HMODULE m_hRuntime = NULL;
    PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr = NULL;
    PFN_xrCreateInstance xrCreateInstance = NULL;
    PFN_xrDestroyInstance xrDestroyInstance = NULL;
    PFN_xrGetSystem xrGetSystem = NULL;
    PFN_xrCreateSession xrCreateSession = NULL;
    PFN_xrDestroySession xrDestroySession = NULL;
    PFN_xrBeginSession xrBeginSession = NULL;
    PFN_xrEndSession xrEndSession = NULL;
    PFN_xrCreateReferenceSpace xrCreateReferenceSpace = NULL;
    PFN_xrDestroySpace xrDestroySpace = NULL;
    PFN_xrLocateSpace xrLocateSpace = NULL;
    PFN_xrLocateViews xrLocateViews = NULL;
    PFN_xrPollEvent xrPollEvent = NULL;
    PFN_xrCreateSwapchain xrCreateSwapchain = NULL;
    PFN_xrDestroySwapchain xrDestroySwapchain = NULL;
    PFN_xrEnumerateSwapchainImages xrEnumerateSwapchainImages = NULL;
    PFN_xrAcquireSwapchainImage xrAcquireSwapchainImage = NULL;
    PFN_xrWaitSwapchainImage xrWaitSwapchainImage = NULL;
    PFN_xrReleaseSwapchainImage xrReleaseSwapchainImage = NULL;
    PFN_xrWaitFrame xrWaitFrame = NULL;
    PFN_xrBeginFrame xrBeginFrame = NULL;
    PFN_xrEndFrame xrEndFrame = NULL;
    PFN_xrGetD3D11GraphicsRequirementsKHR xrGetD3D11GraphicsRequirementsKHR = NULL;
    PFN_xrEnumerateViewConfigurationViews xrEnumerateViewConfigurationViews = NULL;
    PFN_xrEnumerateInstanceExtensionProperties xrEnumerateInstanceExtensionProperties = NULL;

    XrInstance m_instance = XR_NULL_HANDLE;
    XrSystemId m_systemId = XR_NULL_SYSTEM_ID;
    XrSession m_session = XR_NULL_HANDLE;
    XrSpace m_localSpace = XR_NULL_HANDLE;
    XrSpace m_viewSpace = XR_NULL_HANDLE;

    // Swapchains for Left and Right eye
    XrSwapchain m_swapchains[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
    std::vector<XrSwapchainImageD3D11KHR> m_swapchainImages[2];
    uint32_t m_swapchainWidth  = 1280; // Set properly by xrEnumerateViewConfigurationViews
    uint32_t m_swapchainHeight = 1440;

    bool Init() {
        if (m_bInitialized) return true;
        LogXR("OpenXRHandler::Init starting...");

        // 1. Try loading standard openxr_loader.dll first
        m_hRuntime = LoadLibraryA("openxr_loader.dll");
        if (m_hRuntime) {
            LogXR("Loaded standard openxr_loader.dll successfully!");
            xrGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr)GetProcAddress(m_hRuntime, "xrGetInstanceProcAddr");
            if (xrGetInstanceProcAddr) {
                xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrCreateInstance", (PFN_xrVoidFunction*)&xrCreateInstance);
                xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrDestroyInstance", (PFN_xrVoidFunction*)&xrDestroyInstance);
            }
        }

        // 2. Fallback to direct runtime if openxr_loader not available
        if (!xrCreateInstance) {
            const char* defaultPath = "C:\\Program Files\\Virtual Desktop Streamer\\OpenXR\\virtualdesktop-openxr-32.dll";
            m_hRuntime = LoadLibraryA(defaultPath);
            if (!m_hRuntime) {
                HKEY hKey;
                if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Khronos\\OpenXR\\1", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                    char regPath[MAX_PATH];
                    DWORD size = sizeof(regPath);
                    if (RegQueryValueExA(hKey, "ActiveRuntime", NULL, NULL, (LPBYTE)regPath, &size) == ERROR_SUCCESS) {
                        char dllPath[MAX_PATH];
                        strcpy_s(dllPath, regPath);
                        char* ext = strstr(dllPath, ".json");
                        if (ext) strcpy_s(ext, 5, ".dll");
                        m_hRuntime = LoadLibraryA(dllPath);
                    }
                    RegCloseKey(hKey);
                }
            }

            if (m_hRuntime) {
                PFN_xrNegotiateLoaderRuntimeInterface pfnNegotiate = 
                    (PFN_xrNegotiateLoaderRuntimeInterface)GetProcAddress(m_hRuntime, "xrNegotiateLoaderRuntimeInterface");
                if (pfnNegotiate) {
                    XrNegotiateLoaderInfo loaderInfo = {};
                    loaderInfo.structType = (XrStructureType)1; // XR_LOADER_INTERFACE_STRUCT_LOADER_INFO
                    loaderInfo.structVersion = 1;
                    loaderInfo.structSize = sizeof(loaderInfo);
                    loaderInfo.minInterfaceVersion = 1;
                    loaderInfo.maxInterfaceVersion = 1;
                    loaderInfo.minApiVersion = (uint32_t)XR_MAKE_VERSION(1, 0, 0);
                    loaderInfo.maxApiVersion = (uint32_t)XR_MAKE_VERSION(1, 1, 0);

                    XrNegotiateRuntimeRequest runtimeReq = {};
                    runtimeReq.structType = (XrStructureType)3; // XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST
                    runtimeReq.structVersion = 1;
                    runtimeReq.structSize = sizeof(runtimeReq);

                    if (pfnNegotiate(&loaderInfo, &runtimeReq) == XR_SUCCESS && runtimeReq.getInstanceProcAddr) {
                        xrGetInstanceProcAddr = runtimeReq.getInstanceProcAddr;
                        xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrCreateInstance", (PFN_xrVoidFunction*)&xrCreateInstance);
                        xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrDestroyInstance", (PFN_xrVoidFunction*)&xrDestroyInstance);
                    }
                }
            }
        }

        if (!xrCreateInstance) {
            LogXR("ERROR: xrCreateInstance function pointer is null!");
            return false;
        }

        std::vector<const char*> enabledExtensions;
        enabledExtensions.push_back("XR_KHR_D3D11_enable");

        // Query available instance extensions
        if (xrGetInstanceProcAddr) {
            xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties", (PFN_xrVoidFunction*)&xrEnumerateInstanceExtensionProperties);
            if (xrEnumerateInstanceExtensionProperties && g_VRConfig.bEnableDepthSubmission) {
                uint32_t extCount = 0;
                xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr);
                std::vector<XrExtensionProperties> exts(extCount, { XR_TYPE_EXTENSION_PROPERTIES });
                xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, exts.data());
                for (auto& ext : exts) {
                    if (strcmp(ext.extensionName, "XR_KHR_composition_layer_depth") == 0) {
                        m_bDepthSupported = true;
                        enabledExtensions.push_back("XR_KHR_composition_layer_depth");
                        LogXR("Extension XR_KHR_composition_layer_depth detected and enabled for instance");
                        break;
                    }
                }
            }
        }

        XrInstanceCreateInfo createInfo = {};
        createInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
        strcpy_s(createInfo.applicationInfo.applicationName, "DukeNukemMP_VR");
        createInfo.applicationInfo.applicationVersion = 1;
        strcpy_s(createInfo.applicationInfo.engineName, "Prism3D");
        createInfo.applicationInfo.engineVersion = 1;
        createInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
        createInfo.enabledExtensionCount = (uint32_t)enabledExtensions.size();
        createInfo.enabledExtensionNames = enabledExtensions.data();

        XrResult res = xrCreateInstance(&createInfo, &m_instance);
        if (res != XR_SUCCESS) {
            LogXR("xrCreateInstance with extensions failed (%d), retrying with D3D11 only...", res);
            m_bDepthSupported = false;
            const char* fallbackExt[] = { "XR_KHR_D3D11_enable" };
            createInfo.enabledExtensionCount = 1;
            createInfo.enabledExtensionNames = fallbackExt;
            res = xrCreateInstance(&createInfo, &m_instance);
            if (res != XR_SUCCESS) {
                LogXR("xrCreateInstance failed (%d), retrying without extensions...", res);
                createInfo.enabledExtensionCount = 0;
                createInfo.enabledExtensionNames = nullptr;
                res = xrCreateInstance(&createInfo, &m_instance);
                if (res != XR_SUCCESS) {
                    LogXR("ERROR: xrCreateInstance failed: %d", res);
                    return false;
                }
            }
        }
        LogXR("xrCreateInstance succeeded! Instance: %p", m_instance);

        xrGetInstanceProcAddr(m_instance, "xrGetSystem", (PFN_xrVoidFunction*)&xrGetSystem);
        xrGetInstanceProcAddr(m_instance, "xrCreateSession", (PFN_xrVoidFunction*)&xrCreateSession);
        xrGetInstanceProcAddr(m_instance, "xrDestroySession", (PFN_xrVoidFunction*)&xrDestroySession);
        xrGetInstanceProcAddr(m_instance, "xrBeginSession", (PFN_xrVoidFunction*)&xrBeginSession);
        xrGetInstanceProcAddr(m_instance, "xrEndSession", (PFN_xrVoidFunction*)&xrEndSession);
        xrGetInstanceProcAddr(m_instance, "xrCreateReferenceSpace", (PFN_xrVoidFunction*)&xrCreateReferenceSpace);
        xrGetInstanceProcAddr(m_instance, "xrDestroySpace", (PFN_xrVoidFunction*)&xrDestroySpace);
        xrGetInstanceProcAddr(m_instance, "xrLocateSpace", (PFN_xrVoidFunction*)&xrLocateSpace);
        xrGetInstanceProcAddr(m_instance, "xrLocateViews", (PFN_xrVoidFunction*)&xrLocateViews);
        xrGetInstanceProcAddr(m_instance, "xrPollEvent", (PFN_xrVoidFunction*)&xrPollEvent);
        xrGetInstanceProcAddr(m_instance, "xrCreateSwapchain", (PFN_xrVoidFunction*)&xrCreateSwapchain);
        xrGetInstanceProcAddr(m_instance, "xrDestroySwapchain", (PFN_xrVoidFunction*)&xrDestroySwapchain);
        xrGetInstanceProcAddr(m_instance, "xrEnumerateSwapchainImages", (PFN_xrVoidFunction*)&xrEnumerateSwapchainImages);
        xrGetInstanceProcAddr(m_instance, "xrAcquireSwapchainImage", (PFN_xrVoidFunction*)&xrAcquireSwapchainImage);
        xrGetInstanceProcAddr(m_instance, "xrWaitSwapchainImage", (PFN_xrVoidFunction*)&xrWaitSwapchainImage);
        xrGetInstanceProcAddr(m_instance, "xrReleaseSwapchainImage", (PFN_xrVoidFunction*)&xrReleaseSwapchainImage);
        xrGetInstanceProcAddr(m_instance, "xrWaitFrame", (PFN_xrVoidFunction*)&xrWaitFrame);
        xrGetInstanceProcAddr(m_instance, "xrBeginFrame", (PFN_xrVoidFunction*)&xrBeginFrame);
        xrGetInstanceProcAddr(m_instance, "xrEndFrame", (PFN_xrVoidFunction*)&xrEndFrame);
        xrGetInstanceProcAddr(m_instance, "xrGetD3D11GraphicsRequirementsKHR", (PFN_xrVoidFunction*)&xrGetD3D11GraphicsRequirementsKHR);
        xrGetInstanceProcAddr(m_instance, "xrEnumerateViewConfigurationViews", (PFN_xrVoidFunction*)&xrEnumerateViewConfigurationViews);

        XrSystemGetInfo sysInfo = {};
        sysInfo.type = XR_TYPE_SYSTEM_GET_INFO;
        sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XrResult sysRes = xrGetSystem(m_instance, &sysInfo, &m_systemId);
        if (sysRes != XR_SUCCESS) {
            LogXR("ERROR: xrGetSystem failed: %d (Is Quest 3 connected?)", sysRes);
            return false;
        }
        LogXR("xrGetSystem succeeded! SystemId: %llu", m_systemId);

        // Query D3D11 Graphics Requirements to find the exact GPU Adapter
        XrGraphicsRequirementsD3D11KHR reqs = { XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR };
        if (xrGetD3D11GraphicsRequirementsKHR) {
            xrGetD3D11GraphicsRequirementsKHR(m_instance, m_systemId, &reqs);
            LogXR("D3D11 Requirements: Adapter LUID %08x:%08x, MinFeatureLevel: %d", reqs.adapterLuid.HighPart, reqs.adapterLuid.LowPart, reqs.minFeatureLevel);
        }

        // Find matching DXGI Adapter
        IDXGIFactory1* pFactory = nullptr;
        IDXGIAdapter1* pTargetAdapter = nullptr;
        if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory))) {
            IDXGIAdapter1* pAdapter = nullptr;
            for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
                DXGI_ADAPTER_DESC1 desc;
                pAdapter->GetDesc1(&desc);
                if (memcmp(&desc.AdapterLuid, &reqs.adapterLuid, sizeof(LUID)) == 0) {
                    pTargetAdapter = pAdapter;
                    LogXR("Found exact matching DXGI adapter for OpenXR: %ls", desc.Description);
                    break;
                }
                pAdapter->Release();
            }
            pFactory->Release();
        }

        // Initialize D3D11 Device on matching Adapter
        D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL featureLevel;
        D3D_DRIVER_TYPE driverType = pTargetAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
        HRESULT hr = D3D11CreateDevice(pTargetAdapter, driverType, NULL, 0, featureLevels, 3, D3D11_SDK_VERSION, &m_pD3D11Device, &featureLevel, &m_pD3D11Context);
        if (pTargetAdapter) {
            pTargetAdapter->Release();
        }

        if (FAILED(hr)) {
            LogXR("ERROR: D3D11CreateDevice failed: 0x%08x", hr);
            return false;
        }
        LogXR("D3D11 Device created successfully!");

        // Create Session with D3D11 Binding
        XrGraphicsBindingD3D11KHR graphicsBinding = { XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };
        graphicsBinding.device = m_pD3D11Device;

        XrSessionCreateInfo sessionCreateInfo = {};
        sessionCreateInfo.type = XR_TYPE_SESSION_CREATE_INFO;
        sessionCreateInfo.next = &graphicsBinding;
        sessionCreateInfo.systemId = m_systemId;

        XrResult sessRes = xrCreateSession(m_instance, &sessionCreateInfo, &m_session);
        if (sessRes != XR_SUCCESS) {
            LogXR("ERROR: xrCreateSession failed: %d", sessRes);
            return false;
        }
        LogXR("xrCreateSession succeeded! Session: %p", m_session);

        // Create Reference Spaces
        XrReferenceSpaceCreateInfo spaceCreateInfo = {};
        spaceCreateInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
        spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        spaceCreateInfo.poseInReferenceSpace.orientation.w = 1.0f;
        xrCreateReferenceSpace(m_session, &spaceCreateInfo, &m_localSpace);

        spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        xrCreateReferenceSpace(m_session, &spaceCreateInfo, &m_viewSpace);

        // Query headset recommended resolution for informational logging
        uint32_t viewCount = 0;
        if (xrEnumerateViewConfigurationViews &&
            xrEnumerateViewConfigurationViews(m_instance, m_systemId,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr) == XR_SUCCESS
            && viewCount == 2) {
            std::vector<XrViewConfigurationView> vcViews(viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
            if (xrEnumerateViewConfigurationViews(m_instance, m_systemId,
                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, vcViews.data()) == XR_SUCCESS) {
                LogXR("Headset recommended per-eye resolution: %dx%d (game renders at %dx%d)",
                    vcViews[0].recommendedImageRectWidth, vcViews[0].recommendedImageRectHeight,
                    g_VRConfig.iForcedWidth / 2, g_VRConfig.iForcedHeight);
            }
        }

        // Set swapchain size to target transfer resolution (Width/2 x Height)
        // If downscaled on GPU, OpenXR compositor scales and filters onto the headset display.
        UINT initTransferW = g_VRConfig.GetTransferWidth(g_VRConfig.iForcedWidth);
        UINT initTransferH = g_VRConfig.GetTransferHeight(g_VRConfig.iForcedWidth, g_VRConfig.iForcedHeight);

        m_swapchainWidth  = initTransferW / 2;
        m_swapchainHeight = initTransferH;
        LogXR("OpenXR swapchain size set: %dx%d per eye (game renders %dx%d, transfer %dx%d)",
            m_swapchainWidth, m_swapchainHeight, g_VRConfig.iForcedWidth, g_VRConfig.iForcedHeight, initTransferW, initTransferH);

        InitSwapchains(m_swapchainWidth, m_swapchainHeight);

        if (m_bDepthSupported && g_VRConfig.bEnableDepthSubmission) {
            InitDepthSwapchains(m_swapchainWidth, m_swapchainHeight);
        }

        m_bInitialized = true;
        LogXR("OpenXR initialized successfully and ready for VR rendering!");
        return true;
    }

    void InitSwapchains(uint32_t width, uint32_t height) {
        if (!m_session || !xrCreateSwapchain) return;

        m_swapchainWidth = width;
        m_swapchainHeight = height;

        for (int eye = 0; eye < 2; eye++) {
            if (m_swapchains[eye] != XR_NULL_HANDLE) {
                xrDestroySwapchain(m_swapchains[eye]);
                m_swapchains[eye] = XR_NULL_HANDLE;
            }
            m_swapchainImages[eye].clear();

            XrSwapchainCreateInfo swapchainInfo = {};
            swapchainInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
            swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            swapchainInfo.format = DXGI_FORMAT_B8G8R8A8_UNORM;
            swapchainInfo.sampleCount = 1;
            swapchainInfo.width = m_swapchainWidth;
            swapchainInfo.height = m_swapchainHeight;
            swapchainInfo.faceCount = 1;
            swapchainInfo.arraySize = 1;
            swapchainInfo.mipCount = 1;

            XrResult scRes = xrCreateSwapchain(m_session, &swapchainInfo, &m_swapchains[eye]);
            if (scRes == XR_SUCCESS) {
                uint32_t imgCount = 0;
                xrEnumerateSwapchainImages(m_swapchains[eye], 0, &imgCount, nullptr);
                m_swapchainImages[eye].resize(imgCount, { XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
                xrEnumerateSwapchainImages(m_swapchains[eye], imgCount, &imgCount, (XrSwapchainImageBaseHeader*)m_swapchainImages[eye].data());
                LogXR("Eye %d Swapchain created: %dx%d, %d images", eye, width, height, imgCount);
            } else {
                LogXR("ERROR: xrCreateSwapchain for eye %d failed: %d", eye, scRes);
            }
        }
    }

    void InitDepthSwapchains(uint32_t width, uint32_t height) {
        if (!m_session || !xrCreateSwapchain) return;
        for (int eye = 0; eye < 2; eye++) {
            if (m_depthSwapchains[eye] != XR_NULL_HANDLE) {
                xrDestroySwapchain(m_depthSwapchains[eye]);
                m_depthSwapchains[eye] = XR_NULL_HANDLE;
            }
            for (auto& dsv : m_depthDSVs[eye]) {
                if (dsv) { dsv->Release(); dsv = nullptr; }
            }
            m_depthDSVs[eye].clear();
            m_depthSwapchainImages[eye].clear();

            XrSwapchainCreateInfo depthInfo = {};
            depthInfo.type       = XR_TYPE_SWAPCHAIN_CREATE_INFO;
            depthInfo.usageFlags = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            depthInfo.format     = DXGI_FORMAT_D32_FLOAT;
            depthInfo.sampleCount = 1;
            depthInfo.width      = width;
            depthInfo.height     = height;
            depthInfo.faceCount  = 1;
            depthInfo.arraySize  = 1;
            depthInfo.mipCount   = 1;

            if (xrCreateSwapchain(m_session, &depthInfo, &m_depthSwapchains[eye]) == XR_SUCCESS) {
                uint32_t imgCount = 0;
                xrEnumerateSwapchainImages(m_depthSwapchains[eye], 0, &imgCount, nullptr);
                m_depthSwapchainImages[eye].resize(imgCount, { XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR });
                xrEnumerateSwapchainImages(m_depthSwapchains[eye], imgCount, &imgCount,
                    (XrSwapchainImageBaseHeader*)m_depthSwapchainImages[eye].data());

                m_depthDSVs[eye].resize(imgCount, nullptr);
                for (uint32_t i = 0; i < imgCount; i++) {
                    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
                    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
                    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
                    dsvDesc.Texture2D.MipSlice = 0;
                    m_pD3D11Device->CreateDepthStencilView(m_depthSwapchainImages[eye][i].texture, &dsvDesc, &m_depthDSVs[eye][i]);
                }
                LogXR("Depth swapchain eye %d: %dx%d, %d images and DSVs created", eye, width, height, imgCount);
            }
        }
    }

    void Update() {
        if (GetAsyncKeyState(VK_F12) & 1) {
            Recenter();
        }

        // Real-time Positional Tracking Scale adjustment (F11 = +0.1, Shift+F11 = -0.1, F10 = reset to 1.0)
        if (GetAsyncKeyState(VK_F11) & 1) {
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                g_VRConfig.fTrackingPosScale = (g_VRConfig.fTrackingPosScale > 0.15f) ? (g_VRConfig.fTrackingPosScale - 0.1f) : 0.0f;
            } else {
                g_VRConfig.fTrackingPosScale += 0.1f;
            }
            LogXR("TrackingPosScale: %.2f", g_VRConfig.fTrackingPosScale);
        }
        if (GetAsyncKeyState(VK_F10) & 1) {
            g_VRConfig.fTrackingPosScale = 1.0f;
            LogXR("TrackingPosScale reset to: 1.00 (natural 1:1)");
        }

        // Real-time World Scale adjustment (Numpad 9 = +1.0, Shift+Numpad 9 = +5.0, Numpad 7 = -1.0, Numpad 8 = reset to 24.0)
        if (GetAsyncKeyState(VK_NUMPAD9) & 1) {
            float step = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 5.0f : 1.0f;
            g_VRConfig.fWorldScale += step;
            LogXR("WorldScale increased to: %.1f (Duke height = %.2fm)", g_VRConfig.fWorldScale, 48.0f / g_VRConfig.fWorldScale);
        }
        if (GetAsyncKeyState(VK_NUMPAD7) & 1) {
            float step = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 5.0f : 1.0f;
            g_VRConfig.fWorldScale = (g_VRConfig.fWorldScale > step + 0.5f) ? (g_VRConfig.fWorldScale - step) : 1.0f;
            LogXR("WorldScale decreased to: %.1f (Duke height = %.2fm)", g_VRConfig.fWorldScale, 48.0f / g_VRConfig.fWorldScale);
        }
        if (GetAsyncKeyState(VK_NUMPAD8) & 1) {
            g_VRConfig.fWorldScale = 24.0f;
            LogXR("WorldScale reset to: 24.0 (1m = 24 units, Duke height = 2.0m)");
        }

        // Real-time camera distance adjustment (Page Up = pull camera back / zoom out, Page Down = zoom in)
        if (GetAsyncKeyState(VK_PRIOR) & 1) { // Page Up — pull camera back
            float step = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 5.0f : 1.0f;
            g_VRConfig.fCameraDistance += step;
            LogXR("Camera Distance: %.1f", g_VRConfig.fCameraDistance);
        }
        if (GetAsyncKeyState(VK_NEXT) & 1) { // Page Down — push camera forward
            float step = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 5.0f : 1.0f;
            g_VRConfig.fCameraDistance -= step;
            LogXR("Camera Distance: %.1f", g_VRConfig.fCameraDistance);
        }
        if (GetAsyncKeyState(VK_HOME) & 1) { // Home: Reset
            g_VRConfig.fCameraDistance = 3.5f;
            LogXR("Camera Distance reset to: 3.50");
        }

        // Real-time IPD adjustment (Numpad + / Numpad -)
        // Pressing either key disables auto-IPD for this session so the value is preserved.
        if (GetAsyncKeyState(VK_ADD) & 1) {
            g_VRConfig.fIPD += 0.005f;
            m_bIPDManualOverride = true;
            LogXR("VR IPD increased to: %.3f (manual override)", g_VRConfig.fIPD);
        }
        if (GetAsyncKeyState(VK_SUBTRACT) & 1) {
            g_VRConfig.fIPD = (g_VRConfig.fIPD > 0.015f) ? (g_VRConfig.fIPD - 0.005f) : 0.010f;
            m_bIPDManualOverride = true;
            LogXR("VR IPD decreased to: %.3f (manual override)", g_VRConfig.fIPD);
        }

        // F8: Toggle Prism3D Frustum Culling Fix (on/off)
        if (GetAsyncKeyState(VK_F8) & 1) {
            g_VRConfig.bDisableCulling = !g_VRConfig.bDisableCulling;
            CullFix::SetCullingDisabled(g_VRConfig.bDisableCulling);
            LogXR("Frustum Culling Fix: %s", g_VRConfig.bDisableCulling ? "ENABLED (culling disabled, all objects stay visible)" : "DISABLED (original engine culling)");
        }

        // [ and ] (VK_OEM_4 and VK_OEM_6): Adjust HUD Scale
        if (GetAsyncKeyState(VK_OEM_6) & 1) { // ] key — enlarge HUD
            g_VRConfig.fHudScale = (g_VRConfig.fHudScale + 0.05f < 1.2f) ? (g_VRConfig.fHudScale + 0.05f) : 1.2f;
            LogXR("HUD Scale: %.2f", g_VRConfig.fHudScale);
        }
        if (GetAsyncKeyState(VK_OEM_4) & 1) { // [ key — shrink HUD
            g_VRConfig.fHudScale = (g_VRConfig.fHudScale - 0.05f > 0.4f) ? (g_VRConfig.fHudScale - 0.05f) : 0.4f;
            LogXR("HUD Scale: %.2f", g_VRConfig.fHudScale);
        }

        // Insert and Delete: Adjust HUD Distance/Depth
        if (GetAsyncKeyState(VK_INSERT) & 1) { // Insert key — move HUD closer
            g_VRConfig.fHudDepth = (g_VRConfig.fHudDepth - 0.1f > 0.0f) ? (g_VRConfig.fHudDepth - 0.1f) : 0.0f;
            LogXR("HUD Depth: %.2f (closer)", g_VRConfig.fHudDepth);
        }
        if (GetAsyncKeyState(VK_DELETE) & 1) { // Delete key — move HUD further
            g_VRConfig.fHudDepth = (g_VRConfig.fHudDepth + 0.1f < 2.5f) ? (g_VRConfig.fHudDepth + 0.1f) : 2.5f;
            LogXR("HUD Depth: %.2f (further)", g_VRConfig.fHudDepth);
        }

        // Numpad * / Numpad / : Adjust VR Gamma in real time
        if (GetAsyncKeyState(VK_MULTIPLY) & 1) { // Numpad * : Increase Gamma
            g_VRConfig.fGamma += 0.05f;
            UpdateGammaLUT(g_VRConfig.fGamma, g_VRConfig.fBrightness);
            LogXR("VR Gamma increased to: %.2f", g_VRConfig.fGamma);
        }
        if (GetAsyncKeyState(VK_DIVIDE) & 1) { // Numpad / : Decrease Gamma
            g_VRConfig.fGamma = (g_VRConfig.fGamma > 0.55f) ? (g_VRConfig.fGamma - 0.05f) : 0.50f;
            UpdateGammaLUT(g_VRConfig.fGamma, g_VRConfig.fBrightness);
            LogXR("VR Gamma decreased to: %.2f", g_VRConfig.fGamma);
        }

        // ── Poll OpenXR Events & Head Tracking ────────────────────────────────
        if (m_bInitialized) {
            // Poll OpenXR events
            XrEventDataBuffer eventData = {};
            eventData.type = XR_TYPE_EVENT_DATA_BUFFER;
            while (xrPollEvent && xrPollEvent(m_instance, &eventData) == XR_SUCCESS) {
                if (eventData.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                    XrEventDataSessionStateChanged* sessEvent = (XrEventDataSessionStateChanged*)&eventData;
                    LogXR("OpenXR Session State Changed: %d", sessEvent->state);
                    if (sessEvent->state == XR_SESSION_STATE_READY) {
                        XrSessionBeginInfo beginInfo = {};
                        beginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
                        beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        XrResult bRes = xrBeginSession(m_session, &beginInfo);
                        if (bRes == XR_SUCCESS) {
                            m_bSessionRunning = true;
                            LogXR("xrBeginSession SUCCESS! VR session running in headset.");
                        } else {
                            LogXR("ERROR: xrBeginSession failed: %d", bRes);
                        }
                    } else if (sessEvent->state == XR_SESSION_STATE_STOPPING) {
                        if (xrEndSession) xrEndSession(m_session);
                        m_bSessionRunning = false;
                        LogXR("OpenXR Session stopping.");
                    }
                }
                eventData.type = XR_TYPE_EVENT_DATA_BUFFER;
            }

            // Query head pose using predicted display time for minimum latency (#2)
            XrTime queryTime = m_bFrameStarted ? m_frameState.predictedDisplayTime : 0;
            if (m_localSpace != XR_NULL_HANDLE && m_viewSpace != XR_NULL_HANDLE && xrLocateSpace) {
                XrSpaceLocation loc = {};
                loc.type = XR_TYPE_SPACE_LOCATION;
                if (xrLocateSpace(m_viewSpace, m_localSpace, queryTime, &loc) == XR_SUCCESS) {
                    if (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) {
                        m_headPos[0] = loc.pose.position.x - m_recenterPos[0];
                        m_headPos[1] = loc.pose.position.y - m_recenterPos[1];
                        m_headPos[2] = loc.pose.position.z - m_recenterPos[2];
                    }
                    if (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) {
                        m_headRot[0] = loc.pose.orientation.x;
                        m_headRot[1] = loc.pose.orientation.y;
                        m_headRot[2] = loc.pose.orientation.z;
                        m_headRot[3] = loc.pose.orientation.w;
                    }
                }
            }
        }
    }

    void LocateViews(XrTime displayTime) {
        if (g_VRConfig.bDebugSBS) {
            m_bViewsLocated = true;
            // Fake standard Quest 3 FOV
            m_views[0].fov.angleUp = 0.9f; m_views[0].fov.angleDown = -0.9f; m_views[0].fov.angleLeft = -0.9f; m_views[0].fov.angleRight = 0.75f;
            m_views[1].fov.angleUp = 0.9f; m_views[1].fov.angleDown = -0.9f; m_views[1].fov.angleLeft = -0.75f; m_views[1].fov.angleRight = 0.9f;
            
            m_views[0].pose.orientation = {0, 0, 0, 1};
            m_views[0].pose.position = {-0.032f, 0, 0};
            m_views[1].pose.orientation = {0, 0, 0, 1};
            m_views[1].pose.position = {0.032f, 0, 0};
            
            m_headPos[0] = 0.0f; m_headPos[1] = 0.0f; m_headPos[2] = 0.0f;
            m_headRot[0] = 0.0f; m_headRot[1] = 0.0f; m_headRot[2] = 0.0f; m_headRot[3] = 1.0f;
            return;
        }

        if (!m_session || !xrLocateViews || m_localSpace == XR_NULL_HANDLE) {
            return;
        }

        XrViewState viewState = { XR_TYPE_VIEW_STATE };
        XrViewLocateInfo viewLocateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
        viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        viewLocateInfo.displayTime = (displayTime != 0) ? displayTime : (m_frameState.predictedDisplayTime != 0 ? m_frameState.predictedDisplayTime : 1);
        viewLocateInfo.space = m_localSpace;

        uint32_t viewCount = 2;
        m_views[0] = { XR_TYPE_VIEW };
        m_views[1] = { XR_TYPE_VIEW };
        XrResult locRes = xrLocateViews(m_session, &viewLocateInfo, &viewState, 2, &viewCount, m_views);
        if (locRes == XR_SUCCESS) {
            m_bViewsLocated = ((viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0) &&
                              ((viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0);

            // Auto-IPD from headset
            if ((viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) && !m_bIPDManualOverride) {
                float measuredIPD = m_views[1].pose.position.x - m_views[0].pose.position.x;
                if (measuredIPD > 0.04f && measuredIPD < 0.09f) {
                    if (!m_bIPDInitialized) {
                        g_VRConfig.fIPD = measuredIPD;
                        m_bIPDInitialized = true;
                        LogXR("Auto-IPD initialized from headset: %.1f mm", measuredIPD * 1000.0f);
                    } else {
                        g_VRConfig.fIPD = g_VRConfig.fIPD * 0.95f + measuredIPD * 0.05f;
                    }
                }
            }

            if (m_bViewsLocated) {
                if (!m_bInitialRecentered) {
                    m_recenterPos[0] = (m_views[0].pose.position.x + m_views[1].pose.position.x) * 0.5f;
                    m_recenterPos[1] = (m_views[0].pose.position.y + m_views[1].pose.position.y) * 0.5f;
                    m_recenterPos[2] = (m_views[0].pose.position.z + m_views[1].pose.position.z) * 0.5f;
                    m_bInitialRecentered = true;
                    LogXR("Auto-recentered on startup: (%.2f, %.2f, %.2f)", m_recenterPos[0], m_recenterPos[1], m_recenterPos[2]);
                }

                m_headPos[0] = (m_views[0].pose.position.x + m_views[1].pose.position.x) * 0.5f - m_recenterPos[0];
                m_headPos[1] = (m_views[0].pose.position.y + m_views[1].pose.position.y) * 0.5f - m_recenterPos[1];
                m_headPos[2] = (m_views[0].pose.position.z + m_views[1].pose.position.z) * 0.5f - m_recenterPos[2];

                m_headRot[0] = m_views[0].pose.orientation.x;
                m_headRot[1] = m_views[0].pose.orientation.y;
                m_headRot[2] = m_views[0].pose.orientation.z;
                m_headRot[3] = m_views[0].pose.orientation.w;
            }
        }
    }

    // Improvement #1: Called at the START of each game frame (BeginScene hook).
    // xrWaitFrame blocks until the compositor is ready and provides the predicted
    // display time — enabling ATW / reprojection on the Quest 3 compositor.
    void BeginFrame() {
        if (!m_bInitialized || !m_bSessionRunning) {
            if (g_VRConfig.bDebugSBS) {
                LocateViews(0);
                m_bFrameStarted = true;
            }
            return;
        }
        if (m_bFrameStarted) return; // Guard: already started

        XrFrameWaitInfo waitInfo = { XR_TYPE_FRAME_WAIT_INFO };
        m_frameState = { XR_TYPE_FRAME_STATE };
        if (xrWaitFrame(m_session, &waitInfo, &m_frameState) != XR_SUCCESS) return;

        XrFrameBeginInfo beginInfo = { XR_TYPE_FRAME_BEGIN_INFO };
        if (xrBeginFrame(m_session, &beginInfo) != XR_SUCCESS) return;

        m_bFrameStarted = true;

        // Query headset views at BeginFrame so StereoEngine has poses & FOVs for 3D pass
        LocateViews(m_frameState.predictedDisplayTime);
    }

    // Improvement #3 guard fix: call this if a frame was started but RenderDirect
    // will NOT be called this frame (session lost, minimized, etc.).
    // Submitting an empty xrEndFrame keeps the compositor timeline in sync.
    void AbortFrame() {
        if (!m_bFrameStarted) return;
        m_bFrameStarted = false;
        if (!m_bInitialized || !m_bSessionRunning) return;
        XrFrameEndInfo endInfo = { XR_TYPE_FRAME_END_INFO };
        endInfo.displayTime          = m_frameState.predictedDisplayTime;
        endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        endInfo.layerCount           = 0;
        endInfo.layers               = nullptr;
        if (xrEndFrame) xrEndFrame(m_session, &endInfo);
    }

    // bMirror: true = also blit left eye to monitor (replaces SBS with single eye for PC companion window)
    void RenderDirect(IDirect3DDevice9* pDevice9, IDirect3DSurface9* pBackBuffer, bool bMirror = false) {
        if (!m_bInitialized || !m_bSessionRunning || !pDevice9 || !pBackBuffer) return;
        if (!m_bFrameStarted) return; // BeginFrame() must have been called first
        m_bFrameStarted = false;      // Consume the started-frame token

        D3DSURFACE_DESC desc;
        pBackBuffer->GetDesc(&desc);

        UINT transferW = g_VRConfig.GetTransferWidth(desc.Width);
        UINT transferH = g_VRConfig.GetTransferHeight(desc.Width, desc.Height);

        // ── Consolidate DXGI Shared Texture & Fallback Staging Allocation ──────────────
        if (m_nStagingWidth != desc.Width || m_nStagingHeight != desc.Height ||
            m_nTransferWidth != transferW || m_nTransferHeight != transferH) {
            ReleaseD3D9Resources();
            m_nStagingWidth   = desc.Width;
            m_nStagingHeight  = desc.Height;
            m_nTransferWidth  = transferW;
            m_nTransferHeight = transferH;

            // 0. Hardware GPU Downscale Surface (if downscaling is active, e.g. 4K -> 1440p)
            if (transferW < desc.Width || transferH < desc.Height) {
                HRESULT hrDS = pDevice9->CreateRenderTarget(
                    transferW, transferH,
                    desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE,
                    &m_pD3D9DownscaleSurf, NULL);
                if (SUCCEEDED(hrDS)) {
                    LogXR("Hardware GPU Downscale RenderTarget created: %dx%d -> %dx%d (Zero CPU overhead 4K SSAA)",
                        desc.Width, desc.Height, transferW, transferH);
                } else {
                    LogXR("WARNING: Failed to create GPU Downscale RenderTarget: hr=0x%08X", hrDS);
                    m_pD3D9DownscaleSurf = nullptr;
                }
            }

            // 1. Try DXGI Shared Texture (D3D9 → D3D11 zero-copy GPU path)
            UINT sharedW = m_pD3D9DownscaleSurf ? transferW : desc.Width;
            UINT sharedH = m_pD3D9DownscaleSurf ? transferH : desc.Height;
            m_hSharedHandle = NULL;
            HRESULT hr9 = pDevice9->CreateTexture(
                sharedW, sharedH, 1,
                D3DUSAGE_RENDERTARGET,
                D3DFMT_A8R8G8B8,
                D3DPOOL_DEFAULT,
                &m_pD3D9SharedTex,
                &m_hSharedHandle);

            if (SUCCEEDED(hr9) && m_pD3D9SharedTex) {
                m_pD3D9SharedTex->GetSurfaceLevel(0, &m_pD3D9SharedSurf);

                HRESULT hrOpen = E_FAIL;
                if (m_hSharedHandle) {
                    hrOpen = m_pD3D11Device->OpenSharedResource(
                        m_hSharedHandle, __uuidof(ID3D11Texture2D), (void**)&m_pD3D11SharedTex);
                }
                if (SUCCEEDED(hrOpen) && m_pD3D11SharedTex) {
                    LogXR("DXGI Shared Texture created: %dx%d (Zero-Copy GPU path ACTIVE - 0 MB PCIe!)", sharedW, sharedH);
                } else {
                    LogXR("DXGI shared handle not available — will use High-Performance Double-Buffered AVX2 path");
                    if (m_pD3D9SharedSurf) { m_pD3D9SharedSurf->Release(); m_pD3D9SharedSurf = nullptr; }
                    if (m_pD3D9SharedTex)  { m_pD3D9SharedTex->Release();  m_pD3D9SharedTex  = nullptr; }
                }
            } else {
                LogXR("D3D9 shared handle not supported by runtime — using High-Performance Double-Buffered AVX2 path");
            }

            // 2. If Shared Texture not active, prepare AVX2 Double-Buffered Staging Surfaces
            // Fallback staging surfaces use transferW x transferH (saves ~18.5 MB per frame over PCIe!)
            if (!m_pD3D9SharedSurf || !m_pD3D11SharedTex) {
                pDevice9->CreateOffscreenPlainSurface(transferW, transferH, desc.Format, D3DPOOL_SYSTEMMEM, &m_pCPUFallbackSurf[0], NULL);
                pDevice9->CreateOffscreenPlainSurface(transferW, transferH, desc.Format, D3DPOOL_SYSTEMMEM, &m_pCPUFallbackSurf[1], NULL);

                D3D11_TEXTURE2D_DESC td = {};
                td.Width = transferW; td.Height = transferH;
                td.MipLevels = 1; td.ArraySize = 1;
                td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                td.SampleDesc.Count = 1;
                td.Usage = D3D11_USAGE_DYNAMIC;
                td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                m_pD3D11Device->CreateTexture2D(&td, NULL, &m_pCPUFallbackTex);

                m_nFallbackIndex  = 0;
                m_bFallbackPrimed = false;
                LogXR("Initialized High-Performance Double-Buffered AVX2 Transfer: %dx%d (Render %dx%d)", transferW, transferH, desc.Width, desc.Height);
            }

            // 3. Mirror RenderTarget for desktop companion window:
            // Full backbuffer size render target allows StretchRect to perform arbitrary linear scaling/blitting
            HRESULT hrMirror = pDevice9->CreateRenderTarget(
                desc.Width, desc.Height,
                desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE,
                &m_pD3D9MirrorSurf, NULL);
            if (SUCCEEDED(hrMirror)) {
                LogXR("Desktop Mirror RenderTarget created: %dx%d (single clean monitor view)", desc.Width, desc.Height);
            } else {
                LogXR("ERROR: Failed to create Desktop Mirror RenderTarget: hr=0x%08X", hrMirror);
            }

            // 4. Resize OpenXR swapchains if needed
            if (m_swapchainWidth != transferW / 2 || m_swapchainHeight != transferH) {
                InitSwapchains(transferW / 2, transferH);
                if (m_bDepthSupported && g_VRConfig.bEnableDepthSubmission) {
                    InitDepthSwapchains(transferW / 2, transferH);
                }
            }
        }

        // ── Step 1: Prepare mirror surface from ORIGINAL SBS backbuffer (D3D9 GPU work — BEFORE any readback) ──
        // This must happen BEFORE GetRenderTargetData so the readback captures original SBS (not mirrored).
        // After xrEndFrame, we blit mirrorSurf → backbuffer for the PC companion window.
        if (bMirror && m_pD3D9MirrorSurf) {
            UINT halfW = desc.Width / 2;
            RECT srcLeft = { 0, 0, (LONG)halfW, (LONG)desc.Height };
            RECT destRect;

            if (g_VRConfig.iMirrorMode == 1) {
                // Mode 1: Fullscreen 16:9 crop (fills entire monitor without black bars)
                LONG cropH  = (LONG)(halfW * desc.Height / desc.Width);
                LONG offsetY = (LONG)(desc.Height - cropH) / 2;
                srcLeft.top    = offsetY;
                srcLeft.bottom = offsetY + cropH;
                destRect = { 0, 0, (LONG)desc.Width, (LONG)desc.Height };
            } else {
                // Mode 0: Aspect-fit / Pillarbox
                LONG fitW   = (LONG)halfW;
                LONG offsetX = (LONG)(desc.Width - fitW) / 2;
                destRect = { offsetX, 0, offsetX + fitW, (LONG)desc.Height };
            }

            pDevice9->ColorFill(m_pD3D9MirrorSurf, nullptr, D3DCOLOR_XRGB(0, 0, 0));

            HRESULT hrBlit = pDevice9->StretchRect(pBackBuffer, &srcLeft, m_pD3D9MirrorSurf, &destRect, (g_VRConfig.iMirrorMode == 1) ? D3DTEXF_LINEAR : D3DTEXF_NONE);
            if (FAILED(hrBlit)) hrBlit = pDevice9->StretchRect(pBackBuffer, &srcLeft, m_pD3D9MirrorSurf, &destRect, D3DTEXF_POINT);
            if (FAILED(hrBlit))           pDevice9->StretchRect(pBackBuffer, &srcLeft, m_pD3D9MirrorSurf, &destRect, D3DTEXF_NONE);
        }

        // ── Step 2: Copy to D3D11 for OpenXR submission ───────────────────────────────
        // If downscale surface is active, perform high-quality bilinear downscale on the GPU (~0.02 ms)
        IDirect3DSurface9* pSourceSurf = pBackBuffer;
        if (m_pD3D9DownscaleSurf) {
            HRESULT hrDS = pDevice9->StretchRect(pBackBuffer, nullptr, m_pD3D9DownscaleSurf, nullptr, D3DTEXF_LINEAR);
            if (FAILED(hrDS)) {
                hrDS = pDevice9->StretchRect(pBackBuffer, nullptr, m_pD3D9DownscaleSurf, nullptr, D3DTEXF_POINT);
            }
            if (SUCCEEDED(hrDS)) {
                pSourceSurf = m_pD3D9DownscaleSurf;
            }
        }

        ID3D11Texture2D* pSourceTex = nullptr;
        int activeReadIdx = 0;
        if (m_pD3D9SharedSurf && m_pD3D11SharedTex) {
            // GPU-only path: blit to D3D9 shared surface
            pDevice9->StretchRect(pSourceSurf, nullptr, m_pD3D9SharedSurf, nullptr, D3DTEXF_NONE);
            // Flush D3D9 commands so D3D11 can sample it without CPU busy-wait spinlock
            if (!m_pD3D9FlushQuery) {
                pDevice9->CreateQuery(D3DQUERYTYPE_EVENT, &m_pD3D9FlushQuery);
            }
            if (m_pD3D9FlushQuery) {
                m_pD3D9FlushQuery->Issue(D3DISSUE_END);
                m_pD3D9FlushQuery->GetData(nullptr, 0, D3DGETDATA_FLUSH);
            }
            pSourceTex = m_pD3D11SharedTex;
        } else if (m_pCPUFallbackSurf[0] && m_pCPUFallbackSurf[1] && m_pCPUFallbackTex) {
            int writeIdx = m_nFallbackIndex;
            // Record tracking pose used to render this backbuffer into slot writeIdx
            m_fallbackViews[writeIdx][0] = m_views[0];
            m_fallbackViews[writeIdx][1] = m_views[1];

            // Reads transfer surface asynchronously — GPU DMA transfers in background (saves ~18.5 MB per frame!)
            pDevice9->GetRenderTargetData(pSourceSurf, m_pCPUFallbackSurf[writeIdx]);

            int readIdx = m_bFallbackPrimed ? (1 - writeIdx) : writeIdx;
            m_bFallbackPrimed = true;
            m_nFallbackIndex  = 1 - writeIdx;
            activeReadIdx     = readIdx;

            IDirect3DSurface9* pReadSurf = m_pCPUFallbackSurf[readIdx];
            D3DLOCKED_RECT lr;
            if (SUCCEEDED(pReadSurf->LockRect(&lr, NULL, D3DLOCK_READONLY))) {
                D3D11_MAPPED_SUBRESOURCE mapped;
                if (SUCCEEDED(m_pD3D11Context->Map(m_pCPUFallbackTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                    int h = (int)m_nTransferHeight;
                    UINT rowBytes = m_nTransferWidth * 4;

                    bool bApplyGamma = (fabsf(g_VRConfig.fGamma - 1.0f) > 0.01f || fabsf(g_VRConfig.fBrightness - 1.0f) > 0.01f);
                    if (bApplyGamma && (m_cachedGamma != g_VRConfig.fGamma || m_cachedBrightness != g_VRConfig.fBrightness)) {
                        UpdateGammaLUT(g_VRConfig.fGamma, g_VRConfig.fBrightness);
                    }

                    #pragma omp parallel for schedule(static)
                    for (int y = 0; y < h; y++) {
                        const BYTE* srcRow = (const BYTE*)lr.pBits + y * lr.Pitch;
                        BYTE* dstRow = (BYTE*)mapped.pData + y * mapped.RowPitch;
                        if (bApplyGamma) {
                            CopyRowWithGammaLUT(dstRow, srcRow, rowBytes, m_gammaLUT);
                        } else {
                            CopyRowStreamingAVX2(dstRow, srcRow, rowBytes);
                        }
                    }
                    _mm_sfence();
                    m_pD3D11Context->Unmap(m_pCPUFallbackTex, 0);
                }
                pReadSurf->UnlockRect();
            }
            pSourceTex = m_pCPUFallbackTex;
        }
        if (!pSourceTex) {
            // Still blit mirror to backbuffer even if OpenXR submission fails
            if (bMirror && m_pD3D9MirrorSurf)
                pDevice9->StretchRect(m_pD3D9MirrorSurf, nullptr, pBackBuffer, nullptr, D3DTEXF_NONE);
            return;
        }

        // Frame was already started by BeginFrame() — just use the stored frameState
        const XrFrameState& frameState = m_frameState;

        if (frameState.shouldRender && m_swapchains[0] && m_swapchains[1] && pSourceTex) {
            UINT halfWidth = desc.Width / 2;

            // Copy left and right halves to swapchains
            for (int eye = 0; eye < 2; eye++) {
                uint32_t imageIndex = 0;
                XrSwapchainImageAcquireInfo acqInfo = {};
                acqInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
                xrAcquireSwapchainImage(m_swapchains[eye], &acqInfo, &imageIndex);

                XrSwapchainImageWaitInfo waitImageInfo = {};
                waitImageInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
                waitImageInfo.timeout = XR_INFINITE_DURATION;
                xrWaitSwapchainImage(m_swapchains[eye], &waitImageInfo);

                ID3D11Texture2D* pSwapchainTex = m_swapchainImages[eye][imageIndex].texture;

                // Source box from the SbS source texture, scaled to swapchain size
                // (source is game backbuffer width, destination is headset recommended width)
                D3D11_TEXTURE2D_DESC srcDesc = {};
                pSourceTex->GetDesc(&srcDesc);
                UINT srcHalfW = srcDesc.Width / 2;

                D3D11_BOX srcBox = {};
                srcBox.left  = eye * srcHalfW;
                srcBox.right = srcBox.left + srcHalfW;
                srcBox.top    = 0;
                srcBox.bottom = srcDesc.Height;
                srcBox.front  = 0;
                srcBox.back   = 1;

                // If swapchain and source match size: direct copy; otherwise compositor will scale
                m_pD3D11Context->CopySubresourceRegion(pSwapchainTex, 0, 0, 0, 0, pSourceTex, 0, &srcBox);

                XrSwapchainImageReleaseInfo relInfo = {};
                relInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
                xrReleaseSwapchainImage(m_swapchains[eye], &relInfo);
            }

            // Refresh views if needed for current frame
            if (!m_bViewsLocated) {
                LocateViews(frameState.predictedDisplayTime);
            }

            // ── Depth Layer Submission (XR_KHR_composition_layer_depth) ─────────────
            XrCompositionLayerDepthInfoKHR depthInfos[2] = {};
            bool bSubmitDepth = (m_bDepthSupported && g_VRConfig.bEnableDepthSubmission &&
                                 m_depthSwapchains[0] && m_depthSwapchains[1]);

            if (bSubmitDepth) {
                for (int eye = 0; eye < 2; eye++) {
                    uint32_t depthImageIndex = 0;
                    XrSwapchainImageAcquireInfo dAcqInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
                    xrAcquireSwapchainImage(m_depthSwapchains[eye], &dAcqInfo, &depthImageIndex);

                    XrSwapchainImageWaitInfo dWaitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
                    dWaitInfo.timeout = XR_INFINITE_DURATION;
                    xrWaitSwapchainImage(m_depthSwapchains[eye], &dWaitInfo);

                    // Clear depth view
                    if (depthImageIndex < m_depthDSVs[eye].size() && m_depthDSVs[eye][depthImageIndex]) {
                        m_pD3D11Context->ClearDepthStencilView(m_depthDSVs[eye][depthImageIndex], D3D11_CLEAR_DEPTH, 1.0f, 0);
                    }

                    XrSwapchainImageReleaseInfo dRelInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                    xrReleaseSwapchainImage(m_depthSwapchains[eye], &dRelInfo);

                    depthInfos[eye].type = XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR;
                    depthInfos[eye].next = nullptr;
                    depthInfos[eye].subImage.swapchain = m_depthSwapchains[eye];
                    depthInfos[eye].subImage.imageRect.offset = { 0, 0 };
                    depthInfos[eye].subImage.imageRect.extent = { (int32_t)m_swapchainWidth, (int32_t)m_swapchainHeight };
                    depthInfos[eye].subImage.imageArrayIndex = 0;
                    depthInfos[eye].minDepth = 0.0f;
                    depthInfos[eye].maxDepth = 1.0f;
                    depthInfos[eye].nearZ = m_nearZ;
                    depthInfos[eye].farZ  = m_farZ;
                }
            }

            // ── Full 360 Immersive VR Projection ──────────────────────────
            XrCompositionLayerProjectionView projViews[2] = {};
            for (int eye = 0; eye < 2; eye++) {
                projViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                projViews[eye].next = bSubmitDepth ? (const void*)&depthInfos[eye] : nullptr;
                if (m_pD3D9SharedSurf && m_pD3D11SharedTex) {
                    projViews[eye].pose = m_views[eye].pose;
                    projViews[eye].fov  = m_views[eye].fov;
                } else {
                    projViews[eye].pose = m_fallbackViews[activeReadIdx][eye].pose;
                    projViews[eye].fov  = m_fallbackViews[activeReadIdx][eye].fov;
                }
                projViews[eye].subImage.swapchain = m_swapchains[eye];
                projViews[eye].subImage.imageRect.offset = { 0, 0 };
                projViews[eye].subImage.imageRect.extent = { (int32_t)m_swapchainWidth, (int32_t)m_swapchainHeight };
                projViews[eye].subImage.imageArrayIndex = 0;
            }

            XrCompositionLayerProjection projLayer = {};
            projLayer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
            projLayer.space = m_localSpace;
            projLayer.viewCount = 2;
            projLayer.views = projViews;

            const XrCompositionLayerBaseHeader* layers[1] = {
                (const XrCompositionLayerBaseHeader*)&projLayer
            };

            XrFrameEndInfo endInfo = {};
            endInfo.type                 = XR_TYPE_FRAME_END_INFO;
            endInfo.displayTime          = frameState.predictedDisplayTime;
            endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            endInfo.layerCount           = 1;
            endInfo.layers               = layers;
            xrEndFrame(m_session, &endInfo);
        } else {
            XrFrameEndInfo endInfo = {};
            endInfo.type = XR_TYPE_FRAME_END_INFO;
            endInfo.displayTime = frameState.predictedDisplayTime;
            endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            endInfo.layerCount = 0;
            endInfo.layers = nullptr;
            xrEndFrame(m_session, &endInfo);
        }

        // ── Step 3: Blit prepared mirrorSurf → backbuffer AFTER xrEndFrame ─────────────
        // All OpenXR compositor work is done. Now replace SBS backbuffer with single-eye
        // for the PC companion window. No GPU hazard since xrEndFrame already consumed pBackBuffer.
        if (bMirror && m_pD3D9MirrorSurf) {
            pDevice9->StretchRect(m_pD3D9MirrorSurf, nullptr, pBackBuffer, nullptr, D3DTEXF_NONE);
        }
    }

    void Recenter() {
        if (m_bViewsLocated) {
            m_recenterPos[0] = (m_views[0].pose.position.x + m_views[1].pose.position.x) * 0.5f;
            m_recenterPos[1] = (m_views[0].pose.position.y + m_views[1].pose.position.y) * 0.5f;
            m_recenterPos[2] = (m_views[0].pose.position.z + m_views[1].pose.position.z) * 0.5f;
        } else {
            m_recenterPos[0] += m_headPos[0];
            m_recenterPos[1] += m_headPos[1];
            m_recenterPos[2] += m_headPos[2];
        }
        m_headPos[0] = 0.0f;
        m_headPos[1] = 0.0f;
        m_headPos[2] = 0.0f;
        LogXR("Recentered VR position to: (%.2f, %.2f, %.2f)", m_recenterPos[0], m_recenterPos[1], m_recenterPos[2]);
    }

    void ReleaseD3D9Resources() {
        if (m_pD3D9DownscaleSurf) { m_pD3D9DownscaleSurf->Release(); m_pD3D9DownscaleSurf = nullptr; }
        if (m_pD3D9SharedSurf)  { m_pD3D9SharedSurf->Release();  m_pD3D9SharedSurf  = nullptr; }
        if (m_pD3D9SharedTex)   { m_pD3D9SharedTex->Release();   m_pD3D9SharedTex   = nullptr; }
        if (m_pD3D11SharedTex)  { m_pD3D11SharedTex->Release();  m_pD3D11SharedTex  = nullptr; }
        if (m_pD3D9MirrorSurf)  { m_pD3D9MirrorSurf->Release();  m_pD3D9MirrorSurf  = nullptr; }
        for (int i = 0; i < 2; i++) {
            if (m_pCPUFallbackSurf[i]) { m_pCPUFallbackSurf[i]->Release(); m_pCPUFallbackSurf[i] = nullptr; }
        }
        m_nFallbackIndex  = 0;
        m_bFallbackPrimed = false;
        memset(m_fallbackViews, 0, sizeof(m_fallbackViews));
        if (m_pCPUFallbackTex)  { m_pCPUFallbackTex->Release();  m_pCPUFallbackTex  = nullptr; }
        if (m_pD3D9FlushQuery)  { m_pD3D9FlushQuery->Release();  m_pD3D9FlushQuery  = nullptr; }
        m_hSharedHandle   = NULL;
        m_nStagingWidth   = 0;
        m_nStagingHeight  = 0;
        m_nTransferWidth  = 0;
        m_nTransferHeight = 0;
    }

    void Shutdown() {
        for (int eye = 0; eye < 2; eye++) {
            for (auto& dsv : m_depthDSVs[eye]) {
                if (dsv) { dsv->Release(); dsv = nullptr; }
            }
            m_depthDSVs[eye].clear();
        }
        if (m_depthSwapchains[0] && xrDestroySwapchain) xrDestroySwapchain(m_depthSwapchains[0]);
        if (m_depthSwapchains[1] && xrDestroySwapchain) xrDestroySwapchain(m_depthSwapchains[1]);
        if (m_swapchains[0] && xrDestroySwapchain) xrDestroySwapchain(m_swapchains[0]);
        if (m_swapchains[1] && xrDestroySwapchain) xrDestroySwapchain(m_swapchains[1]);
        if (m_localSpace && xrDestroySpace) xrDestroySpace(m_localSpace);
        if (m_viewSpace && xrDestroySpace) xrDestroySpace(m_viewSpace);
        if (m_session && xrDestroySession) xrDestroySession(m_session);
        if (m_instance && xrDestroyInstance) xrDestroyInstance(m_instance);
        ReleaseD3D9Resources();
        if (m_pD3D11Context) m_pD3D11Context->Release();
        if (m_pD3D11Device)  m_pD3D11Device->Release();
        if (m_hRuntime) FreeLibrary(m_hRuntime);
        m_bInitialized = false;
        LogXR("OpenXR Shutdown complete.");
    }
};

extern OpenXRHandler g_OpenXR;
