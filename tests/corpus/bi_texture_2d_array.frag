#version 320 es
precision highp float;
uniform highp sampler2DArray layers;out vec4 color;
void main(){color=texture(layers,vec3(.5,.5,1.));}
