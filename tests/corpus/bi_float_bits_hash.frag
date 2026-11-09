#version 320 es
precision highp float;
out vec4 color;
float h(uint i){return uintBitsToFloat(i*0x1b74a659u>>9u|0x3f800000u)-1.;}void main(){color=vec4(h(floatBitsToUint(gl_FragCoord.x)));}
