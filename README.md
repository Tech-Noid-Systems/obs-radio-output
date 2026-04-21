# obs-radio-output

> **Third-party community plugin** — this project is not affiliated with or endorsed by the OBS Project.

Stream audio from OBS Studio directly to Icecast and SHOUTcast internet radio servers — no external software required. Eliminates the need to run BUTT (Broadcast Using This Tool) or any other sidecar application alongside OBS.

## Features

- Native OBS output — appears alongside built-in outputs, configurable from within OBS
- Icecast 2.x support (SHOUTcast coming in a future release)
- Opus and MP3 audio encoding
- Auto-reconnect on dropped connections
- Connection status display
- macOS (Universal Binary), Windows (x64), and Linux (x86_64) support

## Installation

> Installation packages will be available once the plugin reaches a stable release. Until then, build from source (see below).

**OBS Plugin Browser:** The plugin will be listed in the OBS Plugin Browser after the first stable release.

**Manual install:** Download the appropriate package for your platform from the [Releases](https://github.com/tech-noid-systems/obs-radio-output/releases) page and follow the instructions for your OS.

## Building from Source

### Prerequisites for contributors

In addition to the per-platform build prerequisites below, contributors need these local tools to pass CI-equivalent checks before pushing:

- **clang-format (≥ 16)** — enforces C/C++ formatting. CI runs it via `.clang-format`. Install:
  - macOS: `brew install clang-format`
  - Ubuntu: `apt install clang-format`
  - Windows: via Visual Studio (bundled) or `winget install LLVM.LLVM`
- **gersemi** — enforces CMake formatting. Install: `brew install gersemi` (macOS) or `pipx install gersemi` (any platform with Python).

Run both locally before committing any changes that touch source or CMake files:

```bash
clang-format -i src/*.c src/*.h src/*.cpp src/*.hpp
gersemi -i CMakeLists.txt
```

**Recommended:** install `pre-commit` to have these hooks run automatically on every `git commit`:

```bash
pipx install pre-commit   # or: brew install pre-commit
pre-commit install        # run once per clone
```

Configuration lives in `.pre-commit-config.yaml` at the repo root. Hooks: `clang-format`, `gersemi`, trailing-whitespace fixer, end-of-file fixer, large-file check, YAML/JSON syntax check, merge-conflict marker detection.

### macOS

**Prerequisites:** Xcode Command Line Tools and [Homebrew](https://brew.sh). All other
dependencies (CMake, autotools, libshout, libmp3lame, Qt6) are installed automatically by the setup script.

```zsh
git clone https://github.com/tech-noid-systems/obs-radio-output.git
cd obs-radio-output
zsh scripts/setup-dev-macos.sh
```

The setup script installs dependencies, builds a Universal Binary (arm64 + x86_64) libshout,
and runs CMake configure. Once it completes, build and install:

```zsh
xcodebuild -project build_macos/obs-radio-output.xcodeproj \
           -target obs-radio-output \
           -configuration RelWithDebInfo \
           ONLY_ACTIVE_ARCH=NO -arch arm64 -arch x86_64

cmake --install build_macos --config RelWithDebInfo \
      --prefix release/RelWithDebInfo

cp -r release/RelWithDebInfo/obs-radio-output \
      ~/Library/Application\ Support/obs-studio/plugins/
```

Relaunch OBS. Check **Help → Log Files → Current Log** for:
`[obs-radio-output] plugin loaded successfully`

> **Note:** The libshout build is cached in `/tmp`. If you need to rebuild it (e.g. after a
> macOS update), delete `/tmp/libogg-universal-done` and `/tmp/libshout-universal-done` and
> re-run the setup script.

### Linux

**Prerequisites:** A Debian/Ubuntu-based distribution with `sudo` access. All other
dependencies are installed automatically by the setup script.

```bash
git clone https://github.com/tech-noid-systems/obs-radio-output.git
cd obs-radio-output
bash scripts/setup-dev-linux.sh
```

The script detects your architecture (x86\_64 or aarch64), installs required packages, and
runs CMake configure. Once it completes, build and install:

```bash
cmake --build build_linux_x86_64 --config RelWithDebInfo

cmake --install build_linux_x86_64 --config RelWithDebInfo \
      --prefix ~/.config/obs-studio/plugins/obs-radio-output
```

> **Flatpak OBS:** If you installed OBS via Flatpak, the plugin path is:
> `~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/obs-radio-output`

### Windows

**Prerequisites:** Windows 10/11 x64 with Visual Studio 2022 (`Desktop development with C++`
workload) and `winget`. MSYS2 and libshout will be installed automatically by the setup script.

```powershell
git clone https://github.com/tech-noid-systems/obs-radio-output.git
cd obs-radio-output
.\scripts\Setup-Dev-Windows.ps1
```

Once complete, build and install:

```powershell
cmake --build build_x64 --config RelWithDebInfo

cmake --install build_x64 --config RelWithDebInfo `
      --prefix "$env:APPDATA\obs-studio\plugins\obs-radio-output"
```

> **Note:** Streaming to Icecast/SHOUTcast is not yet functional on Windows. The plugin will
> load in OBS but the streaming output requires MSVC-compatible libshout binaries (tracked as
> a known issue — contributions welcome).

## Usage

1. Open OBS Studio and go to **Settings → Stream**
2. Select **Radio Output** from the Service dropdown
3. Enter your Icecast server hostname, port, mount point, and password
4. Select your audio codec and bitrate
5. Click **Start Streaming**

## Reporting Bugs

Please open an issue on the [GitHub Issues](https://github.com/tech-noid-systems/obs-radio-output/issues) page. Include your OBS version, OS, and the relevant section of your OBS log file.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).

This plugin links against [libobs](https://github.com/obsproject/obs-studio), which is licensed GPL-2.0. As a derived work, this plugin must carry a GPL-compatible license.

## AI Disclosure

Development of this plugin involved the use of AI-assisted tooling. Per OBS forum policy, this is disclosed here and will be included in the plugin's forum submission description.
