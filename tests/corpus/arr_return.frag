#version 320 es
precision highp float;
out vec4 color;
float[2] f(){return float[2](1.,2.);} void main(){float a[2]=f();color=vec4(a[0],a[1],0.,1.)/2.;}
