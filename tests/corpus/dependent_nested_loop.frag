#version 320 es
precision highp float;
uniform int count;uniform int start;uniform int step;out vec4 color;
void main(){float a=0.;for(int i=0;i<4;++i){for(int j=0;j<i;++j){a+=.1;}}color=vec4(a);}
