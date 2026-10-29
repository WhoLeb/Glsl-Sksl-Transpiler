#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
void main(){float a=0.;if(position.x>1.)a=1.;else if(position.x>0.)a=.5;else a=.25;color=vec4(a);}
