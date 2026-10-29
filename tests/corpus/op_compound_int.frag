#version 320 es
precision highp float;
out vec4 color;
void main(){int a=100;a+=1;a-=2;a*=3;a/=2;a%=7;a<<=2;a>>=1;a&=15;a|=2;a^=5;color=vec4(float(a)/100.);}
