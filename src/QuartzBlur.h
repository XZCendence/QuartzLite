// framebuffer copy, gaussian-blurred at half res, drawn under popovers

#pragma once

#include "QuartzTypes.h"

#include "imgui.h"

#include <d3d11.h>
#include <d3dcompiler.h>

struct FBackdropBlur
{
	ID3D11Device*             Device = nullptr;
	ID3D11DeviceContext*      Context = nullptr;
	ID3D11Texture2D*          Capture = nullptr;
	ID3D11ShaderResourceView* CaptureSRV = nullptr;
	ID3D11Texture2D*          TexA = nullptr;
	ID3D11ShaderResourceView* SrvA = nullptr;
	ID3D11RenderTargetView*   RtvA = nullptr;
	ID3D11Texture2D*          TexB = nullptr;
	ID3D11ShaderResourceView* SrvB = nullptr;
	ID3D11RenderTargetView*   RtvB = nullptr;
	ID3D11VertexShader*       VertexShader = nullptr;
	ID3D11PixelShader*        PixelShader = nullptr;
	ID3D11Buffer*             ConstantBuffer = nullptr;
	ID3D11SamplerState*       Sampler = nullptr;
	ID3D11BlendState*         Blend = nullptr;
	ID3D11DepthStencilState*  Depth = nullptr;
	ID3D11RasterizerState*    Rasterizer = nullptr;
	uint32 Width = 0;       // full res
	uint32 Height = 0;
	uint32 BlurWidth = 0;   // half res
	uint32 BlurHeight = 0;
	bool bReady = false;

	struct FBlurConstants
	{
		float Dir[2];
		float Texel[2];
		float Strength;
		float Pad[3];
	};

	static const char* ShaderSource()
	{
		return
		"struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
		"VSOut vs_main(uint id : SV_VertexID) {\n"
		"  VSOut o; float2 uv = float2((id << 1) & 2, id & 2);\n"
		"  o.uv = uv; o.pos = float4(uv * float2(2,-2) + float2(-1,1), 0, 1); return o; }\n"
		"Texture2D    tex : register(t0);\n"
		"SamplerState smp : register(s0);\n"
		"cbuffer CB : register(b0) { float2 dir; float2 texel; float strength; float3 pad; };\n"
		"float4 ps_main(VSOut i) : SV_TARGET {\n"
		"  const float w0=0.227027, w1=0.194594, w2=0.121621, w3=0.054054, w4=0.016216;\n"
		"  float2 o = dir * texel * strength;\n"
		"  float4 c = tex.Sample(smp, i.uv) * w0;\n"
		"  c += tex.Sample(smp, i.uv + o*1.0)*w1; c += tex.Sample(smp, i.uv - o*1.0)*w1;\n"
		"  c += tex.Sample(smp, i.uv + o*2.0)*w2; c += tex.Sample(smp, i.uv - o*2.0)*w2;\n"
		"  c += tex.Sample(smp, i.uv + o*3.0)*w3; c += tex.Sample(smp, i.uv - o*3.0)*w3;\n"
		"  c += tex.Sample(smp, i.uv + o*4.0)*w4; c += tex.Sample(smp, i.uv - o*4.0)*w4;\n"
		"  c.a = 1.0; return c; }\n";
	}

	bool Init(ID3D11Device* InDevice, ID3D11DeviceContext* InContext)
	{
		Device = InDevice;
		Context = InContext;
		ID3DBlob* VsBlob = nullptr;
		ID3DBlob* PsBlob = nullptr;
		ID3DBlob* Errors = nullptr;
		if (FAILED(D3DCompile(ShaderSource(), strlen(ShaderSource()), nullptr, nullptr, nullptr,
			"vs_main", "vs_5_0", 0, 0, &VsBlob, &Errors)))
		{
			if (Errors)
			{
				Errors->Release();
			}
			return false;
		}
		if (FAILED(D3DCompile(ShaderSource(), strlen(ShaderSource()), nullptr, nullptr, nullptr,
			"ps_main", "ps_5_0", 0, 0, &PsBlob, &Errors)))
		{
			if (Errors)
			{
				Errors->Release();
			}
			VsBlob->Release();
			return false;
		}
		Device->CreateVertexShader(VsBlob->GetBufferPointer(), VsBlob->GetBufferSize(), nullptr, &VertexShader);
		Device->CreatePixelShader(PsBlob->GetBufferPointer(), PsBlob->GetBufferSize(), nullptr, &PixelShader);
		VsBlob->Release();
		PsBlob->Release();

		D3D11_BUFFER_DESC BufferDesc = {};
		BufferDesc.ByteWidth = sizeof(FBlurConstants);
		BufferDesc.Usage = D3D11_USAGE_DYNAMIC;
		BufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		Device->CreateBuffer(&BufferDesc, nullptr, &ConstantBuffer);

		D3D11_SAMPLER_DESC SamplerDesc = {};
		SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		SamplerDesc.AddressU = SamplerDesc.AddressV = SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		Device->CreateSamplerState(&SamplerDesc, &Sampler);

		D3D11_BLEND_DESC BlendDesc = {};
		BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		Device->CreateBlendState(&BlendDesc, &Blend);

		D3D11_DEPTH_STENCIL_DESC DepthDesc = {};
		Device->CreateDepthStencilState(&DepthDesc, &Depth);

		D3D11_RASTERIZER_DESC RasterizerDesc = {};
		RasterizerDesc.FillMode = D3D11_FILL_SOLID;
		RasterizerDesc.CullMode = D3D11_CULL_NONE;
		Device->CreateRasterizerState(&RasterizerDesc, &Rasterizer);
		return true;
	}

	template <typename ResourceType>
	static void SafeRelease(ResourceType*& Resource)
	{
		if (Resource)
		{
			Resource->Release();
			Resource = nullptr;
		}
	}

	void ReleaseSized()
	{
		SafeRelease(CaptureSRV);
		SafeRelease(Capture);
		SafeRelease(SrvA);
		SafeRelease(RtvA);
		SafeRelease(TexA);
		SafeRelease(SrvB);
		SafeRelease(RtvB);
		SafeRelease(TexB);
		bReady = false;
	}

	void Shutdown()
	{
		ReleaseSized();
		SafeRelease(Rasterizer);
		SafeRelease(Depth);
		SafeRelease(Blend);
		SafeRelease(Sampler);
		SafeRelease(ConstantBuffer);
		SafeRelease(PixelShader);
		SafeRelease(VertexShader);
	}

	void Resize(uint32 NewWidth, uint32 NewHeight)
	{
		if (NewWidth == 0 || NewHeight == 0)
		{
			return;
		}
		if (NewWidth == Width && NewHeight == Height && bReady)
		{
			return;
		}
		ReleaseSized();
		Width = NewWidth;
		Height = NewHeight;
		BlurWidth = NewWidth / 2;
		BlurHeight = NewHeight / 2;
		if (BlurWidth == 0 || BlurHeight == 0)
		{
			return;
		}

		D3D11_TEXTURE2D_DESC TextureDesc = {};
		TextureDesc.Width = Width;
		TextureDesc.Height = Height;
		TextureDesc.MipLevels = 1;
		TextureDesc.ArraySize = 1;
		TextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		TextureDesc.SampleDesc.Count = 1;
		TextureDesc.Usage = D3D11_USAGE_DEFAULT;
		TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, &Capture)))
		{
			return;
		}
		Device->CreateShaderResourceView(Capture, nullptr, &CaptureSRV);

		TextureDesc.Width = BlurWidth;
		TextureDesc.Height = BlurHeight;
		TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, &TexA)))
		{
			return;
		}
		Device->CreateShaderResourceView(TexA, nullptr, &SrvA);
		Device->CreateRenderTargetView(TexA, nullptr, &RtvA);
		if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, &TexB)))
		{
			return;
		}
		Device->CreateShaderResourceView(TexB, nullptr, &SrvB);
		Device->CreateRenderTargetView(TexB, nullptr, &RtvB);
		bReady = true;
	}

	void Pass(ID3D11ShaderResourceView* Source, uint32 SourceWidth, uint32 SourceHeight,
		ID3D11RenderTargetView* Target, float DirX, float DirY, float Strength)
	{
		D3D11_MAPPED_SUBRESOURCE Mapped;
		if (SUCCEEDED(Context->Map(ConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
		{
			FBlurConstants* Constants = (FBlurConstants*)Mapped.pData;
			Constants->Dir[0] = DirX;
			Constants->Dir[1] = DirY;
			// offsets belong to the source, not the output. first pass reads the full-size capture
			Constants->Texel[0] = 1.0f / (float)SourceWidth;
			Constants->Texel[1] = 1.0f / (float)SourceHeight;
			Constants->Strength = Strength;
			Context->Unmap(ConstantBuffer, 0);
		}
		ID3D11ShaderResourceView* NullSrv[1] = { nullptr };
		Context->PSSetShaderResources(0, 1, NullSrv);
		Context->OMSetRenderTargets(1, &Target, nullptr);

		D3D11_VIEWPORT Viewport = {};
		Viewport.Width = (float)BlurWidth;
		Viewport.Height = (float)BlurHeight;
		Viewport.MaxDepth = 1.0f;
		Context->RSSetViewports(1, &Viewport);

		Context->IASetInputLayout(nullptr);
		Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		Context->VSSetShader(VertexShader, nullptr, 0);
		Context->PSSetShader(PixelShader, nullptr, 0);
		Context->PSSetShaderResources(0, 1, &Source);
		Context->PSSetSamplers(0, 1, &Sampler);
		Context->PSSetConstantBuffers(0, 1, &ConstantBuffer);
		const float BlendFactor[4] = { 0, 0, 0, 0 };
		Context->OMSetBlendState(Blend, BlendFactor, 0xffffffff);
		Context->OMSetDepthStencilState(Depth, 0);
		Context->RSSetState(Rasterizer);
		Context->Draw(3, 0);
		Context->PSSetShaderResources(0, 1, NullSrv);
	}

	// after the ui is rendered, and only when no glass was drawn this frame
	void CaptureAndBlur(IDXGISwapChain* Swap, float Strength)
	{
		if (!bReady)
		{
			return;
		}
		ID3D11Texture2D* Back = nullptr;
		if (FAILED(Swap->GetBuffer(0, IID_PPV_ARGS(&Back))) || !Back)
		{
			return;
		}
		Context->CopyResource(Capture, Back);
		Back->Release();
		Pass(CaptureSRV, Width, Height, RtvA, 1.0f, 0.0f, Strength);
		Pass(SrvA, BlurWidth, BlurHeight, RtvB, 0.0f, 1.0f, Strength);
	}

	ImTextureID Texture() const
	{
		return bReady ? (ImTextureID)(intptr_t)SrvB : (ImTextureID)0;
	}
};
