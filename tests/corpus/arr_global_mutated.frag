#version 320 es
precision highp float;
out vec4 color;
float g[3];void main(){g[1]=.5;g[2]=g[1]*2.;color=vec4(g[1],g[2],0.,1.);}
