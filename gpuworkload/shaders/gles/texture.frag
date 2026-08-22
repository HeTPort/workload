#version 300 es
precision highp float;

in vec2 vUv;
out vec4 oColor;

uniform sampler2D uTexture0;
uniform int uIterations;
uniform int uTextureCount;
uniform vec2 uResolution;

void main() {
    vec2 uv = vUv;
    vec4 c = vec4(0.0);

    int count = max(1, uTextureCount);

    for (int i = 0; i < 512; ++i) {
        if (i >= uIterations) break;

        float fi = float(i);
        vec2 off = vec2(
            sin(fi * 0.17) * 0.013,
            cos(fi * 0.11) * 0.017
        );

        for (int t = 0; t < 16; ++t) {
            if (t >= count) break;
            c += texture(uTexture0, fract(uv + off + float(t) * 0.007));
        }

        uv = fract(uv * 1.013 + off);
    }

    c /= float(max(1, uIterations * count));
    oColor = vec4(c.rgb, 1.0);
}
