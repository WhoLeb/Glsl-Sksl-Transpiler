#version 320 es
precision highp float;
out vec4 color;
void main(){mat3 m=mat3(1.);float s=0.;for(int i=0;i<3;++i){s+=m[i][i];m[i][2-i]+=1.;}color=vec4(s/3.);}
