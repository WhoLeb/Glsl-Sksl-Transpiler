#version 320 es
precision highp float;
out vec4 color;
void main(){vec3 a[2];a[0]=vec3(1.);a[1]=vec3(2.);a[1].y=3.;color=vec4(a[0].x,a[1].y,a[1].z,1.)/3.;}
