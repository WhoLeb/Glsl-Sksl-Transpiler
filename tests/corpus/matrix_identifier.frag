#version 320 es
precision highp float;
out vec4 color;
void main(){float matrix=.2;float matrixValue=.3;float matExtra=.1;color=vec4(matrix+matrixValue+matExtra);}
