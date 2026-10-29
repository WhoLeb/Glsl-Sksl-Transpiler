#version 320 es
precision highp float;
out vec4 color;
void main(){int i=5;float s=0.;for(int i=0;i<3;++i)s+=.1;s+=float(i);color=vec4(s/5.);}
