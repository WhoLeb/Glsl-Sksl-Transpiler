#version 320 es
precision highp float;
out vec4 color;
void main(){vec4 p=gl_FragCoord.xyxy*vec4(.1,.2,.3,.4);color=fwidth(p)+dFdy(p);}
