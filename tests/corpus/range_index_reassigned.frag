#version 320 es
precision highp float;
out vec4 color;
uniform int u;void main(){float a[3]=float[3](.1,.2,.3);int k=0;k+=u;color=vec4(a[k]);}
