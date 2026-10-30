#version 320 es
precision highp float;
out vec4 color;
void main(){int n=3;if(gl_FragCoord.x>100.)n=6;float s=0.;for(int i=0;i<n;i++)s+=.1;color=vec4(s);}
