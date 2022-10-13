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
		800, 800,
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
	sfz_defer[=]() {
		gpuKernelDestroy(gpu, kernel);
	};

	GpuPtr color_ptr = gpuMalloc(gpu, sizeof(f32x4));
	sfz_assert(color_ptr != GPU_NULLPTR);
	sfz_defer[=]() {
		gpuFree(gpu, color_ptr);
	};

	f32x4 color = f32x4_init(0.0f, 0.0f, 0.0f, 1.0f);

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

		color.x += 0.01f;
		if (color.x > 1.0f) color.x -= 1.0f;
		gpuQueueMemcpyUpload(gpu, color_ptr, &color, sizeof(color));

		const i32x2 res = gpuSwapchainGetRes(gpu);
		const i32x2 group_dims = gpuKernelGetGroupDims2(gpu, kernel);
		const i32x2 num_groups = (res + group_dims - i32x2_splat(1)) / group_dims;

		struct {
			i32x2 res;
			GpuPtr color_ptr;
			i32 padding;
			
		} params;
		params.res = res;
		params.color_ptr = color_ptr;
		gpuQueueDispatch(gpu, kernel, num_groups, params);

		gpuSubmitQueuedWork(gpu);
		gpuSwapchainPresent(gpu, true);
	}
	gpuFlush(gpu);

	return 0;
}
