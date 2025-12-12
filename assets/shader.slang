/* Copyright (c) 2025, Sascha Willems
 *
 * SPDX-License-Identifier: MIT
 *
 */

struct VSInput {
	float3 Pos;
	float2 UV;
};

struct UBO {
	float4x4 mvp;
};
[[vk::binding(0,0)]] ConstantBuffer<UBO> ubo;

[[vk::binding(0,1)]] Sampler2D samplerTexture;

struct VSOutput {
	float4 Pos : SV_POSITION;
	float2 UV;
};

[shader("vertex")]
VSOutput main(VSInput input) {
	VSOutput output;
	output.UV = input.UV;
	output.Pos = mul(ubo.mvp, float4(input.Pos.xyz, 1.0));
	return output;
}

[shader("fragment")]
float4 main(VSOutput input) {
	return float4(samplerTexture.Sample(input.UV).rgb, 1.0);
}