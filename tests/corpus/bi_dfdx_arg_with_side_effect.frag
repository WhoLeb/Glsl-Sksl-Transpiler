#version 320 es
precision highp float;
out vec4 color;
float n=0.; float bump(){n+=1.;return n*gl_FragCoord.x;} void main(){float d=dFdx(bump());color=vec4(d,n,0.,1.);}
