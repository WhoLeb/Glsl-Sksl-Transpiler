#version 100
#extension GL_OES_standard_derivatives : enable
precision mediump float;
void main(){gl_FragColor=vec4(dFdx(gl_FragCoord.x),fwidth(gl_FragCoord.y),0.,1.);}
