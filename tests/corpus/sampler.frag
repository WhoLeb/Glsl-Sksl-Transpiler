#version 320 es
precision highp float;
uniform sampler2D image;out vec4 color;
void main(){color=texture(image,vec2(.5));}
