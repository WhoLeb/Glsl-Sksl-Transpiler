#version 320 es
precision highp float;
out vec4 color;
void main(){uvec2 a=uvec2(1u,0xFFFFFFFFu);bvec2 l=lessThan(a,uvec2(2u));uint m=min(a.y,3u);color=vec4(l.x?1.:0.,l.y?1.:0.,float(clamp(m,1u,2u)),1.);}
