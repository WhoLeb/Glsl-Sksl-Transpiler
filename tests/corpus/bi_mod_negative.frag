#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(mod(-1.,3.),mod(5.,-3.)+3.,mod(-5.5,2.),1.)/3.;}
