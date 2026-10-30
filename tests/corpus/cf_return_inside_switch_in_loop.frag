#version 320 es
precision highp float;
out vec4 color;
float f(){for(int i=0;i<4;++i){switch(i){case 2:return float(i)*.25;default:break;}}return 0.;} void main(){color=vec4(f());}
