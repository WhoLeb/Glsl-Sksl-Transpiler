#version 320 es
precision highp float;
uniform atomic_uint counter;out vec4 color;
void main(){color=vec4(float(atomicCounter(counter)));}
