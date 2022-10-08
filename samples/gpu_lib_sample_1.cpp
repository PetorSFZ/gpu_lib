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
		.debug_shader_validation = false
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

	[numthreads(16, 16, 1)]
void CSMain(
	i32x3 groupIdx : SV_GroupID, // Index of group
	i32 groupFlatIdx : SV_GroupIndex, // Flattened version of group index
	i32x3 groupThreadIdx : SV_GroupThreadID, // Index of thread within group
	i32x3 dispatchThreadIdx : SV_DispatchThreadID) // Global index of thread
{
}
)";
	const GpuKernelDesc kernel_desc = GpuKernelDesc{
		.name = "Test",
		.src = KERNEL_SRC,
		.entry = "CSMain"
	};
	const GpuKernel kernel = gpuKernelInit(gpu, &kernel_desc);
	sfz_defer[=]() {
		gpuKernelDestroy(gpu, kernel);
	};


	return 0;
}
