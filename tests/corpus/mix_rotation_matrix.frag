#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
mat2 rot(float a){float c=cos(a),s=sin(a);return mat2(c,-s,s,c);}void main(){vec2 p=rot(time)*(position/size-.5);color=vec4(p+.5,0.,1.);}
