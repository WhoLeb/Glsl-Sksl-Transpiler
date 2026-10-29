#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(floor(1.5),ceil(1.5),round(1.5),trunc(-1.5))/2.;}
