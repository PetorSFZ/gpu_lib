#pragma once
#ifndef GPU_LIB_H
#define GPU_LIB_H

#include <sfz.h>

// gpu_lib init
// ------------------------------------------------------------------------------------------------

struct GpuLib;

sfz_struct(GpuLibInitCfg) {
	SfzAllocator* cpu_allocator;
	u32 gpu_heap_size_bytes;
};

sfz_extern_c GpuLib* gpuLibInit(const GpuLibInitCfg* cfg);
sfz_extern_c void gpuLibDestroy(GpuLib* gpu);



#endif // GPU_LIB_H
