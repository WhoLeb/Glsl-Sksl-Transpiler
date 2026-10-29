#version 320 es
precision highp float;
uniform vec2 position;out vec4 color;
void main(){float v=mod(floor(position.x)+floor(position.y),2.);color=vec4(vec3(v),1.);}
