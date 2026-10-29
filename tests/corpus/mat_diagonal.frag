#version 320 es
precision highp float;
out vec4 color;
void main(){mat3 m=mat3(2.);color=vec4(m[0].x,m[0].y,m[1].y,m[2].z)/2.;}
