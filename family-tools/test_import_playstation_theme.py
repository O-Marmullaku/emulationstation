"""Focused transaction tests for Import-PlayStationTheme.py."""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).with_name("Import-PlayStationTheme.py")
SPEC = importlib.util.spec_from_file_location("import_playstation_theme", SCRIPT)
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)


class ImportThemeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.old, self.new = root / "old", root / "new"
        self.source = self.old / "emulationstation/.emulationstation/themes/es-theme-PlayStation-X"
        self.family = self.new / "saves/family"
        self.source.mkdir(parents=True)
        self.family.mkdir(parents=True)
        self.ids = ("blue-id", "pink-id", "black-id")
        (self.family / "profiles.json").write_text(json.dumps([{"ClientId": p} for p in self.ids]))
        self.original = {}
        for profile in self.ids:
            prefs = {"unrelated": {"type": "string", "value": profile}}
            self.original[profile] = prefs
            folder = self.family / profile
            folder.mkdir()
            (folder / "preferences.json").write_text(json.dumps(prefs))
        theme = self.source
        (theme / "_theme_options/colorsets").mkdir(parents=True)
        (theme / "_theme_music").mkdir()
        (theme / "theme.xml").write_text("""<theme><subset name="colorset"/><subset name="system-username"/><view><sound name="directory"><path>old</path></sound></view></theme>""")
        (theme / "_theme_options/usernames.xml").write_text('<theme><variables/></theme>')
        (theme / "_theme_views").mkdir()
        (theme / "_theme_views/top-info.xml").write_text('<theme><text name="systemInfoEx2"><text>System info</text></text><text name="other"><text>Leave me</text></text></theme>')
        (theme / "_theme_options/colorsets/black.xml").write_text('<theme><variables>' + ''.join(f'<{n}>000000</{n}>' for n in ("gamelistSelectedColor genreColor manualOnColor savegameOnColor starFill gamelistSelectorColor sistema.lineainferior menubgColor menuFontcolor menuTitleColor menuSelectorColor menuSelectorColorEnd menuGrouptitle menuGroupSeparator helpFontColor helpIconColor sistema.pie menuFooter").split()) + '</variables></theme>')
        (theme / "_theme_options/animated-background.xml").write_text('<theme><view/></theme>')
        (theme / "_theme_options/animated-list.xml").write_text('<theme><view name="detailed"><textlist name="gamelist"/></view></theme>')
        (theme / "_theme_music/track.ogg").write_bytes(b"music")
        settings = self.old / "emulationstation/.emulationstation/es_settings.cfg"
        settings.parent.mkdir(parents=True, exist_ok=True)
        settings.write_text('<config><string name="subset.colorset" value="classic"/><bool name="ThemeIconSet" value="custom"/><string name="unrelated" value="keep-out"/></config>')

    def tearDown(self):
        self.tmp.cleanup()

    def migrate(self):
        return IMPORTER.migrate(self.old, self.new, *self.ids)

    def prefs(self, profile):
        return json.loads((self.family / profile / "preferences.json").read_text())

    def test_import_sets_profiles_preserves_unrelated_and_backs_up(self):
        backup = self.migrate()
        blue, pink, black = (self.prefs(p) for p in self.ids)
        self.assertEqual(blue["ThemeSet"]["value"], IMPORTER.THEME)
        self.assertEqual(blue["GamelistViewStyle"]["value"], "detailed")
        self.assertEqual([p["ThemeColorSet"]["value"] for p in (blue, pink, black)], ["blue", "pink", "black"])
        for prefs in (blue, pink, black):
            self.assertEqual(prefs["subset.system-username"]["value"], "family")
            self.assertEqual(prefs["subset.anim-list"]["value"], "YES")
            self.assertEqual(prefs["unrelated"]["value"], self.ids[(blue, pink, black).index(prefs)])
            self.assertNotIn("unrelated", prefs.get("subset", {}))
        self.assertEqual(blue["subset.colorset"]["value"], "classic")
        self.assertEqual(blue["ThemeIconSet"]["value"], "custom")
        self.assertEqual(blue["audio.bgmusic"]["value"], "true")
        self.assertNotIn("audio.bgmusic", pink)
        self.assertNotIn("audio.bgmusic", black)
        theme = self.new / "emulationstation/.emulationstation/themes" / IMPORTER.THEME
        self.assertIn('name="basic, detailed"', (theme / '_theme_options/animated-list.xml').read_text())
        info = IMPORTER.ET.parse(theme / "_theme_views/top-info.xml").getroot()
        self.assertEqual(info.find(".//text[@name='systemInfoEx2']/text").text, "Playtime: {binding:gameTime}")
        self.assertEqual(info.find(".//text[@name='other']/text").text, "Leave me")
        self.assertTrue((theme / "_theme_music").is_dir())
        self.assertEqual(list((theme / "_theme_music").iterdir()), [])
        self.assertEqual((self.family / self.ids[0] / "music/PlayStation-X/track.ogg").read_bytes(), b"music")
        self.assertTrue((backup / "blue-id-preferences.json").is_file())
        self.assertTrue((backup / "pink-id-preferences.json").is_file())
        self.assertTrue((backup / "black-id-preferences.json").is_file())
        for p in self.ids:
            self.assertEqual(json.loads((backup / (p + "-preferences.json")).read_text()), self.original[p])

    def test_rejects_existing_theme_or_music_without_modifying_preferences(self):
        (self.new / "emulationstation/.emulationstation/themes" / IMPORTER.THEME).mkdir(parents=True)
        before = [(self.family / p / "preferences.json").read_bytes() for p in self.ids]
        with self.assertRaises(ValueError):
            self.migrate()
        self.assertEqual(before, [(self.family / p / "preferences.json").read_bytes() for p in self.ids])
        (self.new / "emulationstation/.emulationstation/themes" / IMPORTER.THEME).rmdir()
        music = self.family / self.ids[0] / "music/PlayStation-X"
        music.mkdir(parents=True)
        with self.assertRaises(ValueError):
            self.migrate()
        self.assertEqual(before, [(self.family / p / "preferences.json").read_bytes() for p in self.ids])

    def test_rejects_bad_profile_and_corrupt_preferences_without_changes(self):
        before = [(self.family / p / "preferences.json").read_bytes() for p in self.ids]
        with self.assertRaises(ValueError):
            IMPORTER.migrate(self.old, self.new, self.ids[0], self.ids[0], self.ids[2])
        self.assertEqual(before, [(self.family / p / "preferences.json").read_bytes() for p in self.ids])
        path = self.family / self.ids[1] / "preferences.json"
        path.write_text("{")
        with self.assertRaises(json.JSONDecodeError):
            self.migrate()
        self.assertEqual("{", path.read_text())
        self.assertFalse((self.new / "emulationstation/.emulationstation/themes" / IMPORTER.THEME).exists())

    def test_commit_failure_rolls_back_installed_theme_music_and_preferences(self):
        before = [(self.family / p / "preferences.json").read_bytes() for p in self.ids]
        original_replace = Path.replace

        def fail_second_preferences(source, target):
            if source.name == "pink-id-new.json":
                raise OSError("injected commit failure")
            return original_replace(source, target)

        with mock.patch.object(Path, "replace", fail_second_preferences):
            with self.assertRaisesRegex(OSError, "injected"):
                self.migrate()
        self.assertEqual(before, [(self.family / p / "preferences.json").read_bytes() for p in self.ids])
        self.assertFalse((self.new / "emulationstation/.emulationstation/themes" / IMPORTER.THEME).exists())
        self.assertFalse((self.family / self.ids[0] / "music/PlayStation-X").exists())


if __name__ == "__main__":
    unittest.main()
