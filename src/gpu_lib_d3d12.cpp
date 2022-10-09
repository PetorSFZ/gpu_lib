#include "gpu_lib.h"

#include <stdio.h>

#include <sfz_cpp.hpp>
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

// String functions
// ------------------------------------------------------------------------------------------------

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

sfz_constant u32 GPU_NUM_CMD_LISTS = 3;

sfz_constant u32 GPU_ROOT_PARAM_GLOBAL_HEAP_IDX = 0;
sfz_constant u32 GPU_ROOT_PARAM_LAUNCH_PARAMS_IDX = 1;

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

sfz_struct(GpuSwapchainFB) {
	ComPtr<ID3D12DescriptorHeap> descriptor_heap_rtv;
	D3D12_CPU_DESCRIPTOR_HANDLE rtv_descriptor;
};

sfz_struct(GpuLib) {
	GpuLibInitCfg cfg;

	// Device
	ComPtr<IDXGIAdapter4> dxgi;
	ComPtr<ID3D12Device3> device;
	ComPtr<ID3D12InfoQueue> info_queue;

	// GPU Heap
	ComPtr<ID3D12Resource> gpu_heap;

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
	ComPtr<IDXGISwapChain4> swapchain;
	GpuSwapchainFB swapchain_fbs[GPU_NUM_CMD_LISTS];
};

// Init API
// ------------------------------------------------------------------------------------------------

sfz_extern_c GpuLib* gpuLibInit(const GpuLibInitCfg* cfgIn)
{
	// Copy config so that we can make changes to it before finally storing it in the context
	GpuLibInitCfg cfg = *cfgIn;
	cfg.gpu_heap_size_bytes = u32_clamp(cfg.gpu_heap_size_bytes, GPU_HEAP_MIN_SIZE, GPU_HEAP_MAX_SIZE);

	// Enable debug layers in debug mode
	if (cfg.debug_mode) {

		// Get debug interface
		ComPtr<ID3D12Debug1> debug_interface;
		if (!CHECK_D3D12(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_interface)))) {
			return nullptr;
		}

		// Enable debug layer and GPU based validation
		debug_interface->EnableDebugLayer();

		// Enable GPU based debug mode if requested
		if (cfg.debug_shader_validation) {
			debug_interface->SetEnableGPUBasedValidation(TRUE);
		}
	}

	// Create DXGI factory
	ComPtr<IDXGIFactory6> dxgi_factory;
	{
		UINT flags = 0;
		if (cfg.debug_mode) flags |= DXGI_CREATE_FACTORY_DEBUG;
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
	if (cfg.debug_mode) {

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
		desc.Width = cfg.gpu_heap_size_bytes;
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
				f32(cfg.gpu_heap_size_bytes) / (1024.0f * 1024.0f));
			return nullptr;
		}
	}

	// Create command queue
	ComPtr<ID3D12CommandQueue> cmd_queue;
	ComPtr<ID3D12Fence> cmd_queue_fence;
	HANDLE cmd_queue_fence_event = nullptr;
	{
		D3D12_COMMAND_QUEUE_DESC queue_desc = {};
		queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
		queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE; // D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT
		queue_desc.NodeMask = 0;
		if (!CHECK_D3D12(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&cmd_queue)))) {
			printf("[gpu_lib]: Could not create command queue.\n");
			return nullptr;
		}

		if (!CHECK_D3D12(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&cmd_queue_fence)))) {
			printf("[gpu_lib]: Could not create command queue fence.\n");
			return nullptr;
		}

		cmd_queue_fence_event = CreateEventA(NULL, false, false, "gpu_lib_cmd_queue_fence_event");
	}

	// Create command lists
	GpuCmdListInfo cmd_lists[GPU_NUM_CMD_LISTS];
	for (u32 i = 0; i < GPU_NUM_CMD_LISTS; i++) {
		GpuCmdListInfo& info = cmd_lists[i];

		if (!CHECK_D3D12(device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&info.cmd_allocator)))) {
			printf("[gpu_lib]: Could not create command allocator.\n");
			return nullptr;
		}

		if (!CHECK_D3D12(device->CreateCommandList(
			0,
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			info.cmd_allocator.Get(),
			nullptr,
			IID_PPV_ARGS(&info.cmd_list)))) {

			printf("[gpu_lib]: Could not create command list.\n");
			return nullptr;
		}

		// Close the non active command lists
		if (i != 0) {
			if (!CHECK_D3D12(info.cmd_list->Close())) {
				printf("[gpu_lib]: Could not close command list after creation.\n");
				return nullptr;
			}
		}

		info.fence_value = 0;
	}

	// Load DXC compiler
	ComPtr<IDxcUtils> dxc_utils;
	ComPtr<IDxcCompiler3> dxc_compiler;
	ComPtr<IDxcIncludeHandler> dxc_include_handler;
	{
		if (!CHECK_D3D12(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxc_utils)))) {
			printf("[gpu_lib]: Could not initialize DXC utils.");
			return nullptr;
		}

		if (!CHECK_D3D12(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxc_compiler)))) {
			printf("[gpu_lib]: Could not initialize DXC compiler.");
			return nullptr;
		}

		if (!CHECK_D3D12(dxc_utils->CreateDefaultIncludeHandler(&dxc_include_handler))) {
			printf("[gpu_lib]: Could not create DXC include handler.");
			return nullptr;
		}
	}

	// If we have a window handle specified create swapchain and such
	ComPtr<IDXGISwapChain4> swapchain;
	GpuSwapchainFB swapchain_fbs[GPU_NUM_CMD_LISTS] = {};
	if (cfg.native_window_handle != nullptr) {
		const HWND hwnd = static_cast<const HWND>(cfg.native_window_handle);

		// Check if screen-tearing is allowed
		{
			BOOL tearing_allowed = FALSE;
			CHECK_D3D12(dxgi_factory->CheckFeatureSupport(
				DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing_allowed, sizeof(tearing_allowed)));
			cfg.allow_tearing = tearing_allowed != FALSE;
		}

		// Create swap chain
		{
			DXGI_SWAP_CHAIN_DESC1 desc = {};
			// Dummy initial res, will allocate framebuffers for real at first use.
			desc.Width = 4;
			desc.Height = 4;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.Stereo = FALSE;
			desc.SampleDesc = { 1, 0 }; // No MSAA
			desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // DXGI_USAGE_UNORDERED_ACCESS
			desc.BufferCount = GPU_NUM_CMD_LISTS;
			desc.Scaling = DXGI_SCALING_STRETCH;
			desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
			desc.Flags = (cfg.allow_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);

			ComPtr<IDXGISwapChain1> tmp_swapchain;
			if (!CHECK_D3D12(dxgi_factory->CreateSwapChainForHwnd(
				cmd_queue.Get(), hwnd, &desc, nullptr, nullptr, &tmp_swapchain))) {
				printf("[gpu_lib]: Could not create swapchain.");
				return nullptr;
			}
			if (!CHECK_D3D12(tmp_swapchain.As(&swapchain))) {
				printf("[gpu_lib]: Could not create swapchain.");
				return nullptr;
			}
		}

		// Disable Alt+Enter to fullscreen
		//
		// This fixes issues with DXGI_PRESENT_ALLOW_TEARING, which is required for Adaptive Sync
		// to work correctly with windowed applications. The default Alt+Enter shortcut enters
		// "true" fullscreen (same as calling SetFullscreenState(TRUE)), which is not what we want
		// if we only want to support e.g. borderless fullscreen.
		CHECK_D3D12(dxgi_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

		// Create swapchain descriptor heaps
		for (u32 i = 0; i < GPU_NUM_CMD_LISTS; i++) {
			GpuSwapchainFB& fb = swapchain_fbs[i];

			// Create render target descriptor heap
			D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
			rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			rtv_heap_desc.NumDescriptors = 1;
			rtv_heap_desc.NodeMask = 0;
			if (!CHECK_D3D12(device->CreateDescriptorHeap(
				&rtv_heap_desc, IID_PPV_ARGS(&fb.descriptor_heap_rtv)))) {
				printf("[gpu_lib]: Could not create swapchain RTV descriptor heap.");
				return nullptr;
			}
			fb.rtv_descriptor = fb.descriptor_heap_rtv->GetCPUDescriptorHandleForHeapStart();
		}
	}

	GpuLib* gpu = sfz_new<GpuLib>(cfg.cpu_allocator, sfz_dbg("GpuLib"));
	*gpu = {};
	gpu->cfg = cfg;

	gpu->dxgi = dxgi;
	gpu->device = device;
	gpu->info_queue = info_queue;

	gpu->gpu_heap = gpu_heap;

	gpu->cmd_queue = cmd_queue;
	gpu->cmd_queue_fence = cmd_queue_fence;
	gpu->cmd_queue_fence_event = cmd_queue_fence_event;
	gpu->cmd_queue_fence_value = 0;
	for (u32 i = 0; i < GPU_NUM_CMD_LISTS; i++) gpu->cmd_lists[i] = cmd_lists[i];
	gpu->curr_cmd_list = 0;

	gpu->dxc_utils = dxc_utils;
	gpu->dxc_compiler = dxc_compiler;
	gpu->dxc_include_handler = dxc_include_handler;

	gpu->kernels.init(cfg.max_num_kernels, cfg.cpu_allocator, sfz_dbg("GpuLib::kernels"));

	gpu->swapchain = swapchain;
	for (u32 i = 0; i < GPU_NUM_CMD_LISTS; i++) gpu->swapchain_fbs[i] = swapchain_fbs[i];

	return gpu;
}

sfz_extern_c void gpuLibDestroy(GpuLib* gpu)
{
	if (gpu == nullptr) return;
	
	// Flush all in-flight commands
	gpuFlush(gpu);
	
	// Destroy command queue's fence event
	CloseHandle(gpu->cmd_queue_fence_event);

	SfzAllocator* allocator = gpu->cfg.cpu_allocator;
	sfz_delete(allocator, gpu);
}

// Memory API
// ------------------------------------------------------------------------------------------------

sfz_extern_c GpuPtr gpuMalloc(GpuLib* gpu, u32 num_bytes)
{
	(void)gpu;
	(void)num_bytes;
	return GPU_NULLPTR;
}

sfz_extern_c void gpuFree(GpuLib* gpu, GpuPtr ptr)
{
	(void)gpu;
	(void)ptr;
}

// Kernel API
// ------------------------------------------------------------------------------------------------

sfz_extern_c GpuKernel gpuKernelInit(GpuLib* gpu, const GpuKernelDesc* desc)
{
	// Compile shader
	ComPtr<IDxcBlob> dxil_blob;
	i32x3 group_dims = i32x3_splat(0);
	u32 launch_params_size = 0;
	{
		// Create source blob
		// TODO: From file please
		ComPtr<IDxcBlobEncoding> source_blob;
		if (!CHECK_D3D12(gpu->dxc_utils->CreateBlob(desc->src, u32(strlen(desc->src)), CP_UTF8, &source_blob))) {
			printf("[gpulib]: Failed to create source blob\n");
			return GPU_NULL_KERNEL;
		}
		DxcBuffer src_buffer = {};
		src_buffer.Ptr = source_blob->GetBufferPointer();
		src_buffer.Size = source_blob->GetBufferSize();
		src_buffer.Encoding = 0;

		// Compiler arguments
		/*constexpr u32 ENTRY_MAX_LEN = 32;
		wchar_t entry_wide[ENTRY_MAX_LEN] = {};
		utf8ToWide(entry_wide, ENTRY_MAX_LEN, desc->entry);*/
		constexpr u32 NUM_ARGS = 11;
		LPCWSTR args[NUM_ARGS] = {
			L"-E",
			L"CSMain",
			L"-T",
			L"cs_6_6",
			L"-HV 2021",
			L"-enable-16bit-types",
			L"-O3",
			L"-Zi",
			L"-Qembed_debug",
			DXC_ARG_PACK_MATRIX_ROW_MAJOR,
			L"-DGPU_LIB_HLSL"
		};

		// Compile shader
		ComPtr<IDxcResult> compile_res;
		CHECK_D3D12(gpu->dxc_compiler->Compile(
			&src_buffer, args, NUM_ARGS, gpu->dxc_include_handler.Get(), IID_PPV_ARGS(&compile_res)));
		{
			ComPtr<IDxcBlobUtf8> error_msgs;
			CHECK_D3D12(compile_res->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&error_msgs), nullptr));
			if (error_msgs && error_msgs->GetStringLength() > 0) {
				printf("[gpu_lib]: %s\n", (const char*)error_msgs->GetBufferPointer());
			}

			ComPtr<IDxcBlobUtf8> remarks;
			CHECK_D3D12(compile_res->GetOutput(DXC_OUT_REMARKS, IID_PPV_ARGS(&remarks), nullptr));
			if (remarks && remarks->GetStringLength() > 0) {
				printf("[gpu_lib]: %s\n", (const char*)remarks->GetBufferPointer());
			}

			HRESULT hr = {};
			CHECK_D3D12(compile_res->GetStatus(&hr));
			const bool compile_success = CHECK_D3D12(hr);
			if (!compile_success) {
				printf("[gpu_lib]: Failed to compile kernel\n");
				return GPU_NULL_KERNEL;
			}
		}

		// Get compiled DXIL
		CHECK_D3D12(compile_res->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&dxil_blob), nullptr));
		ComPtr<IDxcBlob> reflection_data;

		// Get reflection data
		ComPtr<ID3D12ShaderReflection> reflection;
		CHECK_D3D12(compile_res->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflection_data), nullptr));
		DxcBuffer reflection_buffer = {};
		reflection_buffer.Ptr = reflection_data->GetBufferPointer();
		reflection_buffer.Size = reflection_data->GetBufferSize();
		reflection_buffer.Encoding = 0;
		CHECK_D3D12(gpu->dxc_utils->CreateReflection(&reflection_buffer, IID_PPV_ARGS(&reflection)));

		// Get group dimensions from reflection
		u32 group_dim_x = 0, group_dim_y = 0, group_dim_z = 0;
		reflection->GetThreadGroupSize(&group_dim_x, &group_dim_y, &group_dim_z);
		group_dims = i32x3_init((i32)group_dim_x, (i32)group_dim_y, (i32)group_dim_z);

		// Get launch parameters info from reflection
		D3D12_SHADER_DESC shader_desc = {};
		CHECK_D3D12(reflection->GetDesc(&shader_desc));
		if (shader_desc.ConstantBuffers > 1) {
			printf("[gpu_lib]: More than 1 constant buffer bound, not allowed.\n");
			return GPU_NULL_KERNEL;
		}
		if (shader_desc.ConstantBuffers == 1) {
			ID3D12ShaderReflectionConstantBuffer* cbuffer_reflection =
				reflection->GetConstantBufferByIndex(0);
			D3D12_SHADER_BUFFER_DESC cbuffer = {};
			CHECK_D3D12(cbuffer_reflection->GetDesc(&cbuffer));
			launch_params_size = cbuffer.Size;
			if (launch_params_size > GPU_LAUNCH_PARAMS_MAX_SIZE) {
				printf("[gpu_lib]: Launch parameters too big, %u bytes, max %u bytes allowed\n",
					launch_params_size, GPU_LAUNCH_PARAMS_MAX_SIZE);
				return GPU_NULL_KERNEL;
			}
		}
	}

	// Create root signature
	ComPtr<ID3D12RootSignature> root_sig;
	{
		constexpr u32 MAX_NUM_ROOT_PARAMS = 2;
		const u32 num_root_params =
			launch_params_size != 0 ? MAX_NUM_ROOT_PARAMS : (MAX_NUM_ROOT_PARAMS - 1);
		D3D12_ROOT_PARAMETER1 root_params[MAX_NUM_ROOT_PARAMS] = {};

		root_params[GPU_ROOT_PARAM_GLOBAL_HEAP_IDX].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		root_params[GPU_ROOT_PARAM_GLOBAL_HEAP_IDX].Descriptor.ShaderRegister = 0;
		root_params[GPU_ROOT_PARAM_GLOBAL_HEAP_IDX].Descriptor.RegisterSpace = 0;
		// Note: UAV is written to during command list execution, thus it MUST be volatile.
		root_params[GPU_ROOT_PARAM_GLOBAL_HEAP_IDX].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
		root_params[GPU_ROOT_PARAM_GLOBAL_HEAP_IDX].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		if (launch_params_size != 0) {
			root_params[GPU_ROOT_PARAM_LAUNCH_PARAMS_IDX].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
			root_params[GPU_ROOT_PARAM_LAUNCH_PARAMS_IDX].Constants.ShaderRegister = 0;
			root_params[GPU_ROOT_PARAM_LAUNCH_PARAMS_IDX].Constants.RegisterSpace = 0;
			root_params[GPU_ROOT_PARAM_LAUNCH_PARAMS_IDX].Constants.Num32BitValues = launch_params_size / 4;
			root_params[GPU_ROOT_PARAM_LAUNCH_PARAMS_IDX].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_sig_desc = {};
		root_sig_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		root_sig_desc.Desc_1_1.NumParameters = num_root_params;
		root_sig_desc.Desc_1_1.pParameters = root_params;
		root_sig_desc.Desc_1_1.NumStaticSamplers = 0;
		root_sig_desc.Desc_1_1.pStaticSamplers = nullptr;
		root_sig_desc.Desc_1_1.Flags =
			D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS;

		ComPtr<ID3DBlob> blob;
		ComPtr<ID3DBlob> error_blob;
		const bool serialize_success = CHECK_D3D12(D3D12SerializeVersionedRootSignature(
			&root_sig_desc, &blob, &error_blob));
		if (!serialize_success) {
			printf("[gpu_lib]: Failed to serialize root signature: %s\n",
				(const char*)error_blob->GetBufferPointer());
			return GPU_NULL_KERNEL;
		}

		const bool create_success = CHECK_D3D12(gpu->device->CreateRootSignature(
			0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root_sig)));
		if (!create_success) {
			printf("[gpu_lib]: Failed to create root signature\n");
			return GPU_NULL_KERNEL;
		} 
	}

	// Create PSO (Pipeline State Object)
	ComPtr<ID3D12PipelineState> pso;
	{
		D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
		pso_desc.pRootSignature = root_sig.Get();
		pso_desc.CS.pShaderBytecode = dxil_blob->GetBufferPointer();
		pso_desc.CS.BytecodeLength = dxil_blob->GetBufferSize();
		pso_desc.NodeMask = 0;
		pso_desc.CachedPSO = {};
		pso_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

		const bool pso_success = CHECK_D3D12(gpu->device->CreateComputePipelineState(
			&pso_desc, IID_PPV_ARGS(&pso)));
		if (!pso_success) {
			printf("[gpu_lib]: Failed to create pso\n");
			return GPU_NULL_KERNEL;
		}
	}

	// Store kernel data and return handle
	const SfzHandle handle = gpu->kernels.allocate();
	if (handle == SFZ_NULL_HANDLE) return GPU_NULL_KERNEL;
	GpuKernelInfo& kernel_info = *gpu->kernels.get(handle);
	kernel_info.pso = pso;
	kernel_info.root_sig = root_sig;
	kernel_info.group_dims = group_dims;
	kernel_info.launch_params_size = launch_params_size;
	return GpuKernel{ handle.bits };
}

sfz_extern_c void gpuKernelDestroy(GpuLib* gpu, GpuKernel kernel)
{
	const SfzHandle handle = SfzHandle{ kernel.handle };
	GpuKernelInfo* info = gpu->kernels.get(handle);
	if (info == nullptr) return;
	gpu->kernels.deallocate(handle);
}

sfz_extern_c i32x3 gpuKernelGetGroupDims(const GpuLib* gpu, GpuKernel kernel)
{
	const SfzHandle handle = SfzHandle{ kernel.handle };
	const GpuKernelInfo* info = gpu->kernels.get(handle);
	if (info == nullptr) return i32x3_splat(0);
	return info->group_dims;
}

// Submission API
// ------------------------------------------------------------------------------------------------

sfz_extern_c void gpuQueueDispatch(
	GpuLib* gpu, GpuKernel kernel, i32x3 num_groups, const void* params, u32 params_size)
{
	GpuCmdListInfo& cmd_list_info = gpu->getCurrCmdList();

	// Set kernel
	const GpuKernelInfo* kernel_info = gpu->kernels.get(SfzHandle{ kernel.handle });
	if (kernel_info == nullptr) {
		printf("[gpu_lib]: Invalid kernel handle.\n");
		return;
	}
	cmd_list_info.cmd_list->SetPipelineState(kernel_info->pso.Get());
	cmd_list_info.cmd_list->SetComputeRootSignature(kernel_info->root_sig.Get());

	// Set inline descriptors
	// TODO: This could probably be done only once somehow
	cmd_list_info.cmd_list->SetComputeRootUnorderedAccessView(
		GPU_ROOT_PARAM_GLOBAL_HEAP_IDX, gpu->gpu_heap->GetGPUVirtualAddress());

	// Set launch params
	if (kernel_info->launch_params_size != params_size) {
		printf("[gpu_lib]: Invalid size of launch parameters, got %u bytes, expected %u bytes.\n",
			params_size, kernel_info->launch_params_size);
		return;
	}
	if (params_size != 0) {
		cmd_list_info.cmd_list->SetComputeRoot32BitConstants(
			GPU_ROOT_PARAM_LAUNCH_PARAMS_IDX, params_size / 4, params, 0);
	}

	// Dispatch
	sfz_assert(0 < num_groups.x && 0 < num_groups.y && 0 < num_groups.z);
	cmd_list_info.cmd_list->Dispatch(u32(num_groups.x), u32(num_groups.y), u32(num_groups.z));
}

sfz_extern_c void gpuQueueSwapchainBegin(GpuLib* gpu, i32x2 window_res)
{
	if (gpu->swapchain == nullptr) return;
	if (window_res.x <= 0 || window_res.y <= 0) {
		printf("[gpu_lib]: Invalid window resolution.\n");
		sfz_assert(false);
		return;
	}

	// Grab old swapchain resolution
	DXGI_SWAP_CHAIN_DESC swapchain_desc = {};
	CHECK_D3D12(gpu->swapchain->GetDesc(&swapchain_desc));
	sfz_assert(swapchain_desc.BufferCount == GPU_NUM_CMD_LISTS);
	const i32x2 old_swapchain_res =
		i32x2_init(swapchain_desc.BufferDesc.Width, swapchain_desc.BufferDesc.Height);
	
	// Resize swapchain if window resolution has changed
	if (old_swapchain_res != window_res) {
		printf("[gpu_lib]: Resizing swapchain framebuffers from %ix%i to %ix%i\n",
			old_swapchain_res.x, old_swapchain_res.y, window_res.x, window_res.y);

		// Flush current work in-progress
		gpuFlush(gpu);

		// Resize swapchain
		if (!CHECK_D3D12(gpu->swapchain->ResizeBuffers(
			GPU_NUM_CMD_LISTS,
			u32(window_res.x),
			u32(window_res.y),
			swapchain_desc.BufferDesc.Format,
			swapchain_desc.Flags))) {
			printf("[gpu_lib]: Failed to resize swapchain framebuffers\n");
			return;
		}

		// Set RTV descriptors
		for (u32 i = 0; i < GPU_NUM_CMD_LISTS; i++) {
			GpuSwapchainFB& fb = gpu->swapchain_fbs[i];
			ComPtr<ID3D12Resource> render_target;
			CHECK_D3D12(gpu->swapchain->GetBuffer(i, IID_PPV_ARGS(&render_target)));
			gpu->device->CreateRenderTargetView(render_target.Get(), nullptr, fb.rtv_descriptor);
		}
	}

	// Grab current swapchain render target and descriptor heap
	const u32 curr_swapchain_fb_idx = gpu->swapchain->GetCurrentBackBufferIndex();
	sfz_assert(curr_swapchain_fb_idx < GPU_NUM_CMD_LISTS);
	GpuSwapchainFB& fb = gpu->swapchain_fbs[curr_swapchain_fb_idx];
	ComPtr<ID3D12Resource> render_target;
	CHECK_D3D12(gpu->swapchain->GetBuffer(curr_swapchain_fb_idx, IID_PPV_ARGS(&render_target)));

	// Create barrier to transition swapchain render target into RENDER_TARGET state
	D3D12_RESOURCE_BARRIER rt_barrier = {};
	rt_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	rt_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	rt_barrier.Transition.pResource = render_target.Get();
	rt_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	rt_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	rt_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	GpuCmdListInfo& cmd_list_info = gpu->getCurrCmdList();
	cmd_list_info.cmd_list->ResourceBarrier(1, &rt_barrier);

	// Set viewport
	D3D12_VIEWPORT viewport = {};
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Width = f32(window_res.x);
	viewport.Height = f32(window_res.y);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	cmd_list_info.cmd_list->RSSetViewports(1, &viewport);
	
	// Set scissor rect
	D3D12_RECT scissor_rect = {};
	scissor_rect.left = 0;
	scissor_rect.top = 0;
	scissor_rect.right = LONG_MAX;
	scissor_rect.bottom = LONG_MAX;
	cmd_list_info.cmd_list->RSSetScissorRects(1, &scissor_rect);

	// Set swapchain render target
	cmd_list_info.cmd_list->OMSetRenderTargets(1, &fb.rtv_descriptor, FALSE, nullptr);
}

sfz_extern_c void gpuQueueSwapchainEnd(GpuLib* gpu)
{
	// Grab current swapchain render target and descriptor heap
	const u32 curr_swapchain_fb_idx = gpu->swapchain->GetCurrentBackBufferIndex();
	sfz_assert(curr_swapchain_fb_idx < GPU_NUM_CMD_LISTS);
	GpuSwapchainFB& fb = gpu->swapchain_fbs[curr_swapchain_fb_idx];
	ComPtr<ID3D12Resource> render_target;
	CHECK_D3D12(gpu->swapchain->GetBuffer(curr_swapchain_fb_idx, IID_PPV_ARGS(&render_target)));

	// Create barrier to transition swapchain render target into PRESENT state
	D3D12_RESOURCE_BARRIER rt_barrier = {};
	rt_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	rt_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	rt_barrier.Transition.pResource = render_target.Get();
	rt_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	rt_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	rt_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	GpuCmdListInfo& cmd_list_info = gpu->getCurrCmdList();
	cmd_list_info.cmd_list->ResourceBarrier(1, &rt_barrier);
}

sfz_extern_c void gpuSwapchainPresent(GpuLib* gpu, bool vsync)
{
	// Present swapchain's render target
	const u32 vsync_val = vsync ? 1 : 0; // Can specify 2-4 for vsync:ing on not every frame
	const u32 flags = gpu->cfg.allow_tearing ? DXGI_PRESENT_ALLOW_TEARING : 0;
	if (!CHECK_D3D12(gpu->swapchain->Present(vsync_val, flags))) {
		printf("[gpu_lib]: Present failure.\n");
		return;
	}
}

sfz_extern_c void gpuSubmitQueuedWork(GpuLib* gpu)
{
	// Execute current command list
	{
		GpuCmdListInfo& cmd_list_info = gpu->getCurrCmdList();

		// Close command list
		if (!CHECK_D3D12(cmd_list_info.cmd_list->Close())) {
			printf("[gpu_lib]: Could not close command list.\n");
			return;
		}

		// Execute command list
		ID3D12CommandList* cmd_lists[1] = {};
		cmd_lists[0] = cmd_list_info.cmd_list.Get();
		gpu->cmd_queue->ExecuteCommandLists(1, cmd_lists);

		// Fence signalling
		if (!CHECK_D3D12(gpu->cmd_queue->Signal(gpu->cmd_queue_fence.Get(), gpu->cmd_queue_fence_value))) {
			printf("[gpu_lib]: Could not signal from command queue\n");
			return;
		}
		// This command list is done once the value above is signalled
		cmd_list_info.fence_value = gpu->cmd_queue_fence_value;
		// Increment value we will signal next time
		gpu->cmd_queue_fence_value += 1;
	}

	// Switch to next command list
	{
		gpu->curr_cmd_list = (gpu->curr_cmd_list + 1) % GPU_NUM_CMD_LISTS;
		GpuCmdListInfo& cmd_list_info = gpu->getCurrCmdList();

		// Wait until command list is done
		if (gpu->cmd_queue_fence->GetCompletedValue() < cmd_list_info.fence_value) {
			CHECK_D3D12(gpu->cmd_queue_fence->SetEventOnCompletion(
				cmd_list_info.fence_value, gpu->cmd_queue_fence_event));
			WaitForSingleObject(gpu->cmd_queue_fence_event, INFINITE);
		}

		if (!CHECK_D3D12(cmd_list_info.cmd_allocator->Reset())) {
			printf("[gpu_lib]: Couldn't reset command allocator\n");
			return;
		}
		if (!CHECK_D3D12(cmd_list_info.cmd_list->Reset(cmd_list_info.cmd_allocator.Get(), nullptr))) {
			printf("[gpu_lib]: Couldn't reset command list\n");
			return;
		}

		/*
		// Set descriptor heap
		ID3D12DescriptorHeap* heaps[] = { mDescriptorBuffer->descriptorHeap.Get() };
		commandList->SetDescriptorHeaps(1, heaps);
		*/
	}
}

sfz_extern_c void gpuFlush(GpuLib* gpu)
{
	CHECK_D3D12(gpu->cmd_queue->Signal(gpu->cmd_queue_fence.Get(), gpu->cmd_queue_fence_value));
	if (gpu->cmd_queue_fence->GetCompletedValue() < gpu->cmd_queue_fence_value) {
		CHECK_D3D12(gpu->cmd_queue_fence->SetEventOnCompletion(
			gpu->cmd_queue_fence_value, gpu->cmd_queue_fence_event));
		WaitForSingleObject(gpu->cmd_queue_fence_event, INFINITE);
	}
	gpu->cmd_queue_fence_value += 1;
}
