#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(any(lessThan(vec2(.2),vec2(.5))) ? 1. : 0.);}
