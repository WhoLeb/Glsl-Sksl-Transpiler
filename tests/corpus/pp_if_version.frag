#version 320 es
precision highp float;
#if __VERSION__ >= 300
out vec4 color;
#endif
void main(){color=vec4(1.);}
