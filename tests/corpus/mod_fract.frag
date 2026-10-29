#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(fract(.7),mod(3.,2.),floor(.9),ceil(.1));}
