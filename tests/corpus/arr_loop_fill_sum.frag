#version 320 es
precision highp float;
out vec4 color;
void main(){float a[8];for(int i=0;i<8;++i)a[i]=float(i);float s=0.;for(int i=0;i<8;++i)s+=a[7-i];color=vec4(s/28.);}
