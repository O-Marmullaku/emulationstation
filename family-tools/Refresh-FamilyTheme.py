"""Refresh the shared console screen using local PlayStation-X assets, without changing profiles."""
import argparse
import copy
from pathlib import Path
import re
import xml.etree.ElementTree as ET


PALETTES = {'blue': ('101A2B', '192C46', '80BEFF'),
            'pink': ('21151E', '382131', 'F2B5D4'),
            'black': ('141518', '25272B', 'D2D6DD')}


def write(path, root):
    ET.indent(root)
    ET.ElementTree(root).write(path, encoding='utf-8', xml_declaration=True)


def props(parent, tag, name, **values):
    node = ET.SubElement(parent, tag, name=name, extra='true')
    for key, value in values.items():
        ET.SubElement(node, key).text = str(value)
    return node


def tile(source, destination):
    """Keep the original vector logo, normalize its framing and monochrome ink."""
    logo = ET.parse(source).getroot()
    bounds = [float(x) for x in re.split(r'[ ,]+', logo.get('viewBox', '0 0 400 200').strip())]
    x, y, width, height = bounds
    scale = min(300 / width, 170 / height)
    svg = ET.Element('svg', xmlns='http://www.w3.org/2000/svg', width='400', height='400', viewBox='0 0 400 400')
    ET.SubElement(svg, 'rect', width='400', height='400', fill='#24272C')
    group = ET.SubElement(svg, 'g', transform=f'translate({200-width*scale/2} {200-height*scale/2}) scale({scale}) translate({-x} {-y})')
    for child in logo:
        group.append(copy.deepcopy(child))
    for node in group.iter():
        node.tag = node.tag.split('}')[-1]
        if node.tag in ('path', 'polygon', 'polyline', 'rect', 'circle', 'ellipse', 'g'):
            node.attrib.pop('style', None)
            if node.get('fill') != 'none': node.set('fill', '#FFFFFF')
            if node.get('stroke') not in (None, 'none'): node.set('stroke', '#FFFFFF')
    write(destination, svg)


def refresh(theme):
    theme = Path(theme)
    main_path = theme / 'theme.xml'
    main = ET.parse(main_path).getroot()
    tiles = theme / 'family-tiles'
    tiles.mkdir(exist_ok=True)
    logos = theme / '_theme_inc/images/logos'
    # Prefer the theme's white marks, otherwise normalize its regular vector.
    for source in sorted(logos.glob('*.svg')):
        if source.stem.endswith('-w'): continue
        white = source.with_name(source.stem + '-w.svg')
        tile(white if white.exists() else source, tiles / source.name)

    root = ET.Element('theme')
    ET.SubElement(root, 'formatVersion').text = '7'
    variables = ET.SubElement(root, 'variables')
    for palette, (bg, panel, accent) in PALETTES.items():
        for key, value in [('family.bg',bg),('family.panel',panel),('family.accent',accent)]:
            ET.SubElement(variables,key,ifSubset='colorset:'+palette).text=value
    view = ET.SubElement(root,'view',name='system')
    for name in ('background-art','overlay','consoles','logo2','border-video'):
        props(view,'image',name,visible='false')
    for name in ('system_video','system_video_bg','default_background'):
        props(view,'video',name,visible='false')
    props(view,'text','system_description',visible='false')
    props(view,'image','background',path='./_theme_inc/images/pixel-blanco.png',color='${family.bg}',size='1 1',pos='0 0',zIndex='1')
    props(view,'image','logo',path='./family-tiles/${system.theme}.svg',color='${family.accent}',saturation='1')
    props(view,'image','marco-activo',color='${family.accent}')
    props(view,'text','system_name',fontSize='0.052')
    # Shared geometry; only palette tokens and per-console copy vary.
    for name, x, width in [('story',.335,.300),('stats',.653,.327)]:
        props(view,'image','family-'+name+'-card',path='./_theme_inc/images/pixel-blanco.png',pos=f'{x} 0.56',size=f'{width} 0.345',color='${family.panel}',zIndex='70')
    props(view,'image','family-hardware',path='./_theme_inc/images/consoles/${system.theme}.png',pos='0.025 0.57',maxSize='0.285 0.30',origin='0 0',zIndex='75')
    def text(name, content, pos, size, font=.025, color='EDF0F5'):
        props(view,'text',name,text=content,pos=pos,size=size,fontPath='./_theme_inc/fonts/SST Roman.ttf',fontSize=font,color=color,alignment='left',verticalAlignment='top',zIndex='80',lineSpacing='1.15')
    text('family-story-title','${system.fullName}','0.352 0.579','0.26 0.04',.027,'${family.accent}')
    text('family-story','${system.manufacturer}\nFirst released: ${system.releaseYear}','0.352 0.63','0.26 0.11')
    text('family-story-note','Choose a game to continue your library.','0.352 0.775','0.26 0.105',.022)
    text('family-stats-title','Your library','0.670 0.579','0.29 0.04',.027,'${family.accent}')
    text('family-stats','Games: {binding:total}\nPlaytime: {binding:gameTime}','0.670 0.635','0.29 0.10')
    text('family-stats-note','Playtime follows your Personal / Everyone display choice.','0.670 0.78','0.29 0.09',.021)

    # PS2 facts are separated from shared layout and palette; no profile names.
    ps2=ET.SubElement(root,'view',name='system',attrib={'if':"{system.theme} == 'ps2'"})
    for name,content in {
        'family-story-title':'PlayStation 2 · 2000',
        'family-story':'DVD movies, PS1 classics, and games from God of War to Gran Turismo.',
        'family-story-note':'Final Fantasy XI kept its PS2 servers online until 2016.',
        'family-stats-title':'160M+ consoles sold',
        'family-stats':'Best sellers · PS2 copies\n1  GTA: San Andreas          ~17.3M\n2  Gran Turismo 3             ~14.9M\n3  GTA: Vice City                ~14.2M',
        'family-stats-note':'Historical sales estimates; reporting dates vary.'
    }.items():props(ps2,'text',name,text=content)
    props(ps2,'text','family-stats',size='0.29 0.18',fontSize='0.023')
    props(ps2,'text','family-stats-note',pos='0.670 0.846',size='0.29 0.04',fontSize='0.017')

    # Optional real hardware footage replaces only the showcase, never gameplay clips.
    for playlist in sorted((theme/'family-hardware').glob('*.m3u')):
        if not playlist.read_text(encoding='utf-8-sig').strip():continue
        v=ET.SubElement(root,'view',name='system',attrib={'if':f"{{system.theme}} == '{playlist.stem}'"})
        props(v,'image','family-hardware',visible='false')
        props(v,'video','family-hardware-video',path='./family-hardware/'+playlist.name,pos='0.025 0.57',size='0.285 0.30',zIndex='75')
    write(theme/'family-console-screen.xml',root)
    for include in list(main.findall('include')):
        if include.text=='./family-console-screen.xml':main.remove(include)
    ET.SubElement(main,'include').text='./family-console-screen.xml'
    write(main_path,main)
    return len(list(tiles.glob('*.svg')))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('theme',type=Path)
    args=parser.parse_args()
    print('Console tiles refreshed:',refresh(args.theme))
