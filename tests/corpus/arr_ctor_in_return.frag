#version 320 es
precision highp float;
out vec4 color;
float f(int k){for(int i=0;i<2;++i)if(i==k)return float[2](.1,.9)[i];return 0.;} void main(){color=vec4(f(1));}
