#version 320 es
precision highp float;
out vec4 color;
void main(){float a[6]=float[6](.1,.2,.3,.4,.5,.6);float s=0.;for(int i=0;i<3;i++){int j=i*2+1;s+=a[j];}color=vec4(s);}
