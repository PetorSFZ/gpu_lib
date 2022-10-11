cbuffer LaunchParams : register(b0) {
	i32x2 res;
	i32 padding1;
	i32 padding2;
}

[numthreads(16, 16, 1)]
void CSMain(
	i32x3 group_idx : SV_GroupID, // Index of group
	i32 group_flat_idx : SV_GroupIndex, // Flattened version of group index
	i32x3 group_thread_idx : SV_GroupThreadID, // Index of thread within group
	i32x3 dispatch_thread_idx : SV_DispatchThreadID) // Global index of thread
{
	const i32x2 idx = dispatch_thread_idx.xy;
	if (res.x <= idx.x || res.y <= idx.y) return;

	if (idx.x == 0 && idx.y == 0) gpu_global_heap.Store(0, 42);

	RWTexture2D<f32x4> swapchain_rt = gpu_rwtex_array[0];
	swapchain_rt[idx] = f32x4(0.0, 1.0, 0.0, 1.0);
#ifdef A_DEFINE
	swapchain_rt[idx] = f32x4(1.0, 0.0, 0.0, 1.0);
#endif
}
