#pragma once

#include <vector>

namespace Render{
    class RHIResourceRegistry{
        public:
            std::vector<int> rhi_texture_pool_;
    };

}