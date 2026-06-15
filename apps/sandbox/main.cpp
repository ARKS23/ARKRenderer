#include "app/SandboxLaunchOptions.h"

#include <string_view>
#include <utility>
#include <vector>

int main(int argc, char** argv) {
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex) {
        if (!argv[argumentIndex]) {
            continue;
        }

        arguments.emplace_back(argv[argumentIndex]);
    }

    ark::ApplicationDesc desc = ark::makeSandboxApplicationDesc(arguments);
    ark::Application app{std::move(desc)};
    return app.run();
}
