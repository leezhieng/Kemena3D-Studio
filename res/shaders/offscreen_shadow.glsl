#version 330 core
layout (location = 0) in vec3 vertexPosition;
layout (location = 6) in ivec4 boneIDs;
layout (location = 7) in vec4 weights;

uniform mat4 lightSpaceMatrix;
uniform mat4 modelMatrix;

const int MAX_BONES          = 128;
const int MAX_BONE_INFLUENCE = 4;
uniform mat4 finalBonesMatrices[MAX_BONES];

void main()
{
    vec4 totalPosition = vec4(vertexPosition, 1.0);
    float totalWeight = 0.0;

    for(int i = 0; i < MAX_BONE_INFLUENCE; i++)
    {
        int boneID = boneIDs[i];
        float weight = weights[i];
        if(boneID == -1 || weight <= 0.0) continue;
        if(boneID >= MAX_BONES) { totalPosition = vec4(vertexPosition, 1.0); break; }
        totalPosition += (finalBonesMatrices[boneID] * vec4(vertexPosition, 1.0)) * weight;
        totalWeight += weight;
    }

    if (totalWeight == 0.0)
        totalPosition = vec4(vertexPosition, 1.0);

    gl_Position = lightSpaceMatrix * (modelMatrix * totalPosition);
}

// --- FRAGMENT ---

#version 330 core
void main() {}
