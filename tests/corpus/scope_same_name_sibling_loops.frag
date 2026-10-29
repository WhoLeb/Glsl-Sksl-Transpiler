#version 320 es
precision highp float;
out vec4 color;
void main(){float s=0.;for(int i=0;i<2;++i)s+=.1;for(int i=0;i<3;++i)s+=.1;color=vec4(s);}
