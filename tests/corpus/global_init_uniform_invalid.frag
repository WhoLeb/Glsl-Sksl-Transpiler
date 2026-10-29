#version 320 es
precision highp float;
uniform float u;out vec4 color;
float g=u*2.;void main(){color=vec4(g);}
