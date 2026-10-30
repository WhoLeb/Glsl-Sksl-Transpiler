#version 300 es
precision highp float;
uniform B { float step; } block;out vec4 c;void main(){c=vec4(block.step);}