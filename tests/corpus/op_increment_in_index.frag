#version 320 es
precision highp float;
out vec4 color;
void main(){float a[3]=float[3](.1,.2,.3);int i=0;float s=a[i++]+a[i++];color=vec4(s,float(i),0.,1.);}
