#version 320 es
precision highp float;
out vec4 color;
float g=0.; void main(){g=gl_FragCoord.x*.5;g+=gl_FragCoord.y;color=vec4(dFdx(g),dFdy(g),0.,1.);}
