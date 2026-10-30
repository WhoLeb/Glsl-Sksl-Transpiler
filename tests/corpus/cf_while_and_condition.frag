#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
void main(){int i=0;float t=0.;while(t<time&&i<10){t+=.5;i++;}color=vec4(t/10.,float(i)/10.,0.,1.);}
