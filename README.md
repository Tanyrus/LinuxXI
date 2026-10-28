# LinuxXI

An Ashita v4 plugin for Final Fantasy XI that fixes the crashes and freezes you get when you play on Linux (Wine or
Proton) and would not get on Windows. It works whether the game draws through DXVK, WineD3D or dgVoodoo.

## What it fixes

- **Running out of memory in busy zones.** Ashita copies every packet once for each addon that reads packets, and
  each copy waits for that addon's Lua to clean it up, which can take minutes. Under Wine that memory is not reused
  until every copy around it is gone, so a busy zone can push the game past its 4 GB limit and it closes. LinuxXI
  keeps the copies in their own space, reuses it as soon as a copy is freed and tells each addon how much each copy
  weighs, so its Lua cleans up on time.
- **Crashes on zone changes with DXVK.** DXVK can draw with a texture or buffer the game has already let go of, most
  often when you change zones with an addon such as XIUI on screen. LinuxXI keeps each one alive for three more
  frames, and for as long as it is still in use, as Windows does.
- **Freezes and crashes from addons that change the language settings.** Some addons (Balloon, tTimers) switch the
  game to your system's language settings, and under Wine that makes Ashita look up the same registry key over and
  over until the game closes, often while it starts. LinuxXI makes those lookups work as they do in English, and
  each addon keeps the settings it asked for.
- **Freezing on your first screenshot.** The game saves screenshots in a PlayOnline folder that only PlayOnline's
  own viewer makes, and when that folder is missing it waits for it forever. LinuxXI makes that folder for each of
  your characters when it loads.

It changes no addon's files. What it adds to an addon goes away when you unload LinuxXI or reload that addon.

## Installing

Put `LinuxXI.dll` in Ashita's `plugins` folder and load it with `/load LinuxXI` in your startup script, before any
`/addon load` line: an addon that loads first can freeze the game before LinuxXI is there to stop it.

A new build of LinuxXI only takes effect after you restart the game.

## Commands

- `/linuxxi`: shows what each fix is doing, how much memory the game has used and has left, and why a fix is not in
  use if one is not.
- `/linuxxi memory`: shows the game's memory and how much each addon's Lua is using, largest first.
- `/linuxxi pool off`, `/linuxxi grace off` and `/linuxxi locale off`: switch a fix off until you restart the game
  (`on` switches it back). All three are on by default.
- `/linuxxi dump`: writes the packet copies in use to `logs/LinuxXI-dump.txt`, for bug reports.

LinuxXI also sends its numbers to every plugin once a second, so a plugin such as HeadsUp can show them.

## Building

You need MinGW (`i686-w64-mingw32-g++`), Python 3 for the ABI check and `luajit` for the tests.

- `./build.sh test` runs the tests.
- `./build.sh plugin` also runs the ABI check and builds `build/LinuxXI.dll`, with its symbols in
  `build/LinuxXI.debug`.
- `./build.sh install` also copies it to the `plugins` folder `LINUXXI_PLUGINS_DIR` names.

## Credits

The Ashita SDK in `third_party/ashita-sdk` is the Ashita team's, under the LGPL-3.0 (its `LICENSE` and `SOURCE.md`
say which version and what was left out).
