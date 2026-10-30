#version 320 es
precision highp float;
out vec4 color;
void main(){float d=dFdx(gl_FragCoord.x);if(d>0.)discard;color=vec4(d);}
