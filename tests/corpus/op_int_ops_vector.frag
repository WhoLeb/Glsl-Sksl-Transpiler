#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
void main(){ivec3 v=ivec3(count,count*3,-count);ivec3 w=(v&ivec3(5))|(v<<2)^(v>>1);w%=ivec3(7);color=vec4(vec3(w)/50.,1.);}
