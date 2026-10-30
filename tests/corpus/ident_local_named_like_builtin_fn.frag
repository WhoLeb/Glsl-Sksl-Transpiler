#version 320 es
precision highp float;
out vec4 color;
void main(){float sin=.5,dot=.25,mix=.125;color=vec4(sin+dot+mix);}
