#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
float f(int n){switch(n){case 0:return .5;case 1:return .25;}return 0.;} void main(){color=vec4(f(count));}
