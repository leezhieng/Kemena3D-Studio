// NOTE: Assumes row-major matrices (standard HLSL). If uploading OpenGL column-major
//       matrices, either transpose before upload or declare float4x4 as column_major.

// =============================================================================
// VERTEX SHADER
// =============================================================================

cbuffer PerBillboard : register(b0)
{
    float3   cameraRightWorldSpace;
    float    _pad0;
    float3   cameraUpWorldSpace;
    float    _pad1;
    float4x4 viewProjection;
    float3   billboardPosition;
    float    _pad2;
    float2   billboardSize;
    float2   _pad3;
};

struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    float3 worldPos = billboardPosition
        + cameraRightWorldSpace * input.position.x * billboardSize.x
        + cameraUpWorldSpace    * input.position.y * billboardSize.y;

    VSOutput o;
    // Flip V: images are uploaded top-row-first, but the quad's bottom maps to
    // v=0 — without this every icon samples upside-down.
    o.uv       = float2(input.position.x + 0.5, 0.5 - input.position.y);
    o.position = mul(viewProjection, float4(worldPos, 1.0));
    return o;
}

// =============================================================================
// PIXEL SHADER
// =============================================================================

static const float ALPHA_CUTOFF = 0.3;

// --- Drop shadow (adjustable) ------------------------------------------------
// Offset is in UV space: +x moves the shadow right, +y moves it down on screen.
// Set SHADOW_OPACITY to 0.0 to disable the shadow entirely.
static const float2 SHADOW_OFFSET   = float2(0.07, 0.07);
static const float3 SHADOW_COLOR    = float3(0.0, 0.0, 0.0);
static const float  SHADOW_OPACITY  = 0.55;
static const float  SHADOW_SOFTNESS = 0.012; // UV blur radius (0 = hard edge)

cbuffer PerIcon : register(b1)
{
    float3 color;
    float  _pad;
};

Texture2D    albedoMap      : register(t0);
SamplerState defaultSampler : register(s0);

// Icon coverage at uv. When softness > 0 a 3x3 tap average gives a soft shadow.
float iconCoverage(float2 uv)
{
    if (SHADOW_SOFTNESS <= 0.0)
        return step(ALPHA_CUTOFF, albedoMap.Sample(defaultSampler, uv).a);
    float a = 0.0;
    [unroll] for (int x = -1; x <= 1; ++x)
        [unroll] for (int y = -1; y <= 1; ++y)
            a += step(ALPHA_CUTOFF, albedoMap.Sample(defaultSampler, uv + float2(x, y) * SHADOW_SOFTNESS).a);
    return a / 9.0;
}

float4 PSMain(VSOutput input) : SV_Target
{
    float4 tex = albedoMap.Sample(defaultSampler, input.uv);
    if (tex.a >= ALPHA_CUTOFF)
        return tex * float4(color, 1.0);

    // Outside the icon — draw its drop shadow (the icon shape shifted by offset).
    float s = (SHADOW_OPACITY > 0.0) ? iconCoverage(input.uv - SHADOW_OFFSET) : 0.0;
    if (s > 0.0)
        return float4(SHADOW_COLOR, SHADOW_OPACITY * s);

    clip(-1); // discard
    return float4(0.0, 0.0, 0.0, 0.0);
}
