#version 320 es
precision highp float;
out vec4 color;
void main(){vec4 v=vec4(1.);v.xy+=vec2(1.);v.zw*=vec2(2.,3.);v.x-=.5;color=v/4.;}
