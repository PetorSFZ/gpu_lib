#pragma once
#ifndef GPU_LIB_H
#define GPU_LIB_H

#include <sfz.h>

// Init API
// ------------------------------------------------------------------------------------------------

struct GpuLib;

sfz_struct(GpuLibInitCfg) {
	SfzAllocator* cpu_allocator;
	u32 gpu_heap_size_bytes;
	u32 max_num_kernels;
	bool debug_mode;
	bool debug_shader_validation;
};

sfz_extern_c GpuLib* gpuLibInit(const GpuLibInitCfg* cfg);
sfz_extern_c void gpuLibDestroy(GpuLib* gpu);

// Memory API
// ------------------------------------------------------------------------------------------------

sfz_struct(GpuPtr) {
	u32 offset;

#ifdef __cplusplus
	constexpr bool operator== (GpuPtr o) const { return offset == o.offset; }
	constexpr bool operator!= (GpuPtr o) const { return offset != o.offset; }
#endif
};

sfz_constant GpuPtr GPU_NULLPTR = {};

sfz_extern_c GpuPtr gpuMalloc(GpuLib* gpu, u32 num_bytes);
sfz_extern_c void gpuFree(GpuLib* gpu, GpuPtr ptr);

// Kernel API
// ------------------------------------------------------------------------------------------------

sfz_struct(GpuKernel) {
	u32 handle;

#ifdef __cplusplus
	constexpr bool operator== (GpuKernel o) const { return handle == o.handle; }
	constexpr bool operator!= (GpuKernel o) const { return handle != o.handle; }
#endif
};

sfz_constant GpuKernel GPU_NULL_KERNEL = {};

sfz_struct(GpuKernelDesc) {
	const char* name;
	const char* src;
	const char* entry;
	//u32 num_defines;
	//const char* const* defines;
};

sfz_extern_c GpuKernel gpuKernelInit(GpuLib* gpu, const GpuKernelDesc* desc);
sfz_extern_c void gpuKernelDestroy(GpuLib* gpu, GpuKernel kernel);

sfz_extern_c i32x3 gpuKernelGetGroupDims(const GpuLib* gpu, GpuKernel kernel);

// Submission API
// ------------------------------------------------------------------------------------------------

sfz_extern_c void gpuEnqueueKernel1(GpuLib* gpu, GpuKernel kernel, i32 num_groups);
sfz_extern_c void gpuEnqueueKernel2(GpuLib* gpu, GpuKernel kernel, i32x2 num_groups);
sfz_extern_c void gpuEnqueueKernel3(GpuLib* gpu, GpuKernel kernel, i32x3 num_groups);

sfz_extern_c void gpuSubmitQueuedWork(GpuLib* gpu);
sfz_extern_c void gpuFlush(GpuLib* gpu);

#endif // GPU_LIB_H
