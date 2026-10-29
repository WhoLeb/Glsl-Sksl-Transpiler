#version 320 es
precision highp float;
uniform vec2 size;out vec4 color;
void main(){color=vec4(gl_FragCoord.xy/size,0.,1.);}
