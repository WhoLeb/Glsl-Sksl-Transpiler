#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4((true?vec2(1.,2.):vec2(3.,4.)).yx,0.,1.);}
