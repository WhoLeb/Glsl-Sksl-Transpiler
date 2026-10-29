#version 320 es
precision highp float;
layout(rgba32f,binding=0) uniform highp image2D target;out vec4 color;
void main(){imageStore(target,ivec2(0),vec4(1.));color=vec4(1.);}
