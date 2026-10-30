#version 320 es
precision highp float;
out vec4 color;
#define X Y
#define Y X
void main(){float X=.5;color=vec4(X);}
