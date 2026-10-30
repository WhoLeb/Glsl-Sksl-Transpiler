#version 320 es
precision highp float;
out vec4 color;
void main(){int a=0xFFFFFFFF,b=0x80000000;color=vec4(a==-1?1.:0.,b<0?1.:0.,0.,1.);}
