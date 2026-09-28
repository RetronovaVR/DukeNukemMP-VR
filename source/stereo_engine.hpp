#pragma once
#include <windows.h>
#include <d3d9.h>
#include <math.h>
#include "vr_config.hpp"
#include "openxr_handler.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

class StereoEngine {
public:
    bool m_bIn3DPass = false;
    bool m_bIsDrawing = false; // Recursion guard

    DWORD m_Width = 1920;
    DWORD m_Height = 1080;

    D3DVIEWPORT9 m_LeftViewport;
    D3DVIEWPORT9 m_RightViewport;
    D3DVIEWPORT9 m_FullViewport;

    D3DVIEWPORT9 m_LeftHudViewport;
    D3DVIEWPORT9 m_RightHudViewport;

    D3DMATRIX m_BaseView;
    D3DMATRIX m_BaseProj;
    D3DMATRIX m_BaseWorld;

    // Identity view matrix for 2D HUD draws — ensures zero IPD parallax
    // so UI/text/logo appears at infinite distance (comfortable, not stuck to face)
    D3DMATRIX m_IdentityView = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };
    // Centered ortho projection for HUD — mirrors game's ortho but without any eye offset
    D3DMATRIX m_HudProj;  // set from BaseProj when in ortho pass

    D3DMATRIX m_LeftView;
    D3DMATRIX m_RightView;
    D3DMATRIX m_LeftProj;
    D3DMATRIX m_RightProj;

    // Dedicated matrices:
    // 1. Skybox pass: NO fCameraDistance, subtle head tracking without model penetration
    D3DMATRIX m_LeftViewSkybox;
    D3DMATRIX m_RightViewSkybox;

    // 2. World pass (Duke, rooftop, underground, water, enemies): WITH fCameraDistance, full 6DoF
    D3DMATRIX m_LeftViewWorld;
    D3DMATRIX m_RightViewWorld;

    bool m_bHasSkyboxPass = false; // Does the current level have a skybox pass? (Learned dynamically each frame)
    bool m_bInSkybox = false;
    bool m_bHad3DThisFrame = false;
    int  m_DepthClearCount = 0;
    int  m_3DDrawCount = 0;
    int  m_LastFrame3DDrawCount = 0;

    bool IsCurrentViewSkybox() const {
        return m_bInSkybox;
    }

    void OnBeginScene() {
        m_LastFrame3DDrawCount = m_3DDrawCount;
        m_3DDrawCount = 0;
        m_DepthClearCount = 0;
        m_bHad3DThisFrame = false;

        // In Duke Nukem Manhattan Project, gameplay levels have hundreds of 3D draw calls (> 40).
        // Menus (main menu, level select, pause menu, options) have few (< 40) or 0.
        g_OpenXR.m_bIn3DScene = (m_LastFrame3DDrawCount > 40);

        m_bInSkybox = m_bHasSkyboxPass;
        m_LeftView  = m_bInSkybox ? m_LeftViewSkybox : m_LeftViewWorld;
        m_RightView = m_bInSkybox ? m_RightViewSkybox : m_RightViewWorld;
        UpdateStereoMatrices();
        UpdateHudViewports();
    }

    void OnClear(DWORD Flags) {
        // In Prism3D, a depth-only clear (D3DCLEAR_ZBUFFER without D3DCLEAR_TARGET)
        // is executed right after the skybox models are drawn, clearing the Z-buffer
        // so that Duke and the main rooftop world draw in front of the skybox.
        if ((Flags & D3DCLEAR_ZBUFFER) && !(Flags & D3DCLEAR_TARGET)) {
            m_DepthClearCount++;
            m_bInSkybox = false;
            m_bHasSkyboxPass = true;
            m_LeftView  = m_LeftViewWorld;
            m_RightView = m_RightViewWorld;
        }
    }

    void OnEndScene() {
        // If 3D geometry was rendered this frame without any depth-only clear,
        // then this level has NO skybox (indoor / underground map).
        if (m_bHad3DThisFrame && m_DepthClearCount == 0) {
            m_bHasSkyboxPass = false;
        }
        m_bInSkybox = false;
        m_LeftView  = m_LeftViewWorld;
        m_RightView = m_RightViewWorld;
        g_OpenXR.m_bIn3DScene = (m_3DDrawCount > 40);
    }

    StereoEngine() {
        memset(&m_BaseView, 0, sizeof(m_BaseView));
        memset(&m_BaseProj, 0, sizeof(m_BaseProj));
        memset(&m_BaseWorld, 0, sizeof(m_BaseWorld));
        memset(&m_LeftView, 0, sizeof(m_LeftView));
        memset(&m_RightView, 0, sizeof(m_RightView));
        memset(&m_LeftProj, 0, sizeof(m_LeftProj));
        memset(&m_RightProj, 0, sizeof(m_RightProj));
        memset(&m_LeftViewSkybox, 0, sizeof(m_LeftViewSkybox));
        memset(&m_RightViewSkybox, 0, sizeof(m_RightViewSkybox));
        memset(&m_LeftViewWorld, 0, sizeof(m_LeftViewWorld));
        memset(&m_RightViewWorld, 0, sizeof(m_RightViewWorld));
        m_BaseView._11 = m_BaseView._22 = m_BaseView._33 = m_BaseView._44 = 1.0f;
        m_BaseProj._11 = m_BaseProj._22 = m_BaseProj._33 = m_BaseProj._44 = 1.0f;
        m_BaseWorld._11 = m_BaseWorld._22 = m_BaseWorld._33 = m_BaseWorld._44 = 1.0f;
        m_LeftViewSkybox = m_RightViewSkybox = m_LeftViewWorld = m_RightViewWorld = m_BaseView;
        m_LeftView = m_RightView = m_BaseView;
    }

    void UpdateHudViewports() {
        DWORD halfW = m_Width / 2;
        float eyeW = (float)halfW;
        float eyeH = (float)m_Height;

        // 1. HUD Scale: how much of the view width does the HUD occupy?
        // Default 0.65 (65% width) - clean, readable, not stretching across full FOV
        float hudScale = (g_VRConfig.fHudScale > 0.3f && g_VRConfig.fHudScale <= 1.2f) ? g_VRConfig.fHudScale : 0.65f;
        float hudW = eyeW * hudScale;

        // 2. Aspect Ratio: preserve 4:3 native Duke aspect ratio for HUD/UI elements
        float targetAspect = 4.0f / 3.0f;
        float hudH = hudW / targetAspect;
        if (hudH > eyeH * hudScale) {
            hudH = eyeH * hudScale;
            hudW = hudH * targetAspect;
        }

        // 3. Binocular optical center and convergence:
        // In Quest 3 / OpenXR, the optical forward axis of each eye is NOT in the middle of the texture (U = 0.5).
        // Because of canted/asymmetric lens FOV:
        // - Left eye optical center is at U ~0.565 (shifted inward towards nose / right)
        // - Right eye optical center is at U ~0.435 (shifted inward towards nose / left)
        // For an object at distance 'hudDist', both eyes converge slightly inward towards the nose.
        float uCenterLeft = 0.5f;
        float uCenterRight = 0.5f;

        if (g_OpenXR.m_bViewsLocated) {
            // Compute exact optical forward axis from headset's OpenXR FOV angles
            float l0 = tanf(g_OpenXR.m_views[0].fov.angleLeft);
            float r0 = tanf(g_OpenXR.m_views[0].fov.angleRight);
            float optCenterLeft = (-(l0 + r0) / (r0 - l0) + 1.0f) * 0.5f;

            float l1 = tanf(g_OpenXR.m_views[1].fov.angleLeft);
            float r1 = tanf(g_OpenXR.m_views[1].fov.angleRight);
            float optCenterRight = (-(l1 + r1) / (r1 - l1) + 1.0f) * 0.5f;

            // Virtual distance of HUD in meters (~4.0m feels relaxed, single clear fused image)
            float hudDist = 2.0f + 2.0f * g_VRConfig.fHudDepth;
            if (hudDist < 1.0f) hudDist = 1.0f;

            float ipd = (g_VRConfig.fIPD > 0.04f && g_VRConfig.fIPD < 0.08f) ? g_VRConfig.fIPD : 0.064f;
            float halfIPD = ipd * 0.5f;
            float convTan = halfIPD / hudDist;

            // Convergence shifts inward towards nose (+X for left eye, -X for right eye):
            float convShiftLeft  = (convTan * (2.0f / (r0 - l0))) * 0.5f;
            float convShiftRight = (convTan * (2.0f / (r1 - l1))) * 0.5f;

            uCenterLeft  = optCenterLeft  + convShiftLeft;
            uCenterRight = optCenterRight - convShiftRight;
        } else {
            // Quest 3 fallback:
            float hudDist = 2.0f + 2.0f * g_VRConfig.fHudDepth;
            if (hudDist < 1.0f) hudDist = 1.0f;
            float conv = (0.032f / hudDist) * 0.5f;

            uCenterLeft  = 0.565f + conv;
            uCenterRight = 0.435f - conv;
        }

        // Viewport vertical positioning (centered vertically)
        float hudY = (eyeH - hudH) * 0.5f;
        if (hudY < 0.0f) hudY = 0.0f;

        // Left eye HUD viewport:
        float leftCenterX = eyeW * uCenterLeft;
        float leftX = leftCenterX - (hudW * 0.5f);
        if (leftX < 0.0f) leftX = 0.0f;
        if (leftX + hudW > eyeW) leftX = eyeW - hudW;

        m_LeftHudViewport.X      = (DWORD)leftX;
        m_LeftHudViewport.Y      = (DWORD)hudY;
        m_LeftHudViewport.Width  = (DWORD)hudW;
        m_LeftHudViewport.Height = (DWORD)hudH;
        m_LeftHudViewport.MinZ   = 0.0f;
        m_LeftHudViewport.MaxZ   = 1.0f;

        // Right eye HUD viewport:
        float rightCenterX = (float)halfW + (eyeW * uCenterRight);
        float rightX = rightCenterX - (hudW * 0.5f);
        if (rightX < (float)halfW) rightX = (float)halfW;
        if (rightX + hudW > (float)m_Width) rightX = (float)m_Width - hudW;

        m_RightHudViewport.X      = (DWORD)rightX;
        m_RightHudViewport.Y      = (DWORD)hudY;
        m_RightHudViewport.Width  = (DWORD)hudW;
        m_RightHudViewport.Height = (DWORD)hudH;
        m_RightHudViewport.MinZ   = 0.0f;
        m_RightHudViewport.MaxZ   = 1.0f;
    }

    void OnReset(DWORD width, DWORD height) {
        m_Width = width;
        m_Height = height;

        DWORD halfW = width / 2;

        m_LeftViewport.X = 0;
        m_LeftViewport.Y = 0;
        m_LeftViewport.Width = halfW;
        m_LeftViewport.Height = height;
        m_LeftViewport.MinZ = 0.0f;
        m_LeftViewport.MaxZ = 1.0f;

        m_RightViewport.X = halfW;
        m_RightViewport.Y = 0;
        m_RightViewport.Width = halfW;
        m_RightViewport.Height = height;
        m_RightViewport.MinZ = 0.0f;
        m_RightViewport.MaxZ = 1.0f;

        m_FullViewport.X = 0;
        m_FullViewport.Y = 0;
        m_FullViewport.Width = width;
        m_FullViewport.Height = height;
        m_FullViewport.MinZ = 0.0f;
        m_FullViewport.MaxZ = 1.0f;

        UpdateHudViewports();
    }

    bool IsPerspective(const D3DMATRIX& proj) {
        // In Direct3D, perspective projections have _44 == 0.0f and _34 == 1.0f
        return (fabsf(proj._44) < 0.001f && fabsf(proj._34) > 0.001f);
    }

    float GetHalfFovY() const {
        if (fabsf(m_BaseProj._22) > 0.001f) {
            return atanf(1.0f / m_BaseProj._22);
        }
        return 0.55f; // ~63 deg default
    }

    float GetHalfFovX() const {
        if (fabsf(m_BaseProj._11) > 0.001f) {
            return atanf(1.0f / m_BaseProj._11);
        }
        return 0.55f;
    }

    void OnSetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) {
        if (!pMatrix) return;

        if (State == D3DTS_PROJECTION) {
            m_BaseProj = *pMatrix;
            m_bIn3DPass = IsPerspective(m_BaseProj);
            if (m_bIn3DPass) {
                m_bHad3DThisFrame = true;
            }
            UpdateStereoMatrices();
        }
        else if (State == D3DTS_VIEW) {
            m_BaseView = *pMatrix;
            UpdateStereoMatrices();
        }
        else if (State == D3DTS_WORLD) {
            m_BaseWorld = *pMatrix;
        }
    }


    static inline D3DMATRIX MatrixMultiply(const D3DMATRIX& A, const D3DMATRIX& B) {
        D3DMATRIX R;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                R.m[i][j] = A.m[i][0] * B.m[0][j] +
                            A.m[i][1] * B.m[1][j] +
                            A.m[i][2] * B.m[2][j] +
                            A.m[i][3] * B.m[3][j];
            }
        }
        return R;
    }

    static inline void BuildOpenXRFovProjection(D3DMATRIX& mat, const XrFovf& fov, float nearZ, float farZ) {
        float l = tanf(fov.angleLeft);
        float r = tanf(fov.angleRight);
        float u = tanf(fov.angleUp);
        float d = tanf(fov.angleDown);

        memset(&mat, 0, sizeof(mat));
        mat._11 = 2.0f / (r - l);
        mat._22 = 2.0f / (u - d);
        mat._31 = -(l + r) / (r - l);
        mat._32 = -(u + d) / (u - d);
        mat._33 = farZ / (farZ - nearZ);
        mat._43 = -nearZ * farZ / (farZ - nearZ);
        mat._34 = 1.0f;
    }

    void BuildOpenXREyeView(D3DMATRIX& outView, const XrPosef& eyePose, bool bIsSkybox) {
        float qx = eyePose.orientation.x;
        float qy = eyePose.orientation.y;
        float qz = eyePose.orientation.z;
        float qw = eyePose.orientation.w;

        float x2 = qx + qx, y2 = qy + qy, z2 = qz + qz;
        float xx = qx * x2, xy = qx * y2, xz = qx * z2;
        float yy = qy * y2, yz = qy * z2, zz = qz * z2;
        float wx = qw * x2, wy = qw * y2, wz = qw * z2;

        float r00 = 1.0f - (yy + zz);
        float r01 = xy + wz;
        float r02 = xz - wy;

        float r10 = xy - wz;
        float r11 = 1.0f - (xx + zz);
        float r12 = yz + wx;

        float r20 = xz + wy;
        float r21 = yz - wx;
        float r22 = 1.0f - (xx + yy);

        // Convert OpenXR Right-Handed to Direct3D Left-Handed View Matrix (Sz * R_xr * Sz)^T:
        float rh00 =  r00, rh01 =  r10, rh02 = -r20;
        float rh10 =  r01, rh11 =  r11, rh12 = -r21;
        float rh20 = -r02, rh21 = -r12, rh22 =  r22;

        // Separate head movement (scaled by fTrackingPosScale) from IPD offset (not scaled by tracking)
        float headPosX = (g_OpenXR.m_views[0].pose.position.x + g_OpenXR.m_views[1].pose.position.x) * 0.5f;
        float headPosY = (g_OpenXR.m_views[0].pose.position.y + g_OpenXR.m_views[1].pose.position.y) * 0.5f;
        float headPosZ = (g_OpenXR.m_views[0].pose.position.z + g_OpenXR.m_views[1].pose.position.z) * 0.5f;

        float eyeOffsetX = eyePose.position.x - headPosX;
        float eyeOffsetY = eyePose.position.y - headPosY;
        float eyeOffsetZ = eyePose.position.z - headPosZ;

        D3DMATRIX V_head;
        memset(&V_head, 0, sizeof(V_head));
        V_head._11 = rh00; V_head._12 = rh01; V_head._13 = rh02;
        V_head._21 = rh10; V_head._22 = rh11; V_head._23 = rh12;
        V_head._31 = rh20; V_head._32 = rh21; V_head._33 = rh22;
        V_head._44 = 1.0f;

        if (!bIsSkybox) {
            // Main scene (Duke, roof, underground, water, foreground):
            // Apply 6DoF head tracking translation + IPD offset
            float hpx = (headPosX - g_OpenXR.m_recenterPos[0]) * g_VRConfig.fTrackingPosScale;
            float hpy = (headPosY - g_OpenXR.m_recenterPos[1]) * g_VRConfig.fTrackingPosScale;
            float hpz = (headPosZ - g_OpenXR.m_recenterPos[2]) * g_VRConfig.fTrackingPosScale;

            // Position in Direct3D:
            float px = (hpx + eyeOffsetX) * g_VRConfig.fWorldScale;
            float py = (hpy + eyeOffsetY) * g_VRConfig.fWorldScale;
            float pz = -(hpz + eyeOffsetZ) * g_VRConfig.fWorldScale;

            // Translation row: t = -pos * R_d3d^T
            V_head._41 = -(px * rh00 + py * rh10 + pz * rh20);
            V_head._42 = -(px * rh01 + py * rh11 + pz * rh21);
            V_head._43 = -(px * rh02 + py * rh12 + pz * rh22);
        } else {
            // Skybox pass:
            // The skybox is an infinite/distant backdrop. We apply full head rotation (above),
            // but do NOT translate into the miniature skybox model, preventing clipping and black voids.
            // Small IPD offset for subtle stereo depth on distant buildings:
            float px = eyeOffsetX * g_VRConfig.fWorldScale * 0.05f;
            float py = eyeOffsetY * g_VRConfig.fWorldScale * 0.05f;
            float pz = -eyeOffsetZ * g_VRConfig.fWorldScale * 0.05f;

            V_head._41 = -(px * rh00 + py * rh10 + pz * rh20);
            V_head._42 = -(px * rh01 + py * rh11 + pz * rh21);
            V_head._43 = -(px * rh02 + py * rh12 + pz * rh22);
        }

        outView = MatrixMultiply(m_BaseView, V_head);

        // Apply camera distance zoom to Duke and the entire game world uniformly!
        // Skybox NEVER receives fCameraDistance zoom, so it stays fixed in the background and never clips/blackouts!
        if (!bIsSkybox && fabsf(g_VRConfig.fCameraDistance) > 0.001f) {
            outView._43 += g_VRConfig.fCameraDistance;
        }
    }

    void UpdateStereoMatrices() {
        if (!m_bIn3DPass) return;

        // Determine near/far planes from base proj
        if (fabsf(m_BaseProj._33) > 0.001f && fabsf(1.0f - m_BaseProj._33) > 0.001f) {
            float nz = -m_BaseProj._43 / m_BaseProj._33;
            float fz = m_BaseProj._43 / (1.0f - m_BaseProj._33);
            if (nz > 0.05f && fz > nz && fz < 50000.0f) {
                g_OpenXR.m_nearZ = nz;
                g_OpenXR.m_farZ  = fz;
            }
        }

        if (g_OpenXR.m_bViewsLocated) {
            // ── Full 360 Immersive VR Projection ──────────────────────────
            float nearZ = (g_OpenXR.m_nearZ > 0.05f) ? g_OpenXR.m_nearZ : 0.5f;
            float farZ  = (g_OpenXR.m_farZ > nearZ) ? g_OpenXR.m_farZ : 10000.0f;

            BuildOpenXRFovProjection(m_LeftProj,  g_OpenXR.m_views[0].fov, nearZ, farZ);
            BuildOpenXRFovProjection(m_RightProj, g_OpenXR.m_views[1].fov, nearZ, farZ);

            // 1. Skybox views (no camera distance zoom, subtle IPD)
            BuildOpenXREyeView(m_LeftViewSkybox,  g_OpenXR.m_views[0].pose, true);
            BuildOpenXREyeView(m_RightViewSkybox, g_OpenXR.m_views[1].pose, true);

            // 2. World views (with camera distance zoom, full 6DoF tracking)
            BuildOpenXREyeView(m_LeftViewWorld,   g_OpenXR.m_views[0].pose, false);
            BuildOpenXREyeView(m_RightViewWorld,  g_OpenXR.m_views[1].pose, false);

            if (fabsf(m_LeftProj._22) > 0.001f) g_OpenXR.m_gameFovY = atanf(1.0f / m_LeftProj._22);
            if (fabsf(m_LeftProj._11) > 0.001f) g_OpenXR.m_gameFovX = atanf(1.0f / m_LeftProj._11);
        } else {
            m_LeftProj = m_BaseProj;
            m_RightProj = m_BaseProj;
            m_LeftViewSkybox = m_BaseView;
            m_RightViewSkybox = m_BaseView;
            m_LeftViewWorld = m_BaseView;
            m_RightViewWorld = m_BaseView;
        }

        m_LeftView  = m_bInSkybox ? m_LeftViewSkybox : m_LeftViewWorld;
        m_RightView = m_bInSkybox ? m_RightViewSkybox : m_RightViewWorld;
        UpdateHudViewports();
    }
};

extern StereoEngine g_StereoEngine;
extern VRConfig g_VRConfig;
extern OpenXRHandler g_OpenXR;
