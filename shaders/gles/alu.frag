#version 300 es
precision highp float;

in vec2 vUv;
out vec4 oColor;

uniform int uIterations;
uniform vec2 uResolution;

void main() {
    vec2 p = vUv * 2.0 - 1.0;
    vec4 x = vec4(p, 0.37, 1.0);

    for (int i = 0; i < 4096; ++i) {
        if (i >= uIterations) break;

        x = x * vec4(1.00013, 0.99991, 1.00007, 0.99989)
              + vec4(0.00031, 0.00017, 0.00023, 0.00029);

        x = fract(x * 1.61803398875 + x.yzwx);
    }

    oColor = vec4(x.rgb, 1.0);
}
