#include "gpu_lib.h"

#include <sfz_cpp.hpp>

// gpu_lib
// ------------------------------------------------------------------------------------------------

sfz_struct(GpuLib) {
	GpuLibInitCfg cfg;
};

// gpu_lib init
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
