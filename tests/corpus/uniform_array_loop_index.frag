#version 320 es
precision highp float;
uniform vec4 u[4];out vec4 color;
void main(){vec4 s=vec4(0.);for(int i=0;i<4;++i)s+=u[i];color=s;}
