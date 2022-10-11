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

sfz_constant u32 GPU_MALLOC_ALIGN = 64;

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
	u32 gpu_heap_next_free;

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

// Kernel prolog
// ------------------------------------------------------------------------------------------------

constexpr char GPU_KERNEL_PROLOG[] = R"(

// Macros
// ------------------------------------------------------------------------------------------------

#define SFZ_HLSL

#define sfz_struct(name) struct name
#define sfz_constant static const
#define sfz_static_assert(cond) _Static_assert(cond, #cond)
#define sfz_constexpr_func


// Scalar primitives
// ------------------------------------------------------------------------------------------------

typedef int16_t i16;
typedef int i32;
typedef int64_t i64;

typedef uint16_t u16;
typedef uint u32;
typedef uint64_t u64;

typedef float16_t f16;
typedef float f32;

sfz_constant i16 I16_MIN = -32768;
sfz_constant i16 I16_MAX = 32767;
sfz_constant i32 I32_MIN = -2147483647 - 1;
sfz_constant i32 I32_MAX = 2147483647;
sfz_constant i64 I64_MIN = -9223372036854775807 - 1;
sfz_constant i64 I64_MAX = 9223372036854775807;

sfz_constant u16 U16_MAX = 0xFFFF;
sfz_constant u32 U32_MAX = 0xFFFFFFFF;
sfz_constant u64 U64_MAX = 0xFFFFFFFFFFFFFFFF;

sfz_constant f32 F32_MAX = 3.402823466e+38f;

sfz_constant f32 SFZ_PI = 3.14159265358979323846f;
sfz_constant f32 SFZ_DEG_TO_RAD = SFZ_PI / 180.0f;
sfz_constant f32 SFZ_RAD_TO_DEG = 180.0f / SFZ_PI;


// Vector primitives
// ------------------------------------------------------------------------------------------------

typedef float16_t2 f16x2;
typedef float16_t3 f16x3;
typedef float16_t4 f16x4;

f16x2 f16x2_init(f16 x, f16 y) { return f16x2(x, y); }
f16x3 f16x3_init(f16 x, f16 y, f16 z) { return f16x3(x, y, z); }
f16x4 f16x4_init(f16 x, f16 y, f16 z, f16 w) { return f16x4(x, y, z, w); }

f16x2 f16x2_splat(f16 v) { return f16x2(v, v); }
f16x3 f16x3_splat(f16 v) { return f16x3(v, v, v); }
f16x4 f16x4_splat(f16 v) { return f16x4(v, v, v, v); }

typedef float2 f32x2;
typedef float3 f32x3;
typedef float4 f32x4;

f32x2 f32x2_init(f32 x, f32 y) { return f32x2(x, y); }
f32x3 f32x3_init(f32 x, f32 y, f32 z) { return f32x3(x, y, z); }
f32x4 f32x4_init(f32 x, f32 y, f32 z, f32 w) { return f32x4(x, y, z, w); }

f32x2 f32x2_splat(f32 v) { return f32x2(v, v); }
f32x3 f32x3_splat(f32 v) { return f32x3(v, v, v); }
f32x4 f32x4_splat(f32 v) { return f32x4(v, v, v, v); }

typedef int16_t2 i16x2;
typedef int16_t3 i16x3;
typedef int16_t4 i16x4;

i16x2 i16x2_init(i16 x, i16 y) { return i16x2(x, y); }
i16x3 i16x3_init(i16 x, i16 y, i16 z) { return i16x3(x, y, z); }
i16x4 i16x4_init(i16 x, i16 y, i16 z, i16 w) { return i16x4(x, y, z, w); }

i16x2 i16x2_splat(i16 v) { return i16x2(v, v); }
i16x3 i16x3_splat(i16 v) { return i16x3(v, v, v); }
i16x4 i16x4_splat(i16 v) { return i16x4(v, v, v, v); }

typedef int2 i32x2;
typedef int3 i32x3;
typedef int4 i32x4;

i32x2 i32x2_init(i32 x, i32 y) { return i32x2(x, y); }
i32x3 i32x3_init(i32 x, i32 y, i32 z) { return i32x3(x, y, z); }
i32x4 i32x4_init(i32 x, i32 y, i32 z, i32 w) { return i32x4(x, y, z, w); }

i32x2 i32x2_splat(i32 v) { return i32x2(v, v); }
i32x3 i32x3_splat(i32 v) { return i32x3(v, v, v); }
i32x4 i32x4_splat(i32 v) { return i32x4(v, v, v, v); }

typedef uint16_t2 u16x2;
typedef uint16_t3 u16x3;
typedef uint16_t4 u16x4;

u16x2 u16x2_init(u16 x, u16 y) { return u16x2(x, y); }
u16x3 u16x3_init(u16 x, u16 y, u16 z) { return u16x3(x, y, z); }
u16x4 u16x4_init(u16 x, u16 y, u16 z, u16 w) { return u16x4(x, y, z, w); }

u16x2 u16x2_splat(u16 v) { return u16x2(v, v); }
u16x3 u16x3_splat(u16 v) { return u16x3(v, v, v); }
u16x4 u16x4_splat(u16 v) { return u16x4(v, v, v, v); }

typedef uint2 u32x2;
typedef uint3 u32x3;
typedef uint4 u32x4;

u32x2 u32x2_init(u32 x, u32 y) { return u32x2(x, y); }
u32x3 u32x3_init(u32 x, u32 y, u32 z) { return u32x3(x, y, z); }
u32x4 u32x4_init(u32 x, u32 y, u32 z, u32 w) { return u32x4(x, y, z, w); }

u32x2 u32x2_splat(u32 v) { return u32x2(v, v); }
u32x3 u32x3_splat(u32 v) { return u32x3(v, v, v); }
u32x4 u32x4_splat(u32 v) { return u32x4(v, v, v, v); }


// Math functions
// ------------------------------------------------------------------------------------------------

f32 f32x2_dot(f32x2 l, f32x2 r) { return dot(l, r); }
f32 f32x3_dot(f32x3 l, f32x3 r) { return dot(l, r); }
f32 f32x4_dot(f32x4 l, f32x4 r) { return dot(l, r); }
i32 i32x2_dot(i32x2 l, i32x2 r) { return dot(l, r); }
i32 i32x3_dot(i32x3 l, i32x3 r) { return dot(l, r); }
i32 i32x4_dot(i32x4 l, i32x4 r) { return dot(l, r); }

f32x3 f32x3_cross(f32x3 l, f32x3 r) { return cross(l, r); }
i32x3 i32x3_cross(i32x3 l, i32x3 r) { return cross(l, r); }

f32 f32x2_length(f32x2 v) { return length(v); }
f32 f32x3_length(f32x3 v) { return length(v); }
f32 f32x4_length(f32x4 v) { return length(v); }

f32x2 f32x2_normalize(f32x2 v) { return normalize(v); }
f32x3 f32x3_normalize(f32x3 v) { return normalize(v); }
f32x4 f32x4_normalize(f32x4 v) { return normalize(v); }

i16 i16_abs(i16 v) { return abs(v); }
i32 i32_abs(i32 v) { return abs(v); }
i64 i64_abs(i64 v) { return abs(v); }
f32 f32_abs(f32 v) { return abs(v); }
f32x2 f32x2_abs(f32x2 v) { return abs(v); }
f32x3 f32x3_abs(f32x3 v) { return abs(v); }
f32x4 f32x4_abs(f32x4 v) { return abs(v); }
i32x2 i32x2_abs(i32x2 v) { return abs(v); }
i32x3 i32x3_abs(i32x3 v) { return abs(v); }
i32x4 i32x4_abs(i32x4 v) { return abs(v); }

i16 i16_min(i16 l, i16 r) { return min(l, r); }
i32 i32_min(i32 l, i32 r) { return min(l, r); }
i64 i64_min(i64 l, i64 r) { return min(l, r); }
u16 u16_min(u16 l, u16 r) { return min(l, r); }
u32 u32_min(u32 l, u32 r) { return min(l, r); }
u64 u64_min(u64 l, u64 r) { return min(l, r); }
f32 f32_min(f32 l, f32 r) { return min(l, r); }
f32x2 f32x2_min(f32x2 l, f32x2 r) { return min(l, r); }
f32x3 f32x3_min(f32x3 l, f32x3 r) { return min(l, r); }
f32x4 f32x4_min(f32x4 l, f32x4 r) { return min(l, r); }
i32x2 i32x2_min(i32x2 l, i32x2 r) { return min(l, r); }
i32x3 i32x3_min(i32x3 l, i32x3 r) { return min(l, r); }
i32x4 i32x4_min(i32x4 l, i32x4 r) { return min(l, r); }

i16 i16_max(i16 l, i16 r) { return max(l, r); }
i32 i32_max(i32 l, i32 r) { return max(l, r); }
i64 i64_max(i64 l, i64 r) { return max(l, r); }
u16 u16_max(u16 l, u16 r) { return max(l, r); }
u32 u32_max(u32 l, u32 r) { return max(l, r); }
u64 u64_max(u64 l, u64 r) { return max(l, r); }
f32 f32_max(f32 l, f32 r) { return max(l, r); }
f32x2 f32x2_max(f32x2 l, f32x2 r) { return max(l, r); }
f32x3 f32x3_max(f32x3 l, f32x3 r) { return max(l, r); }
f32x4 f32x4_max(f32x4 l, f32x4 r) { return max(l, r); }
i32x2 i32x2_max(i32x2 l, i32x2 r) { return max(l, r); }
i32x3 i32x3_max(i32x3 l, i32x3 r) { return max(l, r); }
i32x4 i32x4_max(i32x4 l, i32x4 r) { return max(l, r); }

i32 i32_clamp(i32 v, i32 minVal, i32 maxVal) { return clamp(v, minVal, maxVal); }
u32 u32_clamp(u32 v, u32 minVal, u32 maxVal) { return clamp(v, minVal, maxVal); }
f32 f32_clamp(f32 v, f32 minVal, f32 maxVal) { return clamp(v, minVal, maxVal); }
f32x2 f32x2_clampv(f32x2 v, f32x2 minVal, f32x2 maxVal) { return clamp(v, minVal, maxVal); }
f32x2 f32x2_clamps(f32x2 v, f32 minVal, f32 maxVal) { return clamp(v, minVal, maxVal); }
f32x3 f32x3_clampv(f32x3 v, f32x3 minVal, f32x3 maxVal) { return clamp(v, minVal, maxVal); }
f32x3 f32x3_clamps(f32x3 v, f32 minVal, f32 maxVal) { return clamp(v, minVal, maxVal); }
f32x4 f32x4_clampv(f32x4 v, f32x4 minVal, f32x4 maxVal) { return clamp(v, minVal, maxVal); }
f32x4 f32x4_clamps(f32x4 v, f32 minVal, f32 maxVal) { return clamp(v, minVal, maxVal); }
i32x2 i32x2_clampv(i32x2 v, i32x2 minVal, i32x2 maxVal) { return clamp(v, minVal, maxVal); }
i32x2 i32x2_clamps(i32x2 v, i32 minVal, i32 maxVal) { return clamp(v, minVal, maxVal); }
i32x3 i32x3_clampv(i32x3 v, i32x3 minVal, i32x3 maxVal) { return clamp(v, minVal, maxVal); }
i32x3 i32x3_clamps(i32x3 v, i32 minVal, i32 maxVal) { return clamp(v, minVal, maxVal); }
i32x4 i32x4_clampv(i32x4 v, i32x4 minVal, i32x4 maxVal) { return clamp(v, minVal, maxVal); }
i32x4 i32x4_clamps(i32x4 v, i32 minVal, i32 maxVal) { return clamp(v, minVal, maxVal); }

f32x2 f32x2_from_i32(i32x2 o) { return f32x2(o); }
f32x3 f32x3_from_i32(i32x3 o) { return f32x3(o); }
f32x4 f32x4_from_i32(i32x4 o) { return f32x4(o); }

i32x2 i32x2_from_f32(f32x2 o) { return i32x2(o); }
i32x3 i32x3_from_f32(f32x3 o) { return i32x3(o);; }
i32x4 i32x4_from_f32(f32x4 o) { return i32x4(o); }

f32 f32_floor(f32 v) { return floor(v); }
f32x2 f32x2_floor(f32x2 v) { return floor(v); }
f32x3 f32x3_floor(f32x3 v) { return floor(v); }
f32x4 f32x4_floor(f32x4 v) { return floor(v); }


// Matrix types
// ------------------------------------------------------------------------------------------------

// Note: These ones are potentially a bit dangerous, especially SfzMat33. They will absolutely not
//       have the same alignment requirements in HLSL vs C++, and the 3x3 might not even have the
//       same size, unsure.

typedef row_major float3x3 SfzMat33;
typedef row_major float4x4 SfzMat44;

f32x3 sfzMat44TransformPoint(SfzMat44 m, f32x3 p)
{
	const f32x4 tmp = mul(m, f32x4(p, 1.0f));
	return tmp.xyz / tmp.w;
}

f32x3 sfzMat44TransformDir(SfzMat44 m, f32x3 d)
{
	return mul((row_major float3x3)m, d);
	//return mul(m, f32x4(d, 0.0f)).xyz;
}

SfzMat44 sfzMat44Transpose(SfzMat44 m) { return transpose(m); }


// Assert macro
// ------------------------------------------------------------------------------------------------

// There is (as far as I know) no regular asserts in HLSL, so just skip them.

#define sfz_assert(cond) do { (void)sizeof(cond); } while(0)
#define sfz_assert_hard(cond) do { (void)sizeof(cond); } while(0)


// Root signature
// ------------------------------------------------------------------------------------------------

RWByteAddressBuffer gpu_global_heap : register(u0);
RWTexture2D<f32x4> gpu_rwtex_array[] : register(u1);


// Pointer type
// ------------------------------------------------------------------------------------------------

struct GpuPtr {
	u32 address;

	u32 loadByte()
	{
		const u32 word_address = address & 0xFFFFFFFC;
		const u32 word = gpu_global_heap.Load<u32>(word_address);
		const u32 byte_address = address & 0x00000003;
		const u32 byte_shift = byte_address * 8;
		const u32 byte = (word >> byte_shift) & 0x000000FF;
		return byte;
	}

	template<typename T>
	T load() { return gpu_global_heap.Load<T>(address); }

	template<typename T>
	T loadArrayElem(u32 idx) { return gpu_global_heap.Load<T>(address + idx * sizeof(T)); }

	template<typename T>
	void store(T val) { gpu_global_heap.Store<T>(address, val); }

	template<typename T>
	void storeArrayElem(T val, u32 idx) { gpu_global_heap.Store<T>(address + idx * sizeof(T), val); }
};

)";

constexpr u32 GPU_KERNEL_PROLOG_SIZE = sizeof(GPU_KERNEL_PROLOG) - 1; // -1 because null-terminator

#endif // GPU_LIB_INTERNAL_HPP
