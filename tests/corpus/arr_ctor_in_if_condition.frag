#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(0.);for(int i=0;i<2;++i)if(float[2](.1,.9)[i]>.5)color=vec4(1.);}
