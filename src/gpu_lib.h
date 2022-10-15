#pragma once
#ifndef GPU_LIB_H
#define GPU_LIB_H

#include <sfz.h>

// Constants
// ------------------------------------------------------------------------------------------------

// This constant defines the number of command lists that can be in-flight at the same time. It's
// important for synchronization, if you are downloading data from the GPU every frame you should
// typically have a lag of this many frames before you get the data.
sfz_constant u32 GPU_NUM_CONCURRENT_SUBMITS = 3;

sfz_constant u32 GPU_HEAP_SYSTEM_RESERVED_SIZE = 8 * 1024 * 1024;
sfz_constant u32 GPU_HEAP_MIN_SIZE = GPU_HEAP_SYSTEM_RESERVED_SIZE;
sfz_constant u32 GPU_HEAP_MAX_SIZE = U32_MAX;
sfz_constant u32 GPU_TEXTURES_MIN_NUM = 2;
sfz_constant u32 GPU_TEXTURES_MAX_NUM = 16384;
sfz_constant u32 GPU_LAUNCH_PARAMS_MAX_SIZE = sizeof(u32) * 12;
sfz_constant u32 GPU_KERNEL_MAX_NUM_DEFINES = 8;
sfz_constant u32 GPU_KERNEL_DEFINE_MAX_LEN = 48;


// Init API
// ------------------------------------------------------------------------------------------------

struct GpuLib;

sfz_struct(GpuLibInitCfg) {

	SfzAllocator* cpu_allocator;
	u32 gpu_heap_size_bytes;
	u32 upload_heap_size_bytes;
	u32 download_heap_size_bytes;
	u32 max_num_concurrent_downloads;
	u32 max_num_textures_per_type;
	u32 max_num_kernels;
	
	void* native_window_handle;
	bool allow_tearing;
	
	bool debug_mode;
	bool debug_shader_validation;
};

sfz_extern_c GpuLib* gpuLibInit(const GpuLibInitCfg* cfg);
sfz_extern_c void gpuLibDestroy(GpuLib* gpu);


// Memory API
// ------------------------------------------------------------------------------------------------

typedef u32 GpuPtr;
sfz_constant GpuPtr GPU_NULLPTR = 0;

sfz_extern_c GpuPtr gpuMalloc(GpuLib* gpu, u32 num_bytes);
sfz_extern_c void gpuFree(GpuLib* gpu, GpuPtr ptr);


// Textures API
// ------------------------------------------------------------------------------------------------

// A GpuRWTex is an index to a read-write:able texture in the textures array. Similarly to GpuPtr
// it can freely be copied to GPU and used to bindlessly access the texture it represents.
typedef u16 GpuRWTex;
sfz_constant GpuRWTex GPU_NULL_RWTEX = 0;

typedef enum {
	GPU_FORMAT_UNDEFINED = 0,

	GPU_FORMAT_R_U8_UNORM, // Normalized between [0, 1]
	GPU_FORMAT_RG_U8_UNORM, // Normalized between [0, 1]
	GPU_FORMAT_RGBA_U8_UNORM, // Normalized between [0, 1]

	GPU_FORMAT_R_U8,
	GPU_FORMAT_RG_U8,
	GPU_FORMAT_RGBA_U8,

	GPU_FORMAT_R_U16,
	GPU_FORMAT_RG_U16,
	GPU_FORMAT_RGBA_U16,

	GPU_FORMAT_R_I32,
	GPU_FORMAT_RG_I32,
	GPU_FORMAT_RGBA_I32,

	GPU_FORMAT_R_F16,
	GPU_FORMAT_RG_F16,
	GPU_FORMAT_RGBA_F16,

	GPU_FORMAT_R_F32,
	GPU_FORMAT_RG_F32,
	GPU_FORMAT_RGBA_F32,

	GPU_FORMAT_FORCE_I32 = I32_MAX
} GpuFormat;

sfz_extern_c const char* gpuFormatToString(GpuFormat format);

sfz_struct(GpuRWTexDesc) {
	const char* name;
	GpuFormat format;

	// Resolution of this texture if it is not swapchain relative
	i32x2 fixed_res;

	// If the texture is swapchain relative it will be reallocated whenever the swapchain changes
	// resolution. The parameters below starting with "relative_" are used to determine what the
	// resolution should be relative to the swapchain.
	bool swapchain_relative;
	i32 relative_fixed_height;
	f32 relative_scale;
};

sfz_extern_c GpuRWTex gpuRWTexInit(GpuLib* gpu, const GpuRWTexDesc* desc);
sfz_extern_c void gpuRWTexDestroy(GpuLib* gpu, GpuRWTex tex);

sfz_extern_c const GpuRWTexDesc* gpuRWTexGetDesc(const GpuLib* gpu, GpuRWTex tex);
sfz_extern_c i32x2 gpuRWTexGetRes(const GpuLib* gpu, GpuRWTex tex);

// Changes size of a swapchain relative GpuRWTex.
sfz_extern_c void gpuRWTexSetSwapchainRelativeScale(GpuLib* gpu, GpuRWTex tex, f32 scale);
sfz_extern_c void gpuRWTexSetSwapchainRelativeFixedHeight(GpuLib* gpu, GpuRWTex tex, i32 height);


// Unfortunately we probably do need textures. But maybe we can limit to:
// * 2D only
// * Two separate types, read-only and read-write
// * Can not convert between read-only and read-write
//     * I.e., read-only can only have data supplied from CPU. Can never be written to on GPU.
//     * Read-write can NEVER have data supplied from CPU, must be written to on GPU.
//     * Use cases: read-only for "normal assets" that needs to be rendered, read-write for
//       framebuffers, shadow maps, other similar maps generated on the GPU.
//     * As a consequence, generating mipmaps on GPU becomes impossible, but who cares really.
// * Read-write has no mipmaps
// * 2 global texture arrays (bindless textures), one for read-only and one for read-write
// * All textures allocated using comitted (dedicated) allocations
// * Texture creation and uploading "stops the world" and is very slow, no texture streaming.
// * Limited amount of samplers that are always available in the global root signature
// * Only power of two for read-only textures (because mipmaps)
// * Maybe can have a very limited selection of texture formats

struct GpuROTex; // Read-only


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
	const char* path;
	u32 num_defines;
	const char* const* defines;
};

sfz_extern_c GpuKernel gpuKernelInit(GpuLib* gpu, const GpuKernelDesc* desc);
sfz_extern_c void gpuKernelDestroy(GpuLib* gpu, GpuKernel kernel);

sfz_extern_c i32x3 gpuKernelGetGroupDims(const GpuLib* gpu, GpuKernel kernel);
inline i32x2 gpuKernelGetGroupDims2(const GpuLib* gpu, GpuKernel kernel)
{
	const i32x3 dims = gpuKernelGetGroupDims(gpu, kernel);
	sfz_assert(dims.z == 1);
	return i32x2_init(dims.x, dims.y);
}
inline i32 gpuKernelGetGroupDims1(const GpuLib* gpu, GpuKernel kernel)
{
	const i32x3 dims = gpuKernelGetGroupDims(gpu, kernel);
	sfz_assert(dims.y == 1 && dims.z == 1);
	return dims.x;
}


// Command API
// ------------------------------------------------------------------------------------------------

// The Command API is what you are primarily going to be using frame to frame. The functions are
// ordered in approximately the order you are expected to call them per-frame.

// Returns the index of the current command list. Increments every gpuSubmitQueuedWork().
sfz_extern_c u64 gpuGetCurrSubmitIdx(const GpuLib* gpu);

// Returns the current resolution of the swapchain (window) being rendered to.
sfz_extern_c i32x2 gpuSwapchainGetRes(const GpuLib* gpu);

// Returns the number of ticks per second (i.e. frequency) of the gpu timestamps.
sfz_extern_c u64 gpuTimestampGetFreq(const GpuLib* gpu);

// Takes a timestamp and stores it in the u64 pointed to in the global heap.
sfz_extern_c void gpuQueueTakeTimestamp(GpuLib* gpu, GpuPtr dst);

// Queues an upload to the GPU. Instantly copies input to upload heap, no need to keep src around.
sfz_extern_c void gpuQueueMemcpyUpload(GpuLib* gpu, GpuPtr dst, const void* src, u32 num_bytes);

sfz_struct(GpuTicket) {
	u32 handle;

#ifdef __cplusplus
	constexpr bool operator== (GpuTicket o) const { return handle == o.handle; }
	constexpr bool operator!= (GpuTicket o) const { return handle != o.handle; }
#endif
};
sfz_constant GpuTicket GPU_NULL_TICKET = {};

// Queues a download to the CPU. Downloading takes time, returns a ticket that can be used to
// retrieve the data in a later frame when it's ready.
sfz_extern_c GpuTicket gpuQueueMemcpyDownload(GpuLib* gpu, GpuPtr src, u32 num_bytes);

// Retrieves the data from a previously queued memcpy download.
sfz_extern_c void gpuGetDownloadedData(GpuLib* gpu, GpuTicket ticket, void* dst, u32 num_bytes);

// Queues a kernel dispatch
sfz_extern_c void gpuQueueDispatch(
	GpuLib* gpu, GpuKernel kernel, i32x3 num_groups, const void* params, u32 params_size);

// Queues the insertion of an unordered access barrier for the gpu heap. Not doing this is
// undefined behaviour if there are overlapping write-writes or read-writes (but not read-reads)
// between dispatches. If you are unsure, just insert one after each gpuQueueDispatch().
sfz_extern_c void gpuQueueGpuHeapBarrier(GpuLib* gpu);

// Queues the insertion of an unordered access barrier for a specific RWTex (or for all of them).
// Same rules applies as for gpu heap barriers, necessary for overlapping writes and read-writes,
// but not for overlapping reads.
sfz_extern_c void gpuQueueRWTexBarrier(GpuLib* gpu, GpuRWTex tex_idx);
sfz_extern_c void gpuQueueRWTexBarriers(GpuLib* gpu);

// Submits queued work to GPU and prepares to start recording more.
sfz_extern_c void gpuSubmitQueuedWork(GpuLib* gpu);

// Presents the latest swapchain image to the screen. Will block GPU and resize swapchain if
// resolution has changed.
sfz_extern_c void gpuSwapchainPresent(GpuLib* gpu, bool vsync);

// Flushes (blocks) until all currently submitted GPU work has finished executing.
sfz_extern_c void gpuFlush(GpuLib* gpu);


// C++ helpers
// ------------------------------------------------------------------------------------------------

// These are C++ wrapped variant of random parts of the API to make it nicer to use in C++.

#ifdef __cplusplus

template<typename T>
void gpuQueueMemcpyUpload(GpuLib* gpu, GpuPtr dst, const T& src_data)
{
	gpuQueueMemcpyUpload(gpu, dst, &src_data, sizeof(T));
}

template<typename T>
T gpuGetDownloadedData(GpuLib* gpu, GpuTicket ticket)
{
	T tmp = {};
	gpuGetDownloadedData(gpu, ticket, &tmp, sizeof(T));
	return tmp;
}

template<typename T>
void gpuQueueDispatch(GpuLib* gpu, GpuKernel kernel, i32x3 num_groups, const T& params)
{
	gpuQueueDispatch(gpu, kernel, num_groups, &params, sizeof(T));
}

template<typename T>
void gpuQueueDispatch(GpuLib* gpu, GpuKernel kernel, i32x2 num_groups, const T& params)
{
	gpuQueueDispatch<T>(gpu, kernel, i32x3_init2(num_groups, 1), params);
}

template<typename T>
void gpuQueueDispatch(GpuLib* gpu, GpuKernel kernel, i32 num_groups, const T& params)
{
	gpuQueueDispatch<T>(gpu, kernel, i32x3_init(num_groups, 1, 1), params);
}

#endif

#endif // GPU_LIB_H
