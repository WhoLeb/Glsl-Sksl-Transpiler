#version 320 es
precision highp float;
uniform int i;uniform ivec2 iv;out vec4 color;
void main(){color=vec4(vec2(iv),float(i),1.);}
