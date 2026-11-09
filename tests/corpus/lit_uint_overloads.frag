#version 320 es
precision highp float;
out vec4 color;
float f(int x){return 1.;}float f(uint x){return 2.;}void main(){color=vec4(f(1)+f(1u));}
