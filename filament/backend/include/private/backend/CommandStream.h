/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef TNT_FILAMENT_BACKEND_PRIVATE_COMMANDSTREAM_H
#define TNT_FILAMENT_BACKEND_PRIVATE_COMMANDSTREAM_H

#include "private/backend/CircularBuffer.h"
//#include "private/backend/Dispatcher.h"
#include "private/backend/Driver.h"

#include <backend/BufferDescriptor.h>
#include <backend/BufferObjectStreamDescriptor.h>
#include <backend/CallbackHandler.h>
#include <backend/DriverEnums.h>
#include <backend/Handle.h>
#include <backend/PipelineState.h>
#include <backend/Program.h>
#include <backend/PixelBufferDescriptor.h>
// #include <backend/PresentCallable.h>
// #include <backend/TargetBufferInfo.h>

#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/ThreadUtils.h>
#include <math/mathfwd.h>

#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

#ifndef NDEBUG
#include <thread>
#endif

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

// Set to true to print every command out on log.d. This requires RTTI and DEBUG
#define DEBUG_COMMAND_STREAM false

extern void DiligentCreateProgram(filament::backend::Program&& program);
namespace filament::backend {

class CommandStream {
    template<typename T>
    struct AutoExecute {
        T closure;
        inline explicit AutoExecute(T&& closure) : closure(std::forward<T>(closure)) {}
        inline ~AutoExecute() { closure(); }
    };

public:
    CommandStream(Driver& driver, CircularBuffer& buffer) noexcept;

    CommandStream(CommandStream const& rhs) noexcept = delete;
    CommandStream& operator=(CommandStream const& rhs) noexcept = delete;

    CircularBuffer const& getCircularBuffer() const noexcept { return mCurrentBuffer; }

	bool isWorkaroundNeeded(Workaround) { return false; }
	FeatureLevel getFeatureLevel() { return FeatureLevel::FEATURE_LEVEL_3; }
	math::float2 getClipSpaceParams() { return math::float2{ 1.0f, 0.0f }; }
	uint8_t getMaxDrawBuffers() {
        return 16;// MRT::MAX_SUPPORTED_RENDER_TARGET_COUNT;
	}
	size_t getMaxUniformBufferSize() { return 65536/*16384u*/; }
	size_t getMaxTextureSize(SamplerType target) {
		// NoopDriver is being actively used for other purposes.  This needs to be resolved before we
		// can change it to 2048. b/406832484
		return 16384u;
	}
	size_t getMaxArrayTextureLayers() { return 256u; }
	void updateIndexBuffer(Handle<HwIndexBuffer> ibh, BufferDescriptor&& p, uint32_t byteOffset) {}
	void updateBufferObject(Handle<HwBufferObject> ibh, BufferDescriptor&& p, uint32_t byteOffset) {}
	void destroyBufferObject(Handle<HwBufferObject> boh) {}
	void destroyTexture(Handle<HwTexture> th) {}
	void destroyProgram(Handle<HwProgram> ph) {}
    backend::ProgramHandle createProgram(backend::Program&& program) { DiligentCreateProgram(std::move(program)); return {}; }
    backend::BufferObjectHandle createBufferObject(
        uint32_t byteCount,
        backend::BufferObjectBinding bindingType,
        backend::BufferUsage usage) { return {}; }
    void setDebugTag(backend::HandleBase::HandleId handleId, utils::CString tag) {}
	bool isStereoSupported() { return false; }
	bool isParallelShaderCompileSupported() { return false; }
	void compilePrograms(CompilerPriorityQueue priority, CallbackHandler* handler, CallbackHandler::Callback callback, void* user) {}
    void registerBufferObjectStreams(Handle<HwBufferObject> boh, BufferObjectStreamDescriptor&& streams) {}

    void updateDescriptorSetBuffer(
        backend::DescriptorSetHandle dsh,
        backend::descriptor_binding_t binding,
        backend::BufferObjectHandle boh,
        uint32_t offset,
        uint32_t size
    ) {}
    void updateDescriptorSetTexture(
        backend::DescriptorSetHandle dsh,
        backend::descriptor_binding_t binding,
        backend::TextureHandle th,
        SamplerParams params
    ) {}
	backend::DescriptorSetHandle createDescriptorSet(backend::DescriptorSetLayoutHandle dslh) { return {}; }
    void destroyDescriptorSet(backend::DescriptorSetHandle dsh) {}
	void bindDescriptorSet(
		backend::DescriptorSetHandle dsh,
		backend::descriptor_set_t set,
		backend::DescriptorSetOffsetArray&& offsets) {}
	void destroyVertexBuffer(Handle<HwVertexBuffer> vbh) {}
	void destroyIndexBuffer(Handle<HwIndexBuffer> ibh) {}
    backend::IndexBufferHandle createIndexBuffer(
        backend::ElementType elementType,
        uint32_t indexCount,
        backend::BufferUsage usage) { return {}; }
    backend::VertexBufferHandle createVertexBuffer(
        uint32_t vertexCount,
        backend::VertexBufferInfoHandle vbih) { return {}; }
    void setVertexBufferObject(
        backend::VertexBufferHandle vbh,
        uint32_t index,
        backend::BufferObjectHandle bufferObject) {}

public:
    // This is for debugging only. Currently, CircularBuffer can only be written from a
    // single thread. In debug builds we assert this condition.
    // Call this first in the render loop.
    inline void debugThreading() noexcept {
#ifndef NDEBUG
        mThreadId = utils::ThreadUtils::getThreadId();
#endif
    }

    void execute(void* buffer);

    /*
     * queueCommand() allows to queue a lambda function as a command.
     * This is much less efficient than using the Driver* API.
     */
    void queueCommand(std::function<void()> command);

    /*
     * Allocates memory associated to the current CommandStreamBuffer.
     * This memory will be automatically freed after this command buffer is processed.
     * IMPORTANT: Destructors ARE NOT called
     */
    inline void* allocate(size_t size, size_t alignment = 8) noexcept;

    /*
     * Helper to allocate an array of trivially destructible objects
     */
    template<typename PodType,
            typename = typename std::enable_if<std::is_trivially_destructible<PodType>::value>::type>
    inline PodType* allocatePod(
            size_t count = 1, size_t alignment = alignof(PodType)) noexcept;

private:
    inline void* allocateCommand(size_t size) {
        assert_invariant(utils::ThreadUtils::isThisThread(mThreadId));
        return mCurrentBuffer.allocate(size);
    }

    // We use a copy of Dispatcher (instead of a pointer) because this removes one dereference
    // when executing driver commands.
    Driver& UTILS_RESTRICT mDriver;
    CircularBuffer& UTILS_RESTRICT mCurrentBuffer;
    //Dispatcher mDispatcher;

#ifndef NDEBUG
    // just for debugging...
    std::thread::id mThreadId{};
#endif

    bool mUsePerformanceCounter = false;
};

void* CommandStream::allocate(size_t size, size_t alignment) noexcept {
    // make sure alignment is a power of two
    assert_invariant(alignment && !(alignment & alignment-1));

    return (char*)allocateCommand(size);
}

template<typename PodType, typename>
PodType* CommandStream::allocatePod(size_t count, size_t alignment) noexcept {
    return static_cast<PodType*>(allocate(count * sizeof(PodType), alignment));
}

} // namespace filament::backend

#endif // TNT_FILAMENT_BACKEND_PRIVATE_COMMANDSTREAM_H
