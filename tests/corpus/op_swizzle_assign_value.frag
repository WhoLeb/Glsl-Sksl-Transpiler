#version 320 es
precision highp float;
out vec4 color;
void main(){vec2 v=vec2(0.);vec2 r=(v.yx=vec2(.25,.5));color=vec4(v,r);}
