#version 320 es
precision highp float;
out vec4 color;
void bump(inout float a){a+=.25;} void main(){float a=0.;float b=(bump(a),a);color=vec4(a,b,0.,1.);}
