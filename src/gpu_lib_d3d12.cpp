#include "gpu_lib.h"

#include <stdio.h>

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

using Microsoft::WRL::ComPtr;

// D3D12 Agility SDK exports
// ------------------------------------------------------------------------------------------------

// The version of the Agility SDK we are using, see https://devblogs.microsoft.com/directx/directx12agility/
extern "C" { _declspec(dllexport) extern const u32 D3D12SDKVersion = 606; }

// Specifies that D3D12Core.dll will be available in a directory called D3D12 next to the exe.
extern "C" { _declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

// Error handling
// ------------------------------------------------------------------------------------------------

static const char* resToString(HRESULT res)
{
	switch (res) {
	case DXGI_ERROR_ACCESS_DENIED: return "DXGI_ERROR_ACCESS_DENIED";
	case DXGI_ERROR_ACCESS_LOST: return "DXGI_ERROR_ACCESS_LOST";
	case DXGI_ERROR_ALREADY_EXISTS: return "DXGI_ERROR_ALREADY_EXISTS";
	case DXGI_ERROR_CANNOT_PROTECT_CONTENT: return "DXGI_ERROR_CANNOT_PROTECT_CONTENT";
	case DXGI_ERROR_DEVICE_HUNG: return "DXGI_ERROR_DEVICE_HUNG";
	case DXGI_ERROR_DEVICE_REMOVED: return "DXGI_ERROR_DEVICE_REMOVED";
	case DXGI_ERROR_DEVICE_RESET: return "DXGI_ERROR_DEVICE_RESET";
	case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
	case DXGI_ERROR_FRAME_STATISTICS_DISJOINT: return "DXGI_ERROR_FRAME_STATISTICS_DISJOINT";
	case DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE: return "DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE";
	case DXGI_ERROR_INVALID_CALL: return "DXGI_ERROR_INVALID_CALL";
	case DXGI_ERROR_MORE_DATA: return "DXGI_ERROR_MORE_DATA";
	case DXGI_ERROR_NAME_ALREADY_EXISTS: return "DXGI_ERROR_NAME_ALREADY_EXISTS";
	case DXGI_ERROR_NONEXCLUSIVE: return "DXGI_ERROR_NONEXCLUSIVE";
	case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE: return "DXGI_ERROR_NOT_CURRENTLY_AVAILABLE";
	case DXGI_ERROR_NOT_FOUND: return "DXGI_ERROR_NOT_FOUND";
	case DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED: return "DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED";
	case DXGI_ERROR_REMOTE_OUTOFMEMORY: return "DXGI_ERROR_REMOTE_OUTOFMEMORY";
	case DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE: return "DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE";
	case DXGI_ERROR_SDK_COMPONENT_MISSING: return "DXGI_ERROR_SDK_COMPONENT_MISSING";
	case DXGI_ERROR_SESSION_DISCONNECTED: return "DXGI_ERROR_SESSION_DISCONNECTED";
	case DXGI_ERROR_UNSUPPORTED: return "DXGI_ERROR_UNSUPPORTED";
	case DXGI_ERROR_WAIT_TIMEOUT: return "DXGI_ERROR_WAIT_TIMEOUT";
	case DXGI_ERROR_WAS_STILL_DRAWING: return "DXGI_ERROR_WAS_STILL_DRAWING";

	case S_OK: return "S_OK";
	case E_NOTIMPL: return "E_NOTIMPL";
	case E_NOINTERFACE: return "E_NOINTERFACE";
	case E_POINTER: return "E_POINTER";
	case E_ABORT: return "E_ABORT";
	case E_FAIL: return "E_FAIL";
	case E_UNEXPECTED: return "E_UNEXPECTED";
	case E_ACCESSDENIED: return "E_ACCESSDENIED";
	case E_HANDLE: return "E_HANDLE";
	case E_OUTOFMEMORY: return "E_OUTOFMEMORY";
	case E_INVALIDARG: return "E_INVALIDARG";
	case S_FALSE: return "S_FALSE";
	}
	return "UNKNOWN";
}

static bool checkD3D12(const char* file, i32 line, HRESULT res)
{
	(void)file;
	(void)line;
	if (SUCCEEDED(res)) return true;
	printf("D3D12 error: %s\n", resToString(res));
	return false;
}

// Checks result (HRESULT) from D3D call and log if not success, returns true on success
#define CHECK_D3D12(res) checkD3D12(__FILE__, __LINE__, (res))

/*static i32 utf8ToWide(wchar_t* wideOut, u32 numWideChars, const char* utf8In)
{
	const i32 numCharsWritten = MultiByteToWideChar(CP_UTF8, 0, utf8In, -1, wideOut, numWideChars);
	return numCharsWritten;
}*/

// Debug messages
// ------------------------------------------------------------------------------------------------

static void logDebugMessages(ID3D12InfoQueue* info_queue)
{
	if (info_queue == nullptr) return;

	constexpr u32 MAX_MSG_LEN = 512;
	u8 msg_raw[MAX_MSG_LEN];
	D3D12_MESSAGE* msg = reinterpret_cast<D3D12_MESSAGE*>(msg_raw);

	const u64 num_messages = info_queue->GetNumStoredMessages();
	for (u64 i = 0; i < num_messages; i++) {

		// Get the size of the message
		SIZE_T msg_len = 0;
		CHECK_D3D12(info_queue->GetMessage(0, NULL, &msg_len));
		if (MAX_MSG_LEN < msg_len) {
			sfz_assert(false);
			printf("[gpu_lib]: Message too long, skipping\n");
			continue;
		}

		// Get and print message
		memset(msg, 0, MAX_MSG_LEN);
		CHECK_D3D12(info_queue->GetMessageA(0, msg, &msg_len));
		printf("[gpu_lib]: D3D12 message: %s\n", msg->pDescription);
	}

	// Clear stored messages
	info_queue->ClearStoredMessages();
}


// gpu_lib
// ------------------------------------------------------------------------------------------------

sfz_struct(GpuLib) {
	GpuLibInitCfg cfg;

	// Device
	ComPtr<IDXGIAdapter4> dxgi;
	ComPtr<ID3D12Device3> device;
	ComPtr<ID3D12InfoQueue> info_queue;

	// GPU Heap
	ComPtr<ID3D12Resource> gpu_heap;
};

// Init API
// ------------------------------------------------------------------------------------------------

sfz_extern_c GpuLib* gpuLibInit(const GpuLibInitCfg* cfg)
{
	// Enable debug layers in debug mode
	if (cfg->debug_mode) {

		// Get debug interface
		ComPtr<ID3D12Debug1> debug_interface;
		if (!CHECK_D3D12(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_interface)))) {
			return nullptr;;
		}

		// Enable debug layer and GPU based validation
		debug_interface->EnableDebugLayer();

		// Enable GPU based debug mode if requested
		if (cfg->debug_mode_gpu_validation) {
			debug_interface->SetEnableGPUBasedValidation(TRUE);
		}
	}

	// Create DXGI factory
	ComPtr<IDXGIFactory6> dxgi_factory;
	{
		UINT flags = 0;
		if (cfg->debug_mode) flags |= DXGI_CREATE_FACTORY_DEBUG;
		if (!CHECK_D3D12(CreateDXGIFactory2(flags, IID_PPV_ARGS(&dxgi_factory)))) {
			return nullptr;
		}
	}

	// Create device
	ComPtr<IDXGIAdapter4> dxgi;
	ComPtr<ID3D12Device3> device;
	{
		const bool adapter_success = CHECK_D3D12(dxgi_factory->EnumAdapterByGpuPreference(
			0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&dxgi)));
		if (!adapter_success) return nullptr;

		DXGI_ADAPTER_DESC1 dxgi_desc = {};
		CHECK_D3D12(dxgi->GetDesc1(&dxgi_desc));
		printf("[gpu_lib]: Using adapter: %S\n", dxgi_desc.Description);

		const bool device_success = CHECK_D3D12(D3D12CreateDevice(
			dxgi.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));
		if (!device_success) return nullptr;
	}

	// Enable debug message in debug mode
	ComPtr<ID3D12InfoQueue> info_queue;
	if (cfg->debug_mode) {

		if (!CHECK_D3D12(device->QueryInterface(IID_PPV_ARGS(&info_queue)))) {
			return nullptr;
		}

		// Break on corruption and error messages
		CHECK_D3D12(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE));
		CHECK_D3D12(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE));

		// Log initial messages
		logDebugMessages(info_queue.Get());
	}

	// Allocate our gpu heap
	ComPtr<ID3D12Resource> gpu_heap;
	{
		D3D12_HEAP_PROPERTIES heap_props = {};
		heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
		heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heap_props.CreationNodeMask = 0;
		heap_props.VisibleNodeMask = 0;

		const D3D12_HEAP_FLAGS heap_flags =
			D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS |
			D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Alignment = 0;
		desc.Width = cfg->gpu_heap_size_bytes;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		const D3D12_RESOURCE_STATES initial_res_state = D3D12_RESOURCE_STATE_COMMON;

		const bool heap_success = CHECK_D3D12(device->CreateCommittedResource(
			&heap_props, heap_flags, &desc, initial_res_state, nullptr, IID_PPV_ARGS(&gpu_heap)));
		if (!heap_success) {
			printf("[gpu_lib]: Could not allocate gpu heap of size %.2f MiB, exiting.",
				f32(cfg->gpu_heap_size_bytes) / (1024.0f * 1024.0f));
			return nullptr;
		}
	}

	GpuLib* gpu = sfz_new<GpuLib>(cfg->cpu_allocator, sfz_dbg("GpuLib"));
	*gpu = {};
	gpu->cfg = *cfg;

	gpu->dxgi = dxgi;
	gpu->device = device;
	gpu->info_queue = info_queue;

	gpu->gpu_heap = gpu_heap;

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
