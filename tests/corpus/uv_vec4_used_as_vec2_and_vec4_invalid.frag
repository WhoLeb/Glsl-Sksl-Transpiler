#version 310 es
precision highp float;
layout(location = 0) in highp vec4 uv_coords;
uniform vec2 iResolution;
out vec4 out_color;
void main(){ vec2 d = uv_coords - vec2(0.5); out_color = uv_coords; }
