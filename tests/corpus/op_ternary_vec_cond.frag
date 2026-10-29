#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
void main(){vec4 v=position.x>1.?vec4(1.):vec4(0.);color=v;}
