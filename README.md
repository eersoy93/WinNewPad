# WinNewPad
An old Wordpad-like word processor written with C and Windows API.

## Features

- Rich text editing based on the Windows Rich Edit control (`Msftedit.dll`, with a `Riched20.dll` fallback)
- Classic WordPad-style user interface: menu bar, standard toolbar, format bar, status bar and context menu
- **File:** New, Open, Save, Save As, Print, Page Setup
  - Opens Rich Text (`.rtf`) and plain text (UTF-8, UTF-16 LE and ANSI) documents
  - Saves as Rich Text Format, UTF-8 text or Unicode (UTF-16 LE) text
  - Opens a file passed on the command line or dropped onto the window
- **Edit:** Undo, Redo, Cut, Copy, Paste, Delete, Select All, Find, Find Next, Replace
- **View:** Toggle toolbar, format bar, status bar and word wrap
- **Insert:** Date and time
- **Format:** Font, bold, italic, underline, text color, bullets, alignment and paragraph indentation
- Format bar with font name and font size combo boxes
- Status bar with menu help and the caret position (line and column)
- Prompts to save unsaved changes

## Building

The program is plain C (C99) and uses only the Windows API, so no third-party
libraries are needed. Both **32-bit (x86)** and **64-bit (x64)** builds are supported.

### Visual Studio

Open `WinNewPad.sln` in Visual Studio 2022 (with the *Desktop development with C++*
workload) and choose one of the configurations:

| Configuration | Platform |
|---------------|----------|
| Debug         | x86 (Win32) |
| Release       | x86 (Win32) |
| Debug         | x64 |
| Release       | x64 |

The executable is written to `bin\<Platform>\<Configuration>\WinNewPad.exe`.

From a Developer Command Prompt:

```
msbuild WinNewPad.sln /p:Configuration=Release /p:Platform=x86
msbuild WinNewPad.sln /p:Configuration=Release /p:Platform=x64
```

### MinGW-w64 (Make)

With the MinGW-w64 cross compilers (`i686-w64-mingw32-gcc` and `x86_64-w64-mingw32-gcc`):

```
make            # builds both
make x86        # build/x86/WinNewPad.exe (32-bit)
make x64        # build/x64/WinNewPad.exe (64-bit)
make clean
```

On Windows with MSYS2, use the matching shell (MINGW32 or MINGW64) and clear the prefix,
for example `make x64 PREFIX64=` or `make x86 PREFIX32=`.

### CMake

```
# Visual Studio generator
cmake -S . -B build-x86 -A Win32
cmake -S . -B build-x64 -A x64
cmake --build build-x64 --config Release

# MinGW-w64 cross compilation
cmake -S . -B build-x64 -DCMAKE_SYSTEM_NAME=Windows \
      -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc -DCMAKE_RC_COMPILER=x86_64-w64-mingw32-windres
cmake --build build-x64
```

## License

See [LICENSE](LICENSE).
