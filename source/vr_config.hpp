#pragma once
#include <windows.h>
#include <string>

struct VRConfig {
    bool bEnableVR = true;              // 1 = Enable VR mode with direct OpenXR stereoscopic output
    bool bDisableCulling = true;        // 1 = Disable frustum culling (prevents background/edge objects from disappearing)
    bool bDebugSBS = false;             // Debug Side-by-Side rendering directly on monitor without OpenXR
    float fCameraDistance = 3.0f;       // Camera pullback distance from Duke / wall
    float fWorldScale = 24.0f;          // World scale (24.0 = natural 1:1 scale; 1m = 24 game units, Duke is 48 units = 2.0m tall)
    float fIPD = 0.064f;                // Fallback IPD (meters) — overridden dynamically by OpenXR headset detection
    float fTrackingPosScale = 1.0f;     // Positional head tracking sensitivity (1.0 = natural 1:1 scale)
    float fHudDepth = 1.0f;             // Virtual distance of 2D HUD in 3D space
    float fHudScale = 0.65f;            // Size of 2D HUD (0.65 = 65% of horizontal FOV)
    bool bEnableDepthSubmission = true; // Submit depth buffer to OpenXR for Asynchronous Spacewarp / Timewarp

    bool bForceWidescreen = true;
    int iMirrorMode = 1;                // 0 = Aspect-fit / Pillarbox, 1 = Fullscreen 16:9 crop
    int iForcedWidth = 4128;            // High resolution width (split Width/2 per eye for stereo)
    int iForcedHeight = 2208;           // High resolution height
    int iDownscaleWidth = 0;            // Max transfer width over PCIe (0 = disabled / full native)
    int iFPSLimit = 90;                 // FPS limiter
    float fGamma = 0.8f;                // VR Gamma correction (0.8 = balanced shadows on Quest lenses)
    float fBrightness = 1.0f;           // VR Brightness multiplier

    UINT GetTransferWidth(UINT renderWidth) const {
        if (iDownscaleWidth > 0 && renderWidth > (UINT)iDownscaleWidth) {
            return (UINT)iDownscaleWidth;
        }
        return renderWidth;
    }

    UINT GetTransferHeight(UINT renderWidth, UINT renderHeight) const {
        if (iDownscaleWidth > 0 && renderWidth > (UINT)iDownscaleWidth) {
            return (UINT)((uint64_t)renderHeight * iDownscaleWidth / renderWidth);
        }
        return renderHeight;
    }

    void Load() {
        char iniPath[MAX_PATH];
        GetModuleFileNameA(NULL, iniPath, MAX_PATH);
        char* lastSlash = strrchr(iniPath, '\\');
        if (lastSlash) *(lastSlash + 1) = '\0';
        strcat_s(iniPath, "vr_config.ini");

        // Write defaults if not existing
        if (GetFileAttributesA(iniPath) == INVALID_FILE_ATTRIBUTES) {
            SaveDefaults(iniPath);
        }

        bEnableVR = GetPrivateProfileIntA("VR", "EnableVR", 1, iniPath) != 0;
        bDisableCulling = GetPrivateProfileIntA("VR", "DisableCulling", 1, iniPath) != 0;
        bDebugSBS = GetPrivateProfileIntA("VR", "DebugSBS", 0, iniPath) != 0;
        bEnableDepthSubmission = GetPrivateProfileIntA("VR", "EnableDepthSubmission", 1, iniPath) != 0;

        char buf[64];
        if (GetPrivateProfileStringA("VR", "CameraDistance", "3.0", buf, sizeof(buf), iniPath))
            fCameraDistance = (float)atof(buf);
        if (GetPrivateProfileStringA("VR", "WorldScale", "24.0", buf, sizeof(buf), iniPath))
            fWorldScale = (float)atof(buf);
        if (GetPrivateProfileStringA("VR", "TrackingPosScale", "1.0", buf, sizeof(buf), iniPath))
            fTrackingPosScale = (float)atof(buf);
        if (GetPrivateProfileStringA("VR", "HudDepth", "1.0", buf, sizeof(buf), iniPath))
            fHudDepth = (float)atof(buf);
        if (GetPrivateProfileStringA("VR", "HudScale", "0.65", buf, sizeof(buf), iniPath))
            fHudScale = (float)atof(buf);

        bForceWidescreen = GetPrivateProfileIntA("Graphics", "ForceWidescreen", 1, iniPath) != 0;
        iMirrorMode = GetPrivateProfileIntA("Graphics", "MirrorMode", 1, iniPath);
        iForcedWidth = GetPrivateProfileIntA("Graphics", "Width", 4128, iniPath);
        iForcedHeight = GetPrivateProfileIntA("Graphics", "Height", 2208, iniPath);
        iDownscaleWidth = GetPrivateProfileIntA("Graphics", "DownscaleWidth", 0, iniPath);
        iFPSLimit = GetPrivateProfileIntA("Graphics", "FPSLimit", 90, iniPath);

        if (GetPrivateProfileStringA("Graphics", "Gamma", "0.8", buf, sizeof(buf), iniPath))
            fGamma = (float)atof(buf);
        if (GetPrivateProfileStringA("Graphics", "Brightness", "1.0", buf, sizeof(buf), iniPath))
            fBrightness = (float)atof(buf);
    }

    void SaveDefaults(const char* path) {
        FILE* f = nullptr;
        fopen_s(&f, path, "w");
        if (f) {
            const char* content =
                "[VR]\n"
                "; EnableVR: 1 = Enable VR mode with direct OpenXR stereoscopic output (0 = disabled / standard 2D game)\n"
                "EnableVR=1\n\n"
                "; DisableCulling: 1 = Completely disable frustum culling (prevents objects and buildings from vanishing at edges)\n"
                "; In-game hotkey: F8 = toggle on / off\n"
                "DisableCulling=1\n\n"
                "; DebugSBS: 0 = Direct VR headset output via OpenXR (Quest 3 / Link / Virtual Desktop / SteamVR)\n"
                ";           1 = Test on monitor without headset (Side-by-Side stereo view)\n"
                "DebugSBS=0\n\n"
                "; CameraDistance: Camera distance pullback from Duke\n"
                "; In-game hotkeys: Page Up = move camera further, Page Down = closer, Home = reset to 3.0\n"
                "CameraDistance=3.0\n\n"
                "; WorldScale: World scale unit conversion (24.0 = natural 1:1 scale; 1 meter = 24 game units, Duke is 48 units = 2.0m tall)\n"
                "; In-game hotkeys: Numpad 9 = increase scale (+1.0), Numpad 7 = decrease scale (-1.0), Numpad 8 = reset to 24.0\n"
                "WorldScale=24.0\n\n"
                "; TrackingPosScale: Positional head tracking sensitivity (1.0 = natural 1:1 head movement)\n"
                "; In-game hotkeys: F12 = recenter view, F11 / Shift+F11 = fine tune (+/- 0.1), F10 = reset to 1.0\n"
                "TrackingPosScale=1.0\n\n"
                "; HudDepth: Virtual distance of the 2D HUD (health, weapons, menu) in 3D space\n"
                "; In-game hotkeys: Insert = closer, Delete = further away\n"
                "HudDepth=1.0\n\n"
                "; HudScale: Size of the 2D HUD (0.65 = 65% of horizontal field of view)\n"
                "; In-game hotkeys: [ = decrease HUD size, ] = increase HUD size\n"
                "HudScale=0.65\n\n"
                "; EnableDepthSubmission: 1 = Submit depth buffer to OpenXR for Asynchronous Spacewarp / Timewarp (0 = disabled)\n"
                "EnableDepthSubmission=1\n\n"
                "[Graphics]\n"
                "; MirrorMode: 0 = Aspect-fit / Pillarbox (clean single image centered on monitor with full HUD)\n"
                ";             1 = Fullscreen 16:9 crop (fills monitor edge-to-edge)\n"
                "MirrorMode=1\n\n"
                "; Forced resolution for VR rendering (split into Width/2 x Height per eye)\n"
                "ForceWidescreen=1\n"
                "Width=4128\n"
                "Height=2208\n\n"
                "; DownscaleWidth: Maximum transfer width over PCIe (0 = full native transfer)\n"
                "DownscaleWidth=0\n"
                "FPSLimit=90\n\n"
                "; VR Lens Gamma and Brightness tuning\n"
                "; In-game hotkeys: Numpad * = increase gamma, Numpad / = decrease gamma\n"
                "Gamma=0.8\n"
                "Brightness=1.0\n";
            fputs(content, f);
            fclose(f);
        }
    }
};

extern VRConfig g_VRConfig;
