#version 320 es
precision highp float;
out vec4 color;
vec3 g=vec3(0.);void set(){g.y=.5;g.zx=vec2(.25);}void main(){set();color=vec4(g,1.);}
