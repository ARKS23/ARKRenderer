#include "app/Application.h"

#include <utility>

int main(int argc, char** argv) {
    ark::ApplicationDesc desc{};
    if (argc > 1 && argv[1]) {
        desc.defaultModelPath = argv[1];
    }
    if (argc > 2 && argv[2]) {
        desc.defaultEnvironmentPath = argv[2];
    }

    ark::Application app{std::move(desc)};
    return app.run();
}
