#version 320 es
precision highp float;
out vec4 color;
struct S{float x;vec2 y;}; void main(){S a[2];a[0].x=1.;a[1].y=vec2(2.,3.);a[1].y.x+=1.;color=vec4(a[0].x,a[1].y,1.)/3.;}
