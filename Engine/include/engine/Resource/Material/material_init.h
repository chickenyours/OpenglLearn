#pragma once

#include "engine/Resource/Material/material_interface_loader_registry.h"

#include "engine/Resource/Material/Interfaces/BPR.h"
#include "engine/Resource/Material/Interfaces/BaseParticle.h"


void RegisterAllMaterial(){
    REGISTER_MATERIAL_INTERFACE("IBPR", Resource::IBPR);
    REGISTER_MATERIAL_INTERFACE("IBaseParticle", Resource::IBaseParticle);
}