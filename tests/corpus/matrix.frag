#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(mat3(1.) * vec3(.5),1.);}
