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
#include <filament/Options.h>
#include <details/Engine.h>
#include "details/Material.h"
#include "details/MaterialInstance.h"
#include "ds/ColorPassDescriptorSet.h"
#include "ds/TypedUniformBuffer.h"
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
 
static const char* VSSource =
R"(
cbuffer Constants
{
    float4x4 g_WorldViewProj;
};

// Vertex shader takes two inputs: vertex position and color.
// By convention, Diligent Engine expects vertex shader inputs to be 
// labeled 'ATTRIBn', where n is the attribute number.
struct VSInput
{
    float3 Pos   : ATTRIB0;
    float4 Color : ATTRIB1;
};

struct PSInput 
{
    float4 Pos   : SV_POSITION;
    float4 Color : COLOR0; 
};

// Note that if separate shader objects are not supported (this is only the case for old GLES3.0 devices), vertex
// shader output variable name must match exactly the name of the pixel shader input variable.
// If the variable has structure type (like in this example), the structure declarations must also be identical.
void main(in  VSInput VSIn,
          out PSInput PSIn)
{
    PSIn.Pos   = mul(float4(VSIn.Pos, 1.0), g_WorldViewProj);
    PSIn.Color = VSIn.Color;
}
)";
// 	 R"(
// cbuffer Constants
// {
//     float4x4 g_WorldViewProj;
// };
// 
// // Vertex shader takes two inputs: vertex position and uv coordinates.
// // By convention, Diligent Engine expects vertex shader inputs to be 
// // labeled 'ATTRIBn', where n is the attribute number.
// struct VSInput
// {
//     float3 Pos : ATTRIB0;
//     float2 UV  : ATTRIB1;
// };
// 
// struct PSInput 
// { 
//     float4 Pos : SV_POSITION; 
//     float2 UV  : TEX_COORD; 
// };
// 
// // Note that if separate shader objects are not supported (this is only the case for old GLES3.0 devices), vertex
// // shader output variable name must match exactly the name of the pixel shader input variable.
// // If the variable has structure type (like in this example), the structure declarations must also be identical.
// void main(in  VSInput VSIn,
//           out PSInput PSIn) 
// {
//     PSIn.Pos = mul( float4(VSIn.Pos,1.0), g_WorldViewProj);
//     PSIn.UV  = VSIn.UV;
// }
//  )";
 
 // Pixel shader simply outputs interpolated vertex color
 static const char* PSSource =
	 R"(
struct PSInput
{
    float4 Pos   : SV_POSITION;
    float4 Color : COLOR0;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

// Note that if separate shader objects are not supported (this is only the case for old GLES3.0 devices), vertex
// shader output variable name must match exactly the name of the pixel shader input variable.
// If the variable has structure type (like in this example), the structure declarations must also be identical.
void main(in  PSInput  PSIn,
          out PSOutput PSOut)
{
    float4 Color = PSIn.Color;
#if CONVERT_PS_OUTPUT_TO_GAMMA
    // Use fast approximation for gamma correction.
    Color.rgb = pow(Color.rgb, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
#endif
    PSOut.Color = Color;
}
)";
// 	 R"(
// Texture2D    g_Texture;
// SamplerState g_Texture_sampler; // By convention, texture samplers must use the '_sampler' suffix
// 
// struct PSInput
// {
//     float4 Pos : SV_POSITION;
//     float2 UV  : TEX_COORD;
// };
// 
// struct PSOutput
// {
//     float4 Color : SV_TARGET;
// };
// 
// void main(in  PSInput  PSIn,
//           out PSOutput PSOut)
// {
//     float4 Color = g_Texture.Sample(g_Texture_sampler, PSIn.UV);
// #if CONVERT_PS_OUTPUT_TO_GAMMA
//     // Use fast approximation for gamma correction.
//     Color.rgb = pow(Color.rgb, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
// #endif
//     PSOut.Color = Color;
// }
//  )";

 extern filament::FEngine* g_FilamentEngine;
 extern filament::ColorPassDescriptorSet* g_mColorPassDescriptorSet;
 namespace filament {

	 extern FScene g_scene;
	 CameraInfo computeCameraInfo(FEngine& engine);
	 void prepareLighting(filament::FEngine& engine, filament::CameraInfo const& cameraInfo);
	 const filament::PerRenderableData* getPerRenderableData();
 }

 class Tutorial00App
 {
 public:
     Tutorial00App(filament::FEngine& engine)
		 :
		 mEngine(engine),
		 mUniforms(engine.getDriverApi()),
		 mColorPassDescriptorSet(engine, mUniforms)
     {
		 g_mColorPassDescriptorSet = &mColorPassDescriptorSet;
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

	 void CreateVertexBuffer()
	 {
		 // Layout of this structure matches the one we defined in the pipeline state
		 struct Vertex
		 {
			 float3 pos;
			 float4 color;
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

		 constexpr Vertex CubeVerts[8] =
		 {
			 {float3{-1, -1, -1}, float4{1, 0, 0, 1}},
			 {float3{-1, +1, -1}, float4{0, 1, 0, 1}},
			 {float3{+1, +1, -1}, float4{0, 0, 1, 1}},
			 {float3{+1, -1, -1}, float4{1, 1, 1, 1}},

			 {float3{-1, -1, +1}, float4{1, 1, 0, 1}},
			 {float3{-1, +1, +1}, float4{0, 1, 1, 1}},
			 {float3{+1, +1, +1}, float4{1, 0, 1, 1}},
			 {float3{+1, -1, +1}, float4{0.2f, 0.2f, 0.2f, 1.f}},
		 };

		 // Create a vertex buffer that stores cube vertices
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
			 2,0,1, 2,3,0,
			 4,6,5, 4,7,6,
			 0,7,4, 0,3,7,
			 1,0,4, 1,4,5,
			 1,5,2, 5,6,2,
			 3,6,7, 3,2,6
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
		 int fd = open("D:\\filament-1.59.4\\samples\\materials\\aiDefaultMat.filamat", O_RDONLY);
		 size_t size = fileSize(fd);
		 char* data = (char*)malloc(size);
		 read(fd, data, size);

		 auto material = Material::Builder().package(data, size).build(mEngine);
		 auto mi = m_MaterialInstance = material->createInstance();
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
		 VertexBuffer::AttributeType uvtype = (header->flags & filamesh::TEXCOORD_SNORM16) ?
			 VertexBuffer::AttributeType::SHORT2 : VertexBuffer::AttributeType::HALF2;
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
		 
		 Variant variant;
		 variant.setDirectionalLighting(true/*view.hasDirectionalLighting()*/);
		 variant.setDynamicLighting(false/*view.hasDynamicLighting()*/);
		 variant.setFog(false/*view.hasFog()*/);
		 variant.setVsm(false/*view.hasShadowing() && view.getShadowType() != ShadowType::PCF*/);
		 variant.setStereo(false/*view.hasStereo()*/);

		 m_filament_ready = true;
		 downcast(mi)->getMaterial()->prepareProgram(variant);

		 //
		 filament::CameraInfo cameraInfo = computeCameraInfo(mEngine);
		 mColorPassDescriptorSet.prepareCamera(mEngine, cameraInfo);

		 // prepareShadowing
		 
		 // PerRenderableUib
// 		 mRenderableUbh = driver.createBufferObject(mRenderableUBOSize + sizeof(PerRenderableUib), BufferObjectBinding::UNIFORM, BufferUsage::DYNAMIC);
// 		 scene->updateUBOs(merged, mRenderableUbh);
// 		 g_scene.prepare();
// 		 g_scene.prepareVisibleRenderables();
		 //
		 prepareLighting(mEngine, cameraInfo);
		 //
		 mColorPassDescriptorSet.prepareTime(mEngine, math::float4{0.0f, 0.0f, 0.0f, 0.0f}/*userTime*/);
		 FogOptions mFogOptions;
		 math::mat4 fogTransform;
		 mColorPassDescriptorSet.prepareFog(mEngine, cameraInfo, fogTransform, mFogOptions, mEngine.getDefaultIndirectLight()/*scene->getIndirectLight()*/);
		 TemporalAntiAliasingOptions mTemporalAntiAliasingOptions;
		 mColorPassDescriptorSet.prepareTemporalNoise(mEngine, mTemporalAntiAliasingOptions);
		 mColorPassDescriptorSet.prepareBlending(false/*needsAlphaChannel*/);
		 std::array<math::float4, 4> mMaterialGlobals = { {
															{ 0, 0, 0, 1 },
															{ 0, 0, 0, 1 },
															{ 0, 0, 0, 1 },
															{ 0, 0, 0, 1 },
													} };
		 mColorPassDescriptorSet.prepareMaterialGlobals(mMaterialGlobals);
		 //prepareUpscaler
		 math::float2 scale{1.0f, 1.0f};
		 TemporalAntiAliasingOptions taaOptions;
		 DynamicResolutionOptions dsrOptions;
		 float bias = 0.0f;
		 math::float2 derivativesScale{ 1.0f };
		 if (dsrOptions.enabled && dsrOptions.quality >= QualityLevel::HIGH) {
			 bias = std::log2(std::min(scale.x, scale.y));
		 }
		 if (taaOptions.enabled) {
			 bias += taaOptions.lodBias;
			 if (taaOptions.upscaling) {
				 derivativesScale = 0.5f;
			 }
		 }
		 mColorPassDescriptorSet.prepareLodBias(bias, derivativesScale);
	 }
	 // split shader source code in three:
// - the version line
// - extensions
// - everything else
	 /* static */ std::array<std::string_view, 3> splitShaderSource(std::string_view source) noexcept {
		 auto version_start = source.find("#version");
		 assert_invariant(version_start != std::string_view::npos);

		 auto version_eol = source.find('\n', version_start) + 1;
		 assert_invariant(version_eol != std::string_view::npos);

		 auto prolog_start = version_eol;
		 auto prolog_eol = source.rfind("\n#extension");// last #extension line
		 if (prolog_eol == std::string_view::npos) {
			 prolog_eol = prolog_start;
		 }
		 else {
			 prolog_eol = source.find('\n', prolog_eol + 1) + 1;
		 }
		 auto body_start = prolog_eol;

		 std::string_view const version = source.substr(version_start, version_eol - version_start);
		 std::string_view const prolog = source.substr(prolog_start, prolog_eol - prolog_start);
		 std::string_view const body = source.substr(body_start, source.length() - body_start);
		 return { version, prolog, body };
	 }

	 static inline std::string to_string(bool b) noexcept { return b ? "true" : "false"; }
	 static inline std::string to_string(int i) noexcept { return std::to_string(i); }
	 static inline std::string to_string(float f) noexcept { return "float(" + std::to_string(f) + ")"; }
	 void CreateFilamentProgram(filament::backend::Program&& program)
	 {
		 if (!m_filament_ready) {
			 return;
		 }
		 using namespace filament::backend;
		 // opengl
		 Program::ShaderSource shadersSource = std::move(program.getShadersSource());
		 utils::FixedCapacityVector<Program::SpecializationConstant> const& specializationConstants = program.getSpecializationConstants();
		 bool multiview = false;

		 auto appendSpecConstantString = +[](std::string& s, Program::SpecializationConstant const& sc) {
			 s += "#define SPIRV_CROSS_CONSTANT_ID_" + std::to_string(sc.id) + ' ';
			 s += std::visit([](auto&& arg) { return to_string(arg); }, sc.value);
			 s += '\n';
			 return s;
			 };

		 std::string specializationConstantString;
		 int32_t numViews = 2;
		 for (auto const& sc : specializationConstants) {
			 appendSpecConstantString(specializationConstantString, sc);
			 if (sc.id == 8) {
				 // This constant must match
				 // ReservedSpecializationConstants::CONFIG_STEREO_EYE_COUNT
				 // which we can't use here because it's defined in EngineEnums.h.
				 // (we're breaking layering here, but it's for the good cause).
				 numViews = std::get<int32_t>(sc.value);
			 }
		 }
		 if (!specializationConstantString.empty()) {
			 specializationConstantString += '\n';
		 }

		 // build all shaders
		for (size_t i = 0; i < Program::SHADER_TYPE_COUNT; i++) {
			const ShaderStage stage = static_cast<ShaderStage>(i);
// 				 GLenum glShaderType{};
// 				 switch (stage) {
// 				 case ShaderStage::VERTEX:
// 					 glShaderType = GL_VERTEX_SHADER;
// 					 break;
// 				 case ShaderStage::FRAGMENT:
// 					 glShaderType = GL_FRAGMENT_SHADER;
// 					 break;
// 				 case ShaderStage::COMPUTE:
// #if defined(BACKEND_OPENGL_LEVEL_GLES31)
// 					 glShaderType = GL_COMPUTE_SHADER;
// #else
// 					 continue;
// #endif
// 					 break;
// 				 }

			if (!shadersSource[i].empty()) {
				Program::ShaderBlob& shader = shadersSource[i];
				char* shader_src = reinterpret_cast<char*>(shader.data());
				size_t shader_len = shader.size();

				// remove GOOGLE_cpp_style_line_directive
				//process_GOOGLE_cpp_style_line_directive(context, shader_src, shader_len);

				// replace the value of layout(num_views = X) for multiview extension
// 					 if (multiview && stage == ShaderStage::VERTEX) {
// 						 process_OVR_multiview2(context, numViews, shader_src, shader_len);
// 					 }

				// add support for ARB_shading_language_packing if needed
				//auto const packingFunctions = process_ARB_shading_language_packing(context);
				std::string_view packingFunctions;
				// split shader source, so we can insert the specialization constants and the packing
				// functions
				auto [version, prolog, body] = splitShaderSource({ shader_src, shader_len });

				// enable ESSL 3.10 if available
// 					 if (context.isAtLeastGLES<3, 1>()) {
// 						 version = "#version 310 es\n";
// 					 }

				std::array<std::string_view, 5> sources = {
					version,
					prolog,
					specializationConstantString,
					packingFunctions,
					{ body.data(), body.size() - 1 }  // null-terminated
				};

				// Some of the sources may be zero-length. Remove them as to avoid passing lengths of
				// zero to glShaderSource(). glShaderSource should work with lengths of zero, but some
				// drivers instead interpret zero as a sentinel for a null-terminated string.
				auto partitionPoint = std::stable_partition(
					sources.begin(), sources.end(), [](std::string_view s) { return !s.empty(); });
				size_t count = std::distance(sources.begin(), partitionPoint);

				std::array<const char*, 5> shaderStrings;
				std::array<int, 5> lengths;
				for (size_t i = 0; i < count; i++) {
					shaderStrings[i] = sources[i].data();
					lengths[i] = sources[i].size();
				}
				std::string* outstring;
				FILE* fd = nullptr;
				//std::ofstream file("D:\\Github\\kfengine-tech\\aiDefaultMat.vert", std::ios::out | std::ios::trunc);
				if (stage == ShaderStage::VERTEX) {
					fd = fopen("D:\\Github\\kfengine-tech\\aiDefaultMat.vert", "w+");
					outstring = &mVSSource;
				}
				else if (stage == ShaderStage::FRAGMENT) {
					fd = fopen("D:\\Github\\kfengine-tech\\aiDefaultMat.frag", "w+");
					outstring = &mPSSource;
				}
				if (fd) {
					for (auto& it : sources) {
						if (!it.empty()) {
							fwrite(it.data(), it.size(), 1, fd);
							outstring->append(it.data(), it.size());
						}
					}
					fclose(fd);
				}
			}
		}
		 // vulkan
// 		 constexpr uint8_t const MAX_SHADER_MODULES = 2;
// 		 Program::ShaderSource const& blobs = builder.getShadersSource();
// 		 //auto& modules = mInfo->shaders;
// 		 auto const& specializationConstants = builder.getSpecializationConstants();
// 		 std::vector<uint32_t> shader;
// 
// 		 static_assert(static_cast<ShaderStage>(0) == ShaderStage::VERTEX &&
// 			 static_cast<ShaderStage>(1) == ShaderStage::FRAGMENT &&
// 			 MAX_SHADER_MODULES == 2);
// 
// 		 for (size_t i = 0; i < MAX_SHADER_MODULES; i++) {
// 			 Program::ShaderBlob const& blob = blobs[i];
// 
// 			 uint32_t* data = (uint32_t*)blob.data();
// 			 size_t dataSize = blob.size();
// 
// 			 if (!specializationConstants.empty()) {
// 				 //fvkutils::workaroundSpecConstant(blob, specializationConstants, shader);
// 				 data = (uint32_t*)shader.data();
// 				 dataSize = shader.size() * 4;
// 			 }
// 		 }
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
		 ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;// SHADER_SOURCE_LANGUAGE_HLSL;

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

		 // In this tutorial, we will load shaders from file. To be able to do that,
		 // we need to create a shader source stream factory
// 		 RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
// 		 m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);
// 		 ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;
		 // Create a vertex shader
		 RefCntAutoPtr<IShader> pVS;
		 {
			 ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
			 ShaderCI.EntryPoint = "main";
			 ShaderCI.Desc.Name = "Cube VS";
			 //ShaderCI.FilePath = "cube.vsh";
			 ShaderCI.Source = mVSSource.data();// VSSource;//
			 ShaderCI.SourceLength = mVSSource.length();
			 m_pDevice->CreateShader(ShaderCI, &pVS);
			 // Create dynamic uniform buffer that will store our transformation matrix
			 // Dynamic buffers can be frequently updated by the CPU
			 BufferDesc perRenderableDesc;
			 perRenderableDesc.Name = "ObjectUniforms";
			 perRenderableDesc.Size = sizeof(filament::PerRenderableData);// sizeof(PerRenderableUib);
			 perRenderableDesc.Usage = USAGE_DYNAMIC;
			 perRenderableDesc.BindFlags = BIND_UNIFORM_BUFFER;
			 perRenderableDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
			 m_pDevice->CreateBuffer(perRenderableDesc, nullptr, &m_PerRenderableConstants);
			 

			 BufferDesc perViewDesc;
			 perViewDesc.Name = "FrameUniforms";
			 perViewDesc.Size = sizeof(filament::PerViewUib);
			 perViewDesc.Usage = USAGE_DYNAMIC;
			 perViewDesc.BindFlags = BIND_UNIFORM_BUFFER;
			 perViewDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
			 m_pDevice->CreateBuffer(perViewDesc, nullptr, &m_PerViewConstants);
		 }

		 // Create a pixel shader
		 RefCntAutoPtr<IShader> pPS;
		 {
			 ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
			 ShaderCI.EntryPoint = "main";
			 ShaderCI.Desc.Name = "Cube PS";
			 //ShaderCI.FilePath = "cube.psh";
			 ShaderCI.Source = mPSSource.data();// PSSource;//
			 ShaderCI.SourceLength = mPSSource.length();
			 m_pDevice->CreateShader(ShaderCI, &pPS);

			 BufferDesc lightDesc;
			 lightDesc.Name = "LightsUniforms";
			 lightDesc.Size = filament::CONFIG_MAX_LIGHT_COUNT * sizeof(filament::LightsUib);
			 lightDesc.Usage = USAGE_DYNAMIC;
			 lightDesc.BindFlags = BIND_UNIFORM_BUFFER;
			 lightDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
			 m_pDevice->CreateBuffer(lightDesc, nullptr, &m_PSLightConstants);

			 auto& uniformBuffer = downcast(m_MaterialInstance)->getUniformBuffer();
			 BufferDesc materialDesc;
			 materialDesc.Name = "MaterialUniforms";
			 materialDesc.Size = uniformBuffer.getSize();
			 materialDesc.Usage = USAGE_DYNAMIC;
			 materialDesc.BindFlags = BIND_UNIFORM_BUFFER;
			 materialDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
			 m_pDevice->CreateBuffer(materialDesc, nullptr, &m_PSMaterialParam);
		 }

		 // clang-format off
		 // Define vertex shader input layout
// 		 LayoutElement LayoutElems[] =
// 		 {
// 			 // Attribute 0 - vertex position
// 			 LayoutElement{0, 0, 3, VT_FLOAT32, False},
// 			 // Attribute 1 - vertex color
// 			 LayoutElement{1, 0, 4, VT_FLOAT32, False}
// 		 };

		 LayoutElement LayoutElems[] =
		 {
			 // Attribute 0 - vertex position
			 LayoutElement{0, 0, 4, VT_FLOAT16, False, 0, 8},
			 // Attribute 1 - vertex tangent
			 LayoutElement{1, 1, 4, VT_INT16, True, 142280, 8},
			 // Attribute 2 - vertex color
			 LayoutElement{2, 2, 4, VT_UINT8, True, 284560, 4},
			 // Attribute 3 - vertex uv
			 LayoutElement{3, 3, 2, VT_INT16, True, 355700, 4}
		 };
		 // clang-format on
		 PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = LayoutElems;
		 PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = _countof(LayoutElems);

		 PSOCreateInfo.pVS = pVS;
		 PSOCreateInfo.pPS = pPS;

		 // Define variable type that will be used by default
		 PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

		 m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pPSO);

		 auto pSRV = m_pPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "ObjectUniforms");
		 pSRV->Set(m_PerRenderableConstants);
		 pSRV = m_pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "ObjectUniforms");
		 pSRV->Set(m_PerRenderableConstants);
		 pSRV = m_pPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "FrameUniforms");
		 pSRV->Set(m_PerViewConstants);
		 pSRV = m_pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "FrameUniforms");
		 pSRV->Set(m_PerViewConstants);
		 pSRV = m_pPSO->GetStaticVariableByName(SHADER_TYPE_PIXEL, "MaterialParams");
		 pSRV->Set(m_PSMaterialParam);
		 //pSRV->Set(m_VSPerRenderableConstants);
		 // Since we did not explicitly specify the type for 'Constants' variable, default
		 // type (SHADER_RESOURCE_VARIABLE_TYPE_STATIC) will be used. Static variables never
		 // change and are bound directly through the pipeline state object.
		 //m_pPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")->Set(m_VSConstants);

		 // Create a shader resource binding object and bind all static resources in it
		 m_pPSO->CreateShaderResourceBinding(&m_SRB, true);
	 }
	 void UpdateUniform()
	 {
		 MapHelper<Uint8> perRenderable(m_pImmediateContext, m_PerRenderableConstants, MAP_WRITE, MAP_FLAG_DISCARD);
		 const int count = 1;
		 filament::PerRenderableData const* const renderableData = filament::getPerRenderableData();
		 memcpy((void*)perRenderable, renderableData, count * sizeof(filament::PerRenderableData));

		 auto bufferDescriptor = mUniforms.toBufferDescriptor(mEngine.getDriverApi());
		 MapHelper<Uint8> perView(m_pImmediateContext, m_PerViewConstants, MAP_WRITE, MAP_FLAG_DISCARD);
		 memcpy((void*)perView, bufferDescriptor.buffer, bufferDescriptor.size);

		 MapHelper<Uint8> materialParam(m_pImmediateContext, m_PSMaterialParam, MAP_WRITE, MAP_FLAG_DISCARD);
		 auto& uniformBuffer = downcast(m_MaterialInstance)->getUniformBuffer();
		 memcpy((void*)materialParam, uniformBuffer.getBuffer(), uniformBuffer.getSize());
	 }
     void CreateResources()
     {
         //InitFilament();
		 //CreatePipelineState();
// 		 CreateVertexBuffer();
// 		 CreateIndexBuffer();
		 InitFilament();
		 //LoadTexture();

		 CreatePipelineState();
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
// 			 MapHelper<float4x4> CBConstants(m_pImmediateContext, m_VSConstants, MAP_WRITE, MAP_FLAG_DISCARD);
// 			 *CBConstants = m_WorldViewProjMatrix;

			 UpdateUniform();
		 }

		 // Bind vertex and index buffers
		 const Uint64 offsets[] = {0, 0, 0, 0};
		 IBuffer* pBuffs[] = { m_CubeVertexBuffer, m_CubeVertexBuffer, m_CubeVertexBuffer, m_CubeVertexBuffer };
		 m_pImmediateContext->SetVertexBuffers(0, _countof(pBuffs), pBuffs, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
		 m_pImmediateContext->SetIndexBuffer(m_CubeIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

		 // Set the pipeline state
		 m_pImmediateContext->SetPipelineState(m_pPSO);
		 // Commit shader resources. RESOURCE_STATE_TRANSITION_MODE_TRANSITION mode
		 // makes sure that resources are transitioned to required states.
		 m_pImmediateContext->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

		 DrawIndexedAttribs DrawAttrs;     // This is an indexed draw call
// 		 DrawAttrs.IndexType = VT_UINT32; // Index type
// 		 DrawAttrs.NumIndices = 36;
		 DrawAttrs.IndexType = VT_UINT16; // Index type
		 DrawAttrs.NumIndices = 47232;
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
	 RefCntAutoPtr<IBuffer>                m_PerRenderableConstants;
	 RefCntAutoPtr<IBuffer>                m_PerViewConstants;
	 RefCntAutoPtr<IBuffer>                m_PSLightConstants;
	 RefCntAutoPtr<IBuffer>                m_PSMaterialParam;
	 filament::MaterialInstance* m_MaterialInstance{ nullptr };
	 RefCntAutoPtr<ITextureView>           m_TextureSRV;
	 RefCntAutoPtr<IShaderResourceBinding> m_SRB;
	 float4x4                              m_WorldViewProjMatrix;
	 bool m_ConvertPSOutputToGamma = false;
	 bool m_filament_ready = false;
	 mutable filament::TypedUniformBuffer<filament::PerViewUib> mUniforms;
	 mutable filament::ColorPassDescriptorSet mColorPassDescriptorSet;
	 filament::FEngine& mEngine;
	 std::string mVSSource;
	 std::string mPSSource;
 };
 
 std::unique_ptr<Tutorial00App> g_pTheApp;

 void DiligentCreateProgram(filament::backend::Program&& program)
 {
	 if (g_pTheApp) {
		 g_pTheApp->CreateFilamentProgram(std::move(program));
	 }
 }

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
	 AllocConsole();
	 freopen("CONOUT$", "w", stdout);
	 freopen("CONIN$", "r", stdin);
	 g_FilamentEngine = (filament::FEngine*)filament::FEngine::create();
	 
     g_pTheApp.reset(new Tutorial00App(*g_FilamentEngine));
 
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

	 FreeConsole();
     
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