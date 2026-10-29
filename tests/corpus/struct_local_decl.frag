#version 320 es
precision highp float;
out vec4 color;
void main(){struct L{float v;};L l;l.v=.5;color=vec4(l.v);}
