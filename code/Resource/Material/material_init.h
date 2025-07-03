#pragma once

#include "code/Resource/Material/material_interface_loader_registry.h"

#include "code/Resource/Material/Interfaces/BPR.h"


void RegisterAllMaterial(){
    REGISTER_MATERIAL_INTERFACE("IBPR", Resource::IBPR);
}