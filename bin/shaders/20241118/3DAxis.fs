#version 460 core 
flat in int gID;
out vec4 FragColor;

void main() {
    if(gID == 0)      FragColor = vec4(1,0,0,1);
    else if(gID == 1) FragColor = vec4(0,1,0,1);
    else              FragColor = vec4(0,0,1,1);
}
