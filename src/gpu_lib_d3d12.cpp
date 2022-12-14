#include "gpu_lib_internal.hpp"

// D3D12 Agility SDK exports
// ------------------------------------------------------------------------------------------------

// The version of the Agility SDK we are using, see https://devblogs.microsoft.com/directx/directx12agility/
extern "C" { _declspec(dllexport) extern const u32 D3D12SDKVersion = 606; }

// Specifies that D3D12Core.dll will be available in a directory called D3D12 next to the exe.
extern "C" { _declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

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

// Init API
// ------------------------------------------------------------------------------------------------

sfz_extern_c GpuLib* gpuLibInit(const GpuLibInitCfg* cfgIn)
{
	// Copy config so that we can make changes to it before finally storing it in the context
	GpuLibInitCfg cfg = *cfgIn;
	cfg.gpu_heap_size_bytes = u32_clamp(cfg.gpu_heap_size_bytes, GPU_HEAP_MIN_SIZE, GPU_HEAP_MAX_SIZE);
	cfg.max_num_textures_per_type = u32_clamp(cfg.max_num_textures_per_type, GPU_TEXTURES_MIN_NUM, GPU_TEXTURES_MAX_NUM);
	cfg.upload_heap_size_bytes = sfzRoundUpAlignedU32(cfg.upload_heap_size_bytes, GPU_UPLOAD_HEAP_ALIGN);
	cfg.download_heap_size_bytes = sfzRoundUpAlignedU32(cfg.download_heap_size_bytes, GPU_DOWNLOAD_HEAP_ALIGN);

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
		CHECK_D3D12(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE));
		CHECK_D3D12(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE));
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
	GpuCmdListInfo cmd_lists[GPU_NUM_CONCURRENT_SUBMITS];
	for (u32 i = 0; i < GPU_NUM_CONCURRENT_SUBMITS; i++) {
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
		info.submit_idx = 0;
		info.upload_heap_offset = 0;
		info.download_heap_offset = 0;
	}

	// Create timestamp stuff
	ComPtr<ID3D12QueryHeap> timestamp_query_heap;
	{
		D3D12_QUERY_HEAP_DESC query_desc = {};
		query_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		query_desc.Count = 1;
		query_desc.NodeMask = 0;
		if (!CHECK_D3D12(device->CreateQueryHeap(&query_desc, IID_PPV_ARGS(&timestamp_query_heap)))) {
			printf("[gpu_lib]: Could not create timestap query heap.\n");
			return nullptr;
		}
		setDebugNameLazy(timestamp_query_heap);
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

		const bool heap_success = CHECK_D3D12(device->CreateCommittedResource(
			&heap_props, heap_flags, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&gpu_heap)));
		if (!heap_success) {
			printf("[gpu_lib]: Could not allocate gpu heap of size %.2f MiB, exiting.",
				gpuPrintToMiB(cfg.gpu_heap_size_bytes));
			return nullptr;
		}
		setDebugNameLazy(gpu_heap);
	}

	// Allocate upload heap
	ComPtr<ID3D12Resource> upload_heap;
	u8* upload_heap_mapped_ptr = nullptr; // Persistently mapped, never unmapped
	{
		D3D12_HEAP_PROPERTIES heap_props = {};
		heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
		heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heap_props.CreationNodeMask = 0;
		heap_props.VisibleNodeMask = 0;

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Alignment = 0;
		desc.Width = cfg.upload_heap_size_bytes;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Flags = D3D12_RESOURCE_FLAGS(0);

		const bool heap_success = CHECK_D3D12(device->CreateCommittedResource(
			&heap_props,
			D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
			&desc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&upload_heap)));
		if (!heap_success) {
			printf("[gpu_lib]: Could not allocate upload heap of size %.2f MiB, exiting.",
				gpuPrintToMiB(cfg.upload_heap_size_bytes));
			return nullptr;
		}
		setDebugNameLazy(upload_heap);

		void* mapped_ptr = nullptr;
		if (!CHECK_D3D12(upload_heap->Map(0, nullptr, &mapped_ptr))) {
			printf("[gpu_lib]: Failed to map upload heap\n");
			return nullptr;
		}
		upload_heap_mapped_ptr = static_cast<u8*>(mapped_ptr);
	}

	// Allocate download heap
	ComPtr<ID3D12Resource> download_heap;
	u8* download_heap_mapped_ptr = nullptr; // Persistently mapped, never unmapped
	{
		D3D12_HEAP_PROPERTIES heap_props = {};
		heap_props.Type = D3D12_HEAP_TYPE_READBACK;
		heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heap_props.CreationNodeMask = 0;
		heap_props.VisibleNodeMask = 0;

		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Alignment = 0;
		desc.Width = cfg.download_heap_size_bytes;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		desc.Flags = D3D12_RESOURCE_FLAGS(0);

		const bool heap_success = CHECK_D3D12(device->CreateCommittedResource(
			&heap_props,
			D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
			&desc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&download_heap)));
		if (!heap_success) {
			printf("[gpu_lib]: Could not allocate download heap of size %.2f MiB, exiting.",
				gpuPrintToMiB(cfg.download_heap_size_bytes));
			return nullptr;
		}
		setDebugNameLazy(download_heap);

		void* mapped_ptr = nullptr;
		if (!CHECK_D3D12(download_heap->Map(0, nullptr, &mapped_ptr))) {
			printf("[gpu_lib]: Failed to map download heap\n");
			return nullptr;
		}
		download_heap_mapped_ptr = static_cast<u8*>(mapped_ptr);
	}

	// Create tex descriptor heap
	ComPtr<ID3D12DescriptorHeap> tex_descriptor_heap;
	u32 num_tex_descriptors = 0;
	u32 tex_descriptor_size = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE tex_descriptor_heap_start_cpu = {};
	D3D12_GPU_DESCRIPTOR_HANDLE tex_descriptor_heap_start_gpu = {};
	{
		num_tex_descriptors = cfg.max_num_textures_per_type;
		D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
		heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heap_desc.NumDescriptors = num_tex_descriptors;
		heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		heap_desc.NodeMask = 0;

		if (!CHECK_D3D12(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&tex_descriptor_heap)))) {
			printf("[gpu_lib]: Could not allocate %u descriptors for texture arrays, exiting.\n",
				num_tex_descriptors);
			return nullptr;
		}
		setDebugNameLazy(tex_descriptor_heap);

		tex_descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		tex_descriptor_heap_start_cpu = tex_descriptor_heap->GetCPUDescriptorHandleForHeapStart();
		tex_descriptor_heap_start_gpu = tex_descriptor_heap->GetGPUDescriptorHandleForHeapStart();

		// Set null descriptors for all potential slots in the heap
		for (u32 i = 0; i < cfg.max_num_textures_per_type; i++) {
			D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
			uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			uav_desc.Texture2D.MipSlice = 0;
			uav_desc.Texture2D.PlaneSlice = 0;

			D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor = {};
			cpu_descriptor.ptr = tex_descriptor_heap_start_cpu.ptr + tex_descriptor_size * i;
			device->CreateUnorderedAccessView(nullptr, nullptr, &uav_desc, cpu_descriptor);
		}
	}

	// Initialize RWTex pool
	sfz::Pool<GpuRWTexInfo> rw_textures;
	{
		rw_textures.init(cfg.max_num_textures_per_type, cfg.cpu_allocator, sfz_dbg("GpuLib::rw_textures"));
		const SfzHandle null_slot = rw_textures.allocate();
		sfz_assert(null_slot.idx() == GPU_NULL_RWTEX);
		const SfzHandle swapchain_slot = rw_textures.allocate();
		sfz_assert(swapchain_slot.idx() == RWTEX_SWAPCHAIN_IDX);
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
			desc.BufferCount = GPU_NUM_CONCURRENT_SUBMITS;
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
	}

	GpuLib* gpu = sfz_new<GpuLib>(cfg.cpu_allocator, sfz_dbg("GpuLib"));
	*gpu = {};
	gpu->cfg = cfg;

	gpu->dxgi = dxgi;
	gpu->device = device;
	gpu->info_queue = info_queue;

	gpu->curr_submit_idx = 0;
	gpu->known_completed_submit_idx = 0;
	gpu->cmd_queue = cmd_queue;
	gpu->cmd_queue_fence = cmd_queue_fence;
	gpu->cmd_queue_fence_event = cmd_queue_fence_event;
	gpu->cmd_queue_fence_value = 0;
	for (u32 i = 0; i < GPU_NUM_CONCURRENT_SUBMITS; i++) gpu->cmd_lists[i] = cmd_lists[i];

	gpu->timestamp_query_heap = timestamp_query_heap;

	gpu->gpu_heap = gpu_heap;
	gpu->gpu_heap_state = D3D12_RESOURCE_STATE_COMMON;
	gpu->gpu_heap_next_free = GPU_HEAP_SYSTEM_RESERVED_SIZE;

	gpu->upload_heap = upload_heap;
	gpu->upload_heap_mapped_ptr = upload_heap_mapped_ptr;
	gpu->upload_heap_offset = 0;
	gpu->upload_heap_safe_offset = 0;

	gpu->download_heap = download_heap;
	gpu->download_heap_mapped_ptr = download_heap_mapped_ptr;
	gpu->download_heap_offset = 0;
	gpu->download_heap_safe_offset = 0;
	gpu->downloads.init(cfg.max_num_concurrent_downloads, cfg.cpu_allocator, sfz_dbg("GpuLib::downloads"));

	gpu->tex_descriptor_heap = tex_descriptor_heap;
	gpu->num_tex_descriptors = num_tex_descriptors;
	gpu->tex_descriptor_size = tex_descriptor_size;
	gpu->tex_descriptor_heap_start_cpu = tex_descriptor_heap_start_cpu;
	gpu->tex_descriptor_heap_start_gpu = tex_descriptor_heap_start_gpu;

	gpu->rw_textures = sfz_move(rw_textures);

	gpu->dxc_utils = dxc_utils;
	gpu->dxc_compiler = dxc_compiler;
	gpu->dxc_include_handler = dxc_include_handler;

	gpu->kernels.init(cfg.max_num_kernels, cfg.cpu_allocator, sfz_dbg("GpuLib::kernels"));

	gpu->swapchain_res = i32x2_splat(0);
	gpu->swapchain = swapchain;

	gpu->tmp_barriers.init(cfg.max_num_textures_per_type, cfg.cpu_allocator, sfz_dbg("GpuLib::tmp_barriers"));

	// Do a quick present after initialization has finished, used to set up framebuffers
	gpuSubmitQueuedWork(gpu);
	gpuSwapchainPresent(gpu, false);
	sfz_assert(gpu->curr_submit_idx == 1);
	sfz_assert(gpu->upload_heap_safe_offset == gpu->cfg.upload_heap_size_bytes);
	sfz_assert(gpu->download_heap_safe_offset == gpu->cfg.download_heap_size_bytes);

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
	// TODO: This is obviously a very bad malloc API, please implement real malloc/free.

	// Check if we have enough space left
	const u32 end = gpu->gpu_heap_next_free + num_bytes;
	if (gpu->cfg.gpu_heap_size_bytes < end) {
		printf("[gpu_lib]: Out of GPU memory, trying to allocate %.3f MiB.\n",
			gpuPrintToMiB(num_bytes));
		return GPU_NULLPTR;
	}

	// Get pointer
	const GpuPtr ptr = gpu->gpu_heap_next_free;
	gpu->gpu_heap_next_free = sfzRoundUpAlignedU32(end, GPU_MALLOC_ALIGN);
	return ptr;
}

sfz_extern_c void gpuFree(GpuLib* gpu, GpuPtr ptr)
{
	(void)gpu;
	(void)ptr;
	// TODO: This is obviously a very bad free API, please implement real malloc/free.
}

// Textures API
// ------------------------------------------------------------------------------------------------

sfz_extern_c const char* gpuFormatToString(GpuFormat format)
{
	return formatToString(format);
}

static GpuRWTex gpuRWTexInitInternal(GpuLib* gpu, const GpuRWTexDesc* desc, const SfzHandle* existing_handle = nullptr)
{
	if (desc->format == GPU_FORMAT_UNDEFINED) {
		printf("[gpu_lib]: Must specify a valid texture format when creating an RWTex\n");
		return GPU_NULL_RWTEX;
	}
	if (desc->swapchain_relative && desc->relative_fixed_height != 0 && desc->relative_scale != 0.0f) {
		printf("[gpu_lib]: For swapchain relative textures either fixed height or scale MUST be 0.\n");
		return GPU_NULL_RWTEX;
	}

	const i32x2 tex_res = calcRWTexTargetRes(gpu->swapchain_res, desc);

	// Allocate texture resource
	ComPtr<ID3D12Resource> tex;
	{
		D3D12_HEAP_PROPERTIES heap_props = {};
		heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
		heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
		heap_props.CreationNodeMask = 0;
		heap_props.VisibleNodeMask = 0;

		D3D12_RESOURCE_DESC res_desc = {};
		res_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		res_desc.Alignment = 0;
		res_desc.Width = u32(tex_res.x);
		res_desc.Height = u32(tex_res.y);
		res_desc.DepthOrArraySize = 1;
		res_desc.MipLevels = 1;
		res_desc.Format = formatToD3D12(desc->format);
		res_desc.SampleDesc = { 1, 0 };
		res_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		res_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		const bool success = CHECK_D3D12(gpu->device->CreateCommittedResource(
			&heap_props,
			D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
			&res_desc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nullptr,
			IID_PPV_ARGS(&tex)));
		if (!success) {
			printf("[gpu_lib]: Could not allocate GpuRWTex of size %ix%i and format %s\n",
				tex_res.x, tex_res.y, formatToString(desc->format));
			return GPU_NULL_RWTEX;
		}
		setDebugName(tex.Get(), desc->name);
	}

	// Allocate slot in rwtex array
	SfzHandle handle = SFZ_NULL_HANDLE;
	if (existing_handle != nullptr) {
		handle = *existing_handle;
	}
	else {
		handle = gpu->rw_textures.allocate();
	}
	if (handle == SFZ_NULL_HANDLE) {
		printf("[gpu_lib]: Could not allocate slot in GpuRWTex array, out of slots.\n");
		return GPU_NULL_RWTEX;
	}

	// Store info about texture
	GpuRWTexInfo& info = *gpu->rw_textures.get(handle);
	info.tex = tex;
	info.tex_res = tex_res;
	info.desc = *desc;
	info.name = sfzStr96Init(desc->name);
	info.desc.name = info.name.str; // Need to repoint name, otherwise potential use after free.

	// Set descriptor in tex descriptor heap
	const GpuRWTex tex_idx = GpuRWTex(handle.idx());
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
		uav_desc.Format = formatToD3D12(desc->format);
		uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uav_desc.Texture2D.MipSlice = 0;
		uav_desc.Texture2D.PlaneSlice = 0;

		D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor = {};
		cpu_descriptor.ptr =
			gpu->tex_descriptor_heap_start_cpu.ptr + gpu->tex_descriptor_size * u32(tex_idx);
		gpu->device->CreateUnorderedAccessView(tex.Get(), nullptr, &uav_desc, cpu_descriptor);
	}

	return tex_idx;
}

sfz_extern_c GpuRWTex gpuRWTexInit(GpuLib* gpu, const GpuRWTexDesc* desc)
{
	return gpuRWTexInitInternal(gpu, desc);
}

sfz_extern_c void gpuRWTexDestroy(GpuLib* gpu, GpuRWTex tex)
{
	const SfzHandle handle = gpu->rw_textures.getHandle(tex);
	GpuRWTexInfo* tex_info = gpu->rw_textures.get(handle);
	if (tex_info == nullptr) {
		printf("[gpu_lib]: Trying to destroy a GpuRWTex that doesn't exist.\n");
		return;
	}

	// Set null descriptor in tex descriptor heap
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
		uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uav_desc.Texture2D.MipSlice = 0;
		uav_desc.Texture2D.PlaneSlice = 0;

		D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor = {};
		cpu_descriptor.ptr = gpu->tex_descriptor_heap_start_cpu.ptr + gpu->tex_descriptor_size * u32(tex);
		gpu->device->CreateUnorderedAccessView(nullptr, nullptr, &uav_desc, cpu_descriptor);
	}

	gpu->rw_textures.deallocate(handle);
}

sfz_extern_c const GpuRWTexDesc* gpuRWTexGetDesc(const GpuLib* gpu, GpuRWTex tex)
{
	const SfzHandle handle = gpu->rw_textures.getHandle(tex);
	const GpuRWTexInfo* tex_info = gpu->rw_textures.get(handle);
	if (tex_info == nullptr) return nullptr;
	return &tex_info->desc;
}

sfz_extern_c i32x2 gpuRWTexGetRes(const GpuLib* gpu, GpuRWTex tex)
{
	const SfzHandle handle = gpu->rw_textures.getHandle(tex);
	const GpuRWTexInfo* tex_info = gpu->rw_textures.get(handle);
	if (tex_info == nullptr) return i32x2_splat(0);
	return tex_info->tex_res;
}

sfz_extern_c void gpuRWTexSetSwapchainRelativeScale(GpuLib* gpu, GpuRWTex tex, f32 scale)
{
	const SfzHandle handle = gpu->rw_textures.getHandle(tex);
	const GpuRWTexInfo* tex_info = gpu->rw_textures.get(handle);
	if (tex_info == nullptr) {
		printf("[gpu_lib]: Trying to set relative scale of a texture that doesn't exist (%u).\n",
			u32(tex));
		return;
	}
	if (!tex_info->desc.swapchain_relative) {
		printf("[gpu_lib]: Trying to set relative scale of a texture that is not swapchain relative (%u).\n",
			u32(tex));
		return;
	}
	
	// Just return if we already have the correct scale
	if (tex_info->desc.relative_scale == scale) return;

	// Rebuild texture
	// Need to copy desc to avoid potential aliasing issues
	SfzStr96 name = tex_info->name;
	GpuRWTexDesc desc = tex_info->desc;
	desc.relative_fixed_height = 0;
	desc.relative_scale = scale;
	desc.name = name.str;
	gpuRWTexInitInternal(gpu, &desc, &handle);
}

sfz_extern_c void gpuRWTexSetSwapchainRelativeFixedHeight(GpuLib* gpu, GpuRWTex tex, i32 height)
{
	const SfzHandle handle = gpu->rw_textures.getHandle(tex);
	const GpuRWTexInfo* tex_info = gpu->rw_textures.get(handle);
	if (tex_info == nullptr) {
		printf("[gpu_lib]: Trying to set relative fixed height of a texture that doesn't exist (%u).\n",
			u32(tex));
		return;
	}
	if (!tex_info->desc.swapchain_relative) {
		printf("[gpu_lib]: Trying to set relative fixed height of a texture that is not swapchain relative (%u).\n",
			u32(tex));
		return;
	}

	// Just return if we already have the correct fixed height
	if (tex_info->desc.relative_fixed_height == height) return;

	// Rebuild texture
	// Need to copy desc to avoid potential aliasing issues
	SfzStr96 name = tex_info->name;
	GpuRWTexDesc desc = tex_info->desc;
	desc.relative_fixed_height = height;
	desc.relative_scale = 0.0f;
	desc.name = name.str;
	gpuRWTexInitInternal(gpu, &desc, &handle);
}

// Kernel API
// ------------------------------------------------------------------------------------------------

sfz_extern_c GpuKernel gpuKernelInit(GpuLib* gpu, const GpuKernelDesc* desc)
{
	// Read shader file from disk
	u32 src_size = 0;
	char* src = nullptr;
	{
		// Map shader file
		FileMapData src_map = fileMap(desc->path, true);
		if (src_map.ptr == nullptr) {
			printf("[gpulib]: Failed to map kernel source file \"%s\".\n", desc->path);
			return GPU_NULL_KERNEL;
		}
		sfz_defer[=]() { fileUnmap(src_map); };

		// Allocate memory for src + prolog
		src_size = u32(src_map.size_bytes + GPU_KERNEL_PROLOG_SIZE);
		src = static_cast<char*>(gpu->cfg.cpu_allocator->alloc(sfz_dbg(""), src_size + 1));
		
		// Copy prolog and then src file into buffer
		memcpy(src, GPU_KERNEL_PROLOG, GPU_KERNEL_PROLOG_SIZE);
		memcpy(src + GPU_KERNEL_PROLOG_SIZE, src_map.ptr, src_map.size_bytes);
		src[src_size] = '\0'; // Guarantee null-termination, safe because we allocated 1 byte extra.
	}
	sfz_defer[=]() { gpu->cfg.cpu_allocator->dealloc(src); };

	// Compile shader
	ComPtr<IDxcBlob> dxil_blob;
	i32x3 group_dims = i32x3_splat(0);
	u32 launch_params_size = 0;
	{
		// Create source blob
		ComPtr<IDxcBlobEncoding> source_blob;
		if (!CHECK_D3D12(gpu->dxc_utils->CreateBlob(src, src_size, CP_UTF8, &source_blob))) {
			printf("[gpulib]: Failed to create source blob\n");
			return GPU_NULL_KERNEL;
		}
		DxcBuffer src_buffer = {};
		src_buffer.Ptr = source_blob->GetBufferPointer();
		src_buffer.Size = source_blob->GetBufferSize();
		src_buffer.Encoding = 0;

		// Defines
		const u32 num_defines = u32_min(desc->num_defines, GPU_KERNEL_MAX_NUM_DEFINES);
		wchar_t defines_wide[GPU_KERNEL_DEFINE_MAX_LEN + 3][GPU_KERNEL_MAX_NUM_DEFINES] = {};
		for (u32 i = 0; i < num_defines; i++) {
			defines_wide[i][0] = L'-';
			defines_wide[i][1] = L'D';
			utf8ToWide(defines_wide[i] + 2, GPU_KERNEL_DEFINE_MAX_LEN, desc->defines[i]);
			defines_wide[i][GPU_KERNEL_DEFINE_MAX_LEN + 2] = '\0';
		}

		// Compiler arguments
		constexpr u32 NUM_ARGS_BASE = 11;
		constexpr u32 MAX_NUM_ARGS = NUM_ARGS_BASE + GPU_KERNEL_MAX_NUM_DEFINES;
		const u32 num_args = NUM_ARGS_BASE + num_defines;
		LPCWSTR args[MAX_NUM_ARGS] = {
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
		for (u32 i = 0; i < num_defines; i++) {
			args[NUM_ARGS_BASE + i] = defines_wide[i];
		}

		// Compile shader
		ComPtr<IDxcResult> compile_res;
		CHECK_D3D12(gpu->dxc_compiler->Compile(
			&src_buffer, args, num_args, gpu->dxc_include_handler.Get(), IID_PPV_ARGS(&compile_res)));
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
		constexpr u32 MAX_NUM_ROOT_PARAMS = 3;
		const u32 num_root_params =
			launch_params_size != 0 ? MAX_NUM_ROOT_PARAMS : (MAX_NUM_ROOT_PARAMS - 1);
		D3D12_ROOT_PARAMETER1 root_params[MAX_NUM_ROOT_PARAMS] = {};

		root_params[GPU_ROOT_PARAM_GLOBAL_HEAP_IDX].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		root_params[GPU_ROOT_PARAM_GLOBAL_HEAP_IDX].Descriptor.ShaderRegister = 0;
		root_params[GPU_ROOT_PARAM_GLOBAL_HEAP_IDX].Descriptor.RegisterSpace = 0;
		// Note: UAV is written to during command list execution, thus it MUST be volatile.
		root_params[GPU_ROOT_PARAM_GLOBAL_HEAP_IDX].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
		root_params[GPU_ROOT_PARAM_GLOBAL_HEAP_IDX].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_DESCRIPTOR_RANGE1 desc_range = {};
		desc_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		desc_range.NumDescriptors = UINT_MAX; // Unbounded
		desc_range.BaseShaderRegister = 1;
		desc_range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
		desc_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		root_params[GPU_ROOT_PARAM_RW_TEX_ARRAY_IDX].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		root_params[GPU_ROOT_PARAM_RW_TEX_ARRAY_IDX].DescriptorTable.NumDescriptorRanges = 1;
		root_params[GPU_ROOT_PARAM_RW_TEX_ARRAY_IDX].DescriptorTable.pDescriptorRanges = &desc_range;
		root_params[GPU_ROOT_PARAM_RW_TEX_ARRAY_IDX].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

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
		setDebugName(root_sig.Get(), desc->name);
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
		setDebugName(pso.Get(), desc->name);
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

// Command API
// ------------------------------------------------------------------------------------------------

sfz_extern_c u64 gpuGetCurrSubmitIdx(const GpuLib* gpu)
{
	return gpu->curr_submit_idx;
}

sfz_extern_c i32x2 gpuSwapchainGetRes(const GpuLib* gpu)
{
	return gpu->swapchain_res;
}

sfz_extern_c u64 gpuTimestampGetFreq(const GpuLib* gpu)
{
	u64 ticks_per_sec = 0;
	if (!CHECK_D3D12(gpu->cmd_queue->GetTimestampFrequency(&ticks_per_sec))) {
		printf("[gpu_lib]: Couldn't get timestamp frequency.\n");
		return 0;
	}
	return ticks_per_sec;
}

sfz_extern_c void gpuQueueTakeTimestamp(GpuLib* gpu, GpuPtr dst)
{
	GpuCmdListInfo& cmd_list_info = gpu->getCurrCmdList();

	// Note: This isn't necessarily the fastest/least blocking path. We could query the result
	//       directly to the download heap, and in that case there would be no need to insert a
	//       barrier on the global heap. OTOH, we already need this barrier for memcpy uploads, so
	//       might not matter much.

	// Ensure heap is in COPY_DEST state
	if (gpu->gpu_heap_state != D3D12_RESOURCE_STATE_COPY_DEST) {
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = gpu->gpu_heap.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = gpu->gpu_heap_state;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		cmd_list_info.cmd_list->ResourceBarrier(1, &barrier);
		gpu->gpu_heap_state = D3D12_RESOURCE_STATE_COPY_DEST;
	}

	// Get timestamp and store it in u64 pointed to by gpu pointer
	const u32 timestamp_idx = 0; // We only need one slot because we immediately copy out the data
	cmd_list_info.cmd_list->EndQuery(
		gpu->timestamp_query_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, timestamp_idx);
	cmd_list_info.cmd_list->ResolveQueryData(
		gpu->timestamp_query_heap.Get(),
		D3D12_QUERY_TYPE_TIMESTAMP,
		timestamp_idx,
		1,
		gpu->gpu_heap.Get(),
		dst);
}

sfz_extern_c void gpuQueueMemcpyUpload(GpuLib* gpu, GpuPtr dst, const void* src, u32 num_bytes_original)
{
	if (num_bytes_original == 0) return;
	if (dst < GPU_HEAP_SYSTEM_RESERVED_SIZE || gpu->cfg.gpu_heap_size_bytes <= dst) {
		printf("[gpu_lib]: Trying to memcpy upload to an invalid pointer (%u)\n", dst);
		return;
	}
	const u32 num_bytes = sfzRoundUpAlignedU32(num_bytes_original, GPU_UPLOAD_HEAP_ALIGN);

	// Try to allocate a range
	u64 begin = gpu->upload_heap_offset;
	u64 begin_mapped = begin % gpu->cfg.upload_heap_size_bytes;
	if (gpu->cfg.upload_heap_size_bytes < (begin_mapped + num_bytes)) {
		// Wrap around, try in beginning of heap instead.
		begin = sfzRoundUpAlignedU64(gpu->upload_heap_offset, gpu->cfg.upload_heap_size_bytes);
		begin_mapped = 0;
	}
	const u64 end = begin + num_bytes;
	
	// Check for heap overflow
	if (gpu->upload_heap_safe_offset <= end) {
		printf("[gpu_lib]: Upload heap overflow by %u bytes\n",
			u32(end - gpu->upload_heap_safe_offset));
		return;
	}

	// Memcpy data to upload heap and commit change
	memcpy(gpu->upload_heap_mapped_ptr + begin_mapped, src, num_bytes_original);
	gpu->upload_heap_offset = end;

	// Ensure heap is in COPY_DEST state
	GpuCmdListInfo& cmd_list_info = gpu->getCurrCmdList();
	if (gpu->gpu_heap_state != D3D12_RESOURCE_STATE_COPY_DEST) {
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = gpu->gpu_heap.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = gpu->gpu_heap_state;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
		cmd_list_info.cmd_list->ResourceBarrier(1, &barrier);
		gpu->gpu_heap_state = D3D12_RESOURCE_STATE_COPY_DEST;
	}

	// Copy to heap
	cmd_list_info.cmd_list->CopyBufferRegion(
		gpu->gpu_heap.Get(), dst, gpu->upload_heap.Get(), begin_mapped, num_bytes_original);
}

sfz_extern_c GpuTicket gpuQueueMemcpyDownload(GpuLib* gpu, GpuPtr src, u32 num_bytes_original)
{
	if (num_bytes_original == 0) return GPU_NULL_TICKET;
	if (src < GPU_HEAP_SYSTEM_RESERVED_SIZE || gpu->cfg.gpu_heap_size_bytes <= src) {
		printf("[gpu_lib]: Trying to memcpy download from an invalid pointer (%u)\n", src);
		return GPU_NULL_TICKET;
	}
	const u32 num_bytes = sfzRoundUpAlignedU32(num_bytes_original, GPU_DOWNLOAD_HEAP_ALIGN);

	// Try to allocate a range
	u64 begin = gpu->download_heap_offset;
	u64 begin_mapped = begin % gpu->cfg.download_heap_size_bytes;
	if (gpu->cfg.download_heap_size_bytes < (begin_mapped + num_bytes)) {
		// Wrap around, try in beginning of heap instead.
		begin = sfzRoundUpAlignedU64(gpu->download_heap_offset, gpu->cfg.download_heap_size_bytes);
		begin_mapped = 0;
	}
	const u64 end = begin + num_bytes;

	// Check for heap overflow
	if (gpu->download_heap_safe_offset <= end) {
		printf("[gpu_lib]: Download heap overflow by %u bytes\n",
			u32(end - gpu->download_heap_safe_offset));
		return GPU_NULL_TICKET;
	}

	// Commit change
	gpu->download_heap_offset = end;

	// Ensure heap is in COPY_SOURCE state
	GpuCmdListInfo& cmd_list_info = gpu->getCurrCmdList();
	if (gpu->gpu_heap_state != D3D12_RESOURCE_STATE_COPY_SOURCE) {
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = gpu->gpu_heap.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = gpu->gpu_heap_state;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		cmd_list_info.cmd_list->ResourceBarrier(1, &barrier);
		gpu->gpu_heap_state = D3D12_RESOURCE_STATE_COPY_SOURCE;
	}

	// Copy to download heap
	cmd_list_info.cmd_list->CopyBufferRegion(
		gpu->download_heap.Get(), begin_mapped, gpu->gpu_heap.Get(), src, num_bytes_original);

	// Allocate a pending download slot
	const SfzHandle download_handle = gpu->downloads.allocate();
	if (download_handle == SFZ_NULL_HANDLE) {
		printf("[gpu_lib]: Out of room for more concurrent downloads (max %u)\n",
			gpu->cfg.max_num_concurrent_downloads);
		return GPU_NULL_TICKET;
	}

	// Store data for the pending download
	GpuPendingDownload& pending = *gpu->downloads.get(download_handle);
	pending.heap_offset = u32(begin_mapped);
	pending.num_bytes = num_bytes_original;
	pending.submit_idx = gpu->curr_submit_idx;

	const GpuTicket ticket = { download_handle.bits };
	return ticket;
}

sfz_extern_c void gpuGetDownloadedData(GpuLib* gpu, GpuTicket ticket, void* dst, u32 num_bytes)
{
	const SfzHandle handle = SfzHandle{ ticket.handle };
	GpuPendingDownload* pending = gpu->downloads.get(handle);
	if (pending == nullptr) {
		printf("[gpu_lib]: Invalid ticket.\n");
		return;
	}
	if (pending->num_bytes != num_bytes) {
		printf("[gpu_lib]: Memcpy download size mismatch, requested %u bytes, but %u was downloaded\n",
			num_bytes, pending->num_bytes);
		return;
	}
	if (gpu->known_completed_submit_idx < pending->submit_idx) {
		printf("[gpu_lib]: Memcpy download is not yet done.\n");
		return;
	}
	memcpy(dst, gpu->download_heap_mapped_ptr + pending->heap_offset, num_bytes);
	gpu->downloads.deallocate(handle);
}

sfz_extern_c void gpuQueueDispatch(
	GpuLib* gpu, GpuKernel kernel, i32x3 num_groups, const void* params, u32 params_size)
{
	GpuCmdListInfo& cmd_list_info = gpu->getCurrCmdList();

	// Ensure heap is in UNORDERED_ACCESS state
	if (gpu->gpu_heap_state != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = gpu->gpu_heap.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = gpu->gpu_heap_state;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		cmd_list_info.cmd_list->ResourceBarrier(1, &barrier);
		gpu->gpu_heap_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}

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
	cmd_list_info.cmd_list->SetComputeRootDescriptorTable(
		GPU_ROOT_PARAM_RW_TEX_ARRAY_IDX, gpu->tex_descriptor_heap_start_gpu);

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

sfz_extern_c void gpuQueueGpuHeapBarrier(GpuLib* gpu)
{
	if (gpu->gpu_heap_state != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
		printf("[gpu_lib]: Can't insert a gpu heap barrier, heap is in the wrong internal state.\n");
		return;
	}
	GpuCmdListInfo& cmd_list_info = gpu->getCurrCmdList();
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.UAV.pResource = gpu->gpu_heap.Get();
	cmd_list_info.cmd_list->ResourceBarrier(1, &barrier);
}

sfz_extern_c void gpuQueueRWTexBarrier(GpuLib* gpu, GpuRWTex tex_idx)
{
	const SfzHandle handle = gpu->rw_textures.getHandle(tex_idx);
	const GpuRWTexInfo* tex_info = gpu->rw_textures.get(handle);
	if (tex_info == nullptr) {
		printf("[gpu_lib]: Trying to insert a GpuRWTex barrier for idx %u, which doesn't exist.\n",
			u32(tex_idx));
		return;
	}
	GpuCmdListInfo& cmd_list_info = gpu->getCurrCmdList();
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.UAV.pResource = tex_info->tex.Get();
	cmd_list_info.cmd_list->ResourceBarrier(1, &barrier);
}

sfz_extern_c void gpuQueueRWTexBarriers(GpuLib* gpu)
{
	// Prepare barriers for all GpuRWTex
	gpu->tmp_barriers.clear();
	GpuRWTexInfo* tex_infos = gpu->rw_textures.data();
	const sfz::PoolSlot* slots = gpu->rw_textures.slots();
	const u32 array_size = gpu->rw_textures.arraySize();
	for (u32 idx = RWTEX_SWAPCHAIN_IDX; idx < array_size; idx++) {
		const sfz::PoolSlot slot = slots[idx];
		if (!slot.active()) continue;
		GpuRWTexInfo& info = tex_infos[idx];		
		D3D12_RESOURCE_BARRIER& barrier = gpu->tmp_barriers.add();
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.UAV.pResource = info.tex.Get();
	}

	// Set barriers
	GpuCmdListInfo& cmd_list_info = gpu->getCurrCmdList();
	cmd_list_info.cmd_list->ResourceBarrier(gpu->tmp_barriers.size(), gpu->tmp_barriers.data());
}

sfz_extern_c void gpuSubmitQueuedWork(GpuLib* gpu)
{
	// Copy contents from swapchain RT to actual swapchain
	if (gpu->swapchain != nullptr && gpu->swapchain_rwtex != nullptr) {
		GpuCmdListInfo& cmd_list_info = gpu->getCurrCmdList();

		// Grab current swapchain render target and descriptor heap
		const u32 curr_swapchain_fb_idx = gpu->swapchain->GetCurrentBackBufferIndex();
		sfz_assert(curr_swapchain_fb_idx < GPU_NUM_CONCURRENT_SUBMITS);
		ComPtr<ID3D12Resource> render_target;
		CHECK_D3D12(gpu->swapchain->GetBuffer(curr_swapchain_fb_idx, IID_PPV_ARGS(&render_target)));

		// Barriers to transition swapchain rwtex to COPY_SOURCE and swapchain backing to COPY_DEST.
		{
			D3D12_RESOURCE_BARRIER barriers[2] = {};

			barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barriers[0].Transition.pResource = gpu->swapchain_rwtex.Get();
			barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

			barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barriers[1].Transition.pResource = render_target.Get();
			barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

			cmd_list_info.cmd_list->ResourceBarrier(2, barriers);
		}

		// Copy contents of swapchain rt to actual backbuffer
		cmd_list_info.cmd_list->CopyResource(render_target.Get(), gpu->swapchain_rwtex.Get());

		// Barriers to transition swapchain rwtex to UNORDERED_ACCESS and swapchain backing to PRESENT.
		{
			D3D12_RESOURCE_BARRIER barriers[2] = {};

			barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barriers[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barriers[0].Transition.pResource = gpu->swapchain_rwtex.Get();
			barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
			barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

			barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barriers[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barriers[1].Transition.pResource = render_target.Get();
			barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

			cmd_list_info.cmd_list->ResourceBarrier(2, barriers);
		}
	}

	// Execute current command list
	{
		GpuCmdListInfo& cmd_list_info = gpu->getCurrCmdList();

		// Store current upload and download heap offsets
		cmd_list_info.upload_heap_offset = gpu->upload_heap_offset;
		cmd_list_info.download_heap_offset = gpu->download_heap_offset;

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

	// Log current deubg messages
	logDebugMessages(gpu->info_queue.Get());

	// Advance to next submit idx
	{
		gpu->curr_submit_idx += 1;
	}

	// Start next command list
	{
		GpuCmdListInfo& cmd_list_info = gpu->getCurrCmdList();

		// Wait until command list is done
		if (gpu->cmd_queue_fence->GetCompletedValue() < cmd_list_info.fence_value) {
			CHECK_D3D12(gpu->cmd_queue_fence->SetEventOnCompletion(
				cmd_list_info.fence_value, gpu->cmd_queue_fence_event));
			WaitForSingleObject(gpu->cmd_queue_fence_event, INFINITE);
		}

		// Now we know that the command list we just got has finished executing, thus we can set
		// our known completed submit idx to the idx of the submit it was from.
		gpu->known_completed_submit_idx =
			u64_max(gpu->known_completed_submit_idx, cmd_list_info.submit_idx);

		// Same applies to upload and download heap safe offsets. The safe offset is always + size
		// of the heap in question to handle wrap around in logic.
		gpu->upload_heap_safe_offset = u64_max(gpu->upload_heap_safe_offset, 
			cmd_list_info.upload_heap_offset + gpu->cfg.upload_heap_size_bytes);
		gpu->download_heap_safe_offset = u64_max(gpu->download_heap_safe_offset,
			cmd_list_info.download_heap_offset + gpu->cfg.download_heap_size_bytes);

		// Mark the new command list with the index of the current submit
		cmd_list_info.submit_idx = gpu->curr_submit_idx;

		if (!CHECK_D3D12(cmd_list_info.cmd_allocator->Reset())) {
			printf("[gpu_lib]: Couldn't reset command allocator\n");
			return;
		}
		if (!CHECK_D3D12(cmd_list_info.cmd_list->Reset(cmd_list_info.cmd_allocator.Get(), nullptr))) {
			printf("[gpu_lib]: Couldn't reset command list\n");
			return;
		}

		// Set texture descriptor heap
		ID3D12DescriptorHeap* heaps[] = { gpu->tex_descriptor_heap.Get() };
		cmd_list_info.cmd_list->SetDescriptorHeaps(1, heaps);
	}
}

sfz_extern_c void gpuSwapchainPresent(GpuLib* gpu, bool vsync)
{
	if (gpu->swapchain == nullptr) return;

	// Present swapchain's render target
	const u32 vsync_val = vsync ? 1 : 0; // Can specify 2-4 for vsync:ing on not every frame
	const u32 flags = (!vsync && gpu->cfg.allow_tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
	if (!CHECK_D3D12(gpu->swapchain->Present(vsync_val, flags))) {
		printf("[gpu_lib]: Present failure.\n");
		return;
	}

	// Get current window resolution
	i32x2 window_res = i32x2_splat(0);
	{
		const HWND hwnd = static_cast<const HWND>(gpu->cfg.native_window_handle);
		RECT rect = {};
		BOOL success = GetClientRect(hwnd, &rect);
		sfz_assert(success);
		window_res = i32x2_init(rect.right, rect.bottom);
	}

	if (window_res.x <= 0 || window_res.y <= 0) {
		printf("[gpu_lib]: Invalid window resolution.\n");
		sfz_assert(false);
		return;
	}
	gpu->swapchain_res = window_res;

	// Grab old swapchain resolution
	DXGI_SWAP_CHAIN_DESC swapchain_desc = {};
	CHECK_D3D12(gpu->swapchain->GetDesc(&swapchain_desc));
	sfz_assert(swapchain_desc.BufferCount == GPU_NUM_CONCURRENT_SUBMITS);
	const i32x2 old_swapchain_res =
		i32x2_init(swapchain_desc.BufferDesc.Width, swapchain_desc.BufferDesc.Height);
	
	// Resize swapchain if window resolution has changed
	if (old_swapchain_res != window_res) {
		printf("[gpu_lib]: Resizing swapchain framebuffers from %ix%i to %ix%i\n",
			old_swapchain_res.x, old_swapchain_res.y, window_res.x, window_res.y);

		// Flush current work in-progress
		gpuFlush(gpu);

		// Release old swapchain RT
		gpu->swapchain_rwtex.Reset();

		// Resize swapchain
		if (!CHECK_D3D12(gpu->swapchain->ResizeBuffers(
			GPU_NUM_CONCURRENT_SUBMITS,
			u32(window_res.x),
			u32(window_res.y),
			swapchain_desc.BufferDesc.Format,
			swapchain_desc.Flags))) {
			printf("[gpu_lib]: Failed to resize swapchain framebuffers\n");
			return;
		}

		// Allocate swapchain RT
		{
			D3D12_HEAP_PROPERTIES heap_props = {};
			heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
			heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			heap_props.CreationNodeMask = 0;
			heap_props.VisibleNodeMask = 0;

			D3D12_RESOURCE_DESC desc = {};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			desc.Alignment = 0;
			desc.Width = u32(window_res.x);
			desc.Height = u32(window_res.y);
			desc.DepthOrArraySize = 1;
			desc.MipLevels = 1;
			desc.Format = swapchain_desc.BufferDesc.Format;
			desc.SampleDesc = { 1, 0 };
			desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			desc.Flags =
				D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
				D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

			const bool rt_success = CHECK_D3D12(gpu->device->CreateCommittedResource(
				&heap_props,
				D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
				&desc,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				nullptr,
				IID_PPV_ARGS(&gpu->swapchain_rwtex)));
			if (!rt_success) {
				printf("[gpu_lib]: Could not allocate swapchain render target of size %ix%i.\n",
					window_res.x, window_res.y);
				return;
			}
			setDebugName(gpu->swapchain_rwtex.Get(), "swapchain_rwtex");
		}

		// Set swapchain RT descriptor in tex descriptor heap
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
			uav_desc.Format = swapchain_desc.BufferDesc.Format;
			uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
			uav_desc.Texture2D.MipSlice = 0;
			uav_desc.Texture2D.PlaneSlice = 0;

			D3D12_CPU_DESCRIPTOR_HANDLE cpu_descriptor = {};
			cpu_descriptor.ptr =
				gpu->tex_descriptor_heap_start_cpu.ptr + gpu->tex_descriptor_size * RWTEX_SWAPCHAIN_IDX;
			gpu->device->CreateUnorderedAccessView(gpu->swapchain_rwtex.Get(), nullptr, &uav_desc, cpu_descriptor);
		}

		// Rebuild all swapchain relative GpuRWTex
		GpuRWTexInfo* tex_infos = gpu->rw_textures.data();
		const sfz::PoolSlot* tex_slots = gpu->rw_textures.slots();
		const u32 tex_array_size = gpu->rw_textures.arraySize();
		for (u32 idx = RWTEX_SWAPCHAIN_IDX + 1; idx < tex_array_size; idx++) {
			const sfz::PoolSlot slot = tex_slots[idx];
			if (!slot.active()) continue;
			GpuRWTexInfo& tex_info = tex_infos[idx];
			if (!tex_info.desc.swapchain_relative) continue;
			const SfzHandle tex_handle = gpu->rw_textures.getHandle(idx);
			sfz_assert(tex_handle != SFZ_NULL_HANDLE);

			// Rebuild texture
			// Need to copy desc to avoid potential aliasing issues
			SfzStr96 name = tex_info.name;
			GpuRWTexDesc desc = tex_info.desc;
			desc.name = name.str;
			gpuRWTexInitInternal(gpu, &desc, &tex_handle);
		}
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

	// Since we have flushed all submitted work, it stands to reason that it must have completed.
	// Update known completed submit idx accordingly
	gpu->known_completed_submit_idx = gpu->curr_submit_idx > 0 ? gpu->curr_submit_idx - 1 : 0;

	// Same applies to upload and download heap safe offset. The safe offset is always + size of
	// the heap in question to handle wrap around in logic.
	gpu->upload_heap_safe_offset = u64_max(gpu->upload_heap_safe_offset,
		gpu->getPrevCmdList().upload_heap_offset + gpu->cfg.upload_heap_size_bytes);
	gpu->download_heap_safe_offset = u64_max(gpu->download_heap_safe_offset,
		gpu->getPrevCmdList().download_heap_offset + gpu->cfg.download_heap_size_bytes);
}
