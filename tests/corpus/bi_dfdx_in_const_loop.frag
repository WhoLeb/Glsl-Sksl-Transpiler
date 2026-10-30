#version 320 es
precision highp float;
out vec4 color;
void main(){float s=0.;for(int i=0;i<3;++i)s+=dFdx(gl_FragCoord.x*float(i));color=vec4(s);}
