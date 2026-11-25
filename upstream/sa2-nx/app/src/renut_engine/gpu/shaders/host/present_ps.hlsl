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

    // param0 < -20.0 enables single-pass FidelityFX CAS sharpening (-(20.0 + sharpness));
    // -20.0 <= param0 < -10.0 enables FSR 1 RCAS pass after EASU (-(10.0 + sharpness));
    // -10.0 <= param0 < 0.0 enables single-pass FXAA 3.11 Console smoothing; magnitude is output gamma.
    const bool useCas = g_Push.param0 < -20.0;
    const bool useRcas = !useCas && g_Push.param0 < -10.0;
    const bool useFxaa = !useCas && !useRcas && g_Push.param0 < 0.0;
    const float gamma = (useCas || useRcas) ? 1.0 : abs(g_Push.param0);
    float4 color = float4(0.0, 0.0, 0.0, 1.0);
    if (useCas)
    {
        uint texW = 1u, texH = 1u;
        src.GetDimensions(texW, texH);
        const float2 rcp = 1.0 / float2(max(texW, 1u), max(texH, 1u));
        const float2 uvMin = g_Push.translate + rcp * 0.5;
        const float2 uvMax = max(uvMin, g_Push.translate + g_Push.scale - rcp * 0.5);

        const float3 e = src.SampleLevel(samp, input.uv, 0.0).rgb;
        const float3 b = src.SampleLevel(samp, clamp(input.uv + float2( 0.0, -rcp.y), uvMin, uvMax), 0.0).rgb;
        const float3 d = src.SampleLevel(samp, clamp(input.uv + float2(-rcp.x,  0.0), uvMin, uvMax), 0.0).rgb;
        const float3 f = src.SampleLevel(samp, clamp(input.uv + float2( rcp.x,  0.0), uvMin, uvMax), 0.0).rgb;
        const float3 h = src.SampleLevel(samp, clamp(input.uv + float2( 0.0,  rcp.y), uvMin, uvMax), 0.0).rgb;

        const float3 mn = min(min(b, d), min(f, h));
        const float3 mn2 = min(mn, e);
        const float3 mx = max(max(b, d), max(f, h));
        const float3 mx2 = max(mx, e);
        const float3 amp = saturate(min(mn2, 2.0 - mx2) / max(mx2, 1e-5));
        const float casSharp = clamp(-g_Push.param0 - 20.0, 0.0, 1.0);
        const float peak = -1.0 / lerp(8.0, 5.0, casSharp);
        const float3 w = sqrt(amp) * peak;
        color.rgb = saturate(((b + d + f + h) * w + e) / (1.0 + 4.0 * w));
    }
    else if (useRcas)
    {
        uint texW = 1u, texH = 1u;
        src.GetDimensions(texW, texH);
        const int2 lo = int2(0, 0);
        const int2 hi = int2(max(texW, 1u), max(texH, 1u)) - 1;
        const int2 p = clamp(int2(floor(input.uv * float2(max(texW, 1u), max(texH, 1u)))), lo, hi);

        const float3 b = src.Load(int3(clamp(p + int2( 0, -1), lo, hi), 0)).rgb;
        const float3 d = src.Load(int3(clamp(p + int2(-1,  0), lo, hi), 0)).rgb;
        const float3 e = src.Load(int3(p, 0)).rgb;
        const float3 f = src.Load(int3(clamp(p + int2( 1,  0), lo, hi), 0)).rgb;
        const float3 h = src.Load(int3(clamp(p + int2( 0,  1), lo, hi), 0)).rgb;

        const float bL = b.b * 0.5 + (b.r * 0.5 + b.g);
        const float dL = d.b * 0.5 + (d.r * 0.5 + d.g);
        const float eL = e.b * 0.5 + (e.r * 0.5 + e.g);
        const float fL = f.b * 0.5 + (f.r * 0.5 + f.g);
        const float hL = h.b * 0.5 + (h.r * 0.5 + h.g);

        const float lumaRange = max(max(max(bL, dL), max(fL, hL)), eL) -
                                min(min(min(bL, dL), min(fL, hL)), eL);
        float nz = 0.25 * (bL + dL + fL + hL) - eL;
        nz = clamp(abs(nz) / max(lumaRange, 1e-5), 0.0, 1.0);
        nz = -0.5 * nz + 1.0;

        const float3 mn4 = min(min(b, d), min(f, h));
        const float3 mx4 = max(max(b, d), max(f, h));
        const float3 mn = min(mn4, e);
        const float3 mx = max(mx4, e);
        const float3 hitMin = mn / max(4.0 * mx, 1e-5);
        const float3 hitMax = (1.0 - mx) / min(4.0 * mn - 4.0, -1e-5);
        const float3 lobeRGB = max(-hitMin, hitMax);
        const float rcasSharp = clamp(-g_Push.param0 - 10.0, 0.0, 1.0);
        const float lobe = max(-0.1875, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * rcasSharp * nz;
        color.rgb = saturate((e + lobe * (b + d + f + h)) / (1.0 + 4.0 * lobe));
    }
    else if (useFxaa)
    {
        color = src.SampleLevel(samp, input.uv, 0.0);
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
    else
    {
        color = src.SampleLevel(samp, input.uv, 0.0);
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
