#version 320 es
precision highp float;
out vec4 color;
float f(){for(int i=0;i<4;++i){if(i==2)return float(i);}return -1.;} void main(){color=vec4(f()/2.);}
