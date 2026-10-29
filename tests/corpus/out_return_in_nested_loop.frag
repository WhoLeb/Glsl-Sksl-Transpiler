#version 320 es
precision highp float;
out vec4 color;
void main(){color=vec4(0.);for(int i=0;i<4;++i){for(int j=0;j<4;++j){if(i+j==3){color=vec4(1.);return;}}}color=vec4(.5);}
