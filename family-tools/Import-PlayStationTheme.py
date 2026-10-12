"""Import an existing local PlayStation-X installation without redistributing its music."""
import argparse
import json
import runpy
import shutil
import uuid
from pathlib import Path
import xml.etree.ElementTree as ET


THEME = "es-theme-PlayStation-Family"


def write_xml(path, root):
    root.insert(0, ET.Comment("Based on PlayStation-X by pajarorrojo: https://github.com/pajarorrojo/es-theme-PlayStation-X"))
    ET.indent(root)
    ET.ElementTree(root).write(path, encoding="utf-8", xml_declaration=True)


def customize(theme):
    main = ET.parse(theme / "theme.xml").getroot()
    colors = main.find("subset[@name='colorset']")
    names = main.find("subset[@name='system-username']")
    if colors is None or names is None:
        raise ValueError("Expected PlayStation-X color and username options are missing")
    ET.SubElement(colors, "include", name="pink", displayName="Pink").text = "./_theme_options/colorsets/pink.xml"
    ET.SubElement(names, "include", name="family", displayName="Signed-in profile")
    for sound in main.findall(".//sound[@name='directory']"):
        # Music belongs to the profile, never the shared theme directory.
        sound.find("path").text = "./_theme_music/"
    ET.SubElement(main, "include").text = "./family-palette.xml"
    write_xml(theme / "theme.xml", main)

    usernames = ET.parse(theme / "_theme_options/usernames.xml").getroot()
    ET.SubElement(usernames.find("variables"), "theme.nick", ifSubset="system-username:family").text = "${family.profile.name}"
    write_xml(theme / "_theme_options/usernames.xml", usernames)

    info_path = theme / "_theme_views/top-info.xml"
    info = ET.parse(info_path).getroot()
    for label in info.findall(".//text[@name='systemInfoEx2']/text"):
        label.text = "Playtime: {binding:gameTime}"
    write_xml(info_path, info)

    black_path = theme / "_theme_options/colorsets/black.xml"
    black = ET.parse(black_path).getroot()
    for tag in ("gamelistSelectedColor", "genreColor", "manualOnColor", "savegameOnColor", "starFill", "gamelistSelectorColor", "sistema.lineainferior"):
        black.find("variables/" + tag).text = "E6E6EB"
    write_xml(black_path, black)
    pink = ET.parse(black_path).getroot()
    replacements = {"menubgColor": "321527", "menuFontcolor": "FFF1F8", "menuTitleColor": "FFF1F8",
                    "menuSelectorColor": "86395F", "menuSelectorColorEnd": "A34676", "menuGrouptitle": "FFD0E8",
                    "menuGroupSeparator": "FFD0E8", "gamelistSelectedColor": "FFD0E8", "helpFontColor": "FFF1F8",
                    "helpIconColor": "FFF1F8", "sistema.pie": "240F20ED", "menuFooter": "E5C7D8"}
    for tag, value in replacements.items():
        pink.find("variables/" + tag).text = value
    write_xml(theme / "_theme_options/colorsets/pink.xml", pink)

    animation_path = theme / "_theme_options/animated-background.xml"
    animation = ET.parse(animation_path).getroot()
    video = ET.SubElement(animation.find("view"), "video", name="default_background", extra="true", ifSubset="colorset:pink")
    ET.SubElement(video, "path").text = "./colorsets/black/background-psx-black.mp4"
    ET.SubElement(video, "color").text = "FFB9DA"
    write_xml(animation_path, animation)

    list_path = theme / "_theme_options/animated-list.xml"
    animated_list = ET.parse(list_path).getroot()
    # Basic lists share the offscreen starting position and need the same entrance.
    for view in animated_list.findall("view[@name='detailed']"):
        view.set("name", "basic, detailed")
    write_xml(list_path, animated_list)

    overlay = ET.Element("theme")
    ET.SubElement(overlay, "formatVersion").text = "7"
    # Neutral PlayStation waves accept a palette tint without changing console artwork.
    for palette, tint in (("pink", "FFB9DA"), ("black", "FFFFFF")):
        view = ET.SubElement(overlay, "view", name="system, basic, detailed, grid", ifSubset="colorset:" + palette)
        for name in ("background", "background-art"):
            image = ET.SubElement(view, "image", name=name, extra="true")
            ET.SubElement(image, "saturation").text = "0"
            ET.SubElement(image, "color").text = tint
        system = ET.SubElement(overlay, "view", name="system", ifSubset="colorset:" + palette)
        logo = ET.SubElement(system, "image", name="logo")
        ET.SubElement(logo, "saturation").text = "0"
        ET.SubElement(logo, "color").text = tint
        frame = ET.SubElement(system, "image", name="marco-activo", extra="static")
        ET.SubElement(frame, "color").text = tint
    write_xml(theme / "family-palette.xml", overlay)
    runpy.run_path(str(Path(__file__).with_name("Refresh-FamilyTheme.py")))["refresh"](theme)


def migrate(old, new, blue, pink, black):
    old, new = Path(old).resolve(), Path(new).resolve()
    source = old / "emulationstation/.emulationstation/themes/es-theme-PlayStation-X"
    family = new / "saves/family"
    registry = json.loads((family / "profiles.json").read_text(encoding="utf-8-sig"))
    known = {p["ClientId"] for p in registry}
    ids = (blue, pink, black)
    if len(set(ids)) != 3 or any(p not in known or any(c in p for c in '/\\:') or p in (".", "..") for p in ids):
        raise ValueError("Choose three distinct existing profile IDs")
    if old == new or not (source / "theme.xml").is_file():
        raise ValueError("A separate existing PlayStation-X installation is required")
    target = new / "emulationstation/.emulationstation/themes" / THEME
    if target.exists():
        raise ValueError("Family theme already exists; refusing to overwrite customizations")
    preferences = {}
    for profile in ids:
        path = family / profile / "preferences.json"
        preferences[profile] = json.loads(path.read_text(encoding="utf-8-sig")) if path.exists() else {}
        if not isinstance(preferences[profile], dict):
            raise ValueError("Invalid preferences for " + profile)
    music = family / blue / "music/PlayStation-X"
    tracks = [p for p in (source / "_theme_music").iterdir() if p.is_file()]
    if music.exists():
        raise ValueError("Music already exists; refusing to overwrite it")
    backup = new / "family-backups" / ("playstation-" + uuid.uuid4().hex)
    backup.mkdir(parents=True)
    staged = backup / "theme-stage"
    shutil.copytree(source, staged, ignore=shutil.ignore_patterns("_theme_music"))
    (staged / "_theme_music").mkdir()
    customize(staged)
    for xml in staged.rglob("*.xml"):
        ET.parse(xml)
    staged_music = backup / "music-stage"
    staged_music.mkdir()
    for track in tracks:
        shutil.copy2(track, staged_music / track.name)
    old_settings = old / "emulationstation/.emulationstation/es_settings.cfg"
    old_theme_prefs = {}
    if old_settings.exists():
        for node in ET.parse(old_settings).getroot():
            key = node.get("name", "")
            if key.startswith("subset.") or key in ("ThemeIconSet", "ThemeSystemView", "ThemeGamelistView"):
                old_theme_prefs[key] = {"type": node.tag, "value": node.get("value", "")}
    for profile, color in zip(ids, ("blue", "pink", "black")):
        path = family / profile / "preferences.json"
        prefs = preferences[profile]
        if path.exists():
            shutil.copy2(path, backup / (profile + "-preferences.json"))
        if profile == blue:
            prefs.update(old_theme_prefs)
        for key, value in {"ThemeSet": THEME, "ThemeColorSet": color, "GamelistViewStyle": "detailed", "subset.secondary_colorset": "default",
                           "subset.system-username": "family", "subset.system-avatar": "ps-logo",
                           "subset.video-system": "small-screen",
                           "subset.anim-list": "YES", "subset.anim-system": "YES"}.items():
            prefs[key] = {"type": "string", "value": value}
        if profile == blue:
            prefs["audio.bgmusic"] = {"type": "bool", "value": "true"}
            prefs["audio.useFavoriteMusic"] = {"type": "bool", "value": "false"}
            prefs["audio.persystem"] = {"type": "bool", "value": "false"}
        temp = backup / (profile + "-new.json")
        temp.write_text(json.dumps(prefs, ensure_ascii=False, indent=2), encoding="utf-8")
    changed = []
    installed = []
    try:
        target.parent.mkdir(parents=True, exist_ok=True)
        staged.rename(target)
        installed.append(target)
        music.parent.mkdir(parents=True, exist_ok=True)
        staged_music.rename(music)
        installed.append(music)
        for profile in ids:
            path = family / profile / "preferences.json"
            path.parent.mkdir(parents=True, exist_ok=True)
            (backup / (profile + "-new.json")).replace(path)
            changed.append((profile, path))
    except Exception:
        for profile, path in reversed(changed):
            original = backup / (profile + "-preferences.json")
            if original.exists():
                shutil.copy2(original, path)
            else:
                path.unlink()
        for path in reversed(installed):
            shutil.rmtree(path)
        raise
    return backup


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("old", "new", "blue", "pink", "black"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    print(migrate(args.old, args.new, args.blue, args.pink, args.black))
