#include <irrlicht.h>

#include "../../../include/irr/asset/filters/CSummedAreaTableImageFilter.h"
#include "../ext/ScreenShot/ScreenShot.h"

using namespace irr;
using namespace core;
using namespace asset;
using namespace video;

/*
	Comment IMAGE_VIEW define to use ordinary cpu image.
	You can view the results in Renderdoc.

	When using ordinary IMAGE you can use OVERLAPPING_REGIONS
	to choose whether to use extra overlapping region on output image
	with a custom in-offset and extent

	You can also specify whether to perform sum in
	exclusive mode by EXCLUSIVE_SUM,  
	otherwise in inclusive mode 
*/

#define IMAGE_VIEW 
//#define OVERLAPPING_REGIONS				// @devsh I leave it for you
constexpr bool EXCLUSIVE_SUM = true;
constexpr auto MIPMAP_IMAGE_VIEW = 2u;		// feel free to change the mipmap
constexpr auto MIPMAP_IMAGE = 0u;			// ordinary image used in the example has only 0-th mipmap

int main()
{
	irr::SIrrlichtCreationParameters params;
	params.Bits = 32;
	params.ZBufferBits = 24;
	params.DriverType = video::EDT_OPENGL;
	params.WindowSize = dimension2d<uint32_t>(1600, 900);
	params.Fullscreen = false;
	params.Doublebuffer = true;
	params.Vsync = true;
	params.Stencilbuffer = false;

	auto device = createDeviceEx(params);
	if (!device)
		return false;

	device->getCursorControl()->setVisible(false);
	auto driver = device->getVideoDriver();
	auto assetManager = device->getAssetManager();
	auto sceneManager = device->getSceneManager();

	auto getSummedImage = [](const core::smart_refctd_ptr<ICPUImage> image) -> core::smart_refctd_ptr<ICPUImage>
	{
		using SUM_FILTER = CSummedAreaTableImageFilter<EXCLUSIVE_SUM>;

		core::smart_refctd_ptr<ICPUImage> newSumImage;
		{
			const auto referenceImageParams = image->getCreationParameters();
			const auto referenceBuffer = image->getBuffer();
			const auto referenceRegions = image->getRegions();
			const auto* referenceRegion = referenceRegions.begin();

			auto newImageParams = referenceImageParams;
			core::smart_refctd_ptr<ICPUBuffer> newCpuBuffer;

			#ifdef IMAGE_VIEW
			newImageParams.flags = IImage::ECF_CUBE_COMPATIBLE_BIT;
			newImageParams.format = EF_R16G16B16A16_UNORM;
			#else
			newImageParams.format = EF_R32G32B32A32_SFLOAT;
			#endif // IMAGE_VIEW

			auto newRegions = core::make_refctd_dynamic_array<core::smart_refctd_dynamic_array<ICPUImage::SBufferCopy>>
			(
				#ifdef IMAGE_VIEW
				referenceRegions.size()
				#else

				#ifdef OVERLAPPING_REGIONS
				2u
				#else
				referenceRegions.size() // one region at all
				#endif // OVERLAPPING_REGIONS

				#endif // IMAGE_VIEW
			);

			size_t regionOffsets = {};

			#ifdef IMAGE_VIEW
			for (auto newRegion = newRegions->begin(); newRegion != newRegions->end(); ++newRegion)
			{
				/*
					Regions pulled directly from a loader doesn't overlap, so each following is a certain single mipmap
				*/

				auto idOffset = newRegion - newRegions->begin();
				*newRegion = *(referenceRegion++);
				newRegion->bufferOffset = regionOffsets;

				const auto fullMipMapExtent = image->getMipSize(idOffset);

				regionOffsets += fullMipMapExtent.x * fullMipMapExtent.y * fullMipMapExtent.z * newImageParams.arrayLayers * asset::getTexelOrBlockBytesize(newImageParams.format);
			}
			newCpuBuffer = core::make_smart_refctd_ptr<ICPUBuffer>(regionOffsets);
			#else

			/*
				2 overlapping regions if OVERLAPPING_REGIONS is defined
			*/

			const auto fullMipMapExtent = image->getMipSize(MIPMAP_IMAGE);
			const size_t bufferByteSize = fullMipMapExtent.x * fullMipMapExtent.y * fullMipMapExtent.z * newImageParams.arrayLayers * asset::getTexelOrBlockBytesize(newImageParams.format);
			newCpuBuffer = core::make_smart_refctd_ptr<ICPUBuffer>(bufferByteSize);

			auto newFirstRegion = newRegions->begin();
			*newFirstRegion = *(referenceRegion++);
			newFirstRegion->bufferOffset = regionOffsets;

			#ifdef OVERLAPPING_REGIONS
			auto newSecondRegion = newRegions->begin() + 1;
			*newSecondRegion = *newFirstRegion;

			newSecondRegion->bufferImageHeight = fullMipMapExtent.y;

			auto simdImageOffset = fullMipMapExtent / 4;
			newSecondRegion->imageOffset = { simdImageOffset.x, simdImageOffset.y, simdImageOffset.z };

			auto simdImageExtent = fullMipMapExtent / 2;
			newSecondRegion->imageExtent = { simdImageExtent.x, simdImageExtent.y, 1 };

			#endif // OVERLAPPING_REGIONS

			#endif // IMAGE_VIEW

			newSumImage = ICPUImage::create(std::move(newImageParams));
			newSumImage->setBufferAndRegions(std::move(newCpuBuffer), newRegions);

			SUM_FILTER sumFilter;
			SUM_FILTER::state_type state;
			
			state.inImage = image.get();
			state.outImage = newSumImage.get();
			state.inOffset = { 0, 0, 0 };
			state.inBaseLayer = 0;
			state.outOffset = { 0, 0, 0 };
			state.outBaseLayer = 0;

			#ifdef IMAGE_VIEW
			const auto fullMipMapExtent = image->getMipSize(MIPMAP_IMAGE_VIEW);
			state.extent = { fullMipMapExtent.x, fullMipMapExtent.y, fullMipMapExtent.z };
			#else 
			state.extent =  { referenceImageParams.extent.width, referenceImageParams.extent.height, referenceImageParams.extent.depth };
			#endif // IMAGE_VIEW

			state.layerCount = newSumImage->getCreationParameters().arrayLayers;

			state.scratchMemoryByteSize = state.getRequiredScratchByteSize(state.inImage, state.extent);
			state.scratchMemory = reinterpret_cast<uint8_t*>(_IRR_ALIGNED_MALLOC(state.scratchMemoryByteSize, 32));

			#ifdef IMAGE_VIEW
			state.inMipLevel = MIPMAP_IMAGE_VIEW;
			state.outMipLevel = MIPMAP_IMAGE_VIEW;
			state.normalizeImageByTotalSATValues = true; // pay attention that we may force normalizing output values (but it will do it anyway if input is normalized)
			#else
			state.inMipLevel = MIPMAP_IMAGE;
			state.outMipLevel = MIPMAP_IMAGE;
			#endif // IMAGE_VIEW

			if (!sumFilter.execute(&state))
				os::Printer::log("Something went wrong while performing sum operation!", ELL_WARNING);

			_IRR_ALIGNED_FREE(state.scratchMemory);
		}
		return newSumImage;
	};

	IAssetLoader::SAssetLoadParams lp(0ull, nullptr, IAssetLoader::ECF_DONT_CACHE_REFERENCES);

	#ifdef IMAGE_VIEW
	auto bundle = assetManager->getAsset("../../media/GLI/earth-cubemap3.dds", lp);
	auto cpuImageViewFetched = core::smart_refctd_ptr_static_cast<asset::ICPUImageView>(bundle.getContents().first[0]);

	auto cpuImage = getSummedImage(cpuImageViewFetched->getCreationParameters().image);
	#else
	auto bundle = assetManager->getAsset("../../media/colorexr.exr", lp);
	auto cpuImage = getSummedImage(core::smart_refctd_ptr_static_cast<asset::ICPUImage>(bundle.getContents().first[0]));
	#endif // IMAGE_VIEW

	ICPUImageView::SCreationParams viewParams;
	viewParams.flags = static_cast<ICPUImageView::E_CREATE_FLAGS>(0u);
	viewParams.image = cpuImage;
	viewParams.format = viewParams.image->getCreationParameters().format;

	#ifdef IMAGE_VIEW
	viewParams.viewType = IImageView<ICPUImage>::ET_2D_ARRAY;
	#else
	viewParams.viewType = IImageView<ICPUImage>::ET_2D;
	#endif // IMAGE_VIEW

	viewParams.subresourceRange.baseArrayLayer = 0u;
	viewParams.subresourceRange.layerCount = cpuImage->getCreationParameters().arrayLayers;
	viewParams.subresourceRange.baseMipLevel = 0u;
	viewParams.subresourceRange.levelCount = cpuImage->getCreationParameters().mipLevels;

	auto cpuImageView = ICPUImageView::create(std::move(viewParams));
	assert(cpuImageView.get(), "The imageView didn't pass creation validation!");

	constexpr std::string_view MODE = [&]() constexpr 
	{
		if constexpr (EXCLUSIVE_SUM)
			return "EXCLUSIVE_SAT_";
		else
			return "INCLUSIVE_SAT_";
	}
	();

	asset::IAssetWriter::SAssetWriteParams wparams(cpuImageView.get());
	#ifdef IMAGE_VIEW
	assetManager->writeAsset(std::string(MODE.data()) + "IMG_VIEW.dds", wparams);
	#else

		#ifdef OVERLAPPING_REGIONS
		assetManager->writeAsset(std::string(MODE.data()) + "IMG_OVERLAPPING_REGIONS.exr", wparams);
		#else 
		assetManager->writeAsset(std::string(MODE.data()) + "IMG.exr", wparams);
		#endif // OVERLAPPING_REGIONS

	#endif // IMAGE_VIEW
}