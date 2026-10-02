#version 450

/* Vulkan 1.0 / SPIR-V 1.0. Vertex attributes from a HOST_VISIBLE VBO. */
layout(location = 0) in vec4 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec3 vColor;

void main()
{
    gl_Position = inPos;
    vColor = inColor.rgb;
}
