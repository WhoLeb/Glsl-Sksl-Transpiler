#version 320 es
precision highp float;
out vec4 color;
float g=1.;float twice(){g*=2.;return g;}void main(){float a=twice();float b=twice();color=vec4(a,b,g,1.)/4.;}
