#version 320 es
precision highp float;
out vec4 color;
void main(){int a=1|2&3^1;int b=1+2<<1;bool c=(5&3)==1;color=vec4(float(a+b)/10.,c?1.:0.,0.,1.);}
