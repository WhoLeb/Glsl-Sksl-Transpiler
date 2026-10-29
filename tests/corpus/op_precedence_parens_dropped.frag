#version 320 es
precision highp float;
out vec4 color;
void main(){float a=(1.+2.)*(3.-1.);float b=1.-(2.-3.);float c=1./(2./4.);float d=-(1.+2.);color=vec4(a,b,c,d);}
