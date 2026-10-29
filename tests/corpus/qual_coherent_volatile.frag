#version 320 es
precision highp float;
coherent volatile uniform float v;out vec4 color;
void main(){color=vec4(v);}
