#version 320 es
precision highp float;
out vec4 color;
void main(){float half3x3=.1,half4=.2,short2=.3,int3=.4;color=vec4(half3x3+half4+short2+int3);}
