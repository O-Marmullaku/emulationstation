# Family console screen

`Import-PlayStationTheme.py` applies `Refresh-FamilyTheme.py` after importing the locally owned PlayStation-X theme. Run the latter against an existing installed theme to update only the shared console screen. Keep the two scripts together. Profile preferences, save ownership, input mappings and music are not changed.

The screen uses the existing console SVG marks inside rounded gradient tiles, with framed info panels and subtle background waves. Blue, pink and black palette tokens tint the same vector surfaces, without profile-name tests. Original artwork and credits remain in the installation. The oversized system backdrop and advertising videos are hidden on the console screen. Game-list views are unchanged.

The lower screen contains a console hardware image, a short information panel and a stats panel. PS2 has curated copy; other systems show their existing manufacturer, release year, library count and scoped playtime. The old paragraph is hidden. Playtime still follows Personal / Everyone.

## Hardware footage

The local theme only contains advertisements, not multi-angle console footage. Hardware PNGs are an honest fallback. To add actual hardware clips, place a UTF-8 `<system>.m3u` in `family-hardware/` inside the theme, referencing local clips relative to the playlist, then rerun the refresh script. Native `M3uPlaylist` shuffles the clips. No game-video randomizer or fake camera rotation is used.

## PS2 copy sources

- Sony's console history: https://www.playstation.com/en-hr/playstation-history/2000-ps2-psp/ — first released in Japan on 4 March 2000, North America on 26 October 2000, Europe on 24 November 2000; more than 160 million systems; Final Fantasy XI servers through 2016.
- Historical best-seller figures: https://en.wikipedia.org/wiki/List_of_best-selling_PlayStation_2_video_games — approximately 17.33M San Andreas, 14.89M Gran Turismo 3, 14.2M Vice City. These have different reporting dates; the UI retains approximation marks but omits the historical-sales footer at the owner's request. Later Guinness/VGChartz estimates differ.

No revenue, profit or manufacturing-cost figures are asserted. Those need defensible definitions and sources before inclusion.

## Verification

`python -m unittest discover -s family-tools -p 'test_*theme.py'`

Refresh tests cover repeatable installation, one final include, all three palette tokens, distinct element names, correct library-count binding, removal of old commercial/background layers, and normalized vector marks. A live frontend check is still needed after installation; static tests do not establish rendering.
