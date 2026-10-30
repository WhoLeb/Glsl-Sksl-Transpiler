#version 320 es
precision highp float;
out vec4 color;
void main(){float v=gl_FragCoord.x;float d=dFdx(v++);color=vec4(d,v,0.,1.);}
