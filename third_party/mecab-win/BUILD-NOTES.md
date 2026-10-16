# MeCab 0.996 for Windows / MSVC (x64 + x86) + unidic-lite

Built and verified on this machine (VS2022 Community, MSVC 14.44, CMake 3.31,
Ninja). MeCab source = <https://github.com/taku910/mecab> `mecab/` (version 0.996,
master commit `3a07c4e`) — the same commit the vcpkg port uses.

The dictionary is **unidic-lite 1.0.8** (unidic 2.1.2), taken unmodified from
`...\Python314\Lib\site-packages\unidic_lite\dicdir`. That dictionary was built
with MeCab dictionary format version **102**, which is exactly `DIC_VERSION` in
MeCab 0.996 — so no format mismatch.

---

## 1. Layout

```
mecab-win/
  include/mecab.h                  the ONLY header a consumer needs
  lib/x64/mecab.lib                static lib, /MD  (dynamic CRT)   <- default
  lib/x86/mecab.lib
  lib-mt/x64/mecab.lib             static lib, /MT  (static  CRT), self-contained
  lib-mt/x86/mecab.lib
  bin/x64/mecab.exe                optional CLI tool (/MD)
  bin/x86/mecab.exe
  bin-mt/x64/mecab.exe             optional CLI tool (/MT, standalone)
  bin-mt/x86/mecab.exe
  cmake/mecabConfig.cmake          find_package(mecab) support
  dicdir/                          ship this next to your exe (see §4)
  source/                          complete, buildable MeCab source tree
  patches/mecab-0.996-msvc.patch   the 6-file diff vs. upstream
```

---

## 2. Minimal set of files to copy into your CMake project

The absolute minimum for linking:

| file | why |
|---|---|
| `include/mecab.h` | the C/C++ API |
| `lib/<arch>/mecab.lib` **or** `lib-mt/<arch>/mecab.lib` | the static library |
| `dicdir/*` (see §4) | needed at *runtime*, not at link time |

`config.h` is **not** needed by consumers — it is only used while compiling the
library itself.

## 3. CMake snippet (works for x64 and x86)

### Option A — `find_package` (copy the whole `mecab-win` folder into e.g. `third_party/`)

```cmake
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mecab-win/cmake")
find_package(mecab CONFIG REQUIRED)

add_library(my_ime SHARED ...)
target_link_libraries(my_ime PRIVATE mecab::mecab)          # /MD flavour
# target_link_libraries(my_ime PRIVATE mecab::mecab-staticmt) # /MT flavour
```

### Option B — plain, no package config (copy only `mecab.h` + the two `.lib`s)

```cmake
set(MECAB_WIN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mecab-win")

# pick the architecture
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(MECAB_ARCH x64)
else()
  set(MECAB_ARCH x86)
endif()

# "lib"    = /MD (dynamic CRT)  -> match this with MSVC_RUNTIME_LIBRARY ...DLL
# "lib-mt" = /MT (static  CRT)  -> match this with MSVC_RUNTIME_LIBRARY MultiThreaded
add_library(mecab STATIC IMPORTED GLOBAL)
set_target_properties(mecab PROPERTIES
  IMPORTED_LOCATION             "${MECAB_WIN_ROOT}/lib/${MECAB_ARCH}/mecab.lib"
  INTERFACE_INCLUDE_DIRECTORIES "${MECAB_WIN_ROOT}/include"
  INTERFACE_COMPILE_DEFINITIONS "MECAB_STATIC"      # MANDATORY, see gotchas
  INTERFACE_LINK_LIBRARIES      "advapi32")         # utils.cpp uses the registry

target_link_libraries(my_ime PRIVATE mecab)
```

Both snippets expand to the same command line:

```
cl ... /DMECAB_STATIC /I<mecab-win>\include mycode.cpp /link <mecab-win>\lib\x64\mecab.lib advapi32.lib
```

**Building from source instead** (no prebuilt `.lib` needed): use
`source/CMakeLists.txt` as a subdirectory, or as an external project:

```cmake
add_subdirectory(third_party/mecab-win/source mecab-build)
# then:  target_link_libraries(my_ime PRIVATE mecab)
# It inherits CMAKE_MSVC_RUNTIME_LIBRARY from your project, so no CRT mismatch.
```

## 4. Runtime dictionary layout

`mecab_new2()` / `mecab_new()` do **not** know where the dictionary is; you pass
it explicitly. Ship a `dicdir` folder next to your `.exe` (e.g. `.\dicdir`) or any
other writable-to-read location, then pass it:

```cpp
char *av[] = { (char*)"mecab", (char*)"-r", (char*)"C:\\...\\dicdir\\mecabrc",
                              (char*)"-d", (char*)"C:\\...\\dicdir" };
mecab_t *m = mecab_new(5, av);          // <- use this, not mecab_new2()
```

`dicdir` must contain **exactly these 6 files** (verified: nothing else is read
at runtime):

```
dicdir/
  sys.dic       187,680,870 bytes   <- the actual dictionary (mmap'd, not RAM-resident)
  matrix.bin     71,544,726 bytes   <- connection matrix (mmap'd)
  char.bin          262,496 bytes
  unk.dic             5,475 bytes
  dicrc               1,444 bytes   <- cost-factor, node-format, unidic output format
  mecabrc                62 bytes   <- dummy, but it MUST exist (see gotchas)
```

`left-id.def`, `right-id.def`, `rewrite.def`, `version`, `AUTHORS`, `COPYING`,
`*.pdf` are **not** needed at runtime — they are only inputs for
`mecab-dict-index`. Verified by running with a dicdir containing only the 6 files
above.

Total ≈ 260 MB. `matrix.bin`+`sys.dic` are memory-mapped, so this does not cost
260 MB of RAM, but the files must be present on disk.

## 5. Gotchas (all of these were reproduced on this machine)

1. **`mecab_new2()` cannot handle spaces in paths and does not understand
   quotes.** `Param::open(const char*)` splits its argument string on
   whitespace only. `mecab_new2("-d C:\\My Dir\\dicdir")` fails, and
   `mecab_new2("-d \"C:\\My Dir\\dicdir\"")` fails too (the quote characters
   become part of the path). **Use `mecab_new(argc, argv)` instead** — it never
   re-tokenises. Verified: same dictionary at `...\my dicdir` fails via
   `mecab_new2` and works via `mecab_new`.

2. **A `mecabrc` file must always be reachable, otherwise `mecab_new*` returns
   `NULL`.** `load_dictionary_resource()` looks for the rc file in this order:
   `-r <file>` → `%HOME%\.mecabrc` → `%MECABRC%` →
   `HKLM\software\mecab\mecabrc` → `HKCU\software\mecab\mecabrc` →
   `<directory of the module that contains libmecab>\mecabrc` → `"mecabrc"`
   (relative to the CWD). If none loads, `mecab_new*` fails. The safest thing is
   to always pass `-r <dicdir>\mecabrc` explicitly (or ship `mecabrc` next to
   your `.exe` / next to `mecab.dll`).

   Note the "module directory" differs by flavour: for the **static** build
   `DllInstance == 0`, so `GetModuleFileNameW(NULL)` yields the **.exe**
   directory; for the **DLL** build it is the directory of **mecab.dll**.

3. **`mecab_new*` returns `NULL` with an empty error string** in the common
   failure cases; `mecab_strerror(NULL)` may print nothing. Always check for
   `NULL` and log your own context.

4. **You must define `MECAB_STATIC`** when consuming the static library.
   Upstream `mecab.h` declares the C API `__declspec(dllimport)` on Windows
   unless `MECAB_STATIC` (our addition) or `DLL_EXPORT` is defined; without it
   the link fails with `unresolved external symbol __imp_mecab_new2`.
   (This is the only change to `mecab.h`, see `patches/`.)

5. **You must also link `advapi32.lib`.** `utils.cpp` calls
   `RegOpenKeyExW`/`RegQueryValueExW`/`RegCloseKey`. The CMake snippets above
   add it for you.

6. **CRT must match.** The `/MD` and `/MT` static libraries are both shipped.
   Linking a `/MD` `mecab.lib` into a `/MT` target (or vice versa) fails with
   `LNK2038: 'RuntimeLibrary' mismatch`. Pick the matching folder, or use
   `source/` and let CMake inherit your project's setting.

7. **Runtime DLLs.**
   * `/MD` flavour → your exe needs the **VC++ 2015-2022 x64/x86
     redistributable** (`VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`,
     `MSVCP140.dll` + the `api-ms-win-crt-*` set). Verified with `dumpbin /dependents`.
   * `/MT` flavour → depends only on `KERNEL32.dll`/`ADVAPI32.dll`, i.e. fully
     self-contained. Use this if the IME must run with zero prerequisites.
   * vcpkg's prebuilt `mecab.dll` is `/MD`, so it needs the redist too.

8. **Source encoding / `/utf-8`.** MeCab's own sources are pure ASCII, but your
   own code with Japanese literals or comments triggers MSVC warning `C4819`
   and can produce garbage in narrow string literals under the default CP932
   codepage. Compile your project with `/utf-8` (this was needed for the test
   program). MeCab's API is byte-oriented and expects **UTF-8** in and returns
   **UTF-8** out for this dictionary — no conversion needed. Do **not** pass the
   returned strings through `WideCharToMultiByte`/`MultiByteToWideChar` with the
   ANSI codepage.

9. **`register` keyword.** MeCab 0.996 uses `register`, which is removed in
   C++17. Compile the library (and `source/CMakeLists.txt` does this) with
   `/std:c++14`. In C++17 it is only a warning on MSVC today, but C++14 is what
   upstream and the vcpkg port use.

10. **MeCab's source does not build with MSVC out of the box.** Two upstream
    bugs had to be fixed (both are the same fixes the vcpkg port applies):
    * `common.h` — `WPATH_FORCE` is left undefined for non-GCC Win32 while
      several `.cpp` files call it directly.
    * `dictionary.h` — `dictionary.cpp` uses `std::binary_function` without
      including `<functional>`.
    Plus two of our own:
    * `mecab.h` — honour `MECAB_STATIC` (gotcha 4).
    * `libmecab.cpp` — `DllMain` is never called when the library is linked
      statically into an `.exe`, so the error-buffer TLS slot was never
      allocated and every error message silently became "". A lazy
      `TlsAlloc()` was added.

    See `patches/mecab-0.996-msvc.patch` (apply with `git apply -p1` from the
    `taku910/mecab` root).

## 6. vcpkg alternative (also works, DLL only)

```
vcpkg install mecab:x64-windows mecab:x86-windows
```

gives `mecab.dll` + import `mecab.lib` + headers for both architectures and a
`find_package(mecab CONFIG REQUIRED)` / `mecab::mecab` target. It works fine with
unidic-lite (verified). Downsides vs. the static build:

* you must ship `mecab.dll` (2 architectures) **and** the VC++ redistributable;
* `mecab_version()` returns the literal string `@VERSION@` (the port passes an
  un-substituted CMake placeholder);
* you cannot choose `/MT`.

## 7. Verification performed

`test/mecab_seg.cpp` calls `mecab_new2` / `mecab_new` with the unidic-lite
`dicdir` and prints the segmentation of `東北大学で研究をしています。`:

```
東北	トーホク	トウホク	トウホク	名詞-固有名詞-地名-一般
大学	ダイガク	ダイガク	大学	名詞-普通名詞-一般
で	デ	デ	で	助詞-格助詞
研究	ケンキュー	ケンキュウ	研究	名詞-普通名詞-サ変可能
を	オ	ヲ	を	助詞-格助詞
し	シ	スル	為る	動詞-非自立可能	サ行変格	連用形-一般
て	テ	テ	て	助詞-接続助詞
い	イ	イル	居る	動詞-非自立可能	上一段-ア行	連用形-一般
ます	マス	マス	ます	助動詞	助動詞-マス	終止形-一般
。			。	補助記号-句点
EOS

surfaces : [東北] [大学] [で] [研究] [を] [し] [て] [い] [ます] [。]
wakati   : 東北 大学 で 研究 を し て い ます 。
```

i.e. exactly the expected `東北 / 大学 / で / 研究 / を / し / て / い / ます / 。`.

All 12 combinations built and ran green:
static(/MD) × static(/MT) × vcpkg-DLL, x64 × x86, via `mecab_new` and
`mecab_new2`.
