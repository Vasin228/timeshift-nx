// AMD FidelityFX Super Resolution 1.0 - EASU (Edge-Adaptive Spatial Upsampling).
// Paired with present_vs: input.uv addresses the active source sub-rectangle.
#include "utility.hlsli"

struct PSInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float FsrLuma(float3 c)
{
    return c.b * 0.5 + (c.r * 0.5 + c.g);
}

void FsrEasuSet(inout float2 dir, inout float len, float w,
                float lA, float lB, float lC, float lD, float lE)
{
    float lenX = max(abs(lD - lC), abs(lC - lB));
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = clamp(abs(dirX) / max(lenX, 1e-5), 0.0, 1.0);
    lenX *= lenX;
    len += lenX * w;

    float lenY = max(abs(lE - lC), abs(lC - lA));
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = clamp(abs(dirY) / max(lenY, 1e-5), 0.0, 1.0);
    lenY *= lenY;
    len += lenY * w;
}

void FsrEasuTap(inout float3 aC, inout float aW, float2 off,
                float2 dir, float2 len, float lob, float clp, float3 c)
{
    float2 v = float2(dot(off, dir), dot(off, float2(-dir.y, dir.x))) * len;
    float d2 = min(dot(v, v), clp);
    float wB = (2.0 / 5.0) * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = (25.0 / 16.0) * wB - (25.0 / 16.0 - 1.0);
    float w = wB * wA;
    aC += c * w;
    aW += w;
}

float4 main(PSInput input) : SV_Target0
{
    Texture2D<float4> src = g_Textures[NonUniformResourceIndex(g_Push.textureIndex)];
    uint texW = 1u, texH = 1u;
    src.GetDimensions(texW, texH);
    const float2 dim = float2(max(texW, 1u), max(texH, 1u));
    const int2 lo = int2(floor(g_Push.translate * dim));
    const int2 hi = min(max(int2(ceil((g_Push.translate + g_Push.scale) * dim)) - 1, lo), int2(texW, texH) - 1);

    const float2 pp = input.uv * dim - 0.5;
    const int2 fp = int2(floor(pp));
    const float2 f = pp - float2(fp);

    const float3 cB = src.Load(int3(clamp(fp + int2( 0, -1), lo, hi), 0)).rgb;
    const float3 cC = src.Load(int3(clamp(fp + int2( 1, -1), lo, hi), 0)).rgb;
    const float3 cE = src.Load(int3(clamp(fp + int2(-1,  0), lo, hi), 0)).rgb;
    const float3 cF = src.Load(int3(clamp(fp + int2( 0,  0), lo, hi), 0)).rgb;
    const float3 cG = src.Load(int3(clamp(fp + int2( 1,  0), lo, hi), 0)).rgb;
    const float3 cH = src.Load(int3(clamp(fp + int2( 2,  0), lo, hi), 0)).rgb;
    const float3 cI = src.Load(int3(clamp(fp + int2(-1,  1), lo, hi), 0)).rgb;
    const float3 cJ = src.Load(int3(clamp(fp + int2( 0,  1), lo, hi), 0)).rgb;
    const float3 cK = src.Load(int3(clamp(fp + int2( 1,  1), lo, hi), 0)).rgb;
    const float3 cL = src.Load(int3(clamp(fp + int2( 2,  1), lo, hi), 0)).rgb;
    const float3 cN = src.Load(int3(clamp(fp + int2( 0,  2), lo, hi), 0)).rgb;
    const float3 cO = src.Load(int3(clamp(fp + int2( 1,  2), lo, hi), 0)).rgb;

    const float lB = FsrLuma(cB), lC = FsrLuma(cC);
    const float lE = FsrLuma(cE), lF = FsrLuma(cF), lG = FsrLuma(cG), lH = FsrLuma(cH);
    const float lI = FsrLuma(cI), lJ = FsrLuma(cJ), lK = FsrLuma(cK), lL = FsrLuma(cL);
    const float lN = FsrLuma(cN), lO = FsrLuma(cO);

    float2 dir = float2(0.0, 0.0);
    float len = 0.0;
    FsrEasuSet(dir, len, (1.0 - f.x) * (1.0 - f.y), lB, lE, lF, lG, lJ);
    FsrEasuSet(dir, len,        f.x  * (1.0 - f.y), lC, lF, lG, lH, lK);
    FsrEasuSet(dir, len, (1.0 - f.x) *        f.y , lF, lI, lJ, lK, lN);
    FsrEasuSet(dir, len,        f.x  *        f.y , lG, lJ, lK, lL, lO);

    const float dir2 = dot(dir, dir);
    const bool zro = dir2 < (1.0 / 32768.0);
    dir = zro ? float2(1.0, 0.0) : (dir * rsqrt(dir2));
    len = zro ? 0.0 : len;

    len *= 0.5;
    len *= len;
    const float stretch = 1.0 / max(abs(dir.x), abs(dir.y));
    const float2 len2 = float2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    const float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    const float clp = 1.0 / lob;

    float3 aC = float3(0.0, 0.0, 0.0);
    float aW = 0.0;
    FsrEasuTap(aC, aW, float2( 0.0, -1.0) - f, dir, len2, lob, clp, cB);
    FsrEasuTap(aC, aW, float2( 1.0, -1.0) - f, dir, len2, lob, clp, cC);
    FsrEasuTap(aC, aW, float2(-1.0,  0.0) - f, dir, len2, lob, clp, cE);
    FsrEasuTap(aC, aW, float2( 0.0,  0.0) - f, dir, len2, lob, clp, cF);
    FsrEasuTap(aC, aW, float2( 1.0,  0.0) - f, dir, len2, lob, clp, cG);
    FsrEasuTap(aC, aW, float2( 2.0,  0.0) - f, dir, len2, lob, clp, cH);
    FsrEasuTap(aC, aW, float2(-1.0,  1.0) - f, dir, len2, lob, clp, cI);
    FsrEasuTap(aC, aW, float2( 0.0,  1.0) - f, dir, len2, lob, clp, cJ);
    FsrEasuTap(aC, aW, float2( 1.0,  1.0) - f, dir, len2, lob, clp, cK);
    FsrEasuTap(aC, aW, float2( 2.0,  1.0) - f, dir, len2, lob, clp, cL);
    FsrEasuTap(aC, aW, float2( 0.0,  2.0) - f, dir, len2, lob, clp, cN);
    FsrEasuTap(aC, aW, float2( 1.0,  2.0) - f, dir, len2, lob, clp, cO);

    const float3 min4 = min(min(cF, cG), min(cJ, cK));
    const float3 max4 = max(max(cF, cG), max(cJ, cK));
    return float4(saturate(clamp(aC / max(aW, 1e-5), min4, max4)), 1.0);
}
