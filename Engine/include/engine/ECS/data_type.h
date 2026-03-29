#pragma once

#include <cstdint>

namespace ECS{

    typedef uint32_t EntityID;

    typedef uint32_t SystemID;

    typedef uint32_t ReservationID;

    // struct EntityID{
    //     uint32_t index;
    //     uint32_t generation = 1;
    // };

    constexpr uint32_t INVALID_ENTITY = 0;
    
}

