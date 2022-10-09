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

static i32 utf8ToWide(wchar_t* wideOut, u32 numWideChars, const char* utf8In)
{
	const i32 numCharsWritten = MultiByteToWideChar(CP_UTF8, 0, utf8In, -1, wideOut, numWideChars);
	return numCharsWritten;
}

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

sfz_struct(GpuCmdListInfo) {
	ComPtr<ID3D12GraphicsCommandList> cmd_list;
	ComPtr<ID3D12CommandAllocator> cmd_allocator;
};

sfz_struct(GpuKernelInfo) {
	ComPtr<ID3D12PipelineState> pso;
	ComPtr<ID3D12RootSignature> root_sig; // TODO: Global root sig?
	i32x3 group_dims;
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
	GpuCmdListInfo cmd_lists[GPU_NUM_CMD_LISTS];
	u32 curr_cmd_list;
	GpuCmdListInfo& getCurrCmdList() { return cmd_lists[curr_cmd_list]; }

	// DXC compiler
	ComPtr<IDxcUtils> dxc_utils; // Not thread-safe
	ComPtr<IDxcCompiler3> dxc_compiler; // Not thread-safe
	ComPtr<IDxcIncludeHandler> dxc_include_handler; // Not thread-safe

	// Kernels
	sfz::Pool<GpuKernelInfo> kernels;
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

	GpuLib* gpu = sfz_new<GpuLib>(cfg.cpu_allocator, sfz_dbg("GpuLib"));
	*gpu = {};
	gpu->cfg = cfg;

	gpu->dxgi = dxgi;
	gpu->device = device;
	gpu->info_queue = info_queue;

	gpu->gpu_heap = gpu_heap;

	gpu->cmd_queue = cmd_queue;

	for (u32 i = 0; i < GPU_NUM_CMD_LISTS; i++) gpu->cmd_lists[i] = cmd_lists[i];
	gpu->curr_cmd_list = 0;

	gpu->dxc_utils = dxc_utils;
	gpu->dxc_compiler = dxc_compiler;
	gpu->dxc_include_handler = dxc_include_handler;

	gpu->kernels.init(cfg.max_num_kernels, cfg.cpu_allocator, sfz_dbg("GpuLib::kernels"));

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
	return GPU_NULLPTR;
}

sfz_extern_c void gpuFree(GpuLib* gpu, GpuPtr ptr)
{
	
}

// Kernel API
// ------------------------------------------------------------------------------------------------

sfz_extern_c GpuKernel gpuKernelInit(GpuLib* gpu, const GpuKernelDesc* desc)
{
	// Compile shader
	ComPtr<IDxcBlob> dxil_blob;
	i32x3 group_dims = i32x3_splat(0);
	{
		// Create source blob
		// TODO: From file please
		ComPtr<IDxcBlobEncoding> source_blob;
		if (!CHECK_D3D12(gpu->dxc_utils->CreateBlob(desc->src, strlen(desc->src), CP_UTF8, &source_blob))) {
			printf("[gpulib]: Failed to create source blob\n");
			return GPU_NULL_KERNEL;
		}
		DxcBuffer src_buffer = {};
		src_buffer.Ptr = source_blob->GetBufferPointer();
		src_buffer.Size = source_blob->GetBufferSize();
		src_buffer.Encoding = 0;

		// Compiler arguments
		constexpr u32 ENTRY_MAX_LEN = 32;
		wchar_t entry_wide[ENTRY_MAX_LEN] = {};
		utf8ToWide(entry_wide, ENTRY_MAX_LEN, desc->entry);
		constexpr u32 NUM_ARGS = 11;
		LPCWSTR args[NUM_ARGS] = {
			L"-E",
			entry_wide,
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
				printf("[gpu_lib]: %s\n", error_msgs->GetBufferPointer());
			}

			ComPtr<IDxcBlobUtf8> remarks;
			CHECK_D3D12(compile_res->GetOutput(DXC_OUT_REMARKS, IID_PPV_ARGS(&remarks), nullptr));
			if (remarks && remarks->GetStringLength() > 0) {
				printf("[gpu_lib]: %s\n", remarks->GetBufferPointer());
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

		// Get info from reflection data
		//D3D12_SHADER_DESC shader_desc = {};
		//CHECK_D3D12(reflection->GetDesc(&shader_desc));
	}

	// Create root signature
	ComPtr<ID3D12RootSignature> root_sig;
	{
		constexpr u32 NUM_ROOT_PARAMS = 1;
		D3D12_ROOT_PARAMETER1 root_params[NUM_ROOT_PARAMS] = {};

		root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		root_params[0].Descriptor.ShaderRegister = 0;
		root_params[0].Descriptor.RegisterSpace = 0;
		root_params[0].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
		root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC root_sig_desc = {};
		root_sig_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		root_sig_desc.Desc_1_1.NumParameters = NUM_ROOT_PARAMS;
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
				error_blob->GetBufferPointer());
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

sfz_extern_c void gpuQueueDispatch1(GpuLib* gpu, GpuKernel kernel, i32 num_groups)
{
	gpuQueueDispatch3(gpu, kernel, i32x3_init(num_groups, 1, 1));
}

sfz_extern_c void gpuQueueDispatch2(GpuLib* gpu, GpuKernel kernel, i32x2 num_groups)
{
	gpuQueueDispatch3(gpu, kernel, i32x3_init2(num_groups, 1));
}

sfz_extern_c void gpuQueueDispatch3(GpuLib* gpu, GpuKernel kernel, i32x3 num_groups)
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

	// Dispatch
	sfz_assert(0 < num_groups.x && 0 < num_groups.y && 0 < num_groups.z);
	cmd_list_info.cmd_list->Dispatch(u32(num_groups.x), u32(num_groups.y), u32(num_groups.z));
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


		/*gpu->cmd_queue->Signal(
		CHECK_D3D12 mCommandQueue->Signal(mCommandQueueFence.Get(), mCommandQueueFenceValue);
		u64 fence_value = mCommandQueueFenceValue++;
		
		void waitOnCpuInternal(u64 fenceValue) noexcept
		{
		if (!isFenceValueDone(fenceValue)) {
			CHECK_D3D12 mCommandQueueFence->SetEventOnCompletion(
				fenceValue, mCommandQueueFenceEvent);
				// TODO: Don't wait forever
				::WaitForSingleObject(mCommandQueueFenceEvent, INFINITE);
			}
		}

		bool isFenceValueDone(u64 fenceValue) noexcept
		{
			return mCommandQueueFence->GetCompletedValue() >= fenceValue;
		}
		
		*/
	}

	// Switch to next command list
	{
		gpu->curr_cmd_list = (gpu->curr_cmd_list + 1) % GPU_NUM_CMD_LISTS;
		GpuCmdListInfo& cmd_list_info = gpu->getCurrCmdList();

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
		if (commandListType != D3D12_COMMAND_LIST_TYPE_COPY) {
			ID3D12DescriptorHeap* heaps[] = { mDescriptorBuffer->descriptorHeap.Get() };
			commandList->SetDescriptorHeaps(1, heaps);
		}
		*/
	}
}

sfz_extern_c void gpuFlush(GpuLib* gpu)
{

}
