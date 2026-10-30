#version 300 es
precision highp float;
uniform sampler2D step;out vec4 c;void main(){c=texture(step,vec2(.5));}