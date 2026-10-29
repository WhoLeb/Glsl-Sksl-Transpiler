#version 320 es
precision highp float;
out vec4 color;
float b(float x);float a(float x){return b(x);}float b(float x){return a(x);}void main(){color=vec4(a(1.));}
