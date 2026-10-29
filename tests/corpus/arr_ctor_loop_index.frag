#version 320 es
precision highp float;
out vec4 color;
void main(){float s=0.;for(int i=0;i<3;++i)s+=float[3](.1,.2,.3)[i];color=vec4(s);}
