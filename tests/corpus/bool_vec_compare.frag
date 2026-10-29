#version 320 es
precision highp float;
out vec4 color;
void main(){bool a=vec2(1.)==vec2(1.),b=vec3(1.)!=vec3(2.),c=ivec2(1)==ivec2(1);color=vec4(a?1.:0.,b?1.:0.,c?1.:0.,1.);}
