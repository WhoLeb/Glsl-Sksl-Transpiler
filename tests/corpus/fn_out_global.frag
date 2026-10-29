#version 320 es
precision highp float;
out vec4 color;
float g;void f(out float x){x=.5;g=1.;}void main(){f(g);color=vec4(g);}
