#pragma once

#include <type_traits>

namespace Render::RHI {

    template<typename Enum>
    constexpr auto ToUnderlying(Enum e) noexcept {
        return static_cast<std::underlying_type_t<Enum>>(e);
    }

}
