#version 450

/* Vulkan 1.0 / SPIR-V 1.0. Combined-image sampler, no UBO. */
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(tex, vUv);
}
