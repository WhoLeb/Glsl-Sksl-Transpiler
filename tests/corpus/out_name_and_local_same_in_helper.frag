#version 320 es
precision highp float;
out vec4 color;
float f(){float color=.5;return color;} void main(){color=vec4(f());}
