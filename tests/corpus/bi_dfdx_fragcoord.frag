#version 320 es
precision highp float;
out vec4 color;
void main(){vec2 p=gl_FragCoord.xy*.01;color=vec4(dFdx(p),fwidth(p.x*p.y),1.);}
