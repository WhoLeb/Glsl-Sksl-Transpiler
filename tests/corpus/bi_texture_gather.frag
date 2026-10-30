#version 320 es
precision highp float;
uniform sampler2D t;out vec4 color;
void main(){color=textureGather(t,vec2(.5),1)+textureOffset(t,vec2(.5),ivec2(1,0));}
