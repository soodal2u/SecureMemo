cmake_minimum_required(VERSION 3.20)
project(SecureMemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8 /W4)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter -municode)
  add_compile_definitions(UNICODE _UNICODE)
endif()

add_executable(SecureMemo WIN32
  src/main.cpp
  src/app/Application.cpp
  src/app/NoteManager.cpp
  src/app/SecurityGuard.cpp
  src/app/UpdateChecker.cpp
  src/crypto/Auth.cpp
  src/crypto/Crypto.cpp
  src/crypto/VaultStorage.cpp
  src/crypto/PublicIndex.cpp
  src/model/Note.cpp
  src/ui/LockDialog.cpp
  src/ui/NoteWindow.cpp
  src/ui/NotesListWindow.cpp
  src/ui/TrayIcon.cpp
  resources/app.rc
)

target_include_directories(SecureMemo PRIVATE src)

if(MSVC)
  target_link_libraries(SecureMemo PRIVATE
    bcrypt
    shell32
    comctl32
    ole32
    user32
    gdi32
    advapi32
    dwmapi
    comdlg32
    uxtheme
    winhttp
  )
else()
  target_link_libraries(SecureMemo PRIVATE
    bcrypt
    shell32
    comctl32
    ole32
    user32
    gdi32
    advapi32
    uuid
    dwmapi
    comdlg32
    uxtheme
    winhttp
  )
  add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
  # Static-ish CRT not forced; keep default for MinGW/LLVM
endif()

set_target_properties(SecureMemo PROPERTIES
  OUTPUT_NAME "SecureMemo"
  WIN32_EXECUTABLE TRUE
)
