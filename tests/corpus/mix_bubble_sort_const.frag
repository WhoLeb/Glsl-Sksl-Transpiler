#version 320 es
precision highp float;
out vec4 color;
void main(){float a[5]=float[5](.5,.2,.9,.1,.4);for(int i=0;i<5;++i)for(int j=0;j<4;++j)if(a[j]>a[j+1]){float t=a[j];a[j]=a[j+1];a[j+1]=t;}color=vec4(a[0],a[2],a[4],1.);}
