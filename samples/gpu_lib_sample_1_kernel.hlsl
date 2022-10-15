typedef uint16_t u16;
typedef int i32;
typedef int2 i32x2;
typedef int3 i32x3;
typedef int4 i32x4;
typedef float4 f32x4;

cbuffer LaunchParams : register(b0) {
	i32x2 res;
	GpuPtr color_ptr;
	GpuRWTex tex_idx;
	u16 padding;
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

	i32x2 tex_res = i32x2(0, 0);
	RWTexture2D<f32x4> tex = getRWTex(tex_idx, tex_res);
	RWTexture2D<f32x4> swapchain_tex = getSwapchainRWTex();
	if (idx.x < tex_res.x && idx.y < tex_res.y) {
		swapchain_tex[idx] = tex[idx];
	}
	else {
		swapchain_tex[idx] = ptrLoad<float4>(color_ptr);
	}
}
