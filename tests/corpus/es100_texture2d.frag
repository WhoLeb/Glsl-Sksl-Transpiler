#version 100
precision mediump float;
uniform sampler2D t;
void main(){gl_FragColor=texture2D(t,vec2(.5));}
