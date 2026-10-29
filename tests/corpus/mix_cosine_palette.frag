#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
vec3 pal(float t){return .5+.5*cos(6.28318*(vec3(1.)*t+vec3(0.,.33,.67)));}void main(){color=vec4(pal(position.x/size.x),1.);}
