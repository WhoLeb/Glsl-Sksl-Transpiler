#version 320 es
precision highp float;
out vec4 color;
void main(){float a[4];int n=a.length();vec4 v;int m=v.length();mat3 k;int j=k.length();color=vec4(float(n+m+j)/20.);}
