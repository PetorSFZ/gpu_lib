typedef int i32;
typedef int2 i32x2;
typedef int3 i32x3;
typedef int4 i32x4;
typedef float4 f32x4;

RWByteAddressBuffer gpu_global_heap : register(u0);
RWTexture2D<f32x4> gpu_rwtex_array[] : register(u1);

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
}
