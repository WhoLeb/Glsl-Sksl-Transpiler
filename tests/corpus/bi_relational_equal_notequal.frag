#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(equal(ivec2(1),ivec2(1)).x?1.:0.,notEqual(bvec2(true),bvec2(false)).y?1.:0.,equal(vec2(1.),vec2(2.)).x?1.:0.,all(equal(bvec2(true),bvec2(true)))?1.:0.);}
