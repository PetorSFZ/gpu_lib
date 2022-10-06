#include "gpu_lib.h"

#include <sfz_cpp.hpp>

// Windows.h
#pragma warning(push, 0)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wrl.h> // ComPtr
//#include <Winerror.h>
#pragma warning(pop)

// D3D12 headers
#include <D3D12AgilitySDK/d3d12.h>
#pragma comment (lib, "d3d12.lib")
#include <dxgi1_6.h>
#pragma comment (lib, "dxgi.lib")
//#pragma comment (lib, "dxguid.lib")
#include <D3D12AgilitySDK/d3d12shader.h>

// DXC compiler
#include <dxc/dxcapi.h>

// D3D12 Agility SDK exports
// ------------------------------------------------------------------------------------------------

// Note: It seems this is not enough and must also be in the exe file of the application using
//       ZeroG. A bit annoying, but don't have a good solution to it for now.

// The version of the Agility SDK we are using, see https://devblogs.microsoft.com/directx/directx12agility/
extern "C" { _declspec(dllexport) extern const u32 D3D12SDKVersion = 606; }

// Specifies that D3D12Core.dll will be available in a directory called D3D12 next to the exe.
extern "C" { _declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

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
