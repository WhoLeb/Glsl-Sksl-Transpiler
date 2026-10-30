#version 320 es
precision highp float;
out vec4 color;
void main(){float s=0.;for(int i=0;i<4;++i){if(gl_FragCoord.x>float(i)*2.)break;s+=1.;}color=vec4(dFdx(s));}
