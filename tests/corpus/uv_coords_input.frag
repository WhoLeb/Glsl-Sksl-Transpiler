#version 320 es
precision highp float;
layout(location = 0) in highp vec4 uv_coords;uniform vec2 iResolution;layout(location = 0) out vec4 out_color;
void main(){out_color=vec4(uv_coords.xy,0.,1.);}
