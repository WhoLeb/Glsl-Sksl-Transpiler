#version 320 es
precision highp float;
uniform sampler2D t[3];uniform int k;out vec4 color;
vec4 f(sampler2D s){return texture(s,vec2(.5));}void main(){color=texture(t[k],vec2(.5))+f(t[k]);}
