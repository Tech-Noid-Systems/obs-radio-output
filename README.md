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

### Requirements

- CMake 3.28 or later
- A C compiler (Xcode on macOS, Visual Studio 2022 on Windows, GCC/Clang on Linux)
- libshout 2.4.x
- OBS Studio 31.x headers (fetched automatically by CMake)

### macOS

```bash
git clone https://github.com/tech-noid-systems/obs-radio-output.git
cd obs-radio-output
cmake --preset macos
cmake --build --preset macos
```

### Windows

```powershell
git clone https://github.com/tech-noid-systems/obs-radio-output.git
cd obs-radio-output
cmake --preset windows-x64
cmake --build --preset windows-x64
```

### Linux

```bash
git clone https://github.com/tech-noid-systems/obs-radio-output.git
cd obs-radio-output
sudo apt install libshout3-dev
cmake --preset linux-x86_64
cmake --build --preset linux-x86_64
```

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
