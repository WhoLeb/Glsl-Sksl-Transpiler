#version 320 es
precision highp float;
out vec4 color;
vec4 f(){return vec4(1.,2.,3.,4.);} void main(){color=vec4(f().zw,vec4(5.).xy)/5.;}
