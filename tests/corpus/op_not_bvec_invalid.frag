#version 320 es
precision highp float;
out vec4 color;
void main(){bvec2 b=!bvec2(true,false);color=vec4(b.x?1.:0.);}
