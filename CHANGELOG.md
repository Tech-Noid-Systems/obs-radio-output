# Changelog

All notable changes to obs-radio-output are documented here.
Format follows [Conventional Commits](https://www.conventionalcommits.org/).

## [0.1.0-alpha1] - 2026-04-24

### Added

- **icecast:** Add radio_output state struct and constant definitions ([323e9eb](https://github.com/tech-noid-systems/obs-radio-output/commit/323e9eb6057f8bb2d4d2893ab4c722d5cccf83e8))
- **icecast:** Register radio_output output type with OBS ([c16d638](https://github.com/tech-noid-systems/obs-radio-output/commit/c16d638be83273eae049a494801fb6dfd0e9239b))
- **icecast:** Implement create and destroy callbacks ([104cfbe](https://github.com/tech-noid-systems/obs-radio-output/commit/104cfbe7798b3dfcf1f03d51ebe57391ff3f2f95))
- **icecast:** Implement settings update callback ([2d7d86b](https://github.com/tech-noid-systems/obs-radio-output/commit/2d7d86be0f8839a3e321dea27040ec466dc13010))
- **icecast:** Implement start callback with libshout connection and encoder setup ([3fc4406](https://github.com/tech-noid-systems/obs-radio-output/commit/3fc4406c5a293ea5157ffb77e5db0fe50addfa1c))
- **icecast:** Implement stop and encoded_packet callbacks ([2dfee3e](https://github.com/tech-noid-systems/obs-radio-output/commit/2dfee3e29da257eae6113aa5e8ca92b89354b6c9))
- **reconnect:** Implement auto-reconnect thread with configurable delay and retry limit ([4e752b0](https://github.com/tech-noid-systems/obs-radio-output/commit/4e752b0e902e572d1f8270f929afa08f8de5f549))
- **reconnect:** Integrate reconnect module into encoded_packet, stop, and destroy ([3fc3878](https://github.com/tech-noid-systems/obs-radio-output/commit/3fc387858214f4a72b64c5c277d9fb5d968d5f89))
- **icecast:** Convert reconnect delay setting from ms to seconds ([fcc6cb8](https://github.com/tech-noid-systems/obs-radio-output/commit/fcc6cb84d4e897cba00aa69651996f7752e5eb5e))
- **ui:** Add en-US locale strings for all Phase 1 settings labels ([4d24a3a](https://github.com/tech-noid-systems/obs-radio-output/commit/4d24a3aa66c894ffd175f94749c302667d3e9ab7))
- **mp3:** Implement raw audio pipeline with libmp3lame encoding ([957616b](https://github.com/tech-noid-systems/obs-radio-output/commit/957616b4086fb213acafa5b106b9199f981e9a54))
- **icecast:** Stream MP3 to Icecast via dedicated sender thread ([220d8a3](https://github.com/tech-noid-systems/obs-radio-output/commit/220d8a3b18a55d2012ccd46b4fb916be605d5581))
- **ui:** Add Tools menu config dialog for server, codec, and reconnect settings ([fb0359e](https://github.com/tech-noid-systems/obs-radio-output/commit/fb0359ecf0fcad1e491561fd608635ab1022418e))
- **ui:** Add persistent dock widget with Start/Stop and connection status ([aec066e](https://github.com/tech-noid-systems/obs-radio-output/commit/aec066edf7e4fb085e842fe711af69aa4905605c))
- **ui:** Tie radio start/stop to OBS streaming events via opt-in checkbox ([00b0875](https://github.com/tech-noid-systems/obs-radio-output/commit/00b087572c875ad3219203210afd4f065cc1e040))
- **protocol:** Add Icecast / SHOUTcast v1 selector with dynamic mount visibility ([1f35f1d](https://github.com/tech-noid-systems/obs-radio-output/commit/1f35f1dbc54c2dfb5a7be74c376598263c8cba5f))
- **encoder:** Add codec vtable + libopus/libogg Opus encoder (§C.2 + §C.3) ([d1c1ab0](https://github.com/tech-noid-systems/obs-radio-output/commit/d1c1ab0e90c76fb7240ff7cb410ccbc9cc3367e0))
- **tls:** Add TLS toggle and map to shout_set_tls with clean error paths ([1d422fe](https://github.com/tech-noid-systems/obs-radio-output/commit/1d422fe1115707ad68cbea663d56b3f90a934d3f))
- **tls:** Hint at TLS as cause when shout_open fails with use_tls on ([c091124](https://github.com/tech-noid-systems/obs-radio-output/commit/c091124bd36d2d1598671a05d1e342f79e794e12))
- **metadata:** Add "Now Playing" push via shout_set_metadata + dock input ([7a34f1b](https://github.com/tech-noid-systems/obs-radio-output/commit/7a34f1b4e69a32f17029a51c8bc5fb77f2d54183))

### Documentation

- Add initial README with project description, disclaimer, and AI disclosure ([c33b61e](https://github.com/tech-noid-systems/obs-radio-output/commit/c33b61ee8fca8d29231564a1a6a6b31c2f8e3e26))
- **reconnect:** Remove stale encoded_packet references in comments ([5d3e3c7](https://github.com/tech-noid-systems/obs-radio-output/commit/5d3e3c73e695929b61d6186ae40f579997fa2f3d))
- **readme:** Document contributor formatter prerequisites ([a00a6aa](https://github.com/tech-noid-systems/obs-radio-output/commit/a00a6aa33a9b7d37b53d8900f81a907fcc57639a))
- **scripts:** Note radio-test.lua is a dev aid; native UI is the supported path ([dc4442f](https://github.com/tech-noid-systems/obs-radio-output/commit/dc4442f3c79a2ddd9c777e3610002eb001fae428))
- Update CHANGELOG.md for 0.1.0-alpha1 ([4727692](https://github.com/tech-noid-systems/obs-radio-output/commit/4727692eb002c2261aea4902b8f0fe55a4827971))
- Update CHANGELOG.md for 0.1.0-alpha1 ([2a9c61e](https://github.com/tech-noid-systems/obs-radio-output/commit/2a9c61e0e565b68a6921fc5154b4f06f15261c2c))

### Fixed

- Resolve macOS libogg dependency and Windows library suffix for libshout ([5a4a5b1](https://github.com/tech-noid-systems/obs-radio-output/commit/5a4a5b13ca6f1d810a610838be37e8dff18e6245))
- Use libshout/libogg tarballs and disable optional deps for macOS Universal build ([ecf1b03](https://github.com/tech-noid-systems/obs-radio-output/commit/ecf1b037ae4b5721ae843acc0169f273fee625c8))
- Make LibShout optional on Windows pending MSVC-compatible binaries ([aa9d347](https://github.com/tech-noid-systems/obs-radio-output/commit/aa9d3476199634928257548b7f043cd518bfcc35))
- Suppress implicit-function-declaration error for libshout 2.4.6 on Apple Clang 16 ([22b7d09](https://github.com/tech-noid-systems/obs-radio-output/commit/22b7d0917bf189fa4ddcbf57716006d8f77eaeb4))
- Exclude merge commits from Conventional Commits lint check ([a259585](https://github.com/tech-noid-systems/obs-radio-output/commit/a259585f33111a8257917272beecc6cf321a1f7f))
- Remove unsupported --no-lock flag from brew bundle in setup script ([de36afe](https://github.com/tech-noid-systems/obs-radio-output/commit/de36afe1d5b05ea6a1dcb755d74e59896919425a))
- **scripts:** Correct plugin bundle path in build-and-install-macos.sh generator ([c17773c](https://github.com/tech-noid-systems/obs-radio-output/commit/c17773c5d948f2a918da4a890d95631c664c61a3))
- **style:** Apply clang-format to radio-output.h ([0ec3110](https://github.com/tech-noid-systems/obs-radio-output/commit/0ec3110ff189112e1bbf3c47933b43a1901e81f1))
- **icecast:** Add missing extern declaration for radio_output_info ([e03cca6](https://github.com/tech-noid-systems/obs-radio-output/commit/e03cca693ff05ae60bb8f147cd41335996fad6a6))
- **icecast:** Replace deprecated shout_set_format with shout_set_content_format ([6c72942](https://github.com/tech-noid-systems/obs-radio-output/commit/6c72942a704a445ac74af46f34ea6b15b7ab59b4))
- **ci:** Use build-plugin action to resolve OBS SDK dependency in clang-tidy workflow ([791ce75](https://github.com/tech-noid-systems/obs-radio-output/commit/791ce7596969b53ec52b83897a318a48dafb238c))
- **ci:** Add .clang-tidy config and disable noisy checks ([071b94a](https://github.com/tech-noid-systems/obs-radio-output/commit/071b94a27cd235efbd8b53dc604286bfc4818c2d))
- **ci:** Disable redundant-casting check and lower identifier-length minimum to 2 ([c1383cb](https://github.com/tech-noid-systems/obs-radio-output/commit/c1383cb0688585dc087d6ab067a32bd3eea77d95))
- **scripts:** Add Xcode generator flag for macOS cmake configure ([c05ae67](https://github.com/tech-noid-systems/obs-radio-output/commit/c05ae6708cea53fcc9d739ae48dec46d76225f85))
- **scripts:** Use separate build-tidy directory to avoid generator conflict ([4e6657a](https://github.com/tech-noid-systems/obs-radio-output/commit/4e6657aa53e3c42b44b5582bd1fac0b7c390699c))
- **scripts:** Pass codesign args directly to xcodebuild instead of cmake ([1317c32](https://github.com/tech-noid-systems/obs-radio-output/commit/1317c32c03a2a40ec3729fdaf2b03f4dcd6792fa))
- **scripts:** Document macOS limitation, rely on CI or Docker for clang-tidy ([608a4b3](https://github.com/tech-noid-systems/obs-radio-output/commit/608a4b3d3b8bb300496e4ede6992e966481f536c))
- Check for pre-attached encoder before creating one in start ([9fa9a48](https://github.com/tech-noid-systems/obs-radio-output/commit/9fa9a48c98e5a2aca27024e4872bbc305eda3fbe))
- **icecast:** Handle warn_unused_result on shout_send flush in stop ([ee0b126](https://github.com/tech-noid-systems/obs-radio-output/commit/ee0b126fefb2b1ffac67815b575f5313cd7bc46d))
- **reconnect:** Apply SHOUT_USAGE_AUDIO and audio_info on reconnect ([305b256](https://github.com/tech-noid-systems/obs-radio-output/commit/305b256131d97ddd7d8e807209ece150d96c73c4))
- **stop:** Make radio_output_stop idempotent to dedupe disconnect log ([30513a4](https://github.com/tech-noid-systems/obs-radio-output/commit/30513a4f0bbe75381523a9cd266367da149535fd))
- **send:** Defer shout_close to detached thread to avoid TCP timeout hang ([549e89e](https://github.com/tech-noid-systems/obs-radio-output/commit/549e89e10943b5f77e054d9a26c16409f37eef05))
- **stop:** Call end_data_capture first to prevent audio callback UAF ([08f9c67](https://github.com/tech-noid-systems/obs-radio-output/commit/08f9c679f14a1bf07636dc885b43c5567364d19e))
- **destroy:** Share teardown helper so OBS_OUTPUT_ERROR path logs and frees correctly ([4e23588](https://github.com/tech-noid-systems/obs-radio-output/commit/4e23588ca97c3c39c520e40a223732dd3d55bb0d))
- **lint:** Address clang-tidy diagnostics in Phase 2 UI sources ([abc1559](https://github.com/tech-noid-systems/obs-radio-output/commit/abc1559d4973e2fa506878d07894493420485361))
- **ui:** Quote multi-word locale values and widen spin boxes + dialog ([0f41104](https://github.com/tech-noid-systems/obs-radio-output/commit/0f41104dbcfea35d4ab349a56df7962976c3c67d))
- **build:** Wrap radio-output.h in extern "C" for C++ callers ([7cfc866](https://github.com/tech-noid-systems/obs-radio-output/commit/7cfc866cd7166860a8f9a051484645d46c2d4744))
- **ui:** Dock Stop button now stops Lua-created outputs via back-pointer ([52c554c](https://github.com/tech-noid-systems/obs-radio-output/commit/52c554c182ca6d42a9af69daf3e370f334f89811))
- **build:** Include QFormLayout in dialog header for server_form_ member ([d29abc3](https://github.com/tech-noid-systems/obs-radio-output/commit/d29abc3303e738d5cf6305256a9323d0177958e2))
- **opus:** Re-emit OpusHead+OpusTags on reconnect so listeners can decode ([8356248](https://github.com/tech-noid-systems/obs-radio-output/commit/8356248782a4daf735a88859010cdce225f2f302))
- **metadata:** Use shout_set_metadata_utf8 and check shout_metadata_add return ([ddcf278](https://github.com/tech-noid-systems/obs-radio-output/commit/ddcf2780a68ed19dc07e31ed1ac1578197563ea0))
- **metadata:** Drop QtConcurrent — OBS's bundled Qt omits it ([036b59a](https://github.com/tech-noid-systems/obs-radio-output/commit/036b59a1d708fa40ff31013573eae307d9b9ec0d))


