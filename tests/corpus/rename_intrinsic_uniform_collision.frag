#version 300 es
precision highp float;
uniform float step; uniform float step_; out vec4 c; void main(){c=vec4(step+step_+abs(-1.));}