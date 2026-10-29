#version 320 es
precision highp float;
out vec4 color;
void main(){float a=1.,b=2.;float c=a - -b;float d=a + +b;float e=-(-a);float f=- -a;float g=-+-a;color=vec4(c,d,e,f+g);}
