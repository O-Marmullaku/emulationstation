"""Refresh the shared console screen using local PlayStation-X assets, without changing profiles."""
import argparse
import copy
from pathlib import Path
import re
import xml.etree.ElementTree as ET


PALETTES = {'blue': ('030C19', '192C46', '69B6FF'),
            'pink': ('160911', '382131', 'F2A5CE'),
            'black': ('08090C', '25272B', 'D2D6DD')}


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
    scale = min(310 / width, 140 / height)
    svg = ET.Element('svg', xmlns='http://www.w3.org/2000/svg', width='400', height='320', viewBox='0 0 400 320')
    defs = ET.SubElement(svg, 'defs')
    gradient = ET.SubElement(defs, 'linearGradient', id='tile', x2='0%', y2='100%')
    ET.SubElement(gradient, 'stop', offset='0%', attrib={'stop-color':'#303030'})
    ET.SubElement(gradient, 'stop', offset='100%', attrib={'stop-color':'#101010'})
    ET.SubElement(svg, 'rect', x='2', y='2', width='396', height='316', rx='16', fill='url(#tile)', stroke='#505050', attrib={'stroke-width':'2'})
    group = ET.SubElement(svg, 'g', transform=f'translate({200-width*scale/2} {160-height*scale/2}) scale({scale}) translate({-x} {-y})')
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
    # Neutral vector surfaces are tinted by the selected profile palette.
    (tiles/'surface.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="600" height="380" viewBox="0 0 600 380"><rect x="1" y="1" width="598" height="378" rx="16" fill="#202020" stroke="#505050" stroke-width="2"/><path d="M30 68H570" stroke="#505050"/></svg>', encoding='utf-8')
    (tiles/'waves.svg').write_text('''<svg xmlns="http://www.w3.org/2000/svg" width="1920" height="1080" viewBox="0 0 1920 1080">
<defs><radialGradient id="light"><stop stop-color="#FFFFFF" stop-opacity=".25"/><stop offset="1" stop-color="#FFFFFF" stop-opacity="0"/></radialGradient><linearGradient id="ribbon" x2="0" y2="1"><stop stop-color="#FFFFFF" stop-opacity=".02"/><stop offset="1" stop-color="#FFFFFF" stop-opacity=".12"/></linearGradient></defs>
<ellipse cx="900" cy="40" rx="1050" ry="600" fill="url(#light)"/><ellipse cx="370" cy="850" rx="720" ry="380" fill="url(#light)"/>
<path d="M-100 570C500 960 1120 230 2020 450L2020 660C1000 370 730 1110-100 810Z" fill="url(#ribbon)"/>
<path d="M-100 570C500 960 1120 230 2020 450M-100 810C730 1110 1000 370 2020 660" fill="none" stroke="#FFFFFF" stroke-width="2" opacity=".20"/></svg>''', encoding='utf-8')
    # Layered strokes provide a soft selected edge without unsupported SVG filters.
    focus = ET.Element('svg', xmlns='http://www.w3.org/2000/svg', width='440', height='440', viewBox='0 0 440 440')
    for width, opacity in [(28,.025),(20,.04),(12,.09),(6,.25),(2,1)]:
        ET.SubElement(focus,'rect',x='18',y='18',width='404',height='404',rx='16',fill='none',stroke='#FFFFFF',attrib={'stroke-width':str(width),'opacity':str(opacity)})
    ET.SubElement(focus,'path',d='M20 338H420',stroke='#FFFFFF',opacity='.25')
    write(tiles/'focus.svg',focus)
    (tiles/'footer.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="1920" height="100" viewBox="0 0 1920 100"><rect x="1" y="1" width="1918" height="98" rx="16" fill="#101010" stroke="#404040"/></svg>',encoding='utf-8')
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
    common = ET.SubElement(root,'view',name='system,detailed,basic,grid')
    for name in ('logo-front','plus-picto','info-picto','puntoazul-1','puntoazul-2','users-picto','star-picto','trophy-picto','linea-superior'):
        props(common,'image',name,visible='false')
    for name in ('friends','trophy-level','systemInfoEx','systemInfoEx2'):
        props(common,'text',name,visible='false')
    props(common,'image','avatar',pos='0.031 0.027',maxSize='0.033 0.052',origin='0 0',color='FFFFFF')
    props(common,'text','username',text='${family.profile.name}',pos='0.073 0.037',size='0.22 0.034',fontSize='.025',fontPath='./_theme_inc/fonts/SST Roman.ttf',glowSize='0')
    avatar = ET.SubElement(root,'view',name='system,detailed,basic,grid',ifSubset='system-avatar:angry_cat')
    props(avatar,'image','avatar',path='./family-avatars/angry_cat.png')
    subset = main.find("subset[@name='system-avatar']")
    if subset is not None and subset.find("include[@name='angry_cat']") is None:
        ET.SubElement(subset,'include',name='angry_cat',displayName='Angry cat')
    screen = ET.SubElement(root,'view',name='screen')
    props(screen,'text','clock',pos='.919 .037',size='.06 .034',fontSize='.023',glowSize='0')
    props(common,'helpsystem','help',pos='.035 .943',fontSize='.021',textColor='CCDDF0',iconColor='FFFFFF',glowSize='0')
    view = ET.SubElement(root,'view',name='system')
    for name in ('background-art','overlay','consoles','logo2','border-video'):
        props(view,'image',name,visible='false')
    for name in ('system_video','system_video_bg','default_background'):
        props(view,'video',name,visible='false')
    props(view,'text','system_description',visible='false')
    for name in ('fullName','manufacturerName'):
        props(view,'text',name,visible='false')
    for name in ('linea-inferior','pie-barra'):
        props(view,'image',name,visible='false')
    props(view,'image','background',path='./_theme_inc/images/pixel-blanco.png',color='${family.bg}',size='1 1',pos='0 0',zIndex='1')
    props(view,'image','family-waves',path='./family-tiles/waves.svg',color='${family.accent}',size='1 1',pos='0 0',zIndex='2')
    props(view,'image','family-footer',path='./family-tiles/footer.svg',pos='.015 .917',size='.970 .070',color='${family.accent}',zIndex='70')
    props(view,'image','logo',path='./family-tiles/${system.theme}.svg',color='${family.accent}',saturation='1')
    props(view,'carousel','systemcarousel',pos='-0.535 0.133',size='1.56 1',logoSize='0.137 0.195',logoScale='1.176',minLogoOpacity='0.9')
    props(view,'image','marco-activo',visible='false')
    props(view,'image','family-focus',path='./family-tiles/focus.svg',pos='.156 .126',size='.178 .317',color='${family.accent}',zIndex='89')
    props(view,'text','start',pos='.164 .375',size='.162 .040',fontSize='.026',backgroundColor='00000000',zIndex='90')
    props(view,'text','system_name',pos='.345 .453',size='.60 .066',fontSize='.054',fontPath='./_theme_inc/fonts/SST Bold.ttf',glowSize='0')
    # Shared geometry; only palette tokens and per-console copy vary.
    for name, x, width in [('story',.345,.300),('stats',.657,.317)]:
        props(view,'image','family-'+name+'-card',path='./family-tiles/surface.svg',pos=f'{x} 0.575',size=f'{width} 0.285',color='${family.accent}',zIndex='70')
    props(view,'image','family-hardware',path='./_theme_inc/images/consoles/${system.theme}.png',pos='0.020 0.55',maxSize='0.305 0.31',origin='0 0',zIndex='75')
    def text(name, content, pos, size, font=.025, color='EDF0F5'):
        props(view,'text',name,text=content,pos=pos,size=size,fontPath='./_theme_inc/fonts/SST Roman.ttf',fontSize=font,color=color,alignment='left',verticalAlignment='top',zIndex='80',lineSpacing='1.15')
    text('family-library','{binding:total} games   ·   {binding:gameTime} played','.32 .037','.50 .035',.023)
    text('family-subtitle','${system.manufacturer} · ${system.releaseYear}','.347 .525','.5 .035',.025,'${family.accent}')
    text('family-story-title','${system.fullName}','0.363 0.594','0.26 0.035',.025,'${family.accent}')
    text('family-story','${system.manufacturer}\nFirst released: ${system.releaseYear}','0.363 0.644','0.26 0.11',.022)
    text('family-story-note','Choose a game to continue your library.','0.363 0.772','0.26 0.07',.021)
    text('family-stats-title','Your library','0.676 0.594','0.28 0.035',.025,'${family.accent}')
    text('family-stats','Games: {binding:total}\nPlaytime: {binding:gameTime}','0.676 0.644','0.28 0.18',.023)
    text('family-stats-note','Playtime follows your Personal / Everyone display choice.','0.676 0.78','0.28 0.06',.021)
    for name in ('family-story-title','family-stats-title'):
        ET.SubElement(view.find(f"text[@name='{name}']"),'fontPath').text='./_theme_inc/fonts/SST Bold.ttf'

    # PS2 facts are separated from shared layout and palette; no profile names.
    ps2=ET.SubElement(root,'view',name='system',attrib={'if':"{system.theme} == 'ps2'"})
    for name,content in {
        'family-story-title':'First launched in Japan',
        'family-story':'4 March 2000\nNorth America · 26 October 2000\nEurope · 24 November 2000',
        'family-story-note':'DVD movies. PS1 classics.\nFinal Fantasy XI stayed online until 2016.',
        'family-stats-title':'160M+ consoles sold',
        'family-stats':'Best sellers · PS2 copies\n1  GTA: San Andreas          ~17.3M\n2  Gran Turismo 3             ~14.9M\n3  GTA: Vice City                ~14.2M',
        'family-stats-note':''
    }.items():props(ps2,'text',name,text=content)
    props(ps2,'text','family-stats',size='0.29 0.18',fontSize='0.023')
    props(ps2,'text','family-story',fontSize='0.022')
    props(ps2,'text','family-stats-note',visible='false')

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
    return len([p for p in tiles.glob('*.svg') if p.name not in ('surface.svg','waves.svg','focus.svg','footer.svg')])


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('theme',type=Path)
    args=parser.parse_args()
    print('Console tiles refreshed:',refresh(args.theme))
