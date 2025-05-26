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

filament::FEngine* g_FilamentEngine = nullptr;
filament::ColorPassDescriptorSet* g_mColorPassDescriptorSet = nullptr;

namespace filament {

	class FScene {
    public:
		static constexpr size_t DIRECTIONAL_LIGHTS_COUNT = 1;

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
	};

	FScene::LightSoa mLightData;

	static FScene::LightSoa& getLightData() { return mLightData; }

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
        FScene::LightSoa& lightData = getLightData();

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
        FScene::LightSoa& lightData = getLightData();

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
        using namespace math;
		float mCameraFocalLength = 28.0f;
		float mCameraNear = 0.1f;
		float mCameraFar = 100.0f;
		auto cam = engine.createCamera({});
        cam->setExposure(16.0f, 1 / 125.0f, 100.0f);

        cam->setLensProjection(mCameraFocalLength, 1.0, mCameraNear, mCameraFar);
        //
        auto aspectRatio = double(1024) / 640;//double(mainWidth) / height;
        cam->setScaling({ 1.0 / aspectRatio, 1.0 });

        cam->lookAt({ 4, 0, -4 }, { 0, 0, -4 }, { 0, 1, 0 });

        auto mViewingCamera = cam;
        auto mCullingCamera = mViewingCamera;
        //FScene const* const scene = getScene();

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