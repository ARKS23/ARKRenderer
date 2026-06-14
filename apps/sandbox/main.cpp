#include "app/Application.h"

#include <string_view>
#include <utility>

int main(int argc, char** argv) {
    ark::ApplicationDesc desc{};
    int positionalArgument = 0;
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex) {
        if (!argv[argumentIndex]) {
            continue;
        }

        const std::string_view argument = argv[argumentIndex];
        if (argument == "--debug-orientation") {
            desc.useDebugOrientationEnvironment = true;
            continue;
        }

        if (positionalArgument == 0) {
            desc.defaultModelPath = argv[argumentIndex];
        } else if (positionalArgument == 1) {
            desc.defaultEnvironmentPath = argv[argumentIndex];
        }
        ++positionalArgument;
    }

    ark::Application app{std::move(desc)};
    return app.run();
}
