#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(atan(1.,-1.),atan(-1.,-1.),atan(vec2(1.),vec2(2.)).y,1.)/4.;}
