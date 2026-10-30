#version 320 es
precision highp float;
out vec4 color;
void main(){float v[3]=float[3](.1,.2,.3);float s=0.;for(int i=0;i<3;i++)s+=v[(i+1)%3];color=vec4(s);}
