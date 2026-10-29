#version 320 es
precision highp float;
uniform sampler2D t;out vec4 color;
void main(){color=vec4(vec2(textureSize(t,0)),0.,1.);}
