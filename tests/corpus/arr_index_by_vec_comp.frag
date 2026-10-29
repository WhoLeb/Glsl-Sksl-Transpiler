#version 320 es
precision highp float;
out vec4 color;
void main(){ivec2 i=ivec2(1,0);float a[2]=float[2](.25,.75);color=vec4(a[i.x]);}
