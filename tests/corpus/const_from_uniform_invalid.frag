#version 320 es
precision highp float;
uniform float u;out vec4 color;
const float c=u;void main(){color=vec4(c);}
