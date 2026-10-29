#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(0.);for(int i=0;i<4;++i){color+=vec4(.1);if(color.r>.25)return;}}
