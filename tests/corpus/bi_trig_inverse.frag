#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(asin(.5),acos(.5),atan(1.),atan(1.,2.));}
