#version 320 es
precision highp float;
out vec4 color;
void main(){const float a=.5;const mat2 r=mat2(cos(a),sin(a),-sin(a),cos(a));const vec2 p=vec2(1.)*transpose(r);color=vec4(p,0.,1.);}
