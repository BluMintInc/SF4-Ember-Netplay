set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
if(PORT STREQUAL "sdl3")
    set(VCPKG_LIBRARY_LINKAGE static)
endif()
set(SPDLOG_WCHAR_FILENAMES ON)

# build-current-dependencies.ps1 supplies the same x86 MSVC environment used
# by Ember. This also avoids older vcpkg discovery rejecting VS 18 toolsets.
set(VCPKG_LOAD_VCVARS_ENV OFF)
set(VCPKG_ENV_PASSTHROUGH PATH INCLUDE LIB LIBPATH)

