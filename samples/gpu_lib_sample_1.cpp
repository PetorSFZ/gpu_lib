#include <stdio.h>

#include <SDL.h>
#include <SDL_syswm.h>

#include <sfz.h>
#include <sfz_defer.hpp>
#include <skipifzero_allocators.hpp>

#include <gpu_lib.h>

// Main
// ------------------------------------------------------------------------------------------------

static SfzAllocator global_cpu_allocator = {};

i32 main(i32 argc, char* argv[])
{
	(void)argc;
	(void)argv;

	// Init SDL2
	if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO) < 0) {
		printf("SDL_Init() failed: %s", SDL_GetError());
		return 1;
	}
	sfz_defer[]() { SDL_Quit(); };

	// Create window
	SDL_Window* window = SDL_CreateWindow(
		"[gpu_lib] Sample 1",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		1280, 720,
		SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
	if (window == NULL) {
		printf("SDL_CreateWindow() failed: %s\n", SDL_GetError());
		return 1;
	}
	sfz_defer[=]() { SDL_DestroyWindow(window); };

	// Grab window handle
	SDL_SysWMinfo wm_info = {};
	SDL_VERSION(&wm_info.version);
	if (!SDL_GetWindowWMInfo(window, &wm_info)) return 1;
	const HWND window_handle = wm_info.info.win.window;

	// Initialize global cpu allocator
	global_cpu_allocator = sfz::createStandardAllocator();

	// Initialize gpu_lib
	const GpuLibInitCfg gpu_init_cfg = GpuLibInitCfg{
		.cpu_allocator = &global_cpu_allocator,
		.gpu_heap_size_bytes = 2u * 1024u * 1024u * 1024u,//U32_MAX,
		.upload_heap_size_bytes = 128 * 1024 * 1024,
		.download_heap_size_bytes = 128 * 1024 * 1024,
		.max_num_concurrent_downloads = 1024,
		.max_num_textures_per_type = 1024,
		.max_num_kernels = 128,
		
		.native_window_handle = window_handle,
		.allow_tearing = true,
		
		.debug_mode = true,
		.debug_shader_validation = true
	};
	GpuLib* gpu = gpuLibInit(&gpu_init_cfg);
	if (gpu == nullptr) {
		printf("gpuLibInit failed()");
		return 1;
	}
	sfz_defer[=]() {
		gpuLibDestroy(gpu);
	};

	const GpuKernelDesc kernel_desc = GpuKernelDesc{
		.name = "Test",
		.path = "../../samples/gpu_lib_sample_1_kernel.hlsl",
	};
	const GpuKernel kernel = gpuKernelInit(gpu, &kernel_desc);
	sfz_assert(kernel != GPU_NULL_KERNEL);
	sfz_defer[=]() {
		gpuKernelDestroy(gpu, kernel);
	};

	const GpuPtr color_ptr = gpuMalloc(gpu, sizeof(f32x4));
	sfz_assert(color_ptr != GPU_NULLPTR);
	sfz_defer[=]() {
		gpuFree(gpu, color_ptr);
	};

	const GpuPtr timestamp_ptr = gpuMalloc(gpu, sizeof(u64));
	sfz_assert(timestamp_ptr != GPU_NULLPTR);
	sfz_defer[=]() {
		gpuFree(gpu, timestamp_ptr);
	};

	struct BigChunk {
		u8 data[4096];
	};
	const GpuPtr big_chunk_ptr = gpuMalloc(gpu, sizeof(BigChunk));
	sfz_assert(big_chunk_ptr != GPU_NULLPTR);

	f32x4 color = f32x4_init(0.0f, 0.0f, 0.0f, 1.0f);

	// Retrieve the initial gpu timestamp
	const u64 timestamp_freq = gpuTimestampGetFreq(gpu);
	u64 initial_gpu_timestamp = 0;
	{
		gpuQueueTakeTimestamp(gpu, timestamp_ptr);
		const GpuTicket ticket = gpuQueueMemcpyDownload(gpu, timestamp_ptr, sizeof(u64));
		gpuSubmitQueuedWork(gpu);
		gpuFlush(gpu);
		initial_gpu_timestamp = gpuGetDownloadedData<u64>(gpu, ticket);
	}

	GpuTicket timestamp_tickets[GPU_NUM_CONCURRENT_SUBMITS] = {};
	auto getCurrTimestampTicket = [&]() -> GpuTicket& {
		return timestamp_tickets[gpuGetCurrSubmitIdx(gpu) % GPU_NUM_CONCURRENT_SUBMITS];
	};

	GpuTicket big_chunk_tickets[GPU_NUM_CONCURRENT_SUBMITS] = {};
	auto getCurrBigChunkTicket = [&]() -> GpuTicket& {
		return big_chunk_tickets[gpuGetCurrSubmitIdx(gpu) % GPU_NUM_CONCURRENT_SUBMITS];
	};

	GpuRWTex tex = GPU_NULL_RWTEX;
	{
		const GpuRWTexDesc tex_desc = GpuRWTexDesc{
			.name = "TestTexture",
			.format = GPU_FORMAT_RGBA_F16,
			.fixed_res = i32x2_init(128, 128),
			.swapchain_relative = true,
			//.relative_fixed_height = 100
			.relative_scale = 0.5f
		};
		tex = gpuRWTexInit(gpu, &tex_desc);
		sfz_assert(tex != GPU_NULL_RWTEX);
	}
	sfz_defer[=]() {
		gpuRWTexDestroy(gpu, tex);
	};

	// Run our main loop
	bool running = true;
	while (running) {

		// Query SDL events, loop wrapped in a lambda so we can continue to next iteration of main
		// loop. "return false;" == continue to next iteration
		if (![&]() -> bool {
			SDL_Event event = {};
				while (SDL_PollEvent(&event) != 0) {
					switch (event.type) {
					case SDL_QUIT:
						running = false;
							return false;
					case SDL_KEYUP:
						if (event.key.keysym.sym == SDLK_ESCAPE) {
							running = false;
								return false;
						}
						break;
					}
				}
			return true;
			}()) continue;

		// Take timestamp
		gpuQueueTakeTimestamp(gpu, timestamp_ptr);

		// Grab timestamp ticket from our queue and check if the download is ready
		GpuTicket& timestamp_ticket = getCurrTimestampTicket();
		if (timestamp_ticket != GPU_NULL_TICKET) {
			const u64 timestamp = gpuGetDownloadedData<u64>(gpu, timestamp_ticket);
			timestamp_ticket = GPU_NULL_TICKET;
			const u64 diff = timestamp - initial_gpu_timestamp;
			printf("Current GPU time: %.3f, raw: %llu\n",
				f32(f64(diff) / f64(timestamp_freq)), timestamp);
		}

		// Start timestamp download
		timestamp_ticket = gpuQueueMemcpyDownload(gpu, timestamp_ptr, sizeof(u64));

		BigChunk dummy_chunk = {};
		gpuQueueMemcpyUpload(gpu, big_chunk_ptr, dummy_chunk);

		// Grab big chunk ticket from our queue and check if the download is ready
		GpuTicket& big_chunk_ticket = getCurrBigChunkTicket();
		if (big_chunk_ticket != GPU_NULL_TICKET) {
			const BigChunk dummy = gpuGetDownloadedData<BigChunk>(gpu, big_chunk_ticket);
			big_chunk_ticket = GPU_NULL_TICKET;
		}
		big_chunk_ticket = gpuQueueMemcpyDownload(gpu, big_chunk_ptr, sizeof(BigChunk));

		color.x += 0.01f;
		if (color.x > 1.0f) color.x -= 1.0f;
		gpuQueueMemcpyUpload(gpu, color_ptr, color);

		const i32x2 res = gpuSwapchainGetRes(gpu);
		const i32x2 group_dims = gpuKernelGetGroupDims2(gpu, kernel);
		const i32x2 num_groups = (res + group_dims - i32x2_splat(1)) / group_dims;

		struct {
			i32x2 res;
			GpuPtr color_ptr;
			GpuRWTex tex_idx;
			u16 padding;
			
		} params;
		params.res = res;
		params.color_ptr = color_ptr;
		params.tex_idx = tex;
		gpuQueueDispatch(gpu, kernel, num_groups, params);

		gpuSubmitQueuedWork(gpu);
		gpuSwapchainPresent(gpu, true);
	}

	// Do want to flush before all destructors run, otherwise we might end up trying to destroy
	// stuff in-flight on GPU.
	gpuFlush(gpu);

	return 0;
}
