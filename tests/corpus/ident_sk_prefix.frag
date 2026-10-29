#version 320 es
precision highp float;
out vec4 color;
void main(){float sk_FragCoord=.1,sk_Color=.2;color=vec4(sk_FragCoord+sk_Color);}
