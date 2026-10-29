#version 320 es
precision highp float;
out vec4 color;
struct S{float x;float y;vec2 xy;vec3 rgb;}; void main(){S s;s.x=.1;s.y=.2;s.xy=vec2(.3);s.rgb=vec3(.4);color=vec4(s.xy,s.rgb.xy);}
