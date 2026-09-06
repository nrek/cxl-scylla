# Scintilla 5.6.6 + Lexilla 5.5.3 (vendored; static link into scyllagpt).
# Licenses: third_party/scintilla/License.txt, third_party/lexilla/License.txt

set(SCINTILLA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/scintilla")
set(LEXILLA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lexilla")

file(GLOB SCINTILLA_SRC CONFIGURE_DEPENDS
    "${SCINTILLA_DIR}/src/*.cxx"
    "${SCINTILLA_DIR}/win32/*.cxx"
)
list(FILTER SCINTILLA_SRC EXCLUDE REGEX "ScintillaDLL\\.cxx$")

add_library(scintilla_static STATIC ${SCINTILLA_SRC})
target_include_directories(scintilla_static PUBLIC
    "${SCINTILLA_DIR}/include"
    "${SCINTILLA_DIR}/src"
    "${SCINTILLA_DIR}/win32"
)
target_compile_definitions(scintilla_static PRIVATE
    _CRT_SECURE_NO_WARNINGS
    NO_CXX11_REGEX
)
target_compile_features(scintilla_static PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(scintilla_static PRIVATE /W3 /wd4244 /wd4267 /wd4996 /utf-8)
endif()
target_link_libraries(scintilla_static PUBLIC
    imm32 ole32 oleaut32 uuid gdi32 user32 advapi32
    d2d1 dwrite windowscodecs
)

file(GLOB LEXILLA_LEXERS CONFIGURE_DEPENDS "${LEXILLA_DIR}/lexers/*.cxx")
file(GLOB LEXILLA_LEXLIB CONFIGURE_DEPENDS "${LEXILLA_DIR}/lexlib/*.cxx")
set(LEXILLA_SRC
    "${LEXILLA_DIR}/src/Lexilla.cxx"
    ${LEXILLA_LEXLIB}
    ${LEXILLA_LEXERS}
)

add_library(lexilla_static STATIC ${LEXILLA_SRC})
target_include_directories(lexilla_static PUBLIC
    "${LEXILLA_DIR}/include"
    "${LEXILLA_DIR}/lexlib"
    "${SCINTILLA_DIR}/include"
)
target_compile_definitions(lexilla_static PRIVATE
    LEXILLA_NO_EXPORT
    _CRT_SECURE_NO_WARNINGS
)
target_compile_features(lexilla_static PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(lexilla_static PRIVATE /W3 /wd4244 /wd4267 /wd4996 /utf-8)
endif()
target_link_libraries(lexilla_static PUBLIC scintilla_static)
