#pragma once

namespace ark
{
class Application
{
public:
    virtual ~Application() = default;

    virtual int run() = 0;
};
} // namespace ark
