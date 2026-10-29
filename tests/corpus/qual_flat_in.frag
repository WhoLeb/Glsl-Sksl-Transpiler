#version 320 es
precision highp float;
flat in int i;out vec4 color;
void main(){color=vec4(float(i));}
