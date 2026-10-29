#version 320 es
precision highp float;
uniform mat3 transform;uniform vec4 tint;out vec4 color;
void main(){vec3 rgb=transform*tint.rgb;color=vec4(clamp(rgb,0.,1.)*tint.a,tint.a);}
