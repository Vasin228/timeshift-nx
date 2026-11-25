#include "utility.hlsli"

struct PSInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0
{
    Texture2D<float4> src = g_Textures[NonUniformResourceIndex(g_Push.textureIndex)];
    SamplerState samp = g_Samplers[NonUniformResourceIndex(g_Push.samplerIndex)];
    float4 color = src.SampleLevel(samp, input.uv, 0.0);

    // Negative param0 enables single-pass FXAA 3.11 Console smoothing; magnitude is output gamma.
    const bool useFxaa = g_Push.param0 < 0.0;
    const float gamma = abs(g_Push.param0);
    if (useFxaa)
    {
        uint texW = 1u, texH = 1u;
        src.GetDimensions(texW, texH);
        const float2 rcp = 1.0 / float2(max(texW, 1u), max(texH, 1u));
        const float2 halfRcp = rcp * 0.5;
        const float3 lumaCoeff = float3(0.299, 0.587, 0.114);

        const float3 rgbM = color.rgb;
        const float3 rgbNW = src.SampleLevel(samp, input.uv + float2(-halfRcp.x, -halfRcp.y), 0.0).rgb;
        const float3 rgbNE = src.SampleLevel(samp, input.uv + float2( halfRcp.x, -halfRcp.y), 0.0).rgb;
        const float3 rgbSW = src.SampleLevel(samp, input.uv + float2(-halfRcp.x,  halfRcp.y), 0.0).rgb;
        const float3 rgbSE = src.SampleLevel(samp, input.uv + float2( halfRcp.x,  halfRcp.y), 0.0).rgb;

        const float lumaM  = dot(rgbM,  lumaCoeff);
        const float lumaNW = dot(rgbNW, lumaCoeff);
        const float lumaNE = dot(rgbNE, lumaCoeff);
        const float lumaSW = dot(rgbSW, lumaCoeff);
        const float lumaSE = dot(rgbSE, lumaCoeff);

        const float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
        const float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
        const float lumaRange = lumaMax - lumaMin;

        if (lumaRange >= max(0.05, lumaMax * 0.125))
        {
            float2 dir = float2(
                -((lumaNW + lumaNE) - (lumaSW + lumaSE)),
                 ((lumaNW + lumaSW) - (lumaNE + lumaSE)));
            const float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125, 1.0 / 128.0);
            const float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
            dir = clamp(dir * rcpDirMin, float2(-4.0, -4.0), float2(4.0, 4.0)) * rcp;

            const float3 rgbA = 0.5 * (
                src.SampleLevel(samp, input.uv + dir * (1.0 / 3.0 - 0.5), 0.0).rgb +
                src.SampleLevel(samp, input.uv + dir * (2.0 / 3.0 - 0.5), 0.0).rgb);
            const float3 rgbB = rgbA * 0.5 + 0.25 * (
                src.SampleLevel(samp, input.uv + dir * -0.5, 0.0).rgb +
                src.SampleLevel(samp, input.uv + dir *  0.5, 0.0).rgb);
            const float lumaB = dot(rgbB, lumaCoeff);
            color.rgb = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
        }
    }

    // sa2-nx (2026-10-20): param1's bits, when not 0, = the bindless slot of the game's display gamma ramp
    // (SetGammaRamp; the Xbox 360's display controller applies it at scan-out): 256 texels of red, green, blue in 0-1.
    // The same lookup as the emulated GPU's and sa2-nx's native renderer's output pass: each channel's 8-bit value picks
    // its entry. The slot is read as it is, with no arithmetic: an emulator (Ryujinx) follows a bindless handle only
    // back to a constant buffer load, and a computed one read zero (a black picture).
    const uint ramp = asuint(g_Push.param1);
    if (ramp != 0u)
    {
        const uint3 i = uint3(saturate(color.rgb) * 255.0 + 0.5);
        color.rgb = float3(g_Textures[NonUniformResourceIndex(ramp)].Load(int3(i.x, 0, 0)).x,
                           g_Textures[NonUniformResourceIndex(ramp)].Load(int3(i.y, 0, 0)).y,
                           g_Textures[NonUniformResourceIndex(ramp)].Load(int3(i.z, 0, 0)).z);
    }
    // gamma = output gamma exponent (1 = passthrough). sa2-nx: skipped at 1, where pow's exp2(log2) could move a
    // value by an ulp.
    if (gamma > 0.0 && gamma != 1.0)
        color.rgb = pow(saturate(color.rgb), gamma);
    return float4(color.rgb, 1.0);
}
