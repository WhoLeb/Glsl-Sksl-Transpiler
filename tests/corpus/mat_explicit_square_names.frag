#version 320 es
precision highp float;
out vec4 color;
void main(){mat2x2 a=mat2x2(1.);mat3x3 b=mat3x3(1.);mat4x4 c=mat4x4(1.);color=vec4(a[0].x,b[1].y,c[3].w,1.);}
