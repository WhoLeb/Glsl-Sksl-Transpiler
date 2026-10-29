#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
void main(){float v=position.x;color=vec4(dFdx(v),dFdy(v),fwidth(v),1.);}
