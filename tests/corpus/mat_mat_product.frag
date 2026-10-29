#version 320 es
precision highp float;
out vec4 color;
void main(){mat3 a=mat3(1.,2.,3.,4.,5.,6.,7.,8.,9.);mat3 b=mat3(9.,8.,7.,6.,5.,4.,3.,2.,1.);mat3 c=a*b;color=vec4(c[0].x,c[1].y,c[2].z,c[0].z)/300.;}
