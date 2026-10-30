#version 320 es
precision highp float;
out vec4 color;
float g=0.;bool f(){g+=1.;return g<5.;}void main(){float s=0.;for(int i=0;f()&&i<16;i++){s+=1.;}color=vec4(s/10.,g/10.,0.,1.);}
