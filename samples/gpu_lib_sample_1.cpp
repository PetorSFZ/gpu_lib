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
		.gpu_heap_size_bytes = 1u * 1024u * 1024u * 1024u,//U32_MAX,
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

	constexpr char KERNEL_SRC[] =
R"(

typedef int i32;
typedef int2 i32x2;
typedef int3 i32x3;
typedef int4 i32x4;
typedef float4 f32x4;

RWByteAddressBuffer gpu_global_heap : register(u0);
RWTexture2D<f32x4> gpu_rwtex_array[] : register(u1);

cbuffer LaunchParams : register(b0) {
	i32x2 res;
	i32 padding1;
	i32 padding2;
}

[numthreads(16, 16, 1)]
void CSMain(
	i32x3 group_idx : SV_GroupID, // Index of group
	i32 group_flat_idx : SV_GroupIndex, // Flattened version of group index
	i32x3 group_thread_idx : SV_GroupThreadID, // Index of thread within group
	i32x3 dispatch_thread_idx : SV_DispatchThreadID) // Global index of thread
{
	const i32x2 idx = dispatch_thread_idx.xy;
	if (res.x <= idx.x || res.y <= idx.y) return;

	if (idx.x == 0 && idx.y == 0) gpu_global_heap.Store(0, 42);

	RWTexture2D<f32x4> swapchain_rt = gpu_rwtex_array[0];
	swapchain_rt[idx] = f32x4(0.0, 1.0, 0.0, 1.0);
}

)";
	const GpuKernelDesc kernel_desc = GpuKernelDesc{
		.name = "Test",
		.src = KERNEL_SRC
	};
	const GpuKernel kernel = gpuKernelInit(gpu, &kernel_desc);
	sfz_defer[=]() {
		gpuKernelDestroy(gpu, kernel);
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

		gpuQueueSwapchainBegin(gpu);
		const i32x2 res = gpuSwapchainGetRes(gpu);
		const i32x2 group_dims = gpuKernelGetGroupDims2(gpu, kernel);
		const i32x2 num_groups = (res + group_dims - i32x2_splat(1)) / group_dims;

		struct {
			i32x2 res;
			i32 padding1;
			i32 padding2;
		} params;
		params.res = res;
		gpuQueueDispatch2(gpu, kernel, num_groups, GPU_LAUNCH_PARAMS(params));
		//gpuQueueDispatch2(gpu, kernel, num_groups, GPU_LAUNCH_NO_PARAMS);

		gpuQueueSwapchainEnd(gpu);
		gpuSubmitQueuedWork(gpu);
		gpuSwapchainPresent(gpu, true);
	}
	gpuFlush(gpu);

	return 0;
}
