float fract_f(float x) {
    return x - floor(x);
}

float2 fract2(float2 x) {
    return x - floor(x);
}

float3 fract3(float3 x) {
    return x - floor(x);
}

float4 fract4(float4 x) {
    return x - floor(x);
}

uint hash_u32(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

uint pack_rgba(float4 c) {
    uint r = (uint)(clamp(c.x, 0.0f, 1.0f) * 255.0f);
    uint g = (uint)(clamp(c.y, 0.0f, 1.0f) * 255.0f);
    uint b = (uint)(clamp(c.z, 0.0f, 1.0f) * 255.0f);
    uint a = (uint)(clamp(c.w, 0.0f, 1.0f) * 255.0f);

    return r | (g << 8) | (b << 16) | (a << 24);
}

float4 workload_alu(float2 uv, uint iterations) {
    float4 x = (float4)(uv.x, uv.y, 0.37f, 1.0f);

    for (uint i = 0; i < iterations; ++i) {
        x = x * (float4)(1.00013f, 0.99991f, 1.00007f, 0.99989f)
              + (float4)(0.00031f, 0.00017f, 0.00023f, 0.00029f);
        x = fract4(x * 1.61803398875f + x.yzwx);
    }

    return (float4)(x.x, x.y, x.z, 1.0f);
}

float4 workload_sfu(float2 uv, uint iterations) {
    float2 p = uv * 6.2831853f;
    float x = p.x + 0.17f;
    float y = p.y + 0.31f;

    for (uint i = 0; i < iterations; ++i) {
        x = sin(x) + cos(y) + 0.001f;
        y = cos(y) - sin(x) + 0.002f;
    }

    return (float4)(
        fract_f(fabs(x)),
        fract_f(fabs(y)),
        fract_f(fabs(x + y)),
        1.0f
    );
}

float4 workload_texture_like(
    uint gx,
    uint gy,
    float2 uv,
    uint iterations,
    uint texture_count,
    uint frame_index
) {
    (void)uv;

    float4 c = (float4)(0.0f);
    uint count = max(texture_count, 1u);

    for (uint i = 0; i < iterations; ++i) {
        uint h = hash_u32(
            gx * 73856093u ^
            gy * 19349663u ^
            i * 83492791u ^
            frame_index * 2654435761u
        );

        float3 v = (float3)(
            (float)((h >> 0) & 255u),
            (float)((h >> 8) & 255u),
            (float)((h >> 16) & 255u)
        ) / 255.0f;

        for (uint t = 0; t < count; ++t) {
            uint ht = hash_u32(h ^ t * 2246822519u);

            c += (float4)(
                (float)((ht >> 0) & 255u) / 255.0f,
                (float)((ht >> 8) & 255u) / 255.0f,
                (float)((ht >> 16) & 255u) / 255.0f,
                1.0f
            ) * 0.5f + (float4)(v.x, v.y, v.z, 1.0f) * 0.5f;
        }
    }

    c /= (float)max(iterations * count, 1u);
    return (float4)(c.x, c.y, c.z, 1.0f);
}

float4 workload_fill(float2 uv) {
    return (float4)(uv.x, uv.y, fract_f(uv.x + uv.y), 1.0f);
}

float4 workload_mixed(
    uint gx,
    uint gy,
    float2 uv,
    uint iterations,
    uint texture_count,
    uint frame_index
) {
    float4 x = (float4)(uv.x, uv.y, 0.37f, 1.0f);
    float4 tex = (float4)(0.0f);
    uint count = max(texture_count, 1u);

    for (uint i = 0; i < iterations; ++i) {
        x = x * (float4)(1.00011f, 0.99997f, 1.00005f, 0.99993f)
              + (float4)(0.00019f, 0.00023f, 0.00029f, 0.00031f);

        uint h = hash_u32(
            gx * 73856093u ^
            gy * 19349663u ^
            i * 83492791u ^
            frame_index * 2654435761u
        );

        for (uint t = 0; t < count; ++t) {
            uint ht = hash_u32(h ^ t * 2246822519u);

            tex += (float4)(
                (float)((ht >> 0) & 255u) / 255.0f,
                (float)((ht >> 8) & 255u) / 255.0f,
                (float)((ht >> 16) & 255u) / 255.0f,
                1.0f
            );
        }

        x = fract4(x + tex * 0.0007f + x.yzwx * 0.013f);
    }

    float3 rgb = fract3(
        x.xyz +
        tex.xyz / (float)max(iterations * count, 1u)
    );

    return (float4)(rgb.x, rgb.y, rgb.z, 1.0f);
}

__kernel void gpu_avs_workload(
    __global uint* out_buf,
    uint width,
    uint height,
    uint iterations,
    uint shader_id,
    uint texture_count,
    uint frame_index
) {
    uint gx = get_global_id(0);
    uint gy = get_global_id(1);

    if (gx >= width || gy >= height) {
        return;
    }

    uint index = gy * width + gx;

    float2 uv = (float2)(
        (float)gx / (float)max(width, 1u),
        (float)gy / (float)max(height, 1u)
    );

    float4 color;

    if (shader_id == 1u) {
        color = workload_alu(uv, iterations);
    } else if (shader_id == 2u) {
        color = workload_sfu(uv, iterations);
    } else if (shader_id == 3u) {
        color = workload_texture_like(
            gx,
            gy,
            uv,
            iterations,
            texture_count,
            frame_index
        );
    } else if (shader_id == 4u) {
        color = workload_fill(uv);
    } else {
        color = workload_mixed(
            gx,
            gy,
            uv,
            iterations,
            texture_count,
            frame_index
        );
    }

    out_buf[index] = pack_rgba(color);
}
