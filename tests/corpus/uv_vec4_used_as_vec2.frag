#version 310 es
precision highp float;
layout(location = 0) in highp vec4 uv_coords;
uniform vec2 iResolution;
out vec4 out_color;
void main(){ vec2 d = uv_coords * 2.0 - vec2(1.0); out_color = vec4(d, 0.0, 1.0); }
