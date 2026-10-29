#version 320 es
precision highp float;
out vec4 color;
void main(){mat3 m=mat3(0.);m[1]=vec3(1.,2.,3.);m[2][0]=4.;color=vec4(m[1],m[2].x)/4.;}
