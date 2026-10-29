#version 320 es
precision highp float;
out vec4 color;
void main(){float a=1.,b=2.;float c=a +/**/+ b;float d=a -/**/- b;color=vec4(c+d);}
