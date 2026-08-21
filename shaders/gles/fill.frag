#version 300 es
precision highp float;

in vec2 vUv;
out vec4 oColor;

uniform int uIterations;
uniform vec2 uResolution;

void main() {
    vec2 p = gl_FragCoord.xy / uResolution.xy;
    oColor = vec4(p.x, p.y, fract(p.x + p.y), 1.0);
}
