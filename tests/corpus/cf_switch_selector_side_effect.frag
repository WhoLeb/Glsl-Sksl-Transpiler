#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
void main(){float a=0.;int k=count;switch(k++){case 0:a=.5;break;default:a=.25;}color=vec4(a,float(k),0.,1.);}
