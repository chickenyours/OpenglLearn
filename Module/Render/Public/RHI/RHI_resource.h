#pragma once

namespace Render::RHI {

    class RHIResource {
    public:
        virtual ~RHIResource() = default;
        virtual void Release() {}
    };

} // namespace Render::RHI