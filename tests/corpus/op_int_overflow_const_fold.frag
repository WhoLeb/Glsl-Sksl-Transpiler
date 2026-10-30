#version 320 es
precision highp float;
out vec4 color;
void main(){int a=2147483647+1;int b=65536*65536;color=vec4(a<0?1.:0.,b==0?1.:0.,0.,1.);}
