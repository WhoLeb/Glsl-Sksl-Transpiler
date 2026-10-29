#version 320 es
precision highp float;
out vec4 color;
void f(const out float x){x=1.;} void main(){float y;f(y);color=vec4(y);}
