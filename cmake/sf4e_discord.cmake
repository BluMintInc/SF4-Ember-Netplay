option(SF4E_BUILD_DISCORD "Build the x64 Discord companion (requires the official SDK)" OFF)
file(READ "${CMAKE_SOURCE_DIR}/cmake/discord-sdk-pin.json" discord_pin)
string(JSON discord_app_id GET "${discord_pin}" applicationId)
string(JSON discord_sdk_version GET "${discord_pin}" version)
string(JSON discord_sdk_hash GET "${discord_pin}" sha256)
set(SF4E_DISCORD_APPLICATION_ID "${discord_app_id}" CACHE STRING "Public Discord application ID")
set(SF4E_DISCORD_SDK_ARCHIVE "" CACHE FILEPATH "Original Discord SDK archive")
if(SF4E_BUILD_DISCORD)
    if(NOT SF4E_DISCORD_APPLICATION_ID MATCHES "^[1-9][0-9]+$" OR
       NOT EXISTS "${SF4E_DISCORD_SDK_ARCHIVE}")
        message(FATAL_ERROR "Discord requires an application ID and the pinned SDK archive")
    endif()
    file(SHA256 "${SF4E_DISCORD_SDK_ARCHIVE}" discord_archive_hash)
    string(TOLOWER "${discord_sdk_hash}" expected_discord_hash)
    if(NOT discord_archive_hash STREQUAL expected_discord_hash)
        message(FATAL_ERROR "Discord SDK archive SHA-256 does not match the pin")
    endif()
    # Build exclusively from the verified vendor archive. Unpacking it takes
    # half a minute, so a configure unpacks it again only when the verified
    # hash differs from the one recorded after the last complete unpack.
    set(SF4E_DISCORD_SDK_ROOT "${CMAKE_BINARY_DIR}/discord-sdk/discord_social_sdk")
    set(discord_unpacked "${CMAKE_BINARY_DIR}/discord-sdk-unpacked.sha256")
    set(discord_unpacked_hash "")
    if(EXISTS "${discord_unpacked}")
        file(READ "${discord_unpacked}" discord_unpacked_hash)
    endif()
    if(NOT discord_unpacked_hash STREQUAL discord_archive_hash)
        file(REMOVE_RECURSE "${CMAKE_BINARY_DIR}/discord-sdk")
        file(ARCHIVE_EXTRACT INPUT "${SF4E_DISCORD_SDK_ARCHIVE}" DESTINATION "${CMAKE_BINARY_DIR}/discord-sdk")
        file(WRITE "${discord_unpacked}" "${discord_archive_hash}")
    endif()
    if(NOT EXISTS "${SF4E_DISCORD_SDK_ROOT}/License-Notices.txt")
        message(FATAL_ERROR "The pinned SDK is missing its redistribution notice")
    endif()
    find_path(SF4E_JSON_INCLUDE nlohmann/json.hpp REQUIRED)
    include(ExternalProject)
    string(REGEX REPLACE "/VC/.*$" "" discord_vs_instance "${CMAKE_CXX_COMPILER}")
    ExternalProject_Add(EmberDiscord
        BUILD_ALWAYS TRUE
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/discord"
        BINARY_DIR "${CMAKE_BINARY_DIR}/discord-x64"
        CMAKE_GENERATOR "Visual Studio 18 2026"
        CMAKE_GENERATOR_PLATFORM x64
        CMAKE_GENERATOR_INSTANCE "${discord_vs_instance}"
        CMAKE_ARGS "-DSF4E_PRODUCT_VERSION=${PROJECT_VERSION}"
            "-DSF4E_DISCORD_SDK_ROOT=${SF4E_DISCORD_SDK_ROOT}"
            "-DSF4E_DISCORD_APPLICATION_ID=${SF4E_DISCORD_APPLICATION_ID}"
            "-DSF4E_JSON_INCLUDE=${SF4E_JSON_INCLUDE}"
            "-DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/discord-runtime"
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --config Release
        INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR> --config Release)
    file(WRITE "${CMAKE_BINARY_DIR}/discord-build.json"
        "{\"applicationId\":\"${SF4E_DISCORD_APPLICATION_ID}\",\"sdkVersion\":\"${discord_sdk_version}\",\"sdkSha256\":\"${expected_discord_hash}\"}\n")
    install(DIRECTORY "${CMAKE_BINARY_DIR}/discord-runtime/" DESTINATION .)
    install(FILES "${CMAKE_BINARY_DIR}/discord-build.json" DESTINATION .)
    install(FILES "${SF4E_DISCORD_SDK_ROOT}/License-Notices.txt" DESTINATION notices RENAME Discord-SDK.txt)
endif()
