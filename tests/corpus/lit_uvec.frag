#version 320 es
precision highp float;
out vec4 color;
void main(){uvec3 v=uvec3(1u,2u,3u);color=vec4(vec3(v),1.);}
