#version 320 es
precision highp float;
out vec4 color;
void main(){float v=gl_FragCoord.x;if(v>4.){color=vec4(1.);return;}color=vec4(dFdx(v));}
