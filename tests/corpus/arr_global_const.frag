#version 320 es
precision highp float;
out vec4 color;
const float W[3]=float[3](.2,.3,.5);void main(){float s=0.;for(int i=0;i<3;++i)s+=W[i];color=vec4(s);}
