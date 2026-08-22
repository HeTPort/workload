#version 450

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 oColor;

layout(binding = 0) uniform sampler2D uTexture0;

layout(push_constant) uniform PushConstants {
    uint width;
    uint height;
    uint iterations;
    uint shader_id;
    uint texture_count;
    uint frame_index;
    uint reserved0;
    uint reserved1;
} pc;

vec4 workload_alu(vec2 uv) {
    vec4 x = vec4(uv, 0.37, 1.0);

    for (uint i = 0u; i < pc.iterations; ++i) {
        x = x * vec4(1.00013, 0.99991, 1.00007, 0.99989)
              + vec4(0.00031, 0.00017, 0.00023, 0.00029);

        x = fract(x * 1.61803398875 + x.yzwx);
    }

    return vec4(x.rgb, 1.0);
}

vec4 workload_sfu(vec2 uv) {
    vec2 p = uv * 6.2831853;
    float x = p.x + 0.17;
    float y = p.y + 0.31;

    for (uint i = 0u; i < pc.iterations; ++i) {
        x = sin(x) + cos(y) + 0.001;
        y = cos(y) - sin(x) + 0.002;
    }

    return vec4(fract(abs(x)), fract(abs(y)), fract(abs(x + y)), 1.0);
}

vec4 workload_texture(vec2 uv) {
    vec4 c = vec4(0.0);
    uint count = max(pc.texture_count, 1u);

    for (uint i = 0u; i < pc.iterations; ++i) {
        float fi = float(i);

        vec2 off = vec2(
            sin(fi * 0.17) * 0.013,
            cos(fi * 0.11) * 0.017
        );

        for (uint t = 0u; t < count; ++t) {
            c += texture(
                uTexture0,
                fract(uv + off + float(t) * 0.007)
            );
        }

        uv = fract(uv * 1.013 + off);
    }

    c /= float(max(pc.iterations * count, 1u));
    return vec4(c.rgb, 1.0);
}

vec4 workload_fill(vec2 uv) {
    return vec4(uv.x, uv.y, fract(uv.x + uv.y), 1.0);
}

vec4 workload_mixed(vec2 uv) {
    vec4 x = vec4(uv, 0.37, 1.0);
    vec4 tex = vec4(0.0);

    uint count = max(pc.texture_count, 1u);

    for (uint i = 0u; i < pc.iterations; ++i) {
        x = x * vec4(1.00011, 0.99997, 1.00005, 0.99993)
              + vec4(0.00019, 0.00023, 0.00029, 0.00031);

        float fi = float(i);
        vec2 off = vec2(
            sin(fi * 0.13),
            cos(fi * 0.19)
        ) * 0.01;

        for (uint t = 0u; t < count; ++t) {
            tex += texture(
                uTexture0,
                fract(uv + off + float(t) * 0.011)
            );
        }

        x = fract(x + tex * 0.017 + x.yzwx * 0.013);
        uv = fract(uv * 1.007 + off);
    }

    vec3 rgb = fract(
        x.rgb +
        tex.rgb / float(max(pc.iterations * count, 1u))
    );

    return vec4(rgb, 1.0);
}

void main() {
    vec2 uv = fract(vUv);

    if (pc.shader_id == 1u) {
        oColor = workload_alu(uv);
    } else if (pc.shader_id == 2u) {
        oColor = workload_sfu(uv);
    } else if (pc.shader_id == 3u) {
        oColor = workload_texture(uv);
    } else if (pc.shader_id == 4u) {
        oColor = workload_fill(uv);
    } else {
        oColor = workload_mixed(uv);
    }
}
