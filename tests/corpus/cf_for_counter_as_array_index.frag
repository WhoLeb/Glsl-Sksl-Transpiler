#version 320 es
precision highp float;
out vec4 color;
void main(){float v[4]=float[4](.1,.2,.3,.4);float a=0.;for(int i=0;i<4;++i)a+=v[i];color=vec4(a);}
