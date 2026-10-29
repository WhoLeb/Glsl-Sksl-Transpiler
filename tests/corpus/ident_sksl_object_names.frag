#version 320 es
precision highp float;
out vec4 color;
void main(){float shader=.1,colorFilter=.2,blender=.3;color=vec4(shader+colorFilter+blender);}
