// NOTE: Assumes row-major matrices (standard HLSL). If uploading OpenGL column-major
//       matrices, either transpose before upload or declare float4x4 as column_major.

// =============================================================================
// VERTEX SHADER
// =============================================================================

static const int MAX_BONES          = 128;
static const int MAX_BONE_INFLUENCE = 4;

cbuffer PerObject : register(b0)
{
    float4x4 modelMatrix;
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
    float4x4 normalMatrix;
    float4x4 lightSpaceMatrix;
    float4x4 finalBonesMatrices[MAX_BONES];
};

struct VSInput
{
    float3 position  : POSITION;
    float3 color     : COLOR;
    float2 texCoord  : TEXCOORD0;
    float3 normal    : NORMAL;
    float3 tangent   : TANGENT;
    float3 bitangent : BINORMAL;
    int4   boneIDs   : BLENDINDICES;
    float4 weights   : BLENDWEIGHT;
};

struct VSOutput
{
    float4 position      : SV_Position;
    float3 worldPos      : TEXCOORD0;
    float3 color         : TEXCOORD1;
    float2 texCoord      : TEXCOORD2;
    float4 lightSpacePos : TEXCOORD3;
    float3 T             : TEXCOORD4;
    float3 B             : TEXCOORD5;
    float3 N             : TEXCOORD6;
};

float3x3 inverse3x3(float3x3 m)
{
    float det = m[0][0] * (m[1][1]*m[2][2] - m[1][2]*m[2][1])
              - m[0][1] * (m[1][0]*m[2][2] - m[1][2]*m[2][0])
              + m[0][2] * (m[1][0]*m[2][1] - m[1][1]*m[2][0]);
    float inv = 1.0 / det;
    float3x3 r;
    r[0][0] =  (m[1][1]*m[2][2] - m[1][2]*m[2][1]) * inv;
    r[0][1] = -(m[0][1]*m[2][2] - m[0][2]*m[2][1]) * inv;
    r[0][2] =  (m[0][1]*m[1][2] - m[0][2]*m[1][1]) * inv;
    r[1][0] = -(m[1][0]*m[2][2] - m[1][2]*m[2][0]) * inv;
    r[1][1] =  (m[0][0]*m[2][2] - m[0][2]*m[2][0]) * inv;
    r[1][2] = -(m[0][0]*m[1][2] - m[0][2]*m[1][0]) * inv;
    r[2][0] =  (m[1][0]*m[2][1] - m[1][1]*m[2][0]) * inv;
    r[2][1] = -(m[0][0]*m[2][1] - m[0][1]*m[2][0]) * inv;
    r[2][2] =  (m[0][0]*m[1][1] - m[0][1]*m[1][0]) * inv;
    return r;
}

VSOutput VSMain(VSInput input)
{
    float4 totalPos       = float4(input.position, 1.0);
    float3 totalNormal    = float3(0, 0, 0);
    float3 totalTangent   = float3(0, 0, 0);
    float3 totalBitangent = float3(0, 0, 0);
    float  totalWeight    = 0.0;

    for (int i = 0; i < MAX_BONE_INFLUENCE; i++)
    {
        int   boneID = input.boneIDs[i];
        float weight = input.weights[i];
        if (boneID < 0 || weight <= 0.0) continue;
        if (boneID >= MAX_BONES)
        {
            totalPos       = float4(input.position, 1.0);
            totalNormal    = input.normal;
            totalTangent   = input.tangent;
            totalBitangent = input.bitangent;
            break;
        }
        totalPos       += mul(finalBonesMatrices[boneID], float4(input.position, 1.0)) * weight;
        float3x3 nm     = transpose(inverse3x3((float3x3)finalBonesMatrices[boneID]));
        totalNormal    += mul(nm, input.normal)    * weight;
        totalTangent   += mul(nm, input.tangent)   * weight;
        totalBitangent += mul(nm, input.bitangent) * weight;
        totalWeight    += weight;
    }
    if (totalWeight == 0.0)
    {
        totalPos       = float4(input.position, 1.0);
        totalNormal    = input.normal;
        totalTangent   = input.tangent;
        totalBitangent = input.bitangent;
    }

    bool   anim    = totalWeight > 0.0;
    float3 useN    = anim ? totalNormal    : input.normal;
    float3 useT    = anim ? totalTangent   : input.tangent;
    float3 useB    = anim ? totalBitangent : input.bitangent;

    float3x3 nm3      = (float3x3)normalMatrix;
    float3   worldPos = mul(modelMatrix, totalPos).xyz;

    VSOutput o;
    o.worldPos      = worldPos;
    o.color         = input.color;
    o.texCoord      = input.texCoord;
    o.lightSpacePos = mul(lightSpaceMatrix, float4(worldPos, 1.0));
    o.T             = mul(nm3, useT);
    o.B             = mul(nm3, useB);
    o.N             = mul(nm3, useN);
    o.position      = mul(projectionMatrix, mul(viewMatrix, float4(worldPos, 1.0)));
    return o;
}

// =============================================================================
// PIXEL SHADER
// =============================================================================

struct Material
{
    float2 tiling;
    float2 _pad0;
    float3 ambient;
    float  _pad1;
    float3 diffuse;
    float  _pad2;
    float3 specular;
    float  shininess;
    float  metallic;
    float  roughness;
    float  glossiness;
    float  _pad3;
};

struct SunLight
{
    float  power;
    float3 direction;
    float3 diffuse;
    float  _pad0;
    float3 specular;
    float  _pad1;
};

struct PointLight
{
    float  power;
    float3 position;
    float  constant;
    float  linear;
    float  quadratic;
    float  _pad0;
    float3 diffuse;
    float  _pad1;
    float3 specular;
    float  _pad2;
};

struct SpotLight
{
    float  power;
    float3 position;
    float3 direction;
    float  cutOff;
    float  outerCutOff;
    float  constant;
    float  linear;
    float  quadratic;
    float3 diffuse;
    float  _pad0;
    float3 specular;
    float  _pad1;
};

cbuffer PerMaterial : register(b1)
{
    Material material;
    uint     has_albedoMap;
    uint     has_normalMap;
    uint     has_specularMap;
    uint     has_glossinessMap;
    uint     has_emissiveMap;
    uint3    _matFlagPad;
};

cbuffer PerScene : register(b2)
{
    float3 viewPos;
    float  _scenePad0;
    float3 sceneAmbient;
    float  _scenePad1;
    uint   skyboxAmbientEnabled;
    float  skyboxAmbientStrength;
    float2 _scenePad2;
    int    sunLightNum;
    int    pointLightNum;
    int    spotLightNum;
    int    _scenePad3;
    SunLight   sunLights[32];
    PointLight pointLights[32];
    SpotLight  spotLights[32];
};

Texture2D    albedoMap      : register(t0);
Texture2D    normalMap      : register(t1);
Texture2D    specularMap    : register(t2);
Texture2D    emissiveMap    : register(t3);
TextureCube  skyboxMap      : register(t4);
Texture2D    glossinessMap  : register(t5);
SamplerState defaultSampler : register(s0);

float3 calcSunLight(SunLight light, float3 norm, float3 vdir, float3 specTex, float shininess)
{
    float3 ldir  = normalize(-light.direction);
    float  diff  = max(dot(norm, ldir), 0.0);
    float  shine = max(shininess, 1.0);
    float  spec  = pow(max(dot(vdir, reflect(-ldir, norm)), 0.001), shine);
    return (light.diffuse * material.diffuse * diff +
            light.specular * material.specular * spec * specTex) * light.power;
}

float3 calcPointLight(PointLight light, float3 norm, float3 fragPos, float3 vdir, float3 specTex, float shininess)
{
    float3 ldir  = normalize(light.position - fragPos);
    float  diff  = max(dot(norm, ldir), 0.0);
    float  shine = max(shininess, 1.0);
    float  spec  = pow(max(dot(vdir, reflect(-ldir, norm)), 0.001), shine);
    float  dist  = length(light.position - fragPos);
    float  att   = light.power / (light.constant + light.linear * dist + light.quadratic * dist * dist);
    return (light.diffuse * material.diffuse * diff +
            light.specular * material.specular * spec * specTex) * att;
}

float3 calcSpotLight(SpotLight light, float3 norm, float3 fragPos, float3 vdir, float3 specTex, float shininess)
{
    float3 ldir   = normalize(light.position - fragPos);
    float  diff   = max(dot(norm, ldir), 0.0);
    float  shine  = max(shininess, 1.0);
    float  spec   = pow(max(dot(vdir, reflect(-ldir, norm)), 0.001), shine);
    float  theta  = dot(ldir, normalize(-light.direction));
    float  eps    = light.cutOff - light.outerCutOff;
    float  intens = clamp((theta - light.outerCutOff) / eps, 0.0, 1.0);
    float  dist   = length(light.position - fragPos);
    float  att    = 1.0 / (light.constant + light.linear * dist + light.quadratic * dist * dist);
    return (light.diffuse * material.diffuse * diff +
            light.specular * material.specular * spec * specTex) * light.power * intens * att;
}

float4 PSMain(VSOutput input) : SV_Target
{
    float2 uv = input.texCoord * material.tiling;

    float4 diffuseTex  = has_albedoMap   ? albedoMap.Sample(defaultSampler,   uv) : float4(1, 1, 1, 1);
    float4 normalTex   = has_normalMap   ? normalMap.Sample(defaultSampler,   uv) : float4(0.5, 0.5, 1.0, 1.0);
    float4 specularTex = has_specularMap ? specularMap.Sample(defaultSampler, uv) : float4(1, 1, 1, 1);
    float4 emissiveTex = has_emissiveMap ? emissiveMap.Sample(defaultSampler, uv) : float4(0, 0, 0, 0);

    // Glossiness scales specular power; the optional map modulates it per-pixel (R).
    float  gloss     = material.glossiness * (has_glossinessMap ? glossinessMap.Sample(defaultSampler, uv).r : 1.0);
    float  shininess = material.shininess * gloss;

    float3 Tn = normalize(input.T);
    float3 Bn = normalize(input.B);
    float3 Nn = normalize(input.N);

    float3 tn = normalTex.rgb;
    tn.g      = 1.0 - tn.g;
    tn        = normalize(tn * 2.0 - 1.0);
    float3 norm = normalize(Tn * tn.x + Bn * tn.y + Nn * tn.z);

    float3 vdir   = normalize(viewPos - input.worldPos);
    float3 result = sceneAmbient * material.ambient;

    if (skyboxAmbientEnabled)
        result += skyboxMap.Sample(defaultSampler, norm).rgb * skyboxAmbientStrength * material.ambient;

    for (int i = 0; i < sunLightNum;   i++) result += calcSunLight  (sunLights[i],   norm, vdir, specularTex.xyz, shininess);
    for (int i = 0; i < pointLightNum; i++) result += calcPointLight(pointLights[i], norm, input.worldPos, vdir, specularTex.xyz, shininess);
    for (int i = 0; i < spotLightNum;  i++) result += calcSpotLight (spotLights[i],  norm, input.worldPos, vdir, specularTex.xyz, shininess);

    return float4(clamp(result, 0.0, 1.0), 1.0) * diffuseTex + emissiveTex;
}
