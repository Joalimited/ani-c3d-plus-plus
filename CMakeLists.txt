cmake_minimum_required(VERSION 3.20)
project(ani-c3d VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(ANI_STATIC "Link everything statically (the single-file Windows exe)" OFF)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(REMOVE_ITEM APP_SOURCES ${CMAKE_SOURCE_DIR}/src/main.cpp)

add_library(miniz STATIC third_party/miniz.c)
target_include_directories(miniz PUBLIC third_party)

add_library(anicore STATIC ${APP_SOURCES} src/third_party_impl.cpp)
target_include_directories(anicore PUBLIC src third_party)
target_link_libraries(anicore PUBLIC miniz)

find_package(PkgConfig REQUIRED)
if(WIN32)
  # MinGW cross build: SDL2, curl (on OpenSSL) and libwebp come from /opt (tools/Containerfile)
  set(SDL2_ROOT /opt/sdl2-mingw)
  set(CURL_ROOT /opt/curl-mingw)
  set(WEBP_ROOT /opt/webp-mingw)
  target_include_directories(anicore PUBLIC ${SDL2_ROOT}/include ${SDL2_ROOT}/include/SDL2 ${CURL_ROOT}/include ${WEBP_ROOT}/include)
  target_compile_definitions(anicore PUBLIC CURL_STATICLIB PCRE2_STATIC NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
  target_link_directories(anicore PUBLIC ${SDL2_ROOT}/lib ${CURL_ROOT}/lib ${WEBP_ROOT}/lib)
  target_link_libraries(anicore PUBLIC
    ${CURL_ROOT}/lib/libcurl.a ssl crypto pcre2-8 sqlite3 webp sharpyuv z
    mingw32 SDL2main SDL2
    opengl32 ws2_32 crypt32 bcrypt secur32 iphlpapi wldap32 normaliz pathcch
    winmm imm32 ole32 oleaut32 version uuid advapi32 setupapi shell32 user32 gdi32 dinput8 dxguid dxerr8 hid)
else()
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
  pkg_check_modules(PCRE2 REQUIRED IMPORTED_TARGET libpcre2-8)
  pkg_check_modules(SQLITE REQUIRED IMPORTED_TARGET sqlite3)
  pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
  pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp)
  find_package(OpenGL REQUIRED)
  find_package(Threads REQUIRED)
  target_link_libraries(anicore PUBLIC PkgConfig::SDL2 PkgConfig::CURL PkgConfig::PCRE2 PkgConfig::SQLITE PkgConfig::ZLIB PkgConfig::WEBP
                        OpenGL::GL Threads::Threads ${CMAKE_DL_LIBS})
endif()

if(WIN32)
  # the font and the zipped Windows mpv ride inside the exe as RCDATA resources
  set(RC_FILE ${CMAKE_BINARY_DIR}/ani-c3d.rc)
  set(MPV_ZIP "$ENV{ANI_MPV_ZIP}")
  set(MESA_ZIP "$ENV{ANI_MESA_ZIP}")
  set(RC_TEXT "101 RCDATA \"${CMAKE_SOURCE_DIR}/assets/fonts/NotoSansMono-Bold.ttf\"\n1 ICON \"${CMAKE_SOURCE_DIR}/assets/ani-c3d.ico\"\n")
  if(MPV_ZIP AND EXISTS "${MPV_ZIP}")
    string(APPEND RC_TEXT "102 RCDATA \"${MPV_ZIP}\"\n")
  endif()
  if(MESA_ZIP AND EXISTS "${MESA_ZIP}")
    string(APPEND RC_TEXT "103 RCDATA \"${MESA_ZIP}\"\n")
  endif()
  file(WRITE ${RC_FILE} "${RC_TEXT}")
  add_executable(ani-c3d WIN32 src/main.cpp ${RC_FILE})
  target_link_options(ani-c3d PRIVATE -static -static-libgcc -static-libstdc++ -mwindows -s)
else()
  add_executable(ani-c3d src/main.cpp)
endif()
target_link_libraries(ani-c3d PRIVATE anicore)
