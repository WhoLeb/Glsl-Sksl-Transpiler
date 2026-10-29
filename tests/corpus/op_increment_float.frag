#version 320 es
precision highp float;
out vec4 color;
void main(){float f=.5;f++;--f;++f;color=vec4(f);}
