#version 300 es
precision highp float;

in vec2 vUv;
out vec4 oColor;

uniform int uIterations;
uniform vec2 uResolution;

void main() {
    vec2 p = vUv * 6.2831853;
    float x = p.x + 0.17;
    float y = p.y + 0.31;

    for (int i = 0; i < 4096; ++i) {
        if (i >= uIterations) break;
        x = sin(x) + cos(y) + 0.001;
        y = cos(y) - sin(x) + 0.002;
    }

    oColor = vec4(fract(abs(x)), fract(abs(y)), fract(abs(x + y)), 1.0);
}
