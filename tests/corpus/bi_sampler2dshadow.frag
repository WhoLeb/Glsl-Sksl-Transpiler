#version 320 es
precision highp float;
uniform sampler2DShadow t;out vec4 color;
void main(){color=vec4(texture(t,vec3(.5)));}
