#version 320 es
precision highp float;
out vec4 color;
float f(float,float y){return y;}void main(){color=vec4(f(1.,.5));}
