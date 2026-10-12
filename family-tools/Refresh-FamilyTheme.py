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


# Console materials are independent of profile identity and accent choice.
CONSOLE_STYLES = {
    'snes': ('E3E3E8', 'F4F3F6', 'BDBBC8', '45415D', '282737', 24),
    'gc': ('100D25', '211B3B', '524572', 'C9B9FF', 'F1EDFF', 3),
    'xbox360': ('222A23', 'DBDFD9', 'A4ADA0', '365527', '232A22', 18),
    'neutral': ('11151C', '202630', '3B4553', 'D3DCE8', 'EDF1F6', 14),
}
CONSOLE_GROUPS = {'snes': ('snes', 'satellaview'), 'gc': ('gc',), 'xbox360': ('xbox360',)}


def svg_file(path, width, height, body):
    path.write_text(f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">{body}</svg>', encoding='utf-8')


def console_assets(tiles):
    for style, (bg, panel, border, accent, ink, radius) in CONSOLE_STYLES.items():
        folder = tiles / style
        folder.mkdir(exist_ok=True)
        background = f'<rect width="1920" height="1080" fill="#{bg}"/>'
        if style == 'snes':
            background += '<path d="M-120 940Q480 400 1180 420T2070 170V1100H-120Z" fill="#D7D6DF"/><path d="M-60 970Q650 520 2000 580" fill="none" stroke="#CECDD7" stroke-width="2"/>'
            for x,y,c in [(1600,435,'D64B52'),(1643,478,'D3AC31'),(1557,478,'388962'),(1600,521,'456DB5')]:
                background += f'<circle cx="{x}" cy="{y}" r="15" fill="#{c}" opacity=".42"/>'
        elif style == 'gc':
            for x,y,z in [(40,440,90),(160,560,65),(410,480,42),(1500,440,85),(1740,600,120),(110,850,45),(1660,880,70)]:
                background += f'<path d="M{x} {y}l{z} {-z*.5}l{z} {z*.5}l{-z} {z*.5}Z" fill="#3B2F61"/><path d="M{x} {y}l{z} {z*.5}v{z}l{-z} {-z*.5}Z" fill="#241C40"/><path d="M{x+z} {y+z*.5}l{z} {-z*.5}v{z}l{-z} {z*.5}Z" fill="#30234F"/>'
        elif style == 'xbox360':
            background += '<defs><linearGradient id="blade"><stop stop-color="#7E877B"/><stop offset=".6" stop-color="#353E35"/><stop offset="1" stop-color="#19231B"/></linearGradient></defs><path d="M0 0H130Q20 520 155 1080H0Z" fill="url(#blade)"/><path d="M1920 0H1790Q1900 520 1765 1080H1920Z" fill="url(#blade)"/><path d="M115 0Q10 520 140 1080M1805 0Q1910 520 1780 1080" fill="none" stroke="#8FBE45" stroke-width="5"/><path d="M150 470Q950 340 1780 470V940Q900 825 150 940Z" fill="#2C352D"/>'
        svg_file(folder/'waves.svg',1920,1080,background)
        chrome = {'snes': ('383447','E8E5F0'), 'gc': ('18112F','C9B9FF'), 'xbox360': ('343D34','BBD894'), 'neutral': ('141A23','D3DCE8')}[style]
        head=(tiles/'header.svg').read_text(encoding='utf-8').replace('#0D1118','#'+chrome[0]).replace('#080808','#17171E').replace('#FFFFFF','#'+chrome[1])
        (folder/'header.svg').write_text(head,encoding='utf-8')
        svg_file(folder/'game-header.svg',1920,100,f'<rect width="1920" height="100" fill="#{chrome[0]}"/>')
        svg_file(folder/'footer.svg',1920,100,f'<rect x="1" y="1" width="1918" height="98" rx="{radius}" fill="#{chrome[0]}" stroke="#{border}"/>')
        stripe = ''
        if style == 'snes':
            for x,c in [(32,'456DB5'),(47,'388962'),(62,'D3AC31'),(77,'D64B52')]:
                stripe += f'<circle cx="{x}" cy="355" r="4" fill="#{c}"/>'
        if style == 'xbox360':
            stripe = '<path d="M1 24Q1 1 24 1H100V7H24Q7 7 7 24V66H1Z" fill="#6F9B35"/>'
        svg_file(folder/'surface.svg',600,380,f'<rect x="1" y="1" width="598" height="378" rx="{radius}" fill="#{panel}" stroke="#{border}"/>'+f'<path d="M35 76H565" stroke="#{border}"/>'+stripe)
        svg_file(folder/'focus.svg',440,440,f'<rect x="18" y="18" width="404" height="404" rx="{radius}" fill="none" stroke="#{accent}" stroke-width="3"/><path d="M20 338H420V404Q420 420 404 420H36Q20 420 20 404Z" fill="#{accent}"/>')
        for source in tiles.glob('*.svg'):
            if source.name in ('surface.svg','waves.svg','focus.svg','footer.svg','header.svg','ps-mark.svg','neutral-splash.svg','splash-panel.svg','profile-ring.svg'): continue
            doc=ET.parse(source).getroot()
            for node in doc.iter():
                node.tag=node.tag.split('}')[-1]
                if node.tag.split('}')[-1]=='stop':
                    node.set('stop-color','#'+panel)
                if node.tag.split('}')[-1]=='rect' and node.get('fill')=='url(#tile)':
                    node.set('rx',str(radius)); node.set('stroke','#'+border)
                if node.get('fill')=='#FFFFFF':node.set('fill','#'+accent)
                if node.get('stroke')=='#FFFFFF':node.set('stroke','#'+accent)
            doc.set('xmlns','http://www.w3.org/2000/svg')
            write(folder/source.name,doc)
    svg_file(tiles/'splash-panel.svg',600,380,'<rect x="1" y="1" width="598" height="378" rx="20" fill="#171D27"/>')
    svg_file(tiles/'neutral-splash.svg',1920,1080,'<defs><radialGradient id="ambient"><stop stop-color="#242D3C"/><stop offset="1" stop-color="#0D1118"/></radialGradient></defs><rect width="1920" height="1080" fill="url(#ambient)"/>')


def console_views(root):
    for style, names in CONSOLE_GROUPS.items():
        bg,panel,border,accent,ink,radius=CONSOLE_STYLES[style]
        for name in names:
            view=ET.SubElement(root,'view',name='system',attrib={'if':f"{{system.theme}} == '{name}'"})
            for element, asset in [('family-waves','waves.svg'),('family-story-card','surface.svg'),('family-stats-card','surface.svg'),('family-focus','focus.svg')]:
                props(view,'image',element,path=f'./family-tiles/{style}/{asset}',color='FFFFFF')
            props(view,'image','family-selected-tile',path=f'./family-tiles/{style}/${{system.theme}}.svg')
            props(view,'carousel','systemcarousel',logoPath=f'./family-tiles/{style}/{{system:theme}}.svg')
            for element in ('system_name','family-subtitle'):
                props(view,'text',element,color=('282737' if style=='snes' else 'F1F3F4'))
            for element in ('family-story','family-story-note','family-stats','family-stats-note'):
                props(view,'text',element,color=ink)
            for element in ('family-story-title','family-stats-title'):
                props(view,'text',element,color=accent)
            props(view,'text','start',color=('191329' if style=='gc' else 'FFFFFF'))
            chrome=ET.SubElement(root,'view',name='system,detailed,basic,grid',attrib={'if':f"{{system.theme}} == '{name}'"})
            props(chrome,'image','family-header',path=f'./family-tiles/{style}/header.svg',color='FFFFFF')
            props(chrome,'image','family-footer',path=f'./family-tiles/{style}/footer.svg',pos='.015 .917',size='.970 .070',color='FFFFFF',zIndex='90')
            games=ET.SubElement(root,'view',name='detailed,basic,grid',attrib={'if':f"{{system.theme}} == '{name}'"})
            props(games,'image','family-header',path=f'./family-tiles/{style}/game-header.svg')
            props(games,'image','background',path=f'./family-tiles/{style}/waves.svg',pos='0 0',size='1 1',color='FFFFFF',zIndex='1',opacity='1')
            foreground = ink if style=='snes' else 'F1F3F4'
            for element in ('md_description','md_genre','md_developer','md_name','md_players','gamecarouselLogoText'):
                props(games,'text',element,color=foreground,glowSize='0')
            props(games,'datetime','md_releasedate',color=foreground)
            props(games,'textlist','gamelist',primaryColor=foreground,secondaryColor=foreground,selectorColor=accent,selectedColor=('191329' if style=='gc' else 'FFFFFF'),selectorImagePath='none',glowSize='0')
            for element in ('default','selected'):
                props(games,'gridtile',element,backgroundColor=panel,backgroundEdgeColor=(accent if element=='selected' else border))
            menu=ET.SubElement(root,'view',name='menu',attrib={'if':f"{{system.theme}} == '{name}'"})
            props(menu,'menuBackground','menubg',color=panel,centerColor=panel)
            for element in ('menutext','menutitle','menufooter'):
                props(menu,'menuText',element,color=ink,selectorColor=accent,selectorColorEnd=accent,selectedColor=('191329' if style=='gc' else 'FFFFFF'),separatorColor=border)
            props(menu,'menuTextSmall','menutextsmall',color=ink)
            props(menu,'menuGroup','menugroup',color=accent,separatorColor=border)



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
<rect width="1920" height="99" fill="#0D1118"/><path d="M0 99H1920" stroke="#FFFFFF" opacity=".20"/>
<circle cx="75" cy="50" r="33" fill="#101010" stroke="#FFFFFF" stroke-width="1.5"/>
<rect x="590" y="18" width="740" height="63" rx="32" fill="#080808" opacity=".72"/>
<path d="M830 34V64" stroke="#FFFFFF" opacity=".7"/>
<g transform="translate(638 35)" fill="none" stroke="#FFFFFF" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"><path d="M7 2H29Q34 2 36 9L40 25Q40 30 35 28L27 20H10L3 28Q-2 30-1 24L2 9Q3 2 7 2Z"/><path d="M10 8V17M6 12H14"/><circle cx="28" cy="9" r="1"/><circle cx="32" cy="14" r="1"/></g>
<g transform="translate(887 50)" fill="none" stroke="#FFFFFF" stroke-width="3"><circle r="14"/><path d="M0-9V0H7"/></g></svg>''',encoding='utf-8')
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

    svg_file(tiles/'profile-ring.svg',1920,100,'<circle cx="75" cy="50" r="33" fill="none" stroke="#FFFFFF" stroke-width="1.5"/>')
    console_assets(tiles)
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
    props(common,'image','family-profile-ring',path='./family-tiles/profile-ring.svg',pos='0 0',size='1 .0926',color='${family.accent}',zIndex='98')
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
    props(view,'image','family-waves',path='./family-tiles/neutral/waves.svg',color='FFFFFF',size='1 1',pos='0 0',zIndex='2')
    props(view,'image','family-footer',path='./family-tiles/footer.svg',pos='.015 .917',size='.970 .070',color='${family.accent}',zIndex='70')
    props(view,'image','logo',path='./family-tiles/${family.skin}/${system.theme}.svg',color='FFFFFF',saturation='1')
    props(view,'carousel','systemcarousel',pos='-0.535 0.113',size='1.56 1',logoSize='0.137 0.195',logoScale='1.176',minLogoOpacity='1',logoPath='./family-tiles/neutral/{system:theme}.svg')
    props(view,'image','marco-activo',visible='false')
    props(view,'image','family-selected-tile',path='./family-tiles/neutral/${system.theme}.svg',pos='.164 .120',size='.162 .215',color='FFFFFF',zIndex='85')
    props(view,'image','family-focus',path='./family-tiles/neutral/focus.svg',pos='.156 .106',size='.178 .297',color='FFFFFF',zIndex='89')
    props(view,'text','start',pos='.164 .337',size='.162 .044',fontSize='.026',backgroundColor='00000000',zIndex='90')
    props(view,'text','system_name',pos='.348 .431',size='.60 .066',fontSize='.060',fontPath='./_theme_inc/fonts/SST Bold.ttf',glowSize='0')
    # Shared geometry; only palette tokens and per-console copy vary.
    for name, x, width in [('story',.345,.300),('stats',.657,.317)]:
        props(view,'image','family-'+name+'-card',path='./family-tiles/neutral/surface.svg',pos=f'{x} 0.558',size=f'{width} 0.297',color='FFFFFF',zIndex='70')
    props(view,'image','family-hardware',path='./_theme_inc/images/consoles/${system.theme}.png',pos='0.020 0.55',maxSize='0.305 0.31',origin='0 0',zIndex='75')
    def text(name, content, pos, size, font=.025, color='EDF0F5'):
        props(view,'text',name,text=content,pos=pos,size=size,fontPath='./_theme_inc/fonts/SST Roman.ttf',fontSize=font,color=color,alignment='left',verticalAlignment='top',zIndex='80',lineSpacing='1.15')
    text('family-library','{binding:total} games','.359 .035','.071 .036',.023)
    text('family-header-playtime','{binding:gameTimeReadable} played','.481 .035','.202 .036',.023)
    for name in ('family-library','family-header-playtime'):
        view.find(f"text[@name='{name}']/zIndex").text='100'
    text('family-subtitle','${system.manufacturer} · ${system.releaseYear}','.349 .505','.5 .038',.029,'${family.accent}')
    text('family-story-title','About this console','0.367 0.579','0.26 0.04',.028,'${family.accent}')
    text('family-story','${system.manufacturer}\nFirst released: ${system.releaseYear}','0.367 0.637','0.26 0.11',.024)
    text('family-story-note','Choose a game to continue your library.','0.363 0.772','0.26 0.07',.021)
    text('family-stats-title','Your library','0.677 0.579','0.28 0.04',.028,'${family.accent}')
    text('family-stats','Games: {binding:total}\nPlaytime: {binding:gameTimeReadable}','0.677 0.637','0.28 0.18',.024)
    text('family-stats-note','Playtime follows your Personal / Everyone display choice.','0.676 0.78','0.28 0.06',.021)
    for name in ('family-story-title','family-stats-title'):
        ET.SubElement(view.find(f"text[@name='{name}']"),'fontPath').text='./_theme_inc/fonts/SST Bold.ttf'

    games=ET.SubElement(root,'view',name='detailed,basic,grid')
    props(games,'video','default_background',visible='false')
    for element in ('consoles','logo','background-art','overlay','pie-barra','linea-inferior','linea-infos','md_cheevos','md_notcheevos'):
        props(games,'image',element,visible='false')
    for element in ('md_lbl_playcount','md_playcount','md_lbl_gametime','md_gametime','logoText'):
        props(games,'text',element,visible='false')
    props(games,'image','background',path='./family-tiles/launch/${system.theme}.svg',pos='0 0',size='1 1',color='FFFFFF',opacity='1',zIndex='1')
    props(games,'image','family-header',path='./family-tiles/neutral/game-header.svg',color='FFFFFF')
    props(games,'image','family-footer',path='./family-tiles/neutral/footer.svg',pos='.015 .917',size='.970 .070',color='FFFFFF',zIndex='90')
    props(games,'text','family-game-title',text='{game:name}',pos='.32 .029',size='.40 .055',fontPath='./_theme_inc/fonts/SST Bold.ttf',fontSize='.025',color='FFFFFF',alignment='center',zIndex='100')
    console_views(root)
    for name in ('psx','ps2','ps3'):
        v=ET.SubElement(root,'view',name='system',attrib={'if':f"{{system.theme}} == '{name}'"})
        for element,asset in [('family-waves','waves.svg'),('family-story-card','surface.svg'),('family-stats-card','surface.svg'),('family-focus','focus.svg')]:
            props(v,'image',element,path=f'./family-tiles/blue/{asset}',color='FFFFFF')
        props(v,'image','family-selected-tile',path='./family-tiles/blue/${system.theme}.svg')
        props(v,'carousel','systemcarousel',logoPath='./family-tiles/blue/{system:theme}.svg')

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
    splash_path=theme/'splash.xml'
    splash_backup=theme/'splash-before-family.xml'
    if splash_path.exists() and not splash_backup.exists():
        splash_backup.write_bytes(splash_path.read_bytes())
    splash=ET.Element('theme')
    ET.SubElement(splash,'formatVersion').text='7'
    ET.SubElement(splash,'include').text='./theme.xml'
    loading=ET.SubElement(splash,'view',name='splash')
    props(loading,'splash','splash',backgroundColor='${family.bg}')
    props(loading,'image','background',path='./family-tiles/neutral-splash.svg',pos='0 0',size='1 1',origin='0 0',color='FFFFFF')
    avatar=props(loading,'image','avatar',path='./_theme_options/avatars/${image.avatar}',pos='.5 .41',maxSize='.0675 .12',origin='.5 .5',roundCorners='.08',zIndex='10')
    ET.SubElement(avatar,'path',ifSubset='system-avatar:custom').text='./custom-avatar.png'
    ET.SubElement(avatar,'path',ifSubset='system-avatar:angry_cat').text='./family-avatars/angry_cat.png'
    props(loading,'text','username',text='${settings.FamilyProfileName}',pos='.25 .49',size='.5 .065',fontPath='./_theme_inc/fonts/SST Bold.ttf',fontSize='.042',color='FFFFFF',alignment='center',zIndex='10')
    props(loading,'text','label',visible='false')
    props(loading,'image','progressbar',pos='.4 .575',size='.2 .004',origin='0 0',color='${family.panel}',colorEnd='${family.panel}')
    props(loading,'image','progressbar:active',color='${family.accent}',colorEnd='${family.accent}')
    for node in loading:
        if node.get('name') not in ('avatar','username'):
            node.attrib.pop('extra',None)
    write(splash_path,splash)
    launch_dir=tiles/'launch'; launch_dir.mkdir(exist_ok=True)
    for source in logos.glob('*.svg'):
        if source.stem.endswith('-w'):continue
        style=next((key for key,names in CONSOLE_GROUPS.items() if source.stem in names),'blue' if source.stem in ('psx','ps2','ps3') else 'neutral')
        (launch_dir/source.name).write_bytes((tiles/style/'waves.svg').read_bytes())
    if (theme/'gamesplash.xml').exists() and not (theme/'gamesplash-before-family.xml').exists():
        (theme/'gamesplash-before-family.xml').write_bytes((theme/'gamesplash.xml').read_bytes())
    launch=copy.deepcopy(splash)
    loading=launch.find("view[@name='splash']")
    loading.find("image[@name='background']/path").text='./family-tiles/launch/{game:system:theme}.svg'
    # A quiet center panel keeps names readable over light and dark console palettes.
    props(loading,'image','launch-panel',path='./family-tiles/splash-panel.svg',pos='.28 .27',size='.44 .43',zIndex='6')
    loading.find("text[@name='username']/fontSize").text='.030'
    loading.find("text[@name='username']/pos").text='.25 .48'
    label=loading.find("text[@name='label']")
    label.find('visible').text='true'
    for key,value in dict(pos='.30 .55',size='.40 .07',fontSize='.030',fontPath='./_theme_inc/fonts/SST Bold.ttf',color='FFFFFF',alignment='center',zIndex='12').items():
        ET.SubElement(label,key).text=value
    loading.find("image[@name='progressbar']/pos").text='.4 .65'
    write(theme/'gamesplash.xml',launch)
    for include in list(main.findall('include')):
        if include.text=='./family-console-screen.xml':main.remove(include)
    ET.SubElement(main,'include').text='./family-console-screen.xml'
    write(main_path,main)
    return len([p for p in tiles.glob('*.svg') if p.name not in ('surface.svg','waves.svg','focus.svg','footer.svg','header.svg','ps-mark.svg','neutral-splash.svg','splash-panel.svg','profile-ring.svg')])


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('theme',type=Path)
    args=parser.parse_args()
    print('Console tiles refreshed:',refresh(args.theme))
