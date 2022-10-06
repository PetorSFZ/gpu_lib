#include "gpu_lib.h"

#include <sfz_cpp.hpp>

// gpu_lib
// ------------------------------------------------------------------------------------------------

sfz_struct(GpuLib) {
	GpuLibInitCfg cfg;
};

// Init API
// ------------------------------------------------------------------------------------------------

sfz_extern_c GpuLib* gpuLibInit(const GpuLibInitCfg* cfg)
{
	GpuLib* gpu = sfz_new<GpuLib>(cfg->cpu_allocator, sfz_dbg("GpuLib"));
	*gpu = {};
	gpu->cfg = *cfg;
	return gpu;
}

sfz_extern_c void gpuLibDestroy(GpuLib* gpu)
{
	if (gpu == nullptr) return;
	SfzAllocator* allocator = gpu->cfg.cpu_allocator;
	sfz_delete(allocator, gpu);
}

// Memory API
// ------------------------------------------------------------------------------------------------

sfz_extern_c GpuPtr gpuMalloc(GpuLib* gpu, u32 num_bytes)
{
	return GPU_NULL;
}

sfz_extern_c void gpuFree(GpuLib* gpu, GpuPtr ptr)
{
	
}

// Kernel API
// ------------------------------------------------------------------------------------------------

sfz_extern_c GpuKernelHandle gpuKernelInit(GpuLib* gpu, const GpuKernelDesc* desc)
{
	return GPU_NULL_KERNEL;
}

sfz_extern_c void gpuKernelDestroy(GpuLib* gpu, GpuKernelHandle kernel)
{

}

sfz_extern_c i32x3 gpuKernelGetGroupDims(const GpuLib* gpu, GpuKernelHandle kernel)
{
	return i32x3_splat(0);
}

// Submission API
// ------------------------------------------------------------------------------------------------

sfz_extern_c void gpuEnqueuKernel1(GpuLib* gpu, GpuKernelHandle kernel, i32 num_groups)
{

}

sfz_extern_c void gpuEnqueueKernel2(GpuLib* gpu, GpuKernelHandle kernel, i32x2 num_groups)
{

}

sfz_extern_c void gpuEnqueueKernel3(GpuLib* gpu, GpuKernelHandle kernel, i32x3 num_groups)
{

}

sfz_extern_c void gpuSubmitQueuedWork(GpuLib* gpu)
{

}

sfz_extern_c void gpuFlush(GpuLib* gpu)
{

}
