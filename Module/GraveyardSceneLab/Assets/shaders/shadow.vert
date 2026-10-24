#version 450 core
// The source loader expands this include. Wind deformation is exactly shared
// with the colour pass, rather than maintaining a second moving silhouette.
#define LAB_SHADOW_PASS
#include "scene.vert"
