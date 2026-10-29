#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(vec3(1.,2.,3.).zyx,vec2(4.).y)/4.;}
