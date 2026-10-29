#version 320 es
precision highp float;
out vec4 color;
void main(){mat2 m=mat2(1.);m*=m;m+=m;m*=2.;m-=mat2(1.);m/=2.;color=vec4(m[0],m[1]);}
