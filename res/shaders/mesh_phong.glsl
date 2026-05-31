#version 330 core

const int MAX_BONES         = 128;
const int MAX_BONE_INFLUENCE = 4;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform mat4 normalMatrix;
uniform mat4 lightSpaceMatrix;
uniform mat4 finalBonesMatrices[MAX_BONES];

layout(location = 0) in vec3  vertexPosition;
layout(location = 1) in vec3  vertexColor;
layout(location = 2) in vec2  vertexTexCoord;
layout(location = 3) in vec3  vertexNormal;
layout(location = 4) in vec3  vertexTangent;
layout(location = 5) in vec3  vertexBitangent;
layout(location = 6) in ivec4 boneIDs;
layout(location = 7) in vec4  weights;

out vec3 v_worldPos;
out vec3 v_color;
out vec2 v_texCoord;
out vec4 v_lightSpacePos;
out vec3 v_T;
out vec3 v_B;
out vec3 v_N;

void main()
{
    vec4  totalPos       = vec4(vertexPosition, 1.0);
    vec3  totalNormal    = vec3(0.0);
    vec3  totalTangent   = vec3(0.0);
    vec3  totalBitangent = vec3(0.0);
    float totalWeight    = 0.0;

    for (int i = 0; i < MAX_BONE_INFLUENCE; i++)
    {
        int   boneID = boneIDs[i];
        float weight = weights[i];
        if (boneID < 0 || weight <= 0.0) continue;
        if (boneID >= MAX_BONES)
        {
            totalPos       = vec4(vertexPosition, 1.0);
            totalNormal    = vertexNormal;
            totalTangent   = vertexTangent;
            totalBitangent = vertexBitangent;
            break;
        }
        totalPos       += finalBonesMatrices[boneID] * vec4(vertexPosition, 1.0) * weight;
        mat3 nm         = transpose(inverse(mat3(finalBonesMatrices[boneID])));
        totalNormal    += nm * vertexNormal    * weight;
        totalTangent   += nm * vertexTangent   * weight;
        totalBitangent += nm * vertexBitangent * weight;
        totalWeight    += weight;
    }
    if (totalWeight == 0.0)
    {
        totalPos       = vec4(vertexPosition, 1.0);
        totalNormal    = vertexNormal;
        totalTangent   = vertexTangent;
        totalBitangent = vertexBitangent;
    }

    bool  anim      = totalWeight > 0.0;
    vec3  useNormal = anim ? totalNormal    : vertexNormal;
    vec3  useTan    = anim ? totalTangent   : vertexTangent;
    vec3  useBitan  = anim ? totalBitangent : vertexBitangent;

    mat3  nm3       = mat3(normalMatrix);
    vec3  worldPos  = vec3(modelMatrix * totalPos);

    v_worldPos      = worldPos;
    v_color         = vertexColor;
    v_texCoord      = vertexTexCoord;
    v_lightSpacePos = lightSpaceMatrix * vec4(worldPos, 1.0);
    v_T             = nm3 * useTan;
    v_B             = nm3 * useBitan;
    v_N             = nm3 * useNormal;
    gl_Position     = projectionMatrix * viewMatrix * vec4(worldPos, 1.0);
}

// --- FRAGMENT ---

#version 330 core

struct Material {
    vec2  tiling;
    vec3  ambient;
    vec3  diffuse;
    vec3  specular;
    float shininess;
    float metallic;
    float roughness;
};

struct SunLight {
    float power;
    vec3  direction;
    vec3  diffuse;
    vec3  specular;
};

struct PointLight {
    float power;
    vec3  position;
    float constant;
    float linear;
    float quadratic;
    vec3  diffuse;
    vec3  specular;
};

struct SpotLight {
    float power;
    vec3  position;
    vec3  direction;
    float cutOff;
    float outerCutOff;
    float constant;
    float linear;
    float quadratic;
    vec3  diffuse;
    vec3  specular;
};

uniform Material  material;
uniform vec3      viewPos;

uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform sampler2D specularMap;
uniform sampler2D emissiveMap;
uniform bool      has_albedoMap;
uniform bool      has_normalMap;
uniform bool      has_specularMap;
uniform bool      has_emissiveMap;

uniform vec3        sceneAmbient;
uniform samplerCube skyboxMap;
uniform bool        skyboxAmbientEnabled;
uniform float       skyboxAmbientStrength;

uniform int       sunLightNum;
uniform SunLight  sunLights[32];
uniform int       pointLightNum;
uniform PointLight pointLights[32];
uniform int       spotLightNum;
uniform SpotLight spotLights[32];

uniform sampler2DArray shadowMapArray;
uniform mat4  lightSpaceMatrices[4];
uniform vec4  cascadeSplits;
uniform int   cascadeCount;
uniform float shadowResolution;
uniform bool  enableShadow;
uniform bool  receiveShadow;
uniform int   shadowDebug;
uniform float shadowBias;
uniform float shadowNormalBias;
uniform float shadowSoftness; // PCF tap spacing in texels (default 1.0)

in vec3 v_worldPos;
in vec3 v_color;
in vec2 v_texCoord;
in vec4 v_lightSpacePos;
in vec3 v_T;
in vec3 v_B;
in vec3 v_N;

out vec4 fragColor;

float calcShadow(vec3 worldPos, vec3 norm, vec3 sunDir)
{
    // cascadeCount<=0 means the caller (e.g. kOffscreenRenderer) never set up
    // the shadow uniforms — bail rather than sample garbage and return 1.
    if (!enableShadow || !receiveShadow || cascadeCount <= 0) return 0.0;

    // Pick cascade by distance from camera (cascade splits are camera-space far depths).
    float viewDist = distance(worldPos, viewPos);
    int cascade = cascadeCount - 1;
    for (int i = 0; i < cascadeCount; ++i)
    {
        if (viewDist < cascadeSplits[i]) { cascade = i; break; }
    }

    vec4 lsp  = lightSpaceMatrices[cascade] * vec4(worldPos, 1.0);
    vec3 proj = lsp.xyz / lsp.w;
    proj      = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 0.0;

    float ndl = max(dot(norm, normalize(-sunDir)), 0.0);
    // Scale bias by cascade index — outer cascades have a much wider depth
    // range so the same bias becomes too tight and produces acne.
    float biasScale = 1.0 + float(cascade) * 1.5;
    float bias = (shadowBias + shadowNormalBias * (1.0 - ndl)) * biasScale;

    // 5x5 PCF (25 taps). Tap spacing is shadowSoftness texels — bigger value
    // = softer shadow edge but more blur of fine occluders.
    float shadow = 0.0;
    vec2  texel  = vec2(1.0 / max(shadowResolution, 1.0)) * max(shadowSoftness, 0.0);
    for (int x = -2; x <= 2; ++x)
    for (int y = -2; y <= 2; ++y)
    {
        float d = texture(shadowMapArray,
                          vec3(proj.xy + vec2(x, y) * texel, float(cascade))).r;
        shadow += (proj.z - bias > d) ? 1.0 : 0.0;
    }
    return shadow / 25.0;
}

vec3 calcSunLight(SunLight light, vec3 norm, vec3 vdir, vec3 specTex)
{
    vec3  ldir  = normalize(-light.direction);
    float diff  = max(dot(norm, ldir), 0.0);
    float shine = max(material.shininess, 1.0);
    float spec  = pow(max(dot(vdir, reflect(-ldir, norm)), 0.001), shine);
    return (light.diffuse * material.diffuse * diff +
            light.specular * material.specular * spec * specTex) * light.power;
}

vec3 calcPointLight(PointLight light, vec3 norm, vec3 fragPos, vec3 vdir, vec3 specTex)
{
    vec3  ldir  = normalize(light.position - fragPos);
    float diff  = max(dot(norm, ldir), 0.0);
    float shine = max(material.shininess, 1.0);
    float spec  = pow(max(dot(vdir, reflect(-ldir, norm)), 0.001), shine);
    float dist  = length(light.position - fragPos);
    float att   = light.power / (light.constant + light.linear * dist + light.quadratic * dist * dist);
    return (light.diffuse * material.diffuse * diff +
            light.specular * material.specular * spec * specTex) * att;
}

vec3 calcSpotLight(SpotLight light, vec3 norm, vec3 fragPos, vec3 vdir, vec3 specTex)
{
    vec3  ldir    = normalize(light.position - fragPos);
    float diff    = max(dot(norm, ldir), 0.0);
    float shine   = max(material.shininess, 1.0);
    float spec    = pow(max(dot(vdir, reflect(-ldir, norm)), 0.001), shine);
    float theta   = dot(ldir, normalize(-light.direction));
    float eps     = light.cutOff - light.outerCutOff;
    float intens  = clamp((theta - light.outerCutOff) / eps, 0.0, 1.0);
    float dist    = length(light.position - fragPos);
    float att     = 1.0 / (light.constant + light.linear * dist + light.quadratic * dist * dist);
    return (light.diffuse * material.diffuse * diff +
            light.specular * material.specular * spec * specTex) * light.power * intens * att;
}

void main()
{
    vec2 uv = v_texCoord * material.tiling;

    vec4 diffuseTex  = has_albedoMap   ? texture(albedoMap,   uv) : vec4(1.0);
    vec4 normalTex   = has_normalMap   ? texture(normalMap,   uv) : vec4(0.5, 0.5, 1.0, 1.0);
    vec4 specularTex = has_specularMap ? texture(specularMap, uv) : vec4(0.0);
    vec4 emissiveTex = has_emissiveMap ? texture(emissiveMap, uv) : vec4(0.0);

    vec3 Tn = normalize(v_T);
    vec3 Bn = normalize(v_B);
    vec3 Nn = normalize(v_N);

    vec3 tn = normalTex.rgb;
    tn.g    = 1.0 - tn.g;
    tn      = normalize(tn * 2.0 - 1.0);
    vec3 norm = normalize(Tn * tn.x + Bn * tn.y + Nn * tn.z);

    vec3 vdir   = normalize(viewPos - v_worldPos);
    vec3 result = sceneAmbient * material.ambient;

    if (skyboxAmbientEnabled)
        result += texture(skyboxMap, norm).rgb * skyboxAmbientStrength * material.ambient;

    for (int i = 0; i < sunLightNum; i++)
    {
        vec3 contrib = calcSunLight(sunLights[i], norm, vdir, specularTex.xyz);
        if (i == 0) // Renderer only casts shadow from the first active sun light.
            contrib *= 1.0 - calcShadow(v_worldPos, norm, sunLights[i].direction);
        result += contrib;
    }
    for (int i = 0; i < pointLightNum; i++) result += calcPointLight(pointLights[i], norm, v_worldPos, vdir, specularTex.xyz);
    for (int i = 0; i < spotLightNum;  i++) result += calcSpotLight (spotLights[i],  norm, v_worldPos, vdir, specularTex.xyz);

    fragColor = vec4(clamp(result, 0.0, 1.0), 1.0) * diffuseTex + emissiveTex;
}
