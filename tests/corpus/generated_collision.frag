#version 320 es
precision highp float;
out vec4 color;
float transpiler_color;float transpiler_entry;void main(){transpiler_color=.2;transpiler_entry=.3;color=vec4(transpiler_color+transpiler_entry);}
