#include <FlowGraph/Runtime.hpp>

#include <filesystem>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fg {

struct Runtime::Libs {
#if defined(_WIN32)
    std::vector<HMODULE> handles;
    ~Libs() { for (auto it = handles.rbegin(); it != handles.rend(); ++it) FreeLibrary(*it); }
#else
    std::vector<void*> handles;
    ~Libs() { for (auto it = handles.rbegin(); it != handles.rend(); ++it) dlclose(*it); }
#endif
};

Runtime::Runtime() : libs_(std::make_unique<Libs>()), reg_(std::make_unique<Registry>()) { registerBuiltins(*reg_); }

Runtime::~Runtime() {
    reg_.reset();     // before the libraries are unloaded
    libs_.reset();
}

bool Runtime::loadPlugin(const std::string& path, std::string* error) {
    auto fail = [&](const std::string& m) {
        if (error) *error = m;
        return false;
    };
    std::filesystem::path p(path);
    std::string name = p.stem().string();
#if defined(_WIN32)
    HMODULE h = LoadLibraryW(p.wstring().c_str());
    if (!h) return fail("cannot load '" + path + "' (error " + std::to_string(GetLastError()) + ")");
    auto fn = reinterpret_cast<PluginRegisterFn>(GetProcAddress(h, "fgRegisterPlugin"));
    if (!fn) { FreeLibrary(h); return fail("'" + path + "' does not export fgRegisterPlugin"); }
#else
    void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) return fail(std::string("cannot load '") + path + "': " + dlerror());
    auto fn = reinterpret_cast<PluginRegisterFn>(dlsym(h, "fgRegisterPlugin"));
    if (!fn) { dlclose(h); return fail("'" + path + "' does not export fgRegisterPlugin"); }
#endif
    libs_->handles.push_back(h);
    reg_->beginSource(name);
    try {
        fn(*reg_);
    } catch (const std::exception& e) {
        reg_->endSource();
        return fail("plugin '" + name + "' failed to register: " + e.what());
    }
    reg_->endSource();
    pluginNames_.push_back(name);
    return true;
}

int Runtime::loadPluginsFrom(const std::string& dir, std::vector<std::string>* errors) {
    namespace fs = std::filesystem;
    int n = 0;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        if (ext != ".dll" && ext != ".so" && ext != ".dylib") continue;
        std::string err;
        if (loadPlugin(e.path().string(), &err)) ++n;
        else if (errors) errors->push_back(err);
    }
    return n;
}

} // namespace fg
