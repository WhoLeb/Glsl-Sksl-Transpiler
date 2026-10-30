#version 320 es
precision highp float;
in vec2 uv;out vec4 color;
void main(){color=vec4(interpolateAtCentroid(uv),0.,1.);}
