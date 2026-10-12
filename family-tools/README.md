# Family RetroBat extension

This personal fork is based on RetroBat 8.3 commit `45f37fd74819ee07118ca2e7680a907b25733ac5`. It adds password-free profile selection, owner-colored save entries, selected-owner save routing and exclusive owner locks. Profile names and colors live in the shared `saves/family/profiles.json` registry. Launch through `Start-Family-RetroBat.cmd` to enable family mode; ordinary EmulationStation launches retain the upstream behavior.

On Windows with Visual Studio 2022 C++ tools, Windows SDK and CMake 3.31 installed, first build the matching [personal EmulatorLauncher fork](https://github.com/O-Marmullaku/emulatorlauncher) with its `tests/Build-ThumbnailLauncher.ps1`. Its small watcher fix keeps incremental save thumbnails beside the correct save. Then run from this repository:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File family-tools/Build-FamilyFrontend.ps1 -CMakePath C:/path/to/cmake.exe -LauncherBuildRoot C:/path/to/emulatorlauncher/emulatorLauncher/bin/x64/Release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File family-tools/family/Test-FamilyScripts.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File family-tools/Test-FamilyFrontendDeployment.ps1
```

The build runs the native profile tests and packages the frontend, matching launcher/Common assembly pair, profile picker and startup scripts into `family-tools/family-frontend.zip` with a SHA-256 sidecar. The upstream CMake configuration downloads its Windows dependencies; the verified dependency revision was `468eaba48c028921a4bf2abdfa3f3a00ce8d4c0d`. Install only over a matching RetroBat 8.3 runtime, with its processes closed:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File family-tools/Install-FamilyFrontend.ps1 -PackagePath family-tools/family-frontend.zip -RetroBatRoot B:/GAMES_permanent/MiddleMan/RetroBat
```

The installer prints its backup directory. Pass it as `-RestoreBackup` to restore program files; shared profile data is preserved. A client has a local executable/configuration copy and maps the host library and saves. `family/Setup-FamilyClient.ps1` prepares that copy. The Windows SMB account is separate from the password-free family profile.

Native tests cover registry validation and lock behavior. Runtime verification has covered God of War II through the family startup, selecting another owner's state, continuing that owner's saves and returning to the frontend. Other emulator save behavior remains dependent on the RetroBat generator honoring its save-directory setting; a physical second PC requires separate verification.
