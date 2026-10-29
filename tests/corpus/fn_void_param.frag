#version 320 es
precision highp float;
out vec4 color;
float f(void){return .5;}void main(void){color=vec4(f());}
