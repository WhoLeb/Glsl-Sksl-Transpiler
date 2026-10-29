#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(reflect(vec2(1.,-1.),vec2(0.,1.)),refract(vec2(0.,-1.),vec2(0.,1.),.5));}
