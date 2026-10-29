#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(lessThan(ivec3(1),ivec3(2)).z?1.:0.,any(greaterThan(ivec2(1),ivec2(2)))?1.:0.,0.,1.);}
