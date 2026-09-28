/**
 * Copyright (C) 2015 Patrick Mours. All rights reserved.
 * License: https://github.com/crosire/d3d8to9#license
 */

#include "d3d8to9.hpp"
#include "vr_config.hpp"
#include "stereo_engine.hpp"

static const D3DFORMAT AdapterFormats[] = {
	D3DFMT_A8R8G8B8,
	D3DFMT_X8R8G8B8,
	D3DFMT_R5G6B5,
	D3DFMT_X1R5G5B5,
	D3DFMT_A1R5G5B5
};

Direct3D8::Direct3D8(IDirect3D9 *ProxyInterface) :
	ProxyInterface(ProxyInterface)
{
	D3DDISPLAYMODE pMode;

	CurrentAdapterCount = ProxyInterface->GetAdapterCount();
	if (CurrentAdapterCount > MAX_ADAPTERS)
		CurrentAdapterCount = MAX_ADAPTERS;

	for (UINT Adapter = 0; Adapter < CurrentAdapterCount; Adapter++)
	{
		for (D3DFORMAT Format : AdapterFormats)
		{
			const UINT ModeCount = ProxyInterface->GetAdapterModeCount(Adapter, Format);

			for (UINT Mode = 0; Mode < ModeCount; Mode++)
			{
				ProxyInterface->EnumAdapterModes(Adapter, Format, Mode, &pMode);
				CurrentAdapterModes[Adapter].push_back(pMode);
				CurrentAdapterModeCount[Adapter]++;
			}
		}
	}
}
Direct3D8::~Direct3D8()
{
}

HRESULT STDMETHODCALLTYPE Direct3D8::QueryInterface(REFIID riid, void **ppvObj)
{
	if (ppvObj == nullptr)
		return E_POINTER;

	if (riid == __uuidof(IDirect3D8) ||
		riid == __uuidof(IUnknown))
	{
		AddRef();
		*ppvObj = static_cast<IDirect3D8 *>(this);

		return S_OK;
	}

	return ProxyInterface->QueryInterface(ConvertREFIID(riid), ppvObj);
}
ULONG STDMETHODCALLTYPE Direct3D8::AddRef()
{
	return ProxyInterface->AddRef();
}
ULONG STDMETHODCALLTYPE Direct3D8::Release()
{
	const ULONG LastRefCount = ProxyInterface->Release();

	if (LastRefCount == 0)
		delete this;

	return LastRefCount;
}

HRESULT STDMETHODCALLTYPE Direct3D8::RegisterSoftwareDevice(void *pInitializeFunction)
{
	return ProxyInterface->RegisterSoftwareDevice(pInitializeFunction);
}
UINT STDMETHODCALLTYPE Direct3D8::GetAdapterCount()
{
	return CurrentAdapterCount;
}
HRESULT STDMETHODCALLTYPE Direct3D8::GetAdapterIdentifier(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER8 *pIdentifier)
{
	if (pIdentifier == nullptr)
		return D3DERR_INVALIDCALL;

	D3DADAPTER_IDENTIFIER9 AdapterIndentifier;

	if ((Flags & D3DENUM_NO_WHQL_LEVEL) == 0)
	{
		Flags |= D3DENUM_WHQL_LEVEL;
	}
	else
	{
		Flags ^= D3DENUM_NO_WHQL_LEVEL;
	}

	const HRESULT hr = ProxyInterface->GetAdapterIdentifier(Adapter, Flags, &AdapterIndentifier);
	if (FAILED(hr))
		return hr;

	ConvertAdapterIdentifier(AdapterIndentifier, *pIdentifier);

	return D3D_OK;
}
UINT STDMETHODCALLTYPE Direct3D8::GetAdapterModeCount(UINT Adapter)
{
	return CurrentAdapterModeCount[Adapter];
}
HRESULT STDMETHODCALLTYPE Direct3D8::EnumAdapterModes(UINT Adapter, UINT Mode, D3DDISPLAYMODE *pMode)
{
	if (pMode == nullptr || !(Adapter < CurrentAdapterCount && Mode < CurrentAdapterModeCount[Adapter]))
		return D3DERR_INVALIDCALL;

	pMode->Format = CurrentAdapterModes[Adapter].at(Mode).Format;
	pMode->Height = CurrentAdapterModes[Adapter].at(Mode).Height;
	pMode->RefreshRate = CurrentAdapterModes[Adapter].at(Mode).RefreshRate;
	pMode->Width = CurrentAdapterModes[Adapter].at(Mode).Width;

	return D3D_OK;
}
HRESULT STDMETHODCALLTYPE Direct3D8::GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE *pMode)
{
	return ProxyInterface->GetAdapterDisplayMode(Adapter, pMode);
}
HRESULT STDMETHODCALLTYPE Direct3D8::CheckDeviceType(UINT Adapter, D3DDEVTYPE CheckType, D3DFORMAT DisplayFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed)
{
	return ProxyInterface->CheckDeviceType(Adapter, CheckType, DisplayFormat, BackBufferFormat, bWindowed);
}
HRESULT STDMETHODCALLTYPE Direct3D8::CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat)
{
	if (CheckFormat == D3DFMT_UYVY ||
		CheckFormat == D3DFMT_YUY2 ||
		CheckFormat == MAKEFOURCC('Y', 'V', '1', '2') ||
		CheckFormat == MAKEFOURCC('N', 'V', '1', '2'))
	{
		return D3DERR_NOTAVAILABLE;
	}

	return ProxyInterface->CheckDeviceFormat(Adapter, DeviceType, AdapterFormat, Usage, RType, CheckFormat);
}
HRESULT STDMETHODCALLTYPE Direct3D8::CheckDeviceMultiSampleType(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType)
{
	return ProxyInterface->CheckDeviceMultiSampleType(Adapter, DeviceType, SurfaceFormat, Windowed, MultiSampleType, nullptr);
}
HRESULT STDMETHODCALLTYPE Direct3D8::CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat)
{
	return ProxyInterface->CheckDepthStencilMatch(Adapter, DeviceType, AdapterFormat, RenderTargetFormat, DepthStencilFormat);
}
HRESULT STDMETHODCALLTYPE Direct3D8::GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS8 *pCaps)
{
	if (pCaps == nullptr)
		return D3DERR_INVALIDCALL;

	D3DCAPS9 DeviceCaps;

	const HRESULT hr = ProxyInterface->GetDeviceCaps(Adapter, DeviceType, &DeviceCaps);
	if (FAILED(hr))
		return hr;

	ConvertCaps(DeviceCaps, *pCaps);

	return D3D_OK;
}
HMONITOR STDMETHODCALLTYPE Direct3D8::GetAdapterMonitor(UINT Adapter)
{
	return ProxyInterface->GetAdapterMonitor(Adapter);
}
HRESULT STDMETHODCALLTYPE Direct3D8::CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS8 *pPresentationParameters, IDirect3DDevice8 **ppReturnedDeviceInterface)
{
#ifndef D3D8TO9NOLOG
	LOG << "Redirecting '" << "IDirect3D8::CreateDevice" << "(" << this << ", " << Adapter << ", " << DeviceType << ", " << hFocusWindow << ", " << BehaviorFlags << ", " << pPresentationParameters << ", " << ppReturnedDeviceInterface << ")' ..." << std::endl;
#endif

	if (pPresentationParameters == nullptr || ppReturnedDeviceInterface == nullptr)
		return D3DERR_INVALIDCALL;

	*ppReturnedDeviceInterface = nullptr;

	D3DPRESENT_PARAMETERS PresentParams;
	ConvertPresentParameters(*pPresentationParameters, PresentParams);

	if (g_VRConfig.bEnableVR)
	{
		// Force windowed mode for VR:
		// 1. Allows arbitrary VR render resolution (e.g. 2560x1440 or 3840x2160) on any PC monitor (e.g. 1080p).
		// 2. OpenXR headset receives full stereo resolution while desktop displays companion window without D3DERR_INVALIDCALL.
		PresentParams.Windowed = TRUE;
		PresentParams.FullScreen_RefreshRateInHz = 0;
		PresentParams.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
		PresentParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
	}

	if (g_VRConfig.bEnableVR && g_VRConfig.bForceWidescreen)
	{
		if (PresentParams.BackBufferWidth < (UINT)g_VRConfig.iForcedWidth)
			PresentParams.BackBufferWidth = g_VRConfig.iForcedWidth;
		if (PresentParams.BackBufferHeight < (UINT)g_VRConfig.iForcedHeight)
			PresentParams.BackBufferHeight = g_VRConfig.iForcedHeight;
	}

	IDirect3DDevice9 *DeviceInterface = nullptr;

	HRESULT hr = ProxyInterface->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, &PresentParams, &DeviceInterface);
	if (FAILED(hr))
	{
#ifndef D3D8TO9NOLOG
		LOG << "ProxyInterface->CreateDevice failed with hr=" << std::hex << hr << std::dec << ". Retrying with safe windowed parameters..." << std::endl;
#endif
		// Fallback 1: Try windowed mode with original presentation parameters
		ConvertPresentParameters(*pPresentationParameters, PresentParams);
		PresentParams.Windowed = TRUE;
		PresentParams.FullScreen_RefreshRateInHz = 0;
		PresentParams.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
		PresentParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
		hr = ProxyInterface->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, &PresentParams, &DeviceInterface);

		if (FAILED(hr))
		{
#ifndef D3D8TO9NOLOG
			LOG << "ProxyInterface->CreateDevice fallback 1 failed with hr=" << std::hex << hr << std::dec << ". Retrying with 0x0 windowed..." << std::endl;
#endif
			// Fallback 2: Try windowed mode with 0x0 (uses client window dimensions)
			PresentParams.BackBufferWidth = 0;
			PresentParams.BackBufferHeight = 0;
			hr = ProxyInterface->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, &PresentParams, &DeviceInterface);
		}
	}

	if (FAILED(hr))
	{
#ifndef D3D8TO9NOLOG
		LOG << "ProxyInterface->CreateDevice fatal failure: hr=" << std::hex << hr << std::dec << std::endl;
#endif
		return hr;
	}

	if (g_VRConfig.bEnableVR)
	{
		HWND hTargetWnd = PresentParams.hDeviceWindow ? PresentParams.hDeviceWindow : hFocusWindow;
		if (hTargetWnd)
		{
			int screenW = GetSystemMetrics(SM_CXSCREEN);
			int screenH = GetSystemMetrics(SM_CYSCREEN);
			SetWindowLongPtr(hTargetWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
			SetWindowPos(hTargetWnd, HWND_TOP, 0, 0, screenW, screenH, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
			ShowWindow(hTargetWnd, SW_SHOWNORMAL);
			SetForegroundWindow(hTargetWnd);
		}
	}

	const UINT origBackBufferWidth = pPresentationParameters->BackBufferWidth;
	const UINT origBackBufferHeight = pPresentationParameters->BackBufferHeight;

	g_StereoEngine.OnReset(PresentParams.BackBufferWidth, PresentParams.BackBufferHeight);

	Direct3DDevice8 *Device = new Direct3DDevice8(this, DeviceInterface, BehaviorFlags, PresentParams.EnableAutoDepthStencil ? PresentParams.AutoDepthStencilFormat : D3DFMT_UNKNOWN, (PresentParams.Flags & D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL) != 0);
	Device->SetOrigBackBufferDimensions(origBackBufferWidth, origBackBufferHeight);
	*ppReturnedDeviceInterface = Device;

	// Set default vertex declaration
	DeviceInterface->SetFVF(D3DFVF_XYZ);

	return D3D_OK;
}
