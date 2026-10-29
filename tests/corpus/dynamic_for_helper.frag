#version 320 es
precision highp float;
uniform int count;out vec4 color;
float sum(int n){float a=0.;for(int i=0;i<n;++i){a+=.1;}return a;}void main(){color=vec4(sum(count));}
