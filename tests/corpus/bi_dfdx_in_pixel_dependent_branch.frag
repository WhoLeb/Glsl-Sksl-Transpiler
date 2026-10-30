#version 320 es
precision highp float;
out vec4 color;
void main(){float v=gl_FragCoord.x;color=vec4(0.);if(v>4.)color=vec4(dFdx(v*v));}
