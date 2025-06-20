#include "math/vec2.h"
#include "math/vec3.h"
#include "math/vec4.h"
#include "math/mat3.h"
#include "math/mat4.h"
#include "Culler.h"
#include "components/LightManager.h"
#include "utils/StructureOfArrays.h"

#include "private/filament/UibStructs.h"
#include "details/Camera.h"
#include "details/Engine.h"
#include "details/IndirectLight.h"
#include "ds/ColorPassDescriptorSet.h"
#include "filament/Exposure.h"
#include <camutils/Manipulator.h>
struct Config {
	std::string title;
	std::string iblDirectory;
	std::string dirt;
	float scale = 1.0f;
	bool splitView = false;
	mutable filament::Engine::Backend backend = filament::Engine::Backend::DEFAULT;
	mutable filament::backend::FeatureLevel featureLevel = filament::backend::FeatureLevel::FEATURE_LEVEL_3;
	filament::camutils::Mode cameraMode = filament::camutils::Mode::ORBIT;
	bool resizeable = true;
	bool headless = false;
	int stereoscopicEyeCount = 2;

	// Provided to indicate GPU preference for vulkan
	std::string vulkanGPUHint;
};

filament::FEngine* g_FilamentEngine = nullptr;
filament::ColorPassDescriptorSet* g_mColorPassDescriptorSet = nullptr;
filament::math::mat4f g_ObjectMat;
filament::math::mat4f g_LightMat;
using CameraManipulator = filament::camutils::Manipulator<float>;
class FilamentCamera {
public:
    FilamentCamera(filament::FEngine& engine)
    {
        Config config;
        mConfig = config;
		filament::Engine::Config engineConfig = {};
		engineConfig.stereoscopicEyeCount = config.stereoscopicEyeCount;
#if defined(FILAMENT_SAMPLES_STEREO_TYPE_INSTANCED)
		engineConfig.stereoscopicType = filament::Engine::StereoscopicType::INSTANCED;
#elif defined (FILAMENT_SAMPLES_STEREO_TYPE_MULTIVIEW)
		engineConfig.stereoscopicType = filament::Engine::StereoscopicType::MULTIVIEW;
#else
		engineConfig.stereoscopicType = filament::Engine::StereoscopicType::NONE;
#endif

		// create cameras
// 		utils::EntityManager& em = utils::EntityManager::get();
// 		em.create(3, mCameraEntities);
        mCameras[0] = mMainCamera = engine.createCamera({}/*mCameraEntities[0]*/);
        mCameras[1] = mDebugCamera = engine.createCamera({}/*mCameraEntities[1]*/);
        mCameras[2] = mOrthoCamera = engine.createCamera({}/*mCameraEntities[2]*/);

		// set exposure
		for (auto camera : mCameras) {
			camera->setExposure(16.0f, 1 / 125.0f, 100.0f);
		}

		// create views
// 		mViews.emplace_back(mMainView = new CView(*mRenderer, "Main View"));
// 		if (config.splitView) {
// 			mViews.emplace_back(mDepthView = new CView(*mRenderer, "Depth View"));
// 			mViews.emplace_back(mGodView = new GodView(*mRenderer, "God View"));
// 			mViews.emplace_back(mOrthoView = new CView(*mRenderer, "Shadow View"));
// 		}
// 		mViews.emplace_back(mUiView = new CView(*mRenderer, "UI View"));

		// set-up the camera manipulators
		mMainCameraMan = CameraManipulator::Builder()
			.targetPosition(0, 0, -4)
			.flightMoveDamping(15.0)
			.build(config.cameraMode);
		mDebugCameraMan = CameraManipulator::Builder()
			.targetPosition(0, 0, -4)
			.flightMoveDamping(15.0)
			.build(config.cameraMode);

// 		mMainView->setCamera(mMainCamera);
// 		mMainView->setCameraManipulator(mMainCameraMan);
// 		if (config.splitView) {
// 			// Depth view always uses the main camera
// 			mDepthView->setCamera(mMainCamera);
// 			mDepthView->setCameraManipulator(mMainCameraMan);
// 
// 			// The god view uses the main camera for culling, but the debug camera for viewing
// 			mGodView->setCamera(mMainCamera);
// 			mGodView->setGodCamera(mDebugCamera);
// 			mGodView->setCameraManipulator(mDebugCameraMan);
// 		}

		// configure the cameras
		configureCamerasForWindow();

		mMainCamera->lookAt({ 4, 0, -4 }, { 0, 0, -4 }, { 0, 1, 0 });
    }
	void configureCamerasForWindow() {
        using namespace filament;
        using namespace filament::math;
		float dpiScaleX = 1.0f;
		float dpiScaleY = 1.0f;

		// If the app is not headless, query the window for its physical & virtual sizes.
		if (!mIsHeadless) {
            uint32_t width{ 1280 }, height{ 1024 };
			//SDL_GL_GetDrawableSize(mWindow, (int*)&width, (int*)&height);
			mWidth = (size_t)width;
			mHeight = (size_t)height;

			int virtualWidth{ 1280 }, virtualHeight{ 1024 };
			//SDL_GetWindowSize(mWindow, &virtualWidth, &virtualHeight);
			dpiScaleX = (float)width / virtualWidth;
			dpiScaleY = (float)height / virtualHeight;
		}

		const uint32_t width = mWidth;
		const uint32_t height = mHeight;

		const float3 at(0, 0, -4);
		const double ratio = double(height) / double(width);
		const int sidebar = /*mFilamentApp->*/mSidebarWidth * dpiScaleX;

		//const bool splitview = mViews.size() > 2;

		const uint32_t mainWidth = std::max(2, (int)width - sidebar);

		double near = /*mFilamentApp->*/mCameraNear;
		double far = /*mFilamentApp->*/mCameraFar;
		if (false/*mMainView->getView()->getStereoscopicOptions().enabled*/) {
			mat4 projections[4];
			projections[0] = Camera::projection(/*mFilamentApp->*/mCameraFocalLength, 1.0, near, far);
			projections[1] = projections[0];
			// simulate foveated rendering
			projections[2] = Camera::projection(/*mFilamentApp->*/mCameraFocalLength * 2.0, 1.0, near, far);
			projections[3] = projections[2];
			mMainCamera->setCustomEyeProjection(projections, 4, projections[0], near, far);
		}
		else {
			mMainCamera->setLensProjection(/*mFilamentApp->*/mCameraFocalLength, 1.0, near, far);
		}
		mDebugCamera->setProjection(45.0, double(mainWidth) / height, 0.0625, 4096, Camera::Fov::VERTICAL);

		auto aspectRatio = double(mainWidth) / height;
		if (false/*mMainView->getView()->getStereoscopicOptions().enabled*/) {
			const int ec = mConfig.stereoscopicEyeCount;
			aspectRatio = double(mainWidth) / ec / height;
		}
		mMainCamera->setScaling({ 1.0 / aspectRatio, 1.0 });

		// We're in split view when there are more views than just the Main and UI views.
// 		if (splitview) {
// 			uint32_t const vpw = mainWidth / 2;
// 			uint32_t const vph = height / 2;
// 			mMainView->setViewport({ sidebar + 0,            0, vpw, vph });
// 			mDepthView->setViewport({ sidebar + int32_t(vpw),            0, vpw, vph });
// 			mGodView->setViewport({ sidebar + int32_t(vpw), int32_t(vph), vpw, vph });
// 			mOrthoView->setViewport({ sidebar + 0, int32_t(vph), vpw, vph });
// 		}
// 		else {
// 			mMainView->setViewport({ sidebar, 0, mainWidth, height });
// 		}
// 		mUiView->setViewport({ 0, 0, width, height });
	}
    //
    int mSidebarWidth = 0;
	float mCameraFocalLength = 28.0f;
	float mCameraNear = 0.1f;
	float mCameraFar = 100.0f;
    bool mIsHeadless = false;
	size_t mWidth = 0;
	size_t mHeight = 0;
    Config mConfig;
	CameraManipulator* mMainCameraMan;
	CameraManipulator* mDebugCameraMan;

	utils::Entity mCameraEntities[3];
	filament::Camera* mCameras[3] = { nullptr };
	filament::Camera* mMainCamera;
	filament::Camera* mDebugCamera;
	filament::Camera* mOrthoCamera;
};

namespace filament {
    class FMorphTargetBuffer {
    public:
		backend::TextureHandle mPbHandle;
		backend::TextureHandle mTbHandle;
		uint32_t mVertexCount;
		uint32_t mCount;
    };
    class FInstanceBuffer {
    public:
		utils::FixedCapacityVector<math::mat4f> mLocalTransforms;
		utils::CString mName;
		size_t mInstanceCount;
    };
    class FRenderPrimitive {
    public:
        AttributeBitset mEnabledAttributes = {};
		uint16_t mBlendOrder = 0;
		bool mGlobalBlendOrderEnabled = false;
		backend::PrimitiveType mPrimitiveType = backend::PrimitiveType::TRIANGLES;
    };
    class FRenderableManager {
    public:
        using Instance = uint32_t;
        using PrimitiveType = backend::PrimitiveType;
        class Builder
        {
        public:
            /**
			 * Type of geometry for a Renderable
			 */
            enum class GeometryType : uint8_t {
                DYNAMIC,        //!< dynamic gemoetry has no restriction
                STATIC_BOUNDS,  //!< bounds and world space transform are immutable
                STATIC          //!< skinning/morphing not allowed and Vertex/IndexBuffer immutables
            };
        };
		using GeometryType = Builder::GeometryType;

		// TODO: consider renaming, this pertains to material variants, not strictly visibility.
		struct Visibility {
			uint8_t priority : 3;
			uint8_t channel : 2;
			bool castShadows : 1;
			bool receiveShadows : 1;
			bool culling : 1;

			bool skinning : 1;
			bool morphing : 1;
			bool screenSpaceContactShadows : 1;
			bool reversedWindingOrder : 1;
			bool fog : 1;
			GeometryType geometryType : 2;
		};
		struct SkinningBindingInfo {
			backend::Handle<backend::HwBufferObject> handle;
			uint32_t offset;
			backend::Handle<backend::HwTexture> boneIndicesAndWeightHandle;
		};

// 		inline SkinningBindingInfo getSkinningBufferInfo(Instance instance) const noexcept;
// 		inline uint32_t getBoneCount(Instance instance) const noexcept;

		struct MorphingBindingInfo {
			backend::Handle<backend::HwBufferObject> handle;
			uint32_t count;
			FMorphTargetBuffer const* morphTargetBuffer;
		};
//		inline MorphingBindingInfo getMorphingBufferInfo(Instance instance) const noexcept;

		struct InstancesInfo {
			union {
				FInstanceBuffer* buffer;
				uint64_t padding;          // ensures the pointer is 64 bits on all archs
			};
			backend::Handle<backend::HwBufferObject> handle;
			uint16_t count;
			char padding0[2];
		};
// 		static_assert(sizeof(InstancesInfo) == 16);
// 		inline InstancesInfo getInstancesInfo(Instance instance) const noexcept;
    };
	class FScene {
    public:
		static constexpr size_t DIRECTIONAL_LIGHTS_COUNT = 1;

		using VisibleMaskType = Culler::result_type;

		enum {
			RENDERABLE_INSTANCE,    //   4 | instance of the Renderable component
			WORLD_TRANSFORM,        //  16 | instance of the Transform component
			VISIBILITY_STATE,       //   2 | visibility data of the component
			SKINNING_BUFFER,        //   8 | bones uniform buffer handle, offset, indices and weights
			MORPHING_BUFFER,        //  16 | weights uniform buffer handle, count, morph targets
			INSTANCES,              //  16 | instancing info for this Renderable
			WORLD_AABB_CENTER,      //  12 | world-space bounding box center of the renderable
			VISIBLE_MASK,           //   2 | each bit represents a visibility in a pass
			CHANNELS,               //   1 | currently light channels only

			// These are not needed anymore after culling
			LAYERS,                 //   1 | layers
			WORLD_AABB_EXTENT,      //  12 | world-space bounding box half-extent of the renderable

			// These are temporaries and should be stored out of line
			PRIMITIVES,             //   8 | level-of-detail'ed primitives
			SUMMED_PRIMITIVE_COUNT, //   4 | summed visible primitive counts
			UBO,                    // 128 |
			DESCRIPTOR_SET_HANDLE,

			// FIXME: We need a better way to handle this
			USER_DATA,              //   4 | user data currently used to store the scale
		};
        using RenderableSoa = utils::StructureOfArrays<
            utils::EntityInstance<RenderableManager>,   // RENDERABLE_INSTANCE
            math::mat4f,                                // WORLD_TRANSFORM
            FRenderableManager::Visibility,             // VISIBILITY_STATE
            FRenderableManager::SkinningBindingInfo,    // SKINNING_BUFFER
            FRenderableManager::MorphingBindingInfo,    // MORPHING_BUFFER
            FRenderableManager::InstancesInfo,          // INSTANCES
            math::float3,                               // WORLD_AABB_CENTER
            VisibleMaskType,                            // VISIBLE_MASK
            uint8_t,                                    // CHANNELS
            uint8_t,                                    // LAYERS
            math::float3,                               // WORLD_AABB_EXTENT
            utils::Slice<FRenderPrimitive>,             // PRIMITIVES
            uint32_t,                                   // SUMMED_PRIMITIVE_COUNT
            PerRenderableData,                          // UBO
            backend::DescriptorSetHandle,               // DESCRIPTOR_SET_HANDLE
            // FIXME: We need a better way to handle this
            float                                       // USER_DATA
        >;

//         RenderableSoa const& getRenderableData() const noexcept { return mRenderableData; }
//         RenderableSoa& getRenderableData() noexcept { return mRenderableData; }
// 
//         static inline uint32_t getPrimitiveCount(RenderableSoa const& soa,
//             uint32_t const first, uint32_t const last) noexcept {
//             // the caller must guarantee that last is dereferenceable
//             return soa.elementAt<SUMMED_PRIMITIVE_COUNT>(last) -
//                 soa.elementAt<SUMMED_PRIMITIVE_COUNT>(first);
//         }
// 
//         static inline uint32_t getPrimitiveCount(RenderableSoa const& soa, uint32_t const last) noexcept {
//             // the caller must guarantee that last is dereferenceable
//             return soa.elementAt<SUMMED_PRIMITIVE_COUNT>(last);
//         }

		struct ShadowInfo {
			// These are per-light values.
			// They're packed into 32 bits and stored in the Lights uniform buffer.
			// They're unpacked in the fragment shader and used to calculate punctual shadows.
			bool castsShadows = false;      // whether this light casts shadows
			bool contactShadows = false;    // whether this light casts contact shadows
			uint8_t index = 0;              // an index into the arrays in the Shadows uniform buffer
		};

		enum {
			POSITION_RADIUS,
			DIRECTION,
			SHADOW_DIRECTION,
			SHADOW_REF,
			LIGHT_INSTANCE,
			VISIBILITY,
			SCREEN_SPACE_Z_RANGE,
			SHADOW_INFO
		};

		using LightSoa = utils::StructureOfArrays<
			math::float4,
			math::float3,
			math::float3,
			math::double2,
			FLightManager::Instance,
			Culler::result_type,
			math::float2,
			ShadowInfo
		>;

        void prepare(/*JobSystem& js,*/
            /*RootArenaScope& rootArenaScope,*/
            math::mat4 const& worldTransform,
            bool shadowReceiversAreCasters) noexcept {
            using namespace math;
            // TODO: can we skip this in most cases? Since we rely on indices staying the same,
            //       we could only skip, if nothing changed in the RCM.

//             SYSTRACE_CALL();
// 
//             SYSTRACE_CONTEXT();
// 
//             // This will reset the allocator upon exiting
//             ArenaScope<RootArenaScope::Arena> localArenaScope(rootArenaScope.getArena());
// 
//             FEngine& engine = mEngine;
//             EntityManager const& em = engine.getEntityManager();
//             FRenderableManager const& rcm = engine.getRenderableManager();
//             FTransformManager const& tcm = engine.getTransformManager();
//             FLightManager const& lcm = engine.getLightManager();
//             // go through the list of entities, and gather the data of those that are renderables
            auto& sceneData = mRenderableData;
            auto& lightData = mLightData;
//             auto const& entities = mEntities;
// 
//             using RenderableContainerData = std::pair<RenderableManager::Instance, TransformManager::Instance>;
//             using RenderableInstanceContainer = FixedCapacityVector<RenderableContainerData,
//                 STLAllocator< RenderableContainerData, LinearAllocatorArena >, false>;
// 
//             using LightContainerData = std::pair<LightManager::Instance, TransformManager::Instance>;
//             using LightInstanceContainer = FixedCapacityVector<LightContainerData,
//                 STLAllocator< LightContainerData, LinearAllocatorArena >, false>;
// 
//             RenderableInstanceContainer renderableInstances{
//                     RenderableInstanceContainer::with_capacity(entities.size(), localArenaScope.getArena()) };
// 
//             LightInstanceContainer lightInstances{
//                     LightInstanceContainer::with_capacity(entities.size(), localArenaScope.getArena()) };
// 
//             SYSTRACE_NAME_BEGIN("InstanceLoop");
// 
//             // find the max intensity directional light index in our local array
//             float maxIntensity = 0.0f;
//             std::pair<LightManager::Instance, TransformManager::Instance> directionalLightInstances{};
// 
//             /*
//              * First compute the exact number of renderables and lights in the scene.
//              * Also find the main directional light.
//              */
// 
//             for (Entity const e : entities) {
//                 if (UTILS_LIKELY(em.isAlive(e))) {
//                     auto ti = tcm.getInstance(e);
//                     auto li = lcm.getInstance(e);
//                     auto ri = rcm.getInstance(e);
//                     if (li) {
//                         // we handle the directional light here because it'd prevent multithreading below
//                         if (UTILS_UNLIKELY(lcm.isDirectionalLight(li))) {
//                             // we don't store the directional lights, because we only have a single one
//                             if (lcm.getIntensity(li) >= maxIntensity) {
//                                 maxIntensity = lcm.getIntensity(li);
//                                 directionalLightInstances = { li, ti };
//                             }
//                         }
//                         else {
//                             lightInstances.emplace_back(li, ti);
//                         }
//                     }
//                     if (ri) {
//                         renderableInstances.emplace_back(ri, ti);
//                     }
//                 }
//             }
// 
//             SYSTRACE_NAME_END();

            /*
             * Evaluate the capacity needed for the renderable and light SoAs
             */
            auto entities_size = 1;
             // we need the capacity to be multiple of 16 for SIMD loops
             // we need 1 extra entry at the end for the summed primitive count
            size_t renderableDataCapacity = entities_size;// entities.size();
            renderableDataCapacity = (renderableDataCapacity + 0xFu) & ~0xFu;
            renderableDataCapacity = renderableDataCapacity + 1;

            // The light data list will always contain at least one entry for the
            // dominating directional light, even if there are no entities.
            // we need the capacity to be multiple of 16 for SIMD loops
            size_t lightDataCapacity = std::max<size_t>(DIRECTIONAL_LIGHTS_COUNT, entities_size/*entities.size()*/);
            lightDataCapacity = (lightDataCapacity + 0xFu) & ~0xFu;

            /*
             * Now resize the SoAs if needed
             */

             // TODO: the resize below could happen in a job
            auto renderableInstances_size = 1;
            auto lightInstances_size = 1;
            if (!sceneData.capacity() || sceneData.size() != renderableInstances_size/*renderableInstances.size()*/) {
                sceneData.clear();
                if (sceneData.capacity() < renderableDataCapacity) {
                    sceneData.setCapacity(renderableDataCapacity);
                }
                assert_invariant(renderableInstances_size/*renderableInstances.size()*/ <= sceneData.capacity());
                sceneData.resize(renderableInstances_size/*renderableInstances.size()*/);
            }

            if (lightData.size() != lightInstances_size/*lightInstances.size()*/ + DIRECTIONAL_LIGHTS_COUNT) {
                lightData.clear();
                if (lightData.capacity() < lightDataCapacity) {
                    lightData.setCapacity(lightDataCapacity);
                }
                assert_invariant(lightInstances_size/*lightInstances.size()*/ + DIRECTIONAL_LIGHTS_COUNT <= lightData.capacity());
                lightData.resize(lightInstances_size/*lightInstances.size()*/ + DIRECTIONAL_LIGHTS_COUNT);
            }

            /*
             * Fill the SoA with the JobSystem
             */

//             auto renderableWork = [first = renderableInstances.data(), &rcm, &tcm, &worldTransform,
//                 &sceneData, shadowReceiversAreCasters](auto* p, auto c) {
//                 SYSTRACE_NAME("renderableWork");
                for (size_t i = 0; i < 1; i++) {
                    //auto [ri, ti] = p[i];

                    // this is where we go from double to float for our transforms
                    const mat4f shaderWorldTransform{
                            worldTransform * g_ObjectMat/*tcm.getWorldTransformAccurate(ti)*/ };
                    const bool reversedWindingOrder = det(shaderWorldTransform.upperLeft()) < 0;

                    // compute the world AABB so we can perform culling
                    //const Box worldAABB = rigidTransform(rcm.getAABB(ri), shaderWorldTransform);

                    auto visibility = FRenderableManager::Visibility{};// rcm.getVisibility(ri);
                    visibility.reversedWindingOrder = reversedWindingOrder;
                    if (shadowReceiversAreCasters && visibility.receiveShadows) {
                        visibility.castShadows = true;
                    }

                    // FIXME: We compute and store the local scale because it's needed for glTF but
                    //        we need a better way to handle this
                    const mat4f& transform = g_ObjectMat;// tcm.getTransform(ti);
                    float const scale = (length(transform[0].xyz) + length(transform[1].xyz) +
                        length(transform[2].xyz)) / 3.0f;

                    size_t const index = /*std::distance(first, p) + */i;
                    assert_invariant(index < sceneData.size());

                    sceneData.elementAt<RENDERABLE_INSTANCE>(index) = {};// ri;
                    sceneData.elementAt<WORLD_TRANSFORM>(index) = shaderWorldTransform;
                    sceneData.elementAt<VISIBILITY_STATE>(index) = visibility;
                    sceneData.elementAt<SKINNING_BUFFER>(index) = {};// rcm.getSkinningBufferInfo(ri);
                    sceneData.elementAt<MORPHING_BUFFER>(index) = {};// rcm.getMorphingBufferInfo(ri);
                    sceneData.elementAt<INSTANCES>(index) = {};// rcm.getInstancesInfo(ri);
                    sceneData.elementAt<WORLD_AABB_CENTER>(index) = {};// worldAABB.center;
                    sceneData.elementAt<VISIBLE_MASK>(index) = 0;
                    sceneData.elementAt<CHANNELS>(index) = {};// rcm.getChannels(ri);
                    sceneData.elementAt<LAYERS>(index) = {};// rcm.getLayerMask(ri);
                    sceneData.elementAt<WORLD_AABB_EXTENT>(index) = {1.0f, 1.0f, 1.0f};// worldAABB.halfExtent;
                    //sceneData.elementAt<PRIMITIVES>(index)          = {}; // already initialized, Slice<>
                    sceneData.elementAt<SUMMED_PRIMITIVE_COUNT>(index) = 0;
                    //sceneData.elementAt<UBO>(index)                 = {}; // not needed here
                    sceneData.elementAt<USER_DATA>(index) = scale;
                }
                //};

//             auto lightWork = [first = lightInstances.data(), &lcm, &tcm, &worldTransform,
//                 &lightData](auto* p, auto c) {
//                 SYSTRACE_NAME("lightWork");
                for (size_t i = 0; i < 1; i++) {
                    //auto [li, ti] = p[i];
                    // this is where we go from double to float for our transforms
                    mat4f const shaderWorldTransform{
                            worldTransform * g_LightMat/*tcm.getWorldTransformAccurate(ti)*/ };
                    float4 const position = shaderWorldTransform * float4{ float3{1.0f,1.0f,1.0f}/*lcm.getLocalPosition(li)*/, 1 };
                    float3 d = 0;
                    if (false/*!lcm.isPointLight(li) || lcm.isIESLight(li)*/) {
                        d = float3{ 1.0f,1.0f,1.0f };// lcm.getLocalDirection(li);
                        // using mat3f::getTransformForNormals handles non-uniform scaling
                        d = normalize(mat3f::getTransformForNormals(shaderWorldTransform.upperLeft()) * d);
                    }
                    size_t const index = DIRECTIONAL_LIGHTS_COUNT + /*std::distance(first, p) + */i;
                    assert_invariant(index < lightData.size());
                    lightData.elementAt<POSITION_RADIUS>(index) = float4{ position.xyz, 100.0f/*lcm.getRadius(li)*/ };
                    lightData.elementAt<DIRECTION>(index) = d;
                    lightData.elementAt<LIGHT_INSTANCE>(index) = {};// li;
                }
                //};


//             SYSTRACE_NAME_BEGIN("Renderable and Light jobs");
// 
//             JobSystem::Job* rootJob = js.createJob();
// 
//             auto* renderableJob = parallel_for(js, rootJob,
//                 renderableInstances.data(), renderableInstances.size(),
//                 std::cref(renderableWork), jobs::CountSplitter<64>());
// 
//             auto* lightJob = parallel_for(js, rootJob,
//                 lightInstances.data(), lightInstances.size(),
//                 std::cref(lightWork), jobs::CountSplitter<32, 5>());
// 
//             js.run(renderableJob);
//             js.run(lightJob);

            // Everything below can be done in parallel.

            /*
             * Handle the directional light separately
             */

            //if (auto [li, ti] = directionalLightInstances; li) {
                // in the code below, we only transform directions, so the translation of the
                // world transform is irrelevant, and we don't need to use getWorldTransformAccurate()
                mat4 idmatd;
                mat3 const worldDirectionTransform =
                    mat3::getTransformForNormals(idmatd/*tcm.getWorldTransformAccurate(ti)*/.upperLeft());
                FLightManager::ShadowParams const params = {};// lcm.getShadowParams(li);
                float3 const localDirection = worldDirectionTransform * float3{1.0f, 1.0f, 1.0f}/*lcm.getLocalDirection(li)*/;
                double3 const shadowLocalDirection = params.options.transform * localDirection;

                // using mat3::getTransformForNormals handles non-uniform scaling
                // note: in the common case of the rigid-body transform, getTransformForNormals() returns
                // identity.
                mat3 const worlTransformNormals = mat3::getTransformForNormals(worldTransform.upperLeft());
                double3 const d = worlTransformNormals * localDirection;
                double3 const s = worlTransformNormals * shadowLocalDirection;

                // We compute the reference point for snapping shadowmaps without applying the
                // rotation of `worldOriginTransform` on both sides, so that we don't have any instability
                // due to the limited precision of the "light space" matrix (even at double precision).

                // getMv() Returns the world-to-lightspace transformation. See ShadowMap.cpp.
                auto getMv = [](double3 direction) -> mat3 {
                    // We use the x-axis as the "up" reference so that the math is stable when the light
                    // is pointing down, which is a common case for lights. See ShadowMap.cpp.
                    return transpose(mat3::lookTo(direction, double3{ 1, 0, 0 }));
                    };
                double3 const worldOrigin = transpose(worldTransform.upperLeft()) * worldTransform[3].xyz;
                mat3 const Mv = getMv(shadowLocalDirection);
                double2 const lsReferencePoint = (Mv * worldOrigin).xy;

                constexpr float inf = std::numeric_limits<float>::infinity();
                lightData.elementAt<POSITION_RADIUS>(0) = float4{ 0, 0, 0, inf };
                lightData.elementAt<DIRECTION>(0) = normalize(d);
                lightData.elementAt<SHADOW_DIRECTION>(0) = normalize(s);
                lightData.elementAt<SHADOW_REF>(0) = lsReferencePoint;
                lightData.elementAt<LIGHT_INSTANCE>(0) = {};// li;
//             }
//             else {
//                 lightData.elementAt<LIGHT_INSTANCE>(0) = 0;
//             }

            // some elements past the end of the array will be accessed by SIMD code, we need to make
            // sure the data is valid enough as not to produce errors such as divide-by-zero
            // (e.g. in computeLightRanges())
            for (size_t i = lightData.size(), e = lightData.capacity(); i < e; i++) {
                new(lightData.data<POSITION_RADIUS>() + i) float4{ 0, 0, 0, 1 };
            }

            // Purely for the benefit of MSAN, we can avoid uninitialized reads by zeroing out the
            // unused scene elements between the end of the array and the rounded-up count.
            if (UTILS_HAS_SANITIZE_MEMORY) {
                for (size_t i = sceneData.size(), e = sceneData.capacity(); i < e; i++) {
                    sceneData.data<LAYERS>()[i] = 0;
                    sceneData.data<VISIBLE_MASK>()[i] = 0;
                    sceneData.data<VISIBILITY_STATE>()[i] = {};
                }
            }

//             js.runAndWait(rootJob);
// 
//             SYSTRACE_NAME_END();
        }

		void prepareVisibleRenderables(/*Range<uint32_t> visibleRenderables*/) noexcept {
			//SYSTRACE_CALL();
			RenderableSoa& sceneData = mRenderableData;
			//FRenderableManager const& rcm = mEngine.getRenderableManager();

			//mHasContactShadows = false;
			std::vector<uint32_t> visibleRenderables;
			visibleRenderables.push_back(0);
			for (uint32_t const i : visibleRenderables) {
				PerRenderableData& uboData = sceneData.elementAt<UBO>(i);

				auto const visibility = sceneData.elementAt<VISIBILITY_STATE>(i);
				auto const& model = sceneData.elementAt<WORLD_TRANSFORM>(i);
				auto const ri = sceneData.elementAt<RENDERABLE_INSTANCE>(i);

				// Using mat3f::getTransformForNormals handles non-uniform scaling, but DOESN'T guarantee that
				// the transformed normals will have unit-length, therefore they need to be normalized
				// in the shader (that's already the case anyway, since normalization is needed after
				// interpolation).
				//
				// We pre-scale normals by the inverse of the largest scale factor to avoid
				// large post-transform magnitudes in the shader, especially in the fragment shader, where
				// we use medium precision.
				//
				// Note: if the model matrix is known to be a rigid-transform, we could just use it directly.

				math::mat3f m = math::mat3f::getTransformForNormals(model.upperLeft());
				m = math::prescaleForNormals(m);

				// The shading normal must be flipped for mirror transformations.
				// Basically we're shading the other side of the polygon and therefore need to negate the
				// normal, similar to what we already do to support double-sided lighting.
				if (visibility.reversedWindingOrder) {
					m = -m;
				}

				uboData.worldFromModelMatrix = model;

				uboData.worldFromModelNormalMatrix = m;

				uboData.flagsChannels = PerRenderableData::packFlagsChannels(
					visibility.skinning,
					visibility.morphing,
					visibility.screenSpaceContactShadows,
					sceneData.elementAt<INSTANCES>(i).buffer != nullptr,
					sceneData.elementAt<CHANNELS>(i));

				uboData.morphTargetCount = sceneData.elementAt<MORPHING_BUFFER>(i).count;

				uboData.objectId = i;// rcm.getEntity(ri).getId();

				// TODO: We need to find a better way to provide the scale information per object
				uboData.userData = sceneData.elementAt<USER_DATA>(i);

				//mHasContactShadows = mHasContactShadows || visibility.screenSpaceContactShadows;
			}
		}
		RenderableSoa& getRenderableData() { return mRenderableData; }
		LightSoa& getLightData() { return mLightData; }
	private:
		RenderableSoa mRenderableData;
		LightSoa mLightData;
	};

	FScene g_scene;

	//
    const filament::PerRenderableData* getPerRenderableData() {
        auto& renderableData = g_scene.getRenderableData();
        return renderableData.data<FScene::UBO>();
    }

	void computeLightRanges(
		math::float2* UTILS_RESTRICT const zrange,
		CameraInfo const& UTILS_RESTRICT camera,
		math::float4 const* UTILS_RESTRICT const spheres, size_t count)
	{

		// without this clang seems to assume the src and dst might overlap even if they're
		// restricted.
		// we're guaranteed to have a multiple of 4 lights (at least)
		count = uint32_t(count + 3u) & ~3u;

		for (size_t i = 0; i < count; i++) {
			// this loop gets vectorized x4
			const math::float4 sphere = spheres[i];
			const math::float4 center = camera.view * sphere.xyz; // camera points towards the -z axis
			math::float4 n = center + math::float4{ 0, 0, sphere.w, 0 };
			math::float4 f = center - math::float4{ 0, 0, sphere.w, 0 };
			// project to clip space
			n = camera.projection * n;
			f = camera.projection * f;
			// convert to NDC
			const float min = (n.w > camera.zn) ? (n.z / n.w) : -1.0f;
			const float max = (f.w < camera.zf) ? (f.z / f.w) : 1.0f;
			// convert to screen space
			zrange[i].x = (min + 1.0f) * 0.5f;
			zrange[i].y = (max + 1.0f) * 0.5f;
		}
	}
    void prepareDynamicLights(const CameraInfo& camera/*, Handle<HwBufferObject> lightUbh*/)
    {
        FEngine::DriverApi& driver = g_FilamentEngine->getDriverApi();
        FLightManager const& lcm = g_FilamentEngine->getLightManager();
        FScene::LightSoa& lightData = g_scene.getLightData();

        /*
         * Here we copy our lights data into the GPU buffer.
         */

        size_t const size = lightData.size();
        // number of point-light/spotlights
        size_t const positionalLightCount = size - FScene::DIRECTIONAL_LIGHTS_COUNT;
        assert_invariant(positionalLightCount);

        math::float4 const* const UTILS_RESTRICT spheres = lightData.data<FScene::POSITION_RADIUS>();

        // compute the light ranges (needed when building light trees)
        math::float2* const zrange = lightData.data<FScene::SCREEN_SPACE_Z_RANGE>();
        computeLightRanges(zrange, camera, spheres + FScene::DIRECTIONAL_LIGHTS_COUNT, positionalLightCount);

        LightsUib* const lp = driver.allocatePod<LightsUib>(positionalLightCount);

        auto const* UTILS_RESTRICT directions = lightData.data<FScene::DIRECTION>();
        auto const* UTILS_RESTRICT instances = lightData.data<FScene::LIGHT_INSTANCE>();
        auto const* UTILS_RESTRICT shadowInfo = lightData.data<FScene::SHADOW_INFO>();
        for (size_t i = FScene::DIRECTIONAL_LIGHTS_COUNT, c = size; i < c; ++i) {
            const size_t gpuIndex = i - FScene::DIRECTIONAL_LIGHTS_COUNT;
            auto li = instances[i];
            lp[gpuIndex].positionFalloff = { spheres[i].xyz, lcm.getSquaredFalloffInv(li) };
            lp[gpuIndex].direction = directions[i];
            lp[gpuIndex].reserved1 = {};
            lp[gpuIndex].colorIES = { lcm.getColor(li), 0.0f };
            lp[gpuIndex].spotScaleOffset = lcm.getSpotParams(li).scaleOffset;
            lp[gpuIndex].reserved3 = {};
            lp[gpuIndex].intensity = lcm.getIntensity(li);
            lp[gpuIndex].typeShadow = LightsUib::packTypeShadow(
                lcm.isPointLight(li) ? 0u : 1u,
                shadowInfo[i].contactShadows,
                shadowInfo[i].index);
            lp[gpuIndex].channels = LightsUib::packChannels(
                lcm.getLightChannels(li),
                shadowInfo[i].castsShadows);
        }

        //driver.updateBufferObject(lightUbh, { lp, positionalLightCount * sizeof(LightsUib) }, 0);
    }
    bool hasDynamicLighting() { return false; }
    void prepareLighting(FEngine& engine, CameraInfo const& cameraInfo) noexcept {
        auto& mColorPassDescriptorSet = *g_mColorPassDescriptorSet;
        FScene::LightSoa& lightData = g_scene.getLightData();

        g_scene.prepare(cameraInfo.worldTransform, false);

        g_scene.prepareVisibleRenderables();

        /*
         * Dynamic lights
         */

        if (hasDynamicLighting()) {
            prepareDynamicLights(cameraInfo);
        }

        // here the array of visible lights has been shrunk to CONFIG_MAX_LIGHT_COUNT
        //SYSTRACE_VALUE32("visibleLights", lightData.size() - FScene::DIRECTIONAL_LIGHTS_COUNT);

        /*
         * Exposure
         */

        const float exposure = Exposure::exposure(cameraInfo.ev100);
        mColorPassDescriptorSet.prepareExposure(cameraInfo.ev100);

        /*
         * Indirect light (IBL)
         */

         // If the scene does not have an IBL, use the black 1x1 IBL and honor the fallback intensity
         // associated with the skybox.
        float intensity;
        FIndirectLight const* ibl = nullptr;// scene->getIndirectLight();
//         if (UTILS_LIKELY(ibl)) {
//             intensity = ibl->getIntensity();
//         }
//         else {
            ibl = engine.getDefaultIndirectLight();
            //FSkybox const* const skybox = scene->getSkybox();
            intensity = /*skybox ? skybox->getIntensity() : */FIndirectLight::DEFAULT_INTENSITY;
//        }
        mColorPassDescriptorSet.prepareAmbientLight(engine, *ibl, intensity, exposure);

        /*
         * Directional light (always at index 0)
         */

        FLightManager::Instance const directionalLight = lightData.elementAt<FScene::LIGHT_INSTANCE>(0);
        const math::float3 sceneSpaceDirection = lightData.elementAt<FScene::DIRECTION>(0); // guaranteed normalized
        mColorPassDescriptorSet.prepareDirectionalLight(engine, exposure, sceneSpaceDirection, directionalLight);
    }
    CameraInfo computeCameraInfo(FEngine& engine)
    {
        static FilamentCamera g_FilamentCamera(engine);

		filament::math::float3 eye, center, up;
		g_FilamentCamera.mMainCameraMan->getLookAt(&eye, &center, &up);
		g_FilamentCamera.mMainCamera->lookAt(eye, center, up);
		g_FilamentCamera.mDebugCameraMan->getLookAt(&eye, &center, &up);
		g_FilamentCamera.mDebugCamera->lookAt(eye, center, up);

		auto mViewingCamera = downcast(g_FilamentCamera.mMainCamera);
        auto mCullingCamera = mViewingCamera;
        //FScene const* const scene = getScene();
		using namespace math;

        /*
         * We apply a "world origin" to "everything" in order to implement the IBL rotation.
         * The "world origin" is also used to keep the origin close to the camera position to
         * improve fp precision in the shader for large scenes.
         */
        double3 translation;
        mat3 rotation;

        /*
         * Calculate all camera parameters needed to render this View for this frame.
         */
        FCamera const* const camera = mViewingCamera ? mViewingCamera : mCullingCamera;
        if (engine.debug.view.camera_at_origin) {
            // this moves the camera to the origin, effectively doing all shader computations in
            // view-space, which improves floating point precision in the shader by staying around
            // zero, where fp precision is highest. This also ensures that when the camera is placed
            // very far from the origin, objects are still rendered and lit properly.
            translation = -camera->getPosition();
        }

        FIndirectLight const* const ibl = nullptr;// scene->getIndirectLight();
//         if (ibl) {
//             // the IBL transformation must be a rigid transform
//             rotation = mat3{ transpose(scene->getIndirectLight()->getRotation()) };
//             // it is important to orthogonalize the matrix when converting it to doubles, because
//             // as float, it only has about a 1e-8 precision on the size of the basis vectors
//             rotation = orthogonalize(rotation);
//         }
        return { *camera, mat4{ rotation } *mat4::translation(translation) };
    }

}