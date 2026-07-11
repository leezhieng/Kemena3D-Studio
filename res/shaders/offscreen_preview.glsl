#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 3) in vec3 aNormal;
layout(location = 5) in vec3 aBitangent;
layout(location = 6) in ivec4 boneIDs;
layout(location = 7) in vec4 weights;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;
uniform vec3 viewPos;

const int MAX_BONES          = 128;
const int MAX_BONE_INFLUENCE = 4;
uniform mat4 finalBoneMatrices[MAX_BONES];

out vec3 vNormal;
out vec3 vBitangent;
out vec3 vFragPos;

void main() {
    vec4 totalPosition = vec4(aPosition, 1.0);
    vec3 totalNormal   = aNormal;
    vec3 totalBitangent = aBitangent;
    float totalWeight = 0.0;
    for (int i = 0; i < MAX_BONE_INFLUENCE; i++)
    {
        int boneID = boneIDs[i];
        float weight = weights[i];
        if (boneID == -1 || weight <= 0.0) continue;
        if (boneID >= MAX_BONES) { totalPosition = vec4(aPosition, 1.0); break; }
        totalPosition += (finalBoneMatrices[boneID] * vec4(aPosition, 1.0)) * weight;
        totalNormal   += (mat3(finalBoneMatrices[boneID]) * aNormal) * weight;
        totalBitangent += (mat3(finalBoneMatrices[boneID]) * aBitangent) * weight;
        totalWeight += weight;
    }
    if (totalWeight == 0.0) totalPosition = vec4(aPosition, 1.0);

    mat3 normalMatrix = transpose(inverse(mat3(modelMatrix)));
    vec4 worldPos     = modelMatrix * totalPosition;
    vFragPos    = vec3(worldPos);
    vNormal     = normalize(normalMatrix * totalNormal);
    vBitangent  = normalize(normalMatrix * totalBitangent);
    gl_Position = projectionMatrix * viewMatrix * worldPos;
}

// --- FRAGMENT ---

#version 330 core

in vec3 vNormal;
in vec3 vBitangent;
in vec3 vFragPos;

uniform vec3 viewPos;

out vec4 fragColor;

void main() {
    vec3  n      = normalize(vNormal);
    vec3  b      = normalize(vBitangent);
    vec3  V      = normalize(viewPos - vFragPos);
    float dN     = dot(n, V) * 0.5 + 0.5;
    float dB     = dot(b, V) * 0.5 + 0.5;
    float top    = n.y * 0.5 + 0.5;
    float shade  = dN * 0.50 + dB * 0.25 + top * 0.25;
    vec3  albedo = vec3(0.75, 0.75, 0.75);
    fragColor    = vec4(albedo * (0.10 + shade * 0.90), 1.0);
}
