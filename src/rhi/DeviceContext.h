#pragma once

namespace ark::rhi {
    struct SubmitDesc {};

    class DeviceContext {
    public:
        virtual ~DeviceContext() = default;

        virtual void begin() = 0;
        virtual void end() = 0;
        virtual void submit(const SubmitDesc& desc) = 0;
    };
} // namespace ark::rhi
