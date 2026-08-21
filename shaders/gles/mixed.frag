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
    vec4 x = vec4(uv, 0.37, 1.0);
    vec4 tex = vec4(0.0);

    int texCount = max(1, uTextureCount);

    for (int i = 0; i < 4096; ++i) {
        if (i >= uIterations) break;

        x = x * vec4(1.00011, 0.99997, 1.00005, 0.99993)
              + vec4(0.00019, 0.00023, 0.00029, 0.00031);

        float fi = float(i);
        vec2 off = vec2(sin(fi * 0.13), cos(fi * 0.19)) * 0.01;

        for (int t = 0; t < 8; ++t) {
            if (t >= texCount) break;
            tex += texture(uTexture0, fract(uv + off + float(t) * 0.011));
        }

        x = fract(x + tex * 0.017 + x.yzwx * 0.013);
        uv = fract(uv * 1.007 + off);
    }

    vec3 rgb = fract(x.rgb + tex.rgb / float(max(1, uIterations * texCount)));
    oColor = vec4(rgb, 1.0);
}
