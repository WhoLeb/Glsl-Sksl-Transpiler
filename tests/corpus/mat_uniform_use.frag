#version 320 es
precision highp float;
uniform mat2 m;out vec4 color;
void main(){color=vec4(m*vec2(1.),0.,1.);}
