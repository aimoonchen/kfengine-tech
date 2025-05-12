/*
 *  Copyright 2019-2025 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

 #include <memory>
 #include <iomanip>
 #include <iostream>
 
 #ifndef NOMINMAX
 #    define NOMINMAX
 #endif
 #include <Windows.h>
 #include <crtdbg.h>
 
 #ifndef PLATFORM_WIN32
 #    define PLATFORM_WIN32 1
 #endif
 
 #ifndef ENGINE_DLL
 #    define ENGINE_DLL 1
 #endif
 
 #ifndef D3D11_SUPPORTED
 #    define D3D11_SUPPORTED 1
 #endif
 
 #ifndef D3D12_SUPPORTED
 #    define D3D12_SUPPORTED 1
 #endif
 
 #ifndef GL_SUPPORTED
 #    define GL_SUPPORTED 1
 #endif
 
 #ifndef VULKAN_SUPPORTED
 #    define VULKAN_SUPPORTED 1
 #endif
 
 #include "Graphics/GraphicsEngineD3D11/interface/EngineFactoryD3D11.h"
 #include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
 #include "Graphics/GraphicsEngineOpenGL/interface/EngineFactoryOpenGL.h"
 #include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
 
 #include "Graphics/GraphicsEngine/interface/RenderDevice.h"
 #include "Graphics/GraphicsEngine/interface/DeviceContext.h"
 #include "Graphics/GraphicsEngine/interface/SwapChain.h"
#include "Graphics/GraphicsEngine/interface/GraphicsTypes.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "Graphics/GraphicsTools/interface/GraphicsUtilities.h"

 #include "Common/interface/RefCntAutoPtr.hpp"
#include "Common/interface/BasicMath.hpp"

#include <filameshio/MeshReader.h>
#include <filameshio/filamesh.h>

#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/VertexBuffer.h>
#include <details/Engine.h>

#include <filameshio/MeshReader.h>
#include <fcntl.h>
#if !defined(WIN32)
#    include <unistd.h>
#else
#    include <io.h>
#endif

using namespace Diligent;
static size_t fileSize(int fd) {
	size_t filesize;
	filesize = (size_t)lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	return filesize;
}

 // For this tutorial, we will use simple vertex shader
 // that creates a procedural triangle
 
 // Diligent Engine can use HLSL source on all supported platforms.
 // It will convert HLSL to GLSL in OpenGL mode, while Vulkan backend will compile it directly to SPIRV.
 
 static const char* VSSource = R"(
cbuffer Constants
{
    float4x4 g_WorldViewProj;
};

// Vertex shader takes two inputs: vertex position and uv coordinates.
// By convention, Diligent Engine expects vertex shader inputs to be 
// labeled 'ATTRIBn', where n is the attribute number.
struct VSInput
{
    float3 Pos : ATTRIB0;
    float2 UV  : ATTRIB1;
};

struct PSInput 
{ 
    float4 Pos : SV_POSITION; 
    float2 UV  : TEX_COORD; 
};

// Note that if separate shader objects are not supported (this is only the case for old GLES3.0 devices), vertex
// shader output variable name must match exactly the name of the pixel shader input variable.
// If the variable has structure type (like in this example), the structure declarations must also be identical.
void main(in  VSInput VSIn,
          out PSInput PSIn) 
{
    PSIn.Pos = mul( float4(VSIn.Pos,1.0), g_WorldViewProj);
    PSIn.UV  = VSIn.UV;
}
 )";
 
 // Pixel shader simply outputs interpolated vertex color
 static const char* PSSource = R"(
Texture2D    g_Texture;
SamplerState g_Texture_sampler; // By convention, texture samplers must use the '_sampler' suffix

struct PSInput
{
    float4 Pos : SV_POSITION;
    float2 UV  : TEX_COORD;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

void main(in  PSInput  PSIn,
          out PSOutput PSOut)
{
    float4 Color = g_Texture.Sample(g_Texture_sampler, PSIn.UV);
#if CONVERT_PS_OUTPUT_TO_GAMMA
    // Use fast approximation for gamma correction.
    Color.rgb = pow(Color.rgb, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
#endif
    PSOut.Color = Color;
}
 )";
 
 
 class Tutorial00App
 {
 public:
     Tutorial00App()
     {
     }
 
     ~Tutorial00App()
     {
         m_pImmediateContext->Flush();
     }
 
     bool InitializeDiligentEngine(HWND hWnd)
     {
         SwapChainDesc SCDesc;
         switch (m_DeviceType)
         {
 #if D3D11_SUPPORTED
             case RENDER_DEVICE_TYPE_D3D11:
             {
                 EngineD3D11CreateInfo EngineCI;
 #    if ENGINE_DLL
                 // Load the dll and import GetEngineFactoryD3D11() function
                 GetEngineFactoryD3D11Type GetEngineFactoryD3D11 = LoadGraphicsEngineD3D11();
 #    endif
                 IEngineFactoryD3D11* pFactoryD3D11 = GetEngineFactoryD3D11();
                 pFactoryD3D11->CreateDeviceAndContextsD3D11(EngineCI, &m_pDevice, &m_pImmediateContext);
                 Win32NativeWindow Window{hWnd};
                 pFactoryD3D11->CreateSwapChainD3D11(m_pDevice, m_pImmediateContext, SCDesc, FullScreenModeDesc{}, Window, &m_pSwapChain);
             }
             break;
 #endif
 
 
 #if D3D12_SUPPORTED
             case RENDER_DEVICE_TYPE_D3D12:
             {
 #    if ENGINE_DLL
                 // Load the dll and import GetEngineFactoryD3D12() function
                 GetEngineFactoryD3D12Type GetEngineFactoryD3D12 = LoadGraphicsEngineD3D12();
 #    endif
                 EngineD3D12CreateInfo EngineCI;
 
                 IEngineFactoryD3D12* pFactoryD3D12 = GetEngineFactoryD3D12();
                 pFactoryD3D12->CreateDeviceAndContextsD3D12(EngineCI, &m_pDevice, &m_pImmediateContext);
                 Win32NativeWindow Window{hWnd};
                 pFactoryD3D12->CreateSwapChainD3D12(m_pDevice, m_pImmediateContext, SCDesc, FullScreenModeDesc{}, Window, &m_pSwapChain);
             }
             break;
 #endif
 
 
 #if GL_SUPPORTED
             case RENDER_DEVICE_TYPE_GL:
             {
 #    if EXPLICITLY_LOAD_ENGINE_GL_DLL
                 // Load the dll and import GetEngineFactoryOpenGL() function
                 GetEngineFactoryOpenGLType GetEngineFactoryOpenGL = LoadGraphicsEngineOpenGL();
 #    endif
                 IEngineFactoryOpenGL* pFactoryOpenGL = GetEngineFactoryOpenGL();
 
                 EngineGLCreateInfo EngineCI;
                 EngineCI.Window.hWnd = hWnd;
 
                 pFactoryOpenGL->CreateDeviceAndSwapChainGL(EngineCI, &m_pDevice, &m_pImmediateContext, SCDesc, &m_pSwapChain);
             }
             break;
 #endif
 
 
 #if VULKAN_SUPPORTED
             case RENDER_DEVICE_TYPE_VULKAN:
             {
 #    if EXPLICITLY_LOAD_ENGINE_VK_DLL
                 // Load the dll and import GetEngineFactoryVk() function
                 GetEngineFactoryVkType GetEngineFactoryVk = LoadGraphicsEngineVk();
 #    endif
                 EngineVkCreateInfo EngineCI;
 
                 IEngineFactoryVk* pFactoryVk = GetEngineFactoryVk();
                 pFactoryVk->CreateDeviceAndContextsVk(EngineCI, &m_pDevice, &m_pImmediateContext);
 
                 if (!m_pSwapChain && hWnd != nullptr)
                 {
                     Win32NativeWindow Window{hWnd};
                     pFactoryVk->CreateSwapChainVk(m_pDevice, m_pImmediateContext, SCDesc, Window, &m_pSwapChain);
                 }
             }
             break;
 #endif
 
 
             default:
                 std::cerr << "Unknown/unsupported device type";
                 return false;
                 break;
         }
 
         return true;
     }
 
     bool ProcessCommandLine(const char* CmdLine)
     {
         const char* mode = nullptr;
 
         const char* Keys[] = {"--mode ", "--mode=", "-m "};
         for (size_t i = 0; i < _countof(Keys); ++i)
         {
             const char* Key = Keys[i];
             if ((mode = strstr(CmdLine, Key)) != nullptr)
             {
                 mode += strlen(Key);
                 break;
             }
         }
 
         if (mode != nullptr)
         {
             while (*mode == ' ')
                 ++mode;
 
             if (_stricmp(mode, "D3D11") == 0)
             {
 #if D3D11_SUPPORTED
                 m_DeviceType = RENDER_DEVICE_TYPE_D3D11;
 #else
                 std::cerr << "Direct3D11 is not supported. Please select another device type";
                 return false;
 #endif
             }
             else if (_stricmp(mode, "D3D12") == 0)
             {
 #if D3D12_SUPPORTED
                 m_DeviceType = RENDER_DEVICE_TYPE_D3D12;
 #else
                 std::cerr << "Direct3D12 is not supported. Please select another device type";
                 return false;
 #endif
             }
             else if (_stricmp(mode, "GL") == 0)
             {
 #if GL_SUPPORTED
                 m_DeviceType = RENDER_DEVICE_TYPE_GL;
 #else
                 std::cerr << "OpenGL is not supported. Please select another device type";
                 return false;
 #endif
             }
             else if (_stricmp(mode, "VK") == 0)
             {
 #if VULKAN_SUPPORTED
                 m_DeviceType = RENDER_DEVICE_TYPE_VULKAN;
 #else
                 std::cerr << "Vulkan is not supported. Please select another device type";
                 return false;
 #endif
             }
             else
             {
                 std::cerr << mode << " is not a valid device type. Only the following types are supported: D3D11, D3D12, GL, VK";
                 return false;
             }
         }
         else
         {
 #if D3D12_SUPPORTED
             m_DeviceType = RENDER_DEVICE_TYPE_D3D12;
 #elif VULKAN_SUPPORTED
             m_DeviceType = RENDER_DEVICE_TYPE_VULKAN;
 #elif D3D11_SUPPORTED
             m_DeviceType = RENDER_DEVICE_TYPE_D3D11;
 #elif GL_SUPPORTED
             m_DeviceType = RENDER_DEVICE_TYPE_GL;
 #endif
         }
         return true;
     }

	 void CreatePipelineState()
	 {
		 // Pipeline state object encompasses configuration of all GPU stages

		 GraphicsPipelineStateCreateInfo PSOCreateInfo;

		 // Pipeline state name is used by the engine to report issues.
		 // It is always a good idea to give objects descriptive names.
		 PSOCreateInfo.PSODesc.Name = "Cube PSO";

		 // This is a graphics pipeline
		 PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

		 // clang-format off
		 // This tutorial will render to a single render target
		 PSOCreateInfo.GraphicsPipeline.NumRenderTargets = 1;
		 // Set render target format which is the format of the swap chain's color buffer
		 PSOCreateInfo.GraphicsPipeline.RTVFormats[0] = m_pSwapChain->GetDesc().ColorBufferFormat;
		 // Set depth buffer format which is the format of the swap chain's back buffer
		 PSOCreateInfo.GraphicsPipeline.DSVFormat = m_pSwapChain->GetDesc().DepthBufferFormat;
		 // Primitive topology defines what kind of primitives will be rendered by this pipeline state
		 PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		 // Cull back faces
		 PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_BACK;
		 // Enable depth testing
		 PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = True;
		 // clang-format on

		 ShaderCreateInfo ShaderCI;
		 // Tell the system that the shader source code is in HLSL.
		 // For OpenGL, the engine will convert this into GLSL under the hood.
		 ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;

		 // OpenGL backend requires emulated combined HLSL texture samplers (g_Texture + g_Texture_sampler combination)
		 ShaderCI.Desc.UseCombinedTextureSamplers = true;

		 // Pack matrices in row-major order
		 ShaderCI.CompileFlags = SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR;

		 // Presentation engine always expects input in gamma space. Normally, pixel shader output is
		 // converted from linear to gamma space by the GPU. However, some platforms (e.g. Android in GLES mode,
		 // or Emscripten in WebGL mode) do not support gamma-correction. In this case the application
		 // has to do the conversion manually.
		 ShaderMacro Macros[] = { {"CONVERT_PS_OUTPUT_TO_GAMMA", m_ConvertPSOutputToGamma ? "1" : "0"} };
		 ShaderCI.Macros = { Macros, _countof(Macros) };

		 // Create a shader source stream factory to load shaders from files.
// 		 RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
// 		 m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);
// 		 ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;
		 // Create a vertex shader
		 RefCntAutoPtr<IShader> pVS;
		 {
			 ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
			 ShaderCI.EntryPoint = "main";
			 ShaderCI.Desc.Name = "Cube VS";
			 ShaderCI.Source = VSSource;
			 //ShaderCI.FilePath = "cube.vsh";
			 m_pDevice->CreateShader(ShaderCI, &pVS);
			 // Create dynamic uniform buffer that will store our transformation matrix
			 // Dynamic buffers can be frequently updated by the CPU
			 BufferDesc CBDesc;
			 CBDesc.Name = "VS constants CB";
			 CBDesc.Size = sizeof(float4x4);
			 CBDesc.Usage = USAGE_DYNAMIC;
			 CBDesc.BindFlags = BIND_UNIFORM_BUFFER;
			 CBDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
			 m_pDevice->CreateBuffer(CBDesc, nullptr, &m_VSConstants);
		 }

		 // Create a pixel shader
		 RefCntAutoPtr<IShader> pPS;
		 {
			 ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
			 ShaderCI.EntryPoint = "main";
			 ShaderCI.Desc.Name = "Cube PS";
			 ShaderCI.Source = PSSource;
			 //ShaderCI.FilePath = "cube.psh";
			 m_pDevice->CreateShader(ShaderCI, &pPS);
		 }

		 // clang-format off
		 // Define vertex shader input layout
		 LayoutElement LayoutElems[] =
		 {
			 // Attribute 0 - vertex position
			 LayoutElement{0, 0, 3, VT_FLOAT32, False},
			 // Attribute 1 - texture coordinates
			 LayoutElement{1, 0, 2, VT_FLOAT32, False}
		 };
		 // clang-format on

		 PSOCreateInfo.pVS = pVS;
		 PSOCreateInfo.pPS = pPS;

		 PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
		 PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = _countof(LayoutElems);

		 // Define variable type that will be used by default
		 PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		 // clang-format off
		 // Shader variables should typically be mutable, which means they are expected
		 // to change on a per-instance basis
		 ShaderResourceVariableDesc Vars[] =
		 {
			 {SHADER_TYPE_PIXEL, "g_Texture", SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE}
		 };
		 // clang-format on
		 PSOCreateInfo.PSODesc.ResourceLayout.Variables = Vars;
		 PSOCreateInfo.PSODesc.ResourceLayout.NumVariables = _countof(Vars);

		 // clang-format off
		 // Define immutable sampler for g_Texture. Immutable samplers should be used whenever possible
		 SamplerDesc SamLinearClampDesc
		 {
			 FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
			 TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP
		 };
		 ImmutableSamplerDesc ImtblSamplers[] =
		 {
			 {SHADER_TYPE_PIXEL, "g_Texture", SamLinearClampDesc}
		 };
		 // clang-format on
		 PSOCreateInfo.PSODesc.ResourceLayout.ImmutableSamplers = ImtblSamplers;
		 PSOCreateInfo.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImtblSamplers);

		 m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pPSO);

		 // Since we did not explicitly specify the type for 'Constants' variable, default
		 // type (SHADER_RESOURCE_VARIABLE_TYPE_STATIC) will be used. Static variables
		 // never change and are bound directly through the pipeline state object.
		 m_pPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")->Set(m_VSConstants);

		 // Since we are using mutable variable, we must create a shader resource binding object
		 // http://diligentgraphics.com/2016/03/23/resource-binding-model-in-diligent-engine-2-0/
		 m_pPSO->CreateShaderResourceBinding(&m_SRB, true);
	 }

	 void CreateVertexBuffer()
	 {
		 // Layout of this structure matches the one we defined in the pipeline state
		 struct Vertex
		 {
			 float3 pos;
			 float2 uv;
		 };

		 // Cube vertices

		 //      (-1,+1,+1)________________(+1,+1,+1)
		 //               /|              /|
		 //              / |             / |
		 //             /  |            /  |
		 //            /   |           /   |
		 //(-1,-1,+1) /____|__________/(+1,-1,+1)
		 //           |    |__________|____|
		 //           |   /(-1,+1,-1) |    /(+1,+1,-1)
		 //           |  /            |   /
		 //           | /             |  /
		 //           |/              | /
		 //           /_______________|/
		 //        (-1,-1,-1)       (+1,-1,-1)
		 //

		 // This time we have to duplicate verices because texture coordinates cannot
		 // be shared
		 constexpr Vertex CubeVerts[] =
		 {
			 {float3{-1, -1, -1}, float2{0, 1}},
			 {float3{-1, +1, -1}, float2{0, 0}},
			 {float3{+1, +1, -1}, float2{1, 0}},
			 {float3{+1, -1, -1}, float2{1, 1}},

			 {float3{-1, -1, -1}, float2{0, 1}},
			 {float3{-1, -1, +1}, float2{0, 0}},
			 {float3{+1, -1, +1}, float2{1, 0}},
			 {float3{+1, -1, -1}, float2{1, 1}},

			 {float3{+1, -1, -1}, float2{0, 1}},
			 {float3{+1, -1, +1}, float2{1, 1}},
			 {float3{+1, +1, +1}, float2{1, 0}},
			 {float3{+1, +1, -1}, float2{0, 0}},

			 {float3{+1, +1, -1}, float2{0, 1}},
			 {float3{+1, +1, +1}, float2{0, 0}},
			 {float3{-1, +1, +1}, float2{1, 0}},
			 {float3{-1, +1, -1}, float2{1, 1}},

			 {float3{-1, +1, -1}, float2{1, 0}},
			 {float3{-1, +1, +1}, float2{0, 0}},
			 {float3{-1, -1, +1}, float2{0, 1}},
			 {float3{-1, -1, -1}, float2{1, 1}},

			 {float3{-1, -1, +1}, float2{1, 1}},
			 {float3{+1, -1, +1}, float2{0, 1}},
			 {float3{+1, +1, +1}, float2{0, 0}},
			 {float3{-1, +1, +1}, float2{1, 0}},
		 };

		 BufferDesc VertBuffDesc;
		 VertBuffDesc.Name = "Cube vertex buffer";
		 VertBuffDesc.Usage = USAGE_IMMUTABLE;
		 VertBuffDesc.BindFlags = BIND_VERTEX_BUFFER;
		 VertBuffDesc.Size = sizeof(CubeVerts);
		 BufferData VBData;
		 VBData.pData = CubeVerts;
		 VBData.DataSize = sizeof(CubeVerts);
		 m_pDevice->CreateBuffer(VertBuffDesc, &VBData, &m_CubeVertexBuffer);
	 }

	 void CreateIndexBuffer()
	 {
		 // clang-format off
		 constexpr Uint32 Indices[] =
		 {
			 2,0,1,    2,3,0,
			 4,6,5,    4,7,6,
			 8,10,9,   8,11,10,
			 12,14,13, 12,15,14,
			 16,18,17, 16,19,18,
			 20,21,22, 20,22,23
		 };
		 // clang-format on

		 BufferDesc IndBuffDesc;
		 IndBuffDesc.Name = "Cube index buffer";
		 IndBuffDesc.Usage = USAGE_IMMUTABLE;
		 IndBuffDesc.BindFlags = BIND_INDEX_BUFFER;
		 IndBuffDesc.Size = sizeof(Indices);
		 BufferData IBData;
		 IBData.pData = Indices;
		 IBData.DataSize = sizeof(Indices);
		 m_pDevice->CreateBuffer(IndBuffDesc, &IBData, &m_CubeIndexBuffer);
	 }

	 void LoadTexture()
	 {
// 		 TextureLoadInfo loadInfo;
// 		 loadInfo.IsSRGB = true;
		RefCntAutoPtr<ITexture> Tex;
// 		 CreateTextureFromFile("DGLogo.png", loadInfo, m_pDevice, &Tex);
		 static constexpr Uint32 TexDim = 128;
		 TextureDesc TexDesc;
		 TexDesc.Name = "White texture for PBR renderer";
		 TexDesc.Type = RESOURCE_DIM_TEX_2D;
		 TexDesc.Usage = USAGE_IMMUTABLE;
		 TexDesc.BindFlags = BIND_SHADER_RESOURCE;
		 TexDesc.Width = TexDim;
		 TexDesc.Height = TexDim;
		 TexDesc.Format = TEX_FORMAT_RGBA8_UNORM;
		 TexDesc.MipLevels = 1;
		 std::vector<Uint32> Data(TexDim * TexDim, 0xFFFFFFFF);
		 TextureSubResData   Level0Data{ Data.data(), TexDim * 4 };
		 TextureData         InitData{ &Level0Data, 1 };

		 m_pDevice->CreateTexture(TexDesc, &InitData, &Tex);
		 // Get shader resource view from the texture
		 m_TextureSRV = Tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

		 // Set texture SRV in the SRB
		 m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture")->Set(m_TextureSRV);
	 }

	 void InitFilament()
	 {
		 using namespace filament;
		 //     Engine::Config engineConfig = {};
		 Engine* engine = FEngine::create();

		 int fd = open("D:\\filament-1.59.4\\samples\\materials\\aiDefaultMat.filamat", O_RDONLY);
		 size_t size = fileSize(fd);
		 char* data = (char*)malloc(size);
		 read(fd, data, size);

		 auto material = Material::Builder().package(data, size).build(*engine);
		 auto mi = material->createInstance();
		 mi->setParameter("baseColor", RgbType::LINEAR, math::float3{ 0.8 });
		 mi->setParameter("metallic", 1.0f);
		 mi->setParameter("roughness", 0.4f);
		 mi->setParameter("reflectance", 0.5f);
		 free(data);
		 close(fd);
		 //auto mesh = filamesh::MeshReader::loadMeshFromBuffer(engine, MONKEY_SUZANNE_DATA, nullptr, nullptr, mi);
		 fd = open("D:\\filament-1.59.4\\assets\\models\\monkey\\monkey.filamesh", O_RDONLY);
		 size = fileSize(fd);
		 data = (char*)malloc(size);
		 read(fd, data, size);
		 static const char MAGICID[]{ 'F', 'I', 'L', 'A', 'M', 'E', 'S', 'H' };
// 		 filamesh::MeshReader::Mesh mesh;
// 		 if (data) {
// 			 char* p = data;
// 			 char* magic = p;
// 			 p += sizeof(MAGICID);
// 
// 			 if (!strncmp(MAGICID, magic, 8)) {
// 				 filamesh::MeshReader::MaterialRegistry reg;
// 				 reg.registerMaterialInstance(utils::CString("DefaultMaterial"), mi);
// 				 mesh = filamesh::MeshReader::loadMeshFromBuffer(engine, data, nullptr, nullptr, reg);
// 			 }
// 			 free(data);
// 		 }
// 		 close(fd);

		 const uint8_t* p = (const uint8_t*)data;
		 if (strncmp(MAGICID, (const char*)p, 8)) {
			 // 		 utils::slog.e << "Magic string not found." << utils::io::endl;
			 // 		 return {};
			 ;
		 }
		 p += 8;

		 filamesh::Header* header = (filamesh::Header*)p;
		 p += sizeof(filamesh::Header);

		 uint8_t const* vertexData = p;
		 p += header->vertexSize;

		 uint8_t const* indices = p;
		 p += header->indexSize;

		 filamesh::Part* parts = (filamesh::Part*)p;
		 p += header->parts * sizeof(filamesh::Part);

		 uint32_t materialCount = (uint32_t)*p;
		 p += sizeof(uint32_t);

		 std::vector<std::string> partsMaterial(materialCount);
		 for (size_t i = 0; i < materialCount; i++) {
			 uint32_t nameLength = (uint32_t)*p;
			 p += sizeof(uint32_t);
			 partsMaterial[i] = (const char*)p;
			 p += nameLength + 1; // null terminated
		 }

		 const size_t indicesSize = header->indexSize;
		 //
		 BufferDesc IndBuffDesc;
		 IndBuffDesc.Name = "Cube index buffer";
		 IndBuffDesc.Usage = USAGE_IMMUTABLE;
		 IndBuffDesc.BindFlags = BIND_INDEX_BUFFER;
		 IndBuffDesc.Size = indicesSize;
		 BufferData IBData;
		 IBData.pData = indices;
		 IBData.DataSize = indicesSize;
         m_pDevice->CreateBuffer(IndBuffDesc, &IBData, &m_CubeIndexBuffer);
		 //
         const size_t verticesSize = header->vertexSize;
		 BufferDesc VertBuffDesc;
		 VertBuffDesc.Name = "Cube vertex buffer";
		 VertBuffDesc.Usage = USAGE_IMMUTABLE;
		 VertBuffDesc.BindFlags = BIND_VERTEX_BUFFER;
		 VertBuffDesc.Size = verticesSize;
		 BufferData VBData;
		 VBData.pData = vertexData;
		 VBData.DataSize = verticesSize;
         m_pDevice->CreateBuffer(VertBuffDesc, &VBData, &m_CubeVertexBuffer);
         free(data);
         close(fd);
	 }

     void CreateResources()
     {
         //InitFilament();
		 CreatePipelineState();
		 CreateVertexBuffer();
		 CreateIndexBuffer();
		 LoadTexture();
		 return;
         // Pipeline state object encompasses configuration of all GPU stages
 
         GraphicsPipelineStateCreateInfo PSOCreateInfo;
 
         // Pipeline state name is used by the engine to report issues.
         // It is always a good idea to give objects descriptive names.
         PSOCreateInfo.PSODesc.Name = "Simple triangle PSO";
 
         // This is a graphics pipeline
         PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
 
         // clang-format off
         // This tutorial will render to a single render target
         PSOCreateInfo.GraphicsPipeline.NumRenderTargets             = 1;
         // Set render target format which is the format of the swap chain's color buffer
         PSOCreateInfo.GraphicsPipeline.RTVFormats[0]                = m_pSwapChain->GetDesc().ColorBufferFormat;
         // Use the depth buffer format from the swap chain
         PSOCreateInfo.GraphicsPipeline.DSVFormat                    = m_pSwapChain->GetDesc().DepthBufferFormat;
         // Primitive topology defines what kind of primitives will be rendered by this pipeline state
         PSOCreateInfo.GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
         // No back face culling for this tutorial
         PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_NONE;
         // Disable depth testing
         PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = False;
         // clang-format on
 
         ShaderCreateInfo ShaderCI;
         // Tell the system that the shader source code is in HLSL.
         // For OpenGL, the engine will convert this into GLSL under the hood
         ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
         // OpenGL backend requires emulated combined HLSL texture samplers (g_Texture + g_Texture_sampler combination)
         ShaderCI.Desc.UseCombinedTextureSamplers = true;
         // Create a vertex shader
         RefCntAutoPtr<IShader> pVS;
         {
             ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
             ShaderCI.EntryPoint      = "main";
             ShaderCI.Desc.Name       = "Triangle vertex shader";
             ShaderCI.Source          = VSSource;
             m_pDevice->CreateShader(ShaderCI, &pVS);
         }
 
         // Create a pixel shader
         RefCntAutoPtr<IShader> pPS;
         {
             ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
             ShaderCI.EntryPoint      = "main";
             ShaderCI.Desc.Name       = "Triangle pixel shader";
             ShaderCI.Source          = PSSource;
             m_pDevice->CreateShader(ShaderCI, &pPS);
         }
 
         // Finally, create the pipeline state
         PSOCreateInfo.pVS = pVS;
         PSOCreateInfo.pPS = pPS;
         m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pPSO);
     }
 
	 void Render()
	 {

		 IDeviceContext* pCtx = m_pImmediateContext;// GetImmediateContext();
		 pCtx->ClearStats();

		 ITextureView* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
		 ITextureView* pDSV = m_pSwapChain->GetDepthBufferDSV();
		 pCtx->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

// 		 ITextureView* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
// 		 ITextureView* pDSV = m_pSwapChain->GetDepthBufferDSV();
		 // Clear the back buffer
		 float4 ClearColor = { 0.350f, 0.350f, 0.350f, 1.0f };
// 		 if (m_ConvertPSOutputToGamma)
// 		 {
// 			 // If manual gamma correction is required, we need to clear the render target with sRGB color
// 			 ClearColor = LinearToSRGB(ClearColor);
// 		 }
		 m_pImmediateContext->ClearRenderTarget(pRTV, ClearColor.Data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
		 m_pImmediateContext->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

		 {
			 // Map the buffer and write current world-view-projection matrix
			 MapHelper<float4x4> CBConstants(m_pImmediateContext, m_VSConstants, MAP_WRITE, MAP_FLAG_DISCARD);
			 *CBConstants = m_WorldViewProjMatrix;
		 }

		 // Bind vertex and index buffers
		 const Uint64 offset = 0;
		 IBuffer* pBuffs[] = { m_CubeVertexBuffer };
		 m_pImmediateContext->SetVertexBuffers(0, 1, pBuffs, &offset, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
		 m_pImmediateContext->SetIndexBuffer(m_CubeIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

		 // Set the pipeline state
		 m_pImmediateContext->SetPipelineState(m_pPSO);
		 // Commit shader resources. RESOURCE_STATE_TRANSITION_MODE_TRANSITION mode
		 // makes sure that resources are transitioned to required states.
		 m_pImmediateContext->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

		 DrawIndexedAttribs DrawAttrs;     // This is an indexed draw call
		 DrawAttrs.IndexType = VT_UINT32; // Index type
		 DrawAttrs.NumIndices = 36;
		 // Verify the state of vertex and index buffers
		 DrawAttrs.Flags = DRAW_FLAG_VERIFY_ALL;
		 m_pImmediateContext->DrawIndexed(DrawAttrs);

		 pCtx->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
	 }
	 float4x4 GetSurfacePretransformMatrix(const float3& f3CameraViewAxis) const
	 {
		 const auto& SCDesc = m_pSwapChain->GetDesc();
		 switch (SCDesc.PreTransform)
		 {
		 case SURFACE_TRANSFORM_ROTATE_90:
			 // The image content is rotated 90 degrees clockwise.
			 return float4x4::RotationArbitrary(f3CameraViewAxis, -PI_F / 2.f);

		 case SURFACE_TRANSFORM_ROTATE_180:
			 // The image content is rotated 180 degrees clockwise.
			 return float4x4::RotationArbitrary(f3CameraViewAxis, -PI_F);

		 case SURFACE_TRANSFORM_ROTATE_270:
			 // The image content is rotated 270 degrees clockwise.
			 return float4x4::RotationArbitrary(f3CameraViewAxis, -PI_F * 3.f / 2.f);

		 case SURFACE_TRANSFORM_OPTIMAL:
			 UNEXPECTED("SURFACE_TRANSFORM_OPTIMAL is only valid as parameter during swap chain initialization.");
			 return float4x4::Identity();

		 case SURFACE_TRANSFORM_HORIZONTAL_MIRROR:
		 case SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90:
		 case SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180:
		 case SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270:
			 UNEXPECTED("Mirror transforms are not supported");
			 return float4x4::Identity();

		 default:
			 return float4x4::Identity();
		 }
	 }
	 float4x4 GetAdjustedProjectionMatrix(float FOV, float NearPlane, float FarPlane) const
	 {
		 const auto& SCDesc = m_pSwapChain->GetDesc();

		 float AspectRatio = static_cast<float>(SCDesc.Width) / static_cast<float>(SCDesc.Height);
		 float XScale, YScale;
		 if (SCDesc.PreTransform == SURFACE_TRANSFORM_ROTATE_90 ||
			 SCDesc.PreTransform == SURFACE_TRANSFORM_ROTATE_270 ||
			 SCDesc.PreTransform == SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90 ||
			 SCDesc.PreTransform == SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270)
		 {
			 // When the screen is rotated, vertical FOV becomes horizontal FOV
			 XScale = 1.f / std::tan(FOV / 2.f);
			 // Aspect ratio is inversed
			 YScale = XScale * AspectRatio;
		 }
		 else
		 {
			 YScale = 1.f / std::tan(FOV / 2.f);
			 XScale = YScale / AspectRatio;
		 }

		 float4x4 Proj;
		 Proj._11 = XScale;
		 Proj._22 = YScale;
		 Proj.SetNearFarClipPlanes(NearPlane, FarPlane, m_pDevice->GetDeviceInfo().NDC.MinZ == -1);
		 return Proj;
	 }
	 void Update(double CurrTime, double ElapsedTime, bool DoUpdateUI)
	 {
		 //SampleBase::Update(CurrTime, ElapsedTime, DoUpdateUI);

		 // Apply rotation
		 float4x4 CubeModelTransform = float4x4::RotationY(static_cast<float>(CurrTime) * 1.0f) * float4x4::RotationX(-PI_F * 0.1f);

		 // Camera is at (0, 0, -5) looking along the Z axis
		 float4x4 View = float4x4::Translation(0.f, 0.0f, 5.0f);

		 // Get pretransform matrix that rotates the scene according the surface orientation
		 float4x4 SrfPreTransform = GetSurfacePretransformMatrix(float3{ 0, 0, 1 });

		 // Get projection matrix adjusted to the current screen orientation
		 float4x4 Proj = GetAdjustedProjectionMatrix(PI_F / 4.0f, 0.1f, 100.f);

		 // Compute world-view-projection matrix
		 m_WorldViewProjMatrix = CubeModelTransform * View * SrfPreTransform * Proj;
	 }

     void Render0()
     {
         // Set render targets before issuing any draw command.
         // Note that Present() unbinds the back buffer if it is set as render target.
         ITextureView* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
         ITextureView* pDSV = m_pSwapChain->GetDepthBufferDSV();
         m_pImmediateContext->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
 
         // Clear the back buffer
         const float ClearColor[] = {0.350f, 0.350f, 0.350f, 1.0f};
         // Let the engine perform required state transitions
         m_pImmediateContext->ClearRenderTarget(pRTV, ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
         m_pImmediateContext->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
 
         // Set the pipeline state in the immediate context
         m_pImmediateContext->SetPipelineState(m_pPSO);
 
         // Typically we should now call CommitShaderResources(), however shaders in this example don't
         // use any resources.
 
         DrawAttribs drawAttrs;
         drawAttrs.NumVertices = 3; // Render 3 vertices
         m_pImmediateContext->Draw(drawAttrs);
     }
 
     void Present()
     {
         m_pSwapChain->Present();
     }
 
     void WindowResize(Uint32 Width, Uint32 Height)
     {
         if (m_pSwapChain)
             m_pSwapChain->Resize(Width, Height);
     }
 
     RENDER_DEVICE_TYPE GetDeviceType() const { return m_DeviceType; }
 
 private:
     RefCntAutoPtr<IRenderDevice>  m_pDevice;
     RefCntAutoPtr<IDeviceContext> m_pImmediateContext;
     RefCntAutoPtr<ISwapChain>     m_pSwapChain;
     RefCntAutoPtr<IPipelineState> m_pPSO;
     RENDER_DEVICE_TYPE            m_DeviceType = RENDER_DEVICE_TYPE_D3D11;
	 //
	 RefCntAutoPtr<IBuffer>                m_CubeVertexBuffer;
	 RefCntAutoPtr<IBuffer>                m_CubeIndexBuffer;
	 RefCntAutoPtr<IBuffer>                m_VSConstants;
	 RefCntAutoPtr<ITextureView>           m_TextureSRV;
	 RefCntAutoPtr<IShaderResourceBinding> m_SRB;
	 float4x4                              m_WorldViewProjMatrix;
	 bool m_ConvertPSOutputToGamma = false;
 };
 
 std::unique_ptr<Tutorial00App> g_pTheApp;
 
 class Timer
 {
 public:
	 Timer();
	 void   Restart();
	 double GetElapsedTime() const;
	 float  GetElapsedTimef() const;

 private:
	 std::chrono::high_resolution_clock::time_point m_StartTime;
 };

 using namespace std::chrono;
 Timer::Timer()
 {
	 Restart();
 }

 void Timer::Restart()
 {
	 m_StartTime = high_resolution_clock().now();
 }

 template <typename T>
 T GetElapsedTimeT(high_resolution_clock::time_point StartTime)
 {
	 auto CurrTime = high_resolution_clock::now();
	 auto time_span = duration_cast<duration<T>>(CurrTime - StartTime);
	 return time_span.count();
 }

 double Timer::GetElapsedTime() const
 {
	 return GetElapsedTimeT<double>(m_StartTime);
 }

 LRESULT CALLBACK MessageProc(HWND, UINT, WPARAM, LPARAM);
 // Main
 int WINAPI WinMain(_In_ HINSTANCE     hInstance,
                    _In_opt_ HINSTANCE hPrevInstance,
                    _In_ LPSTR         lpCmdLine,
                    _In_ int           nShowCmd)
 {
 #if defined(_DEBUG) || defined(DEBUG)
     _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
 #endif
 
     g_pTheApp.reset(new Tutorial00App);
 
     LPSTR cmdLine = GetCommandLineA();
     if (!g_pTheApp->ProcessCommandLine(cmdLine))
         return -1;
 
     std::wstring Title(L"Tutorial00: Hello Win32");
     switch (g_pTheApp->GetDeviceType())
     {
         case RENDER_DEVICE_TYPE_D3D11: Title.append(L" (D3D11)"); break;
         case RENDER_DEVICE_TYPE_D3D12: Title.append(L" (D3D12)"); break;
         case RENDER_DEVICE_TYPE_GL: Title.append(L" (GL)"); break;
         case RENDER_DEVICE_TYPE_VULKAN: Title.append(L" (VK)"); break;
     }
     // Register our window class
     WNDCLASSEX wcex = {sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, MessageProc,
                        0L, 0L, hInstance, NULL, NULL, NULL, NULL, L"SampleApp", NULL};
     RegisterClassEx(&wcex);
 
     // Create a window
     LONG WindowWidth  = 1280;
     LONG WindowHeight = 1024;
     RECT rc           = {0, 0, WindowWidth, WindowHeight};
     AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
     HWND wnd = CreateWindow(L"SampleApp", Title.c_str(),
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, hInstance, NULL);
     if (!wnd)
     {
         MessageBox(NULL, L"Cannot create window", L"Error", MB_OK | MB_ICONERROR);
         return 0;
     }
     ShowWindow(wnd, nShowCmd);
     UpdateWindow(wnd);
 
     if (!g_pTheApp->InitializeDiligentEngine(wnd))
         return -1;
 
     g_pTheApp->CreateResources();

	 Timer timer;

	 double PrevTime = timer.GetElapsedTime();
     // Main message loop
     MSG msg = {0};
     while (WM_QUIT != msg.message)
     {
         if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
         {
             TranslateMessage(&msg);
             DispatchMessage(&msg);
         }
         else
         {
			 double CurrTime = timer.GetElapsedTime();
			 double ElapsedTime = CurrTime - PrevTime;
			 PrevTime = CurrTime;
			 g_pTheApp->Update(CurrTime, 0.0, false);
             g_pTheApp->Render();
             g_pTheApp->Present();
         }
     }
 
     g_pTheApp.reset();
 
     return (int)msg.wParam;
 }
 
 // Called every time the NativeNativeAppBase receives a message
 LRESULT CALLBACK MessageProc(HWND wnd, UINT message, WPARAM wParam, LPARAM lParam)
 {
     switch (message)
     {
         case WM_PAINT:
         {
             PAINTSTRUCT ps;
             BeginPaint(wnd, &ps);
             EndPaint(wnd, &ps);
             return 0;
         }
         case WM_SIZE: // Window size has been changed
             if (g_pTheApp)
             {
                 g_pTheApp->WindowResize(LOWORD(lParam), HIWORD(lParam));
             }
             return 0;
 
         case WM_CHAR:
             if (wParam == VK_ESCAPE)
                 PostQuitMessage(0);
             return 0;
 
         case WM_DESTROY:
             PostQuitMessage(0);
             return 0;
 
         case WM_GETMINMAXINFO:
         {
             LPMINMAXINFO lpMMI = (LPMINMAXINFO)lParam;
 
             lpMMI->ptMinTrackSize.x = 320;
             lpMMI->ptMinTrackSize.y = 240;
             return 0;
         }
 
         default:
             return DefWindowProc(wnd, message, wParam, lParam);
     }
 }