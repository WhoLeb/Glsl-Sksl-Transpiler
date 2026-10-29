#version 320 es
precision highp float;
out vec4 color;
float a(){return .25;}void main(){color=vec4(a());}float b(){return .5;}
