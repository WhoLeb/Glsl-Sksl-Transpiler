#version 320 es
precision highp float;
out vec4 color;
void main(){vec2 v=vec2(1.,2.);float f=v.x;f+=v.y;color=vec4(f);}
