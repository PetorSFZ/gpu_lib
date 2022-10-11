#pragma once
#ifndef GPU_LIB_INTERNAL_HPP
#define GPU_LIB_INTERNAL_HPP

#include <gpu_lib.h>

#include <stdio.h>

#include <sfz_cpp.hpp>
#include <sfz_defer.hpp>
#include <skipifzero_pool.hpp>

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

// gpu_lib
// ------------------------------------------------------------------------------------------------

sfz_constant u32 GPU_NUM_CMD_LISTS = 3;

sfz_constant u32 GPU_ROOT_PARAM_GLOBAL_HEAP_IDX = 0;
sfz_constant u32 GPU_ROOT_PARAM_RW_TEX_ARRAY_IDX = 1;
sfz_constant u32 GPU_ROOT_PARAM_LAUNCH_PARAMS_IDX = 2;

sfz_constant u32 RWTEX_ARRAY_SWAPCHAIN_RT_IDX = 0;

sfz_struct(GpuCmdListInfo) {
	ComPtr<ID3D12GraphicsCommandList> cmd_list;
	ComPtr<ID3D12CommandAllocator> cmd_allocator;
	u64 fence_value;
};

sfz_struct(GpuKernelInfo) {
	ComPtr<ID3D12PipelineState> pso;
	ComPtr<ID3D12RootSignature> root_sig;
	i32x3 group_dims;
	u32 launch_params_size;
};

sfz_struct(GpuLib) {
	GpuLibInitCfg cfg;

	// Device
	ComPtr<IDXGIAdapter4> dxgi;
	ComPtr<ID3D12Device3> device;
	ComPtr<ID3D12InfoQueue> info_queue;

	// GPU Heap
	ComPtr<ID3D12Resource> gpu_heap;

	// RWTex descriptor heap
	ComPtr<ID3D12DescriptorHeap> tex_descriptor_heap;
	u32 num_tex_descriptors;
	u32 tex_descriptor_size;
	D3D12_CPU_DESCRIPTOR_HANDLE tex_descriptor_heap_start_cpu;
	D3D12_GPU_DESCRIPTOR_HANDLE tex_descriptor_heap_start_gpu;

	// Commands
	ComPtr<ID3D12CommandQueue> cmd_queue;
	ComPtr<ID3D12Fence> cmd_queue_fence;
	HANDLE cmd_queue_fence_event;
	u64 cmd_queue_fence_value;
	GpuCmdListInfo cmd_lists[GPU_NUM_CMD_LISTS];
	u32 curr_cmd_list;
	GpuCmdListInfo& getCurrCmdList() { return cmd_lists[curr_cmd_list]; }

	// DXC compiler
	ComPtr<IDxcUtils> dxc_utils; // Not thread-safe
	ComPtr<IDxcCompiler3> dxc_compiler; // Not thread-safe
	ComPtr<IDxcIncludeHandler> dxc_include_handler; // Not thread-safe

	// Kernels
	sfz::Pool<GpuKernelInfo> kernels;

	// Swapchain
	i32x2 swapchain_res;
	ComPtr<IDXGISwapChain4> swapchain;
	ComPtr<ID3D12Resource> swapchain_rt;
};

// Kernel prolog
// ------------------------------------------------------------------------------------------------

constexpr char GPU_KERNEL_PROLOG[] = R"(

typedef int i32;
typedef int2 i32x2;
typedef int3 i32x3;
typedef int4 i32x4;
typedef float4 f32x4;

RWByteAddressBuffer gpu_global_heap : register(u0);
RWTexture2D<f32x4> gpu_rwtex_array[] : register(u1);

)";

constexpr u32 GPU_KERNEL_PROLOG_SIZE = sizeof(GPU_KERNEL_PROLOG) - 1; // -1 because null-terminator

// Error handling
// ------------------------------------------------------------------------------------------------

inline const char* resToString(HRESULT res)
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

inline bool checkD3D12(const char* file, i32 line, HRESULT res)
{
	(void)file;
	(void)line;
	if (SUCCEEDED(res)) return true;
	printf("D3D12 error: %s\n", resToString(res));
	return false;
}

// Checks result (HRESULT) from D3D call and log if not success, returns true on success
#define CHECK_D3D12(res) checkD3D12(__FILE__, __LINE__, (res))

// String functions
// ------------------------------------------------------------------------------------------------

inline i32 utf8ToWide(wchar_t* wide_out, u32 num_wide_chars, const char* utf8_in)
{
	const i32 num_chars_written = MultiByteToWideChar(CP_UTF8, 0, utf8_in, -1, wide_out, num_wide_chars);
	return num_chars_written;
}

constexpr u32 WIDE_STR_MAX = 320;

sfz_struct(WideStr) {
	wchar_t str[WIDE_STR_MAX];
};

inline WideStr expandUtf8(const char* utf8)
{
	WideStr wide = {};
	const i32 num_wide_chars = utf8ToWide(wide.str, WIDE_STR_MAX, utf8);
	(void)num_wide_chars;
	return wide;
}

// IO functions
// ------------------------------------------------------------------------------------------------

inline WideStr getLastErrorStr()
{
	WideStr err_wide = {};
	FormatMessageW(
		FORMAT_MESSAGE_FROM_SYSTEM,
		nullptr,
		GetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err_wide.str, WIDE_STR_MAX, nullptr);
	return err_wide;
}

sfz_struct(FileMapData) {
	void* ptr;
	HANDLE h_file;
	HANDLE h_map;
	u64 size_bytes;
};

inline FileMapData fileMap(const char* path, bool read_only)
{
	FileMapData map_data = {};
	const WideStr path_w = expandUtf8(path);

	// Open file
	const DWORD fileAccess = GENERIC_READ | (read_only ? 0 : GENERIC_WRITE);
	const DWORD shareMode = FILE_SHARE_READ; // Other processes shouldn't write to our file
	const DWORD flagsAndAttribs = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN;
	map_data.h_file = CreateFileW(
		path_w.str, fileAccess, shareMode, nullptr, OPEN_EXISTING, flagsAndAttribs, nullptr);
	if (map_data.h_file == INVALID_HANDLE_VALUE) {
		WideStr errWide = getLastErrorStr();
		printf("Failed to open file (\"%s\"), reason: %S\n", path, errWide.str);
		return FileMapData{};
	}

	// Get file info
	BY_HANDLE_FILE_INFORMATION fileInfo = {};
	const BOOL fileInfoRes = GetFileInformationByHandle(map_data.h_file, &fileInfo);
	if (!fileInfoRes) {
		WideStr errWide = getLastErrorStr();
		printf("Failed to get file info for (\"%s\"), reason: %S\n", path, errWide.str);
		CloseHandle(map_data.h_file);
		return FileMapData{};
	}
	map_data.size_bytes =
		(u64(fileInfo.nFileSizeHigh) * u64(MAXDWORD + 1)) + u64(fileInfo.nFileSizeLow);

	// Create file mapping object
	map_data.h_map = CreateFileMappingA(
		map_data.h_file, nullptr, read_only ? PAGE_READONLY : PAGE_READWRITE, 0, 0, nullptr);
	if (map_data.h_map == INVALID_HANDLE_VALUE) {
		WideStr errWide = getLastErrorStr();
		printf("Failed to create file mapping object for (\"%s\"), reason: %S\n", path, errWide.str);
		CloseHandle(map_data.h_file);
		return FileMapData{};
	}

	// Map file
	map_data.ptr = MapViewOfFile(map_data.h_map, read_only ? FILE_MAP_READ : FILE_MAP_ALL_ACCESS, 0, 0, 0);
	if (map_data.ptr == nullptr) {
		WideStr errWide = getLastErrorStr();
		printf("Failed to map (\"%s\"), reason: %S\n", path, errWide.str);
		CloseHandle(map_data.h_map);
		CloseHandle(map_data.h_file);
		return FileMapData{};
	}

	return map_data;
}

inline void fileUnmap(FileMapData map_data)
{
	if (map_data.ptr == nullptr) return;
	if (!CloseHandle(map_data.h_map)) {
		WideStr errWide = getLastErrorStr();
		printf("Failed to CloseHandle(), reason: %S\n", errWide.str);
	}
	if (!CloseHandle(map_data.h_file)) {
		WideStr errWide = getLastErrorStr();
		printf("Failed to CloseHandle(), reason: %S\n", errWide.str);
	}
}

#endif // GPU_LIB_INTERNAL_HPP
