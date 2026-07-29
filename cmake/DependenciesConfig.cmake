# --- Threading support (still needed for GCC even with C++11)
set(CMAKE_THREAD_PREFER_PTHREAD YES)
find_package(Threads REQUIRED)

# --- Ogre 3D graphics engine ---
if (ROR_OGRE14)
    find_package(OGRE 14.5.2 EXACT CONFIG REQUIRED
        COMPONENTS Bites Overlay Paging RTShaderSystem MeshLodGenerator Terrain)
    find_package(SDL2 2.32.10 EXACT CONFIG REQUIRED)
else ()
    find_package(OGRE 1.11 REQUIRED
        COMPONENTS Bites Overlay Paging RTShaderSystem MeshLodGenerator Terrain)
endif ()

# --- Object Oriented Input System ---
if (ROR_OGRE14)
    find_package(ois CONFIG REQUIRED)
    # The upstream OIS package advertises include/, while its public header
    # lives in include/ois/. Repair the imported target so direct-toolchain and
    # Conan dependency-provider builds expose the same valid include contract.
    set(_ror_ois_include_dirs ${ois_INCLUDE_DIRS})
    get_target_property(_ror_ois_target_include_dirs
        ois::ois INTERFACE_INCLUDE_DIRECTORIES)
    list(APPEND _ror_ois_include_dirs ${_ror_ois_target_include_dirs})
    set(_ror_ois_header_found OFF)
    foreach (_ror_ois_include_dir IN LISTS _ror_ois_include_dirs)
        if (EXISTS "${_ror_ois_include_dir}/OIS.h")
            set(_ror_ois_header_found ON)
            break()
        elseif (EXISTS "${_ror_ois_include_dir}/ois/OIS.h")
            set_property(TARGET ois::ois APPEND PROPERTY
                INTERFACE_INCLUDE_DIRECTORIES "${_ror_ois_include_dir}/ois")
            set(_ror_ois_header_found ON)
            break()
        endif ()
    endforeach ()
    if (NOT _ror_ois_header_found)
        message(FATAL_ERROR "The pinned OIS package does not contain OIS.h")
    endif ()
    unset(_ror_ois_header_found)
    unset(_ror_ois_include_dir)
    unset(_ror_ois_include_dirs)
    unset(_ror_ois_target_include_dirs)
else ()
    find_package(OIS REQUIRED)
endif ()

# --- MyGUI - graphical user inferface ---
find_package(MyGUI REQUIRED)

# --- fmt - A modern formatting library  ---
find_package(fmt REQUIRED)

# --- RapidJSON - JSON parser/generator ---
find_package(RapidJSON REQUIRED)

# --- OpenSSL - authenticated terrain bundle verification ---
find_package(OpenSSL REQUIRED)

# Components

# --- OpenAL - audio library ---
find_package(OpenAL)
cmake_dependent_option(ROR_USE_OPENAL "use OPENAL" ON "OPENAL_FOUND" OFF)

# --- Discord RPC -- Rich Presence for discord ---
find_package(discord_rpc)
cmake_dependent_option(ROR_USE_DISCORD_RPC "use discord-rpc" ON "discord_rpc_FOUND" OFF)

# --- SocketW - networking library ---
find_package(SocketW)
cmake_dependent_option(ROR_USE_SOCKETW "use SOCKETW" ON "TARGET SocketW::SocketW" OFF)

# --- AngelScript - scripting interface ---
find_package(Angelscript)
cmake_dependent_option(ROR_USE_ANGELSCRIPT "use angelscript" ON "TARGET Angelscript::angelscript" OFF)

# --- cURL ---
find_package(CURL)
cmake_dependent_option(ROR_USE_CURL "use curl" ON "CURL_FOUND" OFF)

# --- Caelum -- Ogre addon for realistic sky rendering ---
if (ROR_OGRE14)
    set(ROR_USE_CAELUM OFF CACHE BOOL "use caelum" FORCE)
else ()
    find_package(Caelum)
    cmake_dependent_option(ROR_USE_CAELUM "use caelum" ON "TARGET Caelum::Caelum" OFF)
endif ()

# --- PagedGeometry -- Ogre addon ---
if (ROR_OGRE14)
    set(ROR_USE_PAGED OFF CACHE BOOL "use pagedgeometry" FORCE)
else ()
    find_package(PagedGeometry)
    cmake_dependent_option(ROR_USE_PAGED "use pagedgeometry" ON "TARGET PagedGeometry::PagedGeometry" OFF)
endif ()
