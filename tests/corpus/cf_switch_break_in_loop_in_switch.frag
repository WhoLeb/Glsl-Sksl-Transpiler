#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
void main(){float a=0.;switch(count){case 0:for(int i=0;i<4;++i){if(i==2)break;a+=.1;}a+=.5;break;}color=vec4(a);}
