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

sfz_constant GpuPtr GPU_NULL = {};

sfz_extern_c GpuPtr gpuMalloc(GpuLib* gpu, u32 num_bytes);
sfz_extern_c void gpuFree(GpuLib* gpu, GpuPtr ptr);

// Kernel API
// ------------------------------------------------------------------------------------------------

sfz_struct(GpuKernelHandle) {
	u32 handle;

#ifdef __cplusplus
	constexpr bool operator== (GpuKernelHandle o) const { return handle == o.handle; }
	constexpr bool operator!= (GpuKernelHandle o) const { return handle != o.handle; }
#endif
};

sfz_constant GpuKernelHandle GPU_NULL_KERNEL = {};

sfz_struct(GpuKernelDesc) {
	
};

sfz_extern_c GpuKernelHandle gpuKernelInit(GpuLib* gpu, const GpuKernelDesc* desc);
sfz_extern_c void gpuKernelDestroy(GpuLib* gpu, GpuKernelHandle kernel);

sfz_extern_c i32x3 gpuKernelGetGroupDims(const GpuLib* gpu, GpuKernelHandle kernel);

// Submission API
// ------------------------------------------------------------------------------------------------

sfz_extern_c void gpuEnqueuKernel1(GpuLib* gpu, GpuKernelHandle kernel, i32 num_groups);
sfz_extern_c void gpuEnqueueKernel2(GpuLib* gpu, GpuKernelHandle kernel, i32x2 num_groups);
sfz_extern_c void gpuEnqueueKernel3(GpuLib* gpu, GpuKernelHandle kernel, i32x3 num_groups);

sfz_extern_c void gpuSubmitQueuedWork(GpuLib* gpu);
sfz_extern_c void gpuFlush(GpuLib* gpu);

#endif // GPU_LIB_H
