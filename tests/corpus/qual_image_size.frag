#version 320 es
precision highp float;
layout(rgba8,binding=0) readonly uniform highp image2D img;out vec4 color;
void main(){color=vec4(vec2(imageSize(img)),0.,1.);}
