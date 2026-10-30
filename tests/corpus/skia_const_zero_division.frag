#version 320 es
precision highp float;
out vec4 color;
void main(){const float a=1.;color=vec4((a-a)/(a-a),1./(a-a),0.,1.);}
