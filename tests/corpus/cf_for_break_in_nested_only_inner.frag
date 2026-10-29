#version 320 es
precision highp float;
out vec4 color;
void main(){float a=0.;for(int i=0;i<3;++i){for(int j=0;j<3;++j){if(j==1)break;a+=.1;}}color=vec4(a);}
