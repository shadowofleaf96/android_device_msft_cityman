#version 450

/* Vulkan 1.0 / SPIR-V 1.0. No UBO, no vertex buffer, no push constants. */
layout(location = 0) out vec3 vColor;

void main()
{
    /* Hardcoded clip-space positions; gl_VertexIndex selects the vertex. */
    if (gl_VertexIndex == 0) {
        gl_Position = vec4(0.0, -0.5, 0.0, 1.0);
        vColor = vec3(1.0, 0.0, 0.0);
    } else if (gl_VertexIndex == 1) {
        gl_Position = vec4(0.5, 0.5, 0.0, 1.0);
        vColor = vec3(0.0, 1.0, 0.0);
    } else {
        gl_Position = vec4(-0.5, 0.5, 0.0, 1.0);
        vColor = vec3(0.0, 0.0, 1.0);
    }
}
