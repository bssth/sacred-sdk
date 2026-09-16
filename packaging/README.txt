=======================================================================
  SacredSDK
=======================================================================

  A modding toolkit for Sacred Gold (Ascaron 2004 / Encore, Steam and
  GOG editions). Verified on build 2.0.2.28 (2006-10-13).

  Mods are written in Lua. The game's own files are never modified.


--- Install ----------------------------------------------------------

  1. Copy everything from this archive into your Sacred Gold folder --
     the one with Sacred.exe. Typical path:

       C:\Program Files (x86)\Steam\steamapps\common\Sacred Gold\

  2. Double-click install.cmd.

     It renames the game's own ijl15.dll to ijl15_real.dll and puts the
     SDK proxy in its place. That is the whole installation: Sacred
     loads ijl15.dll at startup, so the SDK comes up with it.

     Steam's "Verify integrity of game files" puts the original
     ijl15.dll back, and the SDK no longer loads. Run install.cmd again
     after a verification.

  3. Start the game. The first launch bakes the Lua that ships with the
     SDK (a second or two) and writes sdk\logs\sdk_loaded.log.

  To remove it again: uninstall.cmd. The original DLL goes back and the
  install is vanilla.


--- What is where ----------------------------------------------------

  install.cmd            puts the SDK in place (and back out)
  uninstall.cmd
  sdk\ijl15.dll          the SDK itself, copied in by install.cmd
  sdk\custom\lua\lib\    the framework: quests, NPCs, dialog, zones...
  sdk\custom\lua\examples\  copy-paste starter mods
  sdk\docs\              the full documentation, page by page
  MODDING_GUIDE.md       start here
  MODDING_COOKBOOK.md    "how do I ..." recipes
  sdk.ini.example        optional: borderless window, HD resolution.
                         Rename to sdk.ini to activate.

  custom\                YOUR mods -- create it yourself, next to
                         Sacred.exe. Anything you put at
                         custom\lua\<same path> wins over the copy in
                         sdk\custom\lua\, so you can replace any part of
                         the framework without editing it.


--- Your first mod ---------------------------------------------------

  1. Make the folder  custom\lua\mods\

  2. Save a recipe from MODDING_COOKBOOK.md in it, e.g. as
     custom\lua\mods\kolb.lua, and edit it.

     Keep mods like these, which end in "return {}", out of
     custom\lua\bin\. A file there bakes to the same path under
     custom\bin\ and the game reads it in place of its own script:
     saved as custom\lua\bin\TYPE_NPC_GLADIATOR\FunkCode.lua, a recipe
     would give that class an empty FunkCode.bin.

  3. Restart Sacred. The bake runs at startup; the log line
     "[lua_bake] baked ..." says it worked.

  MODDING_GUIDE.md explains the model, MODDING_COOKBOOK.md has ready
  recipes, and sdk\docs\ is the reference.


--- Troubleshooting --------------------------------------------------

  The game does not start
      Look at sdk\logs\sdk_loaded.log -- the SDK writes what it did and
      the failure is usually on the last line. If the file is not there
      at all, the proxy was not loaded: run install.cmd again.

  My mod did nothing
      1. Is there a "[lua_bake] baked" line for your file in the log?
      2. Did the baked output appear under custom\ ? (A mod in
         custom\lua\mods\ gives an empty custom\mods\<name>.bin.)
      3. The bake runs once per launch -- restart the game after edits.

  I want the game back the way it was
      uninstall.cmd, then delete custom\ and sdk\.


--- License ----------------------------------------------------------

  MIT -- see LICENSE.

  This is fan modding work. No Ascaron / Encore code or content is
  included in this archive: the SDK proxy is our own, and the game's
  Intel JPEG library stays yours (install.cmd only renames it).
