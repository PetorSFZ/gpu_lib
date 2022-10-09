#include <sfz.h>
#include <sfz_defer.hpp>
#include <skipifzero_allocators.hpp>

#include <gpu_lib.h>

#include <stdio.h>

// Main
// ------------------------------------------------------------------------------------------------

static SfzAllocator global_cpu_allocator = {};

i32 main(i32 argc, char* argv[])
{
	(void)argc;
	(void)argv;

	// Initialize global cpu allocator
	global_cpu_allocator = sfz::createStandardAllocator();

	// Initialize gpu_lib
	const GpuLibInitCfg gpu_init_cfg = GpuLibInitCfg{
		.cpu_allocator = &global_cpu_allocator,
		.gpu_heap_size_bytes = 2u * 1024u * 1024u * 1024u,//U32_MAX,
		.max_num_kernels = 128,
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
typedef int3 i32x3;
typedef int4 i32x4;

RWByteAddressBuffer gpu_global_heap : register(u0);

cbuffer LaunchParams : register(b0) {
	i32x4 params;
}

[numthreads(16, 16, 1)]
void CSMain(
	i32x3 group_idx : SV_GroupID, // Index of group
	i32 group_flat_idx : SV_GroupIndex, // Flattened version of group index
	i32x3 group_thread_idx : SV_GroupThreadID, // Index of thread within group
	i32x3 dispatch_thread_idx : SV_DispatchThreadID) // Global index of thread
{
	gpu_global_heap.Store(0, params.x);
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

	for (i32 i = 0; i < 100; i++) {

		struct {
			i32x4 params;
		} params;
		gpuQueueDispatch(gpu, kernel, i32x3_init(1, 1, 1), params);

		gpuSubmitQueuedWork(gpu);
	}
	gpuFlush(gpu);

	return 0;
}
