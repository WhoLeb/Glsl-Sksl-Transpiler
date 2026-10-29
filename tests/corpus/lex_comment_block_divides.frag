#version 320 es
precision highp float;
out vec4 color;
void main(){float a=4.;float b=a//* not a block comment
/2.;color=vec4(b);}
