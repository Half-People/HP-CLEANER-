#include "HRC_Assets.h"
#include "HPage.h"
#include <d3d9.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace {
	IDirect3DDevice9* g_render_device = nullptr;
}

void HRC::SetRenderDevice(IDirect3DDevice9* device)
{
	g_render_device = device;
}

IDirect3DDevice9* HRC::GetRenderDevice()
{
	return g_render_device;
}

HRC::AssetInfo HRC::GetResourceData(LPCWSTR resourceName, LPCWSTR resourceType)
{
	HRSRC hRes = FindResourceW(nullptr, resourceName, resourceType);
	if (hRes) {
		HGLOBAL hData = LoadResource(nullptr, hRes);
		void* pFontData = LockResource(hData);
		const DWORD fontSize = SizeofResource(nullptr, hRes);
		if (pFontData == nullptr) {
			HLOG_ERROR("Failed to read resource: {}", reinterpret_cast<intptr_t>(resourceName));
		}
		else {
			HLOG_INFO("Successfully loaded resource: {}, size: {} bytes",
				reinterpret_cast<intptr_t>(resourceName), fontSize);
		}

		return { pFontData, fontSize };
	}

	HLOG_ERROR("Failed to find resource: {}", reinterpret_cast<intptr_t>(resourceName));
	return { nullptr, 0 };
}

HRC::HTexture HRC::LoadTextureFromDevice(IDirect3DDevice9* device, LPCWSTR resourceName, LPCWSTR resourceType)
{
	if (device == nullptr) {
		return {};
	}

	const HRC::AssetInfo asset = GetResourceData(resourceName, resourceType);
	if (asset.data == nullptr || asset.size == 0) {
		return {};
	}

	int image_width = 0;
	int image_height = 0;
	const auto* buffer = static_cast<const unsigned char*>(asset.data);
	unsigned char* image_data = stbi_load_from_memory(buffer, static_cast<int>(asset.size),
		&image_width, &image_height, nullptr, 4);
	if (image_data == nullptr) {
		return {};
	}

	LPDIRECT3DTEXTURE9 texture = nullptr;
	const HRESULT hr = device->CreateTexture(image_width, image_height, 1, 0, D3DFMT_A8R8G8B8,
		D3DPOOL_MANAGED, &texture, nullptr);
	if (FAILED(hr)) {
		stbi_image_free(image_data);
		return {};
	}

	D3DLOCKED_RECT rect = {};
	if (SUCCEEDED(texture->LockRect(0, &rect, nullptr, 0))) {
		unsigned char* src = image_data;
		for (int y = 0; y < image_height; ++y) {
			unsigned char* row_dest = static_cast<unsigned char*>(rect.pBits) + (y * rect.Pitch);
			for (int x = 0; x < image_width; ++x) {
				row_dest[x * 4 + 0] = src[x * 4 + 2];
				row_dest[x * 4 + 1] = src[x * 4 + 1];
				row_dest[x * 4 + 2] = src[x * 4 + 0];
				row_dest[x * 4 + 3] = src[x * 4 + 3];
			}
			src += image_width * 4;
		}
		texture->UnlockRect(0);
	}

	stbi_image_free(image_data);

	HRC::HTexture result;
	result.texture = reinterpret_cast<intptr_t>(texture);
	result.image_width = image_width;
	result.image_height = image_height;
	return result;
}

HRC::HTexture HRC::LoadTextureFromRgba(IDirect3DDevice9* device, const unsigned char* rgba,
	int width, int height)
{
	HRC::HTexture result = {};
	if (device == nullptr || rgba == nullptr || width <= 0 || height <= 0) {
		return result;
	}

	LPDIRECT3DTEXTURE9 texture = nullptr;
	const HRESULT hr = device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8,
		D3DPOOL_MANAGED, &texture, nullptr);
	if (FAILED(hr)) {
		return result;
	}

	D3DLOCKED_RECT rect = {};
	if (SUCCEEDED(texture->LockRect(0, &rect, nullptr, 0))) {
		const unsigned char* src = rgba;
		for (int y = 0; y < height; ++y) {
			unsigned char* row_dest = static_cast<unsigned char*>(rect.pBits) + (y * rect.Pitch);
			for (int x = 0; x < width; ++x) {
				row_dest[x * 4 + 0] = src[x * 4 + 2];
				row_dest[x * 4 + 1] = src[x * 4 + 1];
				row_dest[x * 4 + 2] = src[x * 4 + 0];
				row_dest[x * 4 + 3] = src[x * 4 + 3];
			}
			src += width * 4;
		}
		texture->UnlockRect(0);
	}

	result.texture = reinterpret_cast<intptr_t>(texture);
	result.image_width = width;
	result.image_height = height;
	return result;
}

bool HRC::FreeTexture(HRC::HTexture& texture)
{
	if (texture.texture != 0) {
		reinterpret_cast<LPDIRECT3DTEXTURE9>(texture.texture)->Release();
		texture.texture = 0;
		return true;
	}
	return false;
}
