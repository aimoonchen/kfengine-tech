/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <filament/FilamentAPI.h>
#include <details/Engine.h>
#include <details/VertexBuffer.h>
#include <details/Material.h>
#include <details/IndirectLight.h>
#include "details/Skybox.h"
#include <components/LightManager.h>
#include <backend/DriverEnums.h>
#include <private/backend/Driver.h>
#include <algorithm>

#include <fcntl.h>
#if !defined(WIN32)
#    include <unistd.h>
#else
#    include <io.h>
#endif

namespace filament {

void builderMakeName(utils::CString& outName, const char* name, size_t const len) noexcept {
    if (!name) {
        return;
    }
    size_t const length = std::min(len, size_t { 128u });
    outName = utils::CString(name, length);
}
const Engine::Config& Engine::getConfig() const noexcept {
	return downcast(this)->getConfig();
}

backend::FeatureLevel Engine::getActiveFeatureLevel() const noexcept {
	return downcast(this)->getActiveFeatureLevel();
}

HwVertexBufferInfoFactory::HwVertexBufferInfoFactory() {}
HwVertexBufferInfoFactory::~HwVertexBufferInfoFactory() noexcept {}
HwDescriptorSetLayoutFactory::HwDescriptorSetLayoutFactory() {}
HwDescriptorSetLayoutFactory::~HwDescriptorSetLayoutFactory() noexcept {}

static size_t fileSize(int fd) {
	size_t filesize;
	filesize = (size_t)lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	return filesize;
}

FEngine::FEngine()
	: mLightManager(*this)
	, mCameraManager(*this)
{
	int fd = open("D:\\filament-1.59.4\\samples\\materials\\aiDefaultMat.filamat", O_RDONLY);
	size_t size = fileSize(fd);
	char* data = (char*)malloc(size);
	read(fd, data, size);

	FMaterial::DefaultMaterialBuilder defaultMaterialBuilder;
	switch (mConfig.stereoscopicType) {
	case StereoscopicType::NONE:
	case StereoscopicType::INSTANCED:
		defaultMaterialBuilder.package(
			data, size
			//MATERIALS_DEFAULTMATERIAL_DATA, MATERIALS_DEFAULTMATERIAL_SIZE
		);
		break;
	case StereoscopicType::MULTIVIEW:
#ifdef FILAMENT_ENABLE_MULTIVIEW
		defaultMaterialBuilder.package(
			MATERIALS_DEFAULTMATERIAL_MULTIVIEW_DATA, MATERIALS_DEFAULTMATERIAL_MULTIVIEW_SIZE);
#else
		assert_invariant(false);
#endif
		break;
	}
	mDefaultMaterial = downcast(defaultMaterialBuilder.build(*this));

	constexpr float sh[9 * 3] = { 0.0f };
	mDefaultIbl = downcast(IndirectLight::Builder()
		.irradiance(3, reinterpret_cast<const math::float3*>(sh))
		.build(*this));
}
Engine* FEngine::create() {
	FEngine* instance = new FEngine();
	return instance;
}

template<typename T, typename ... ARGS>
T* FEngine::create(ResourceList<T>& list,
	typename T::Builder const& builder, ARGS&& ... args) noexcept {
	T* p = mHeapAllocator.make<T>(*this, builder, std::forward<ARGS>(args)...);
	if (UTILS_LIKELY(p)) {
		list.insert(p);
	}
	return p;
}

FBufferObject* FEngine::createBufferObject(const BufferObject::Builder& builder) noexcept {
	return create(mBufferObjects, builder);
}

FVertexBuffer* FEngine::createVertexBuffer(const VertexBuffer::Builder& builder) noexcept {
	return create(mVertexBuffers, builder);
}

FIndexBuffer* FEngine::createIndexBuffer(const IndexBuffer::Builder& builder) noexcept {
	return create(mIndexBuffers, builder);
}
FMaterial* FEngine::createMaterial(const Material::Builder& builder,
	std::unique_ptr<MaterialParser> materialParser) noexcept {
	return create(mMaterials, builder, std::move(materialParser));
}

void FEngine::createLight(const LightManager::Builder& builder, utils::Entity const entity) {
	mLightManager.create(builder, entity);
}

FCamera* FEngine::createCamera(utils::Entity const entity) noexcept {
	return mCameraManager.create(*this, entity);
}

FIndirectLight* FEngine::createIndirectLight(const IndirectLight::Builder& builder) noexcept {
	return create(mIndirectLights, builder);
}

FMaterialInstance* FEngine::createMaterialInstance(const FMaterial* material,
	const FMaterialInstance* other, const char* name) noexcept {
	FMaterialInstance* p = mHeapAllocator.make<FMaterialInstance>(*this, other, name);
	if (UTILS_LIKELY(p)) {
		auto const pos = mMaterialInstances.emplace(material, "MaterialInstance");
		pos.first->second.insert(p);
	}
	return p;
}

FMaterialInstance* FEngine::createMaterialInstance(const FMaterial* material,
	const char* name) noexcept {
	FMaterialInstance* p = mHeapAllocator.make<FMaterialInstance>(*this, material, name);
	if (UTILS_LIKELY(p)) {
		auto pos = mMaterialInstances.emplace(material, "MaterialInstance");
		pos.first->second.insert(p);
	}
	return p;
}

FSkybox* FEngine::createSkybox(const Skybox::Builder& builder) noexcept {
	return create(mSkyboxes, builder);
}

size_t backend::Driver::getElementTypeSize(backend::ElementType type) noexcept {
	switch (type) {
	case backend::ElementType::BYTE:     return sizeof(int8_t);
	case backend::ElementType::BYTE2:    return sizeof(math::byte2);
	case backend::ElementType::BYTE3:    return sizeof(math::byte3);
	case backend::ElementType::BYTE4:    return sizeof(math::byte4);
	case backend::ElementType::UBYTE:    return sizeof(uint8_t);
	case backend::ElementType::UBYTE2:   return sizeof(math::ubyte2);
	case backend::ElementType::UBYTE3:   return sizeof(math::ubyte3);
	case backend::ElementType::UBYTE4:   return sizeof(math::ubyte4);
	case backend::ElementType::SHORT:    return sizeof(int16_t);
	case backend::ElementType::SHORT2:   return sizeof(math::short2);
	case backend::ElementType::SHORT3:   return sizeof(math::short3);
	case backend::ElementType::SHORT4:   return sizeof(math::short4);
	case backend::ElementType::USHORT:   return sizeof(uint16_t);
	case backend::ElementType::USHORT2:  return sizeof(math::ushort2);
	case backend::ElementType::USHORT3:  return sizeof(math::ushort3);
	case backend::ElementType::USHORT4:  return sizeof(math::ushort4);
	case backend::ElementType::INT:      return sizeof(int32_t);
	case backend::ElementType::UINT:     return sizeof(uint32_t);
	case backend::ElementType::FLOAT:    return sizeof(float);
	case backend::ElementType::FLOAT2:   return sizeof(math::float2);
	case backend::ElementType::FLOAT3:   return sizeof(math::float3);
	case backend::ElementType::FLOAT4:   return sizeof(math::float4);
	case backend::ElementType::HALF:     return sizeof(math::half);
	case backend::ElementType::HALF2:    return sizeof(math::half2);
	case backend::ElementType::HALF3:    return sizeof(math::half3);
	case backend::ElementType::HALF4:    return sizeof(math::half4);
	}
}
} // namespace filament
