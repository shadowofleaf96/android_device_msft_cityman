#version 450

/* Vulkan 1.0 / SPIR-V 1.0. Interpolated vertex color, no UBO. */
layout(location = 0) in vec3 vColor;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(vColor, 1.0);
}
