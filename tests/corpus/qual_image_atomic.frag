#version 320 es
precision highp float;
layout(r32ui,binding=0) uniform highp uimage2D img;out vec4 color;
void main(){imageAtomicAdd(img,ivec2(0),1u);color=vec4(1.);}
