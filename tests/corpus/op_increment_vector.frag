#version 320 es
precision highp float;
out vec4 color;
void main(){vec2 v=vec2(1.);v++;++v;v.x--;--v.y;mat2 m=mat2(1.);m[0][0]++;m[1]--;color=vec4(v,m[0][0],m[1][1]);}
