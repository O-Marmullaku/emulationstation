"""Refresh the shared console screen using local PlayStation-X assets, without changing profiles."""
import argparse
import copy
from pathlib import Path
import re
import xml.etree.ElementTree as ET


PALETTES = {'blue': ('030C19', '192C46', '69B6FF'),
            'pink': ('160911', '382131', 'F2A5CE'),
            'black': ('08090C', '25272B', 'D2D6DD')}
SURFACES = {'blue': ('103C78','03132A','237BCD','0066FF'),
            'pink': ('69284E','200A1A','AC568A','E13594'),
            'black': ('353A44','101217','747D8C','8191AA')}


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
    (tiles/'surface.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="600" height="380" viewBox="0 0 600 380"><defs><linearGradient id="panel" x2="100%" y2="100%"><stop stop-color="#303030" stop-opacity=".5"/><stop offset="100%" stop-color="#101010" stop-opacity=".85"/></linearGradient></defs><rect x="1" y="1" width="598" height="378" rx="16" fill="url(#panel)" stroke="#505050" stroke-width="1"/><path d="M35 76H565" stroke="#505050" opacity=".65"/></svg>', encoding='utf-8')
    (tiles/'waves.svg').write_text('''<svg xmlns="http://www.w3.org/2000/svg" width="1920" height="1080" viewBox="0 0 1920 1080">
<defs><radialGradient id="light"><stop stop-color="#FFFFFF" stop-opacity=".42"/><stop offset="100%" stop-color="#FFFFFF" stop-opacity="0"/></radialGradient><linearGradient id="ribbon" x2="0%" y2="100%"><stop stop-color="#FFFFFF" stop-opacity=".065"/><stop offset="100%" stop-color="#FFFFFF" stop-opacity="0"/></linearGradient></defs>
<ellipse cx="970" cy="0" rx="920" ry="510" fill="url(#light)"/><ellipse cx="350" cy="775" rx="690" ry="380" fill="url(#light)" opacity=".65"/>
<path d="M300 720C900 540 1430 420 2020 320L2020 490C1410 370 1030 690 300 800Z" fill="url(#ribbon)"/>
<path d="M-80 610C500 825 800 1000 1460 1030L1460 1120C600 1030 240 790-80 750Z" fill="url(#ribbon)"/>
<path d="M300 720C900 540 1430 420 2020 320M-80 610C500 825 800 1000 1460 1030" fill="none" stroke="#FFFFFF" stroke-width="16" opacity=".025"/>
<path d="M300 720C900 540 1430 420 2020 320M-80 610C500 825 800 1000 1460 1030" fill="none" stroke="#FFFFFF" stroke-width="7" opacity=".06"/>
<path d="M300 720C900 540 1430 420 2020 320M-80 610C500 825 800 1000 1460 1030" fill="none" stroke="#FFFFFF" stroke-width="1.5" opacity=".45"/></svg>''', encoding='utf-8')
    # Layered strokes provide a soft selected edge without unsupported SVG filters.
    focus = ET.Element('svg', xmlns='http://www.w3.org/2000/svg', width='440', height='440', viewBox='0 0 440 440')
    for width, opacity in [(32,.04),(22,.08),(14,.16),(7,.4),(3,1)]:
        ET.SubElement(focus,'rect',x='18',y='18',width='404',height='404',rx='16',fill='none',stroke='#FFFFFF',attrib={'stroke-width':str(width),'opacity':str(opacity)})
    ET.SubElement(focus,'path',d='M20 338H420',stroke='#FFFFFF',opacity='.25')
    ET.SubElement(focus,'path',d='M20 338H420V404Q420 420 404 420H36Q20 420 20 404Z',fill='#303030')
    write(tiles/'focus.svg',focus)
    (tiles/'footer.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" width="1920" height="100" viewBox="0 0 1920 100"><rect x="1" y="1" width="1918" height="98" rx="16" fill="#101010" stroke="#404040"/></svg>',encoding='utf-8')
    (tiles/'header.svg').write_text('''<svg xmlns="http://www.w3.org/2000/svg" width="1920" height="100" viewBox="0 0 1920 100">
<rect width="1920" height="99" fill="#FFFFFF" opacity=".025"/><path d="M0 99H1920" stroke="#FFFFFF" opacity=".20"/>
<circle cx="75" cy="50" r="33" fill="#101010" stroke="#FFFFFF" stroke-width="1.5"/>
<rect x="695" y="18" width="530" height="63" rx="32" fill="#080808" opacity=".72"/>
<path d="M934 34V64" stroke="#FFFFFF" opacity=".7"/>
<g transform="translate(743 35)" fill="none" stroke="#FFFFFF" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><path d="M7 2H29Q34 2 36 9L40 25Q40 30 35 28L27 20H10L3 28Q-2 30-1 24L2 9Q3 2 7 2Z"/><path d="M10 8V17M6 12H14"/><circle cx="28" cy="9" r="1"/><circle cx="32" cy="14" r="1"/></g>
<g transform="translate(991 50)" fill="none" stroke="#FFFFFF" stroke-width="3"><circle r="14"/><path d="M0-9V0H7"/></g></svg>''',encoding='utf-8')
    logos = theme / '_theme_inc/images/logos'
    # Prefer the theme's white marks, otherwise normalize its regular vector.
    for source in sorted(logos.glob('*.svg')):
        if source.stem.endswith('-w'): continue
        white = source.with_name(source.stem + '-w.svg')
        tile(white if white.exists() else source, tiles / source.name)

    # Bake surface colors separately from logo ink: pale multiplicative tints wash out the lighting.
    for palette, (top,bottom,border,light) in SURFACES.items():
        target=tiles/palette; target.mkdir(exist_ok=True)
        for source in tiles.glob('*.svg'):
            data=source.read_text(encoding='utf-8')
            for old,new in [('#303030','#'+(light if source.name=='focus.svg' else top)),('#101010','#'+bottom),('#505050','#'+border),('#FFFFFF','#'+(light if source.name=='waves.svg' else PALETTES[palette][2]))]:
                data=data.replace(old,new)
            (target/source.name).write_text(data,encoding='utf-8')
    mark=logos/'psx-w.svg'
    if mark.exists():
        symbol=ET.parse(mark).getroot()
        symbol.set('viewBox','0 0 150 113.032'); symbol.set('width','150'); symbol.set('height','113.032')
        for node in symbol.iter():
            node.tag=node.tag.split('}')[-1]
            if node.tag in ('path','g'): node.set('fill','#FFFFFF')
        symbol.set('xmlns','http://www.w3.org/2000/svg')
        write(tiles/'ps-mark.svg',symbol)

    root = ET.Element('theme')
    ET.SubElement(root, 'formatVersion').text = '7'
    variables = ET.SubElement(root, 'variables')
    for palette, (bg, panel, accent) in PALETTES.items():
        ET.SubElement(variables,'family.skin',ifSubset='colorset:'+palette).text=palette
        ET.SubElement(variables,'family.light',ifSubset='colorset:'+palette).text=SURFACES[palette][3]
        for key, value in [('family.bg',bg),('family.panel',panel),('family.accent',accent)]:
            ET.SubElement(variables,key,ifSubset='colorset:'+palette).text=value
    common = ET.SubElement(root,'view',name='system,detailed,basic,grid')
    for name in ('logo-front','plus-picto','info-picto','puntoazul-1','puntoazul-2','users-picto','star-picto','trophy-picto','linea-superior'):
        props(common,'image',name,visible='false')
    for name in ('friends','trophy-level','systemInfoEx','systemInfoEx2'):
        props(common,'text',name,visible='false')
    props(common,'image','family-header',path='./family-tiles/header.svg',pos='0 0',size='1 .0926',color='${family.accent}',zIndex='95')
    props(common,'image','avatar',pos='0.024 0.019',maxSize='0.030 0.054',origin='0 0',color='FFFFFF',roundCorners='.5',zIndex='99')
    props(common,'text','username',text='${family.profile.name}',pos='0.076 0.035',size='0.22 0.036',fontSize='.025',fontPath='./_theme_inc/fonts/SST Bold.ttf',glowSize='0')
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
    props(view,'image','family-waves',path='./family-tiles/${family.skin}/waves.svg',color='FFFFFF',size='1 1',pos='0 0',zIndex='2')
    props(view,'image','family-footer',path='./family-tiles/footer.svg',pos='.015 .917',size='.970 .070',color='${family.accent}',zIndex='70')
    props(view,'image','logo',path='./family-tiles/${family.skin}/${system.theme}.svg',color='FFFFFF',saturation='1')
    props(view,'carousel','systemcarousel',pos='-0.535 0.113',size='1.56 1',logoSize='0.137 0.195',logoScale='1.176',minLogoOpacity='1')
    props(view,'image','marco-activo',visible='false')
    props(view,'image','family-selected-tile',path='./family-tiles/${family.skin}/${system.theme}.svg',pos='.164 .120',size='.162 .215',color='FFFFFF',zIndex='85')
    props(view,'image','family-focus',path='./family-tiles/${family.skin}/focus.svg',pos='.156 .106',size='.178 .297',color='FFFFFF',zIndex='89')
    props(view,'text','start',pos='.164 .337',size='.162 .044',fontSize='.026',backgroundColor='00000000',zIndex='90')
    props(view,'text','system_name',pos='.348 .431',size='.60 .066',fontSize='.060',fontPath='./_theme_inc/fonts/SST Bold.ttf',glowSize='0')
    # Shared geometry; only palette tokens and per-console copy vary.
    for name, x, width in [('story',.345,.300),('stats',.657,.317)]:
        props(view,'image','family-'+name+'-card',path='./family-tiles/${family.skin}/surface.svg',pos=f'{x} 0.558',size=f'{width} 0.297',color='FFFFFF',zIndex='70')
    props(view,'image','family-hardware',path='./_theme_inc/images/consoles/${system.theme}.png',pos='0.020 0.55',maxSize='0.305 0.31',origin='0 0',zIndex='75')
    def text(name, content, pos, size, font=.025, color='EDF0F5'):
        props(view,'text',name,text=content,pos=pos,size=size,fontPath='./_theme_inc/fonts/SST Roman.ttf',fontSize=font,color=color,alignment='left',verticalAlignment='top',zIndex='80',lineSpacing='1.15')
    text('family-library','{binding:total} games','.414 .035','.071 .036',.023)
    text('family-header-playtime','{binding:gameTime} played','.536 .035','.101 .036',.023)
    for name in ('family-library','family-header-playtime'):
        view.find(f"text[@name='{name}']/zIndex").text='100'
    text('family-subtitle','${system.manufacturer} · ${system.releaseYear}','.349 .505','.5 .038',.029,'${family.accent}')
    text('family-story-title','${system.fullName}','0.367 0.579','0.26 0.04',.028,'${family.accent}')
    text('family-story','${system.manufacturer}\nFirst released: ${system.releaseYear}','0.367 0.637','0.26 0.11',.024)
    text('family-story-note','Choose a game to continue your library.','0.363 0.772','0.26 0.07',.021)
    text('family-stats-title','Your library','0.677 0.579','0.28 0.04',.028,'${family.accent}')
    text('family-stats','Games: {binding:total}\nPlaytime: {binding:gameTime}','0.677 0.637','0.28 0.18',.024)
    text('family-stats-note','Playtime follows your Personal / Everyone display choice.','0.676 0.78','0.28 0.06',.021)
    for name in ('family-story-title','family-stats-title'):
        ET.SubElement(view.find(f"text[@name='{name}']"),'fontPath').text='./_theme_inc/fonts/SST Bold.ttf'

    # PS2 facts are separated from shared layout and palette; no profile names.
    ps2=ET.SubElement(root,'view',name='system',attrib={'if':"{system.theme} == 'ps2'"})
    if mark.exists():
        props(ps2,'image','family-ps-mark',path='./family-tiles/ps-mark.svg',pos='.008 .381',size='.145 .217',color='${family.light}50',zIndex='3')
    if (theme/'family-hardware/ps2.png').exists():
        props(ps2,'image','family-hardware',path='./family-hardware/ps2.png',pos='.003 .540',maxSize='.355 .340')
    for name,content in {
        'family-story-title':'First launched in Japan',
        'family-story':'4 March 2000\nNorth America · 26 October 2000\nEurope · 24 November 2000',
        'family-story-note':'DVD movies. PS1 classics.\nFinal Fantasy XI stayed online until 2016.',
        'family-stats-title':'160M+ consoles sold',
        'family-stats':'Best sellers · PS2 copies\n1  GTA: San Andreas          ~17.3M\n2  Gran Turismo 3             ~14.9M\n3  GTA: Vice City                ~14.2M',
        'family-stats-note':''
    }.items():props(ps2,'text',name,text=content)
    props(ps2,'text','family-stats',size='0.29 0.18',fontSize='0.024')
    props(ps2,'text','family-story',fontSize='0.024')
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
    return len([p for p in tiles.glob('*.svg') if p.name not in ('surface.svg','waves.svg','focus.svg','footer.svg','header.svg','ps-mark.svg')])


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('theme',type=Path)
    args=parser.parse_args()
    print('Console tiles refreshed:',refresh(args.theme))
