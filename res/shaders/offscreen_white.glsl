#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 6) in ivec4 boneIDs;
layout(location = 7) in vec4 weights;

uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

const int MAX_BONES          = 128;
const int MAX_BONE_INFLUENCE = 4;
uniform mat4 finalBoneMatrices[MAX_BONES];

void main() {
    vec4 totalPosition = vec4(aPosition, 1.0);
    float totalWeight = 0.0;
    for (int i = 0; i < MAX_BONE_INFLUENCE; i++)
    {
        int boneID = boneIDs[i];
        float weight = weights[i];
        if (boneID == -1 || weight <= 0.0) continue;
        if (boneID >= MAX_BONES) { totalPosition = vec4(aPosition, 1.0); break; }
        totalPosition += (finalBoneMatrices[boneID] * vec4(aPosition, 1.0)) * weight;
        totalWeight += weight;
    }
    if (totalWeight == 0.0) totalPosition = vec4(aPosition, 1.0);
    gl_Position = projectionMatrix * viewMatrix * modelMatrix * totalPosition;
}

// --- FRAGMENT ---

#version 330 core

out vec4 fragColor;

void main() {
    fragColor = vec4(1.0, 1.0, 1.0, 1.0);
}
