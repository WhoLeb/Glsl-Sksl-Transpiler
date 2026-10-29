#version 320 es
precision highp float;
out vec4 color;
float f(float x){return x>0.?f(x-1.):0.;}void main(){color=vec4(f(2.));}
