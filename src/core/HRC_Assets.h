#pragma once
#include <windows.h>
#include <d3d9.h>
#include "resource.h"
#define RT_PNG      MAKEINTRESOURCE(103)
namespace HRC {
	struct HTexture
	{
		int image_width = 0;
		int image_height = 0;
		unsigned long long texture = 0;
	};
	struct AssetInfo{
		void* data = nullptr;
		size_t size = 0;
	};

	AssetInfo GetResourceData(LPCWSTR resourceName, LPCWSTR resourceType);

	HTexture LoadTexture(LPCWSTR resourceName, LPCWSTR resourceType);
	HTexture LoadTextureFromDevice(IDirect3DDevice9* device, LPCWSTR resourceName, LPCWSTR resourceType);
	HTexture LoadTextureFromRgba(IDirect3DDevice9* device, const unsigned char* rgba,
		int width, int height);
	void SetRenderDevice(IDirect3DDevice9* device);
	IDirect3DDevice9* GetRenderDevice();
	bool FreeTexture(HTexture& texture);
}