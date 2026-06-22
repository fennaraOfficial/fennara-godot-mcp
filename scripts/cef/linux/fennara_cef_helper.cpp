#include <dlfcn.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include <include/capi/cef_app_capi.h>

using cef_execute_process_t = decltype(&::cef_execute_process);

int main(int argc, char **argv) {
    const std::filesystem::path executable =
        argc > 0 && argv[0] != nullptr ? std::filesystem::path(argv[0]) : std::filesystem::path();
    const std::filesystem::path runtime_dir =
        executable.has_parent_path() ? executable.parent_path() : std::filesystem::current_path();
    const std::filesystem::path libcef_path = runtime_dir / "libcef.so";

    void *handle = dlopen(libcef_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char *open_error = dlerror();
        std::cerr << "fennara_cef_helper: failed to open " << libcef_path << ": "
                  << (open_error != nullptr ? open_error : "unknown error") << "\n";
        return EXIT_FAILURE;
    }

    dlerror();
    auto execute_process =
        reinterpret_cast<cef_execute_process_t>(dlsym(handle, "cef_execute_process"));
    const char *symbol_error = dlerror();
    if (symbol_error != nullptr || execute_process == nullptr) {
        std::cerr << "fennara_cef_helper: libcef.so is missing cef_execute_process\n";
        dlclose(handle);
        return EXIT_FAILURE;
    }

    cef_main_args_t args{};
    args.argc = argc;
    args.argv = argv;
    const int exit_code = execute_process(&args, nullptr, nullptr);
    dlclose(handle);
    return exit_code >= 0 ? exit_code : EXIT_SUCCESS;
}
