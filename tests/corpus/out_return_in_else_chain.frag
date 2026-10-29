#version 320 es
precision highp float;
uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;
void main(){if(position.x<1.){color=vec4(1.);return;}else if(position.x<2.){color=vec4(.5);return;}else{color=vec4(.25);}}
