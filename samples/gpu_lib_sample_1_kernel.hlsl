typedef int i32;
typedef int2 i32x2;
typedef int3 i32x3;
typedef int4 i32x4;
typedef float4 f32x4;

cbuffer LaunchParams : register(b0) {
	i32x2 res;
	GpuPtr color_ptr;
	i32 padding;
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

	RWTexture2D<f32x4> swapchain_tex = getSwapchainRWTex();
	swapchain_tex[idx] = ptrLoad<float4>(color_ptr);
}
