#version 320 es
precision highp float;
out vec4 color;
void main(){mat2 s=mat2(2.,3.,4.,5.);mat4 m=mat4(s);color=vec4(m[0].x,m[1].y,m[2].z,m[3].w)/5.;}
