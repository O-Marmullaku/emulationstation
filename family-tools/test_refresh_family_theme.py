import importlib.util
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

spec=importlib.util.spec_from_file_location('refresh',Path(__file__).with_name('Refresh-FamilyTheme.py'))
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class RefreshTest(unittest.TestCase):
    def test_shared_palette_and_repeatable_install(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'theme.xml').write_text('<theme><include>existing.xml</include></theme>')
            logos=root/'_theme_inc/images/logos';logos.mkdir(parents=True)
            (logos/'ps2.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 30"><path fill="red" d="M0 0H100V30H0Z"/></svg>')
            self.assertEqual(module.refresh(root),1)
            before=(root/'family-console-screen.xml').read_bytes()
            module.refresh(root)
            self.assertEqual(before,(root/'family-console-screen.xml').read_bytes())
            includes=[n.text for n in ET.parse(root/'theme.xml').getroot().findall('include')]
            self.assertEqual(includes,['existing.xml','./family-console-screen.xml'])
            layout=ET.parse(root/'family-console-screen.xml').getroot()
            for palette in module.PALETTES:
                self.assertIsNotNone(layout.find(f"variables/family.accent[@ifSubset='colorset:{palette}']"))
            self.assertEqual(layout.find("view/image[@name='background-art']/visible").text,'false')
            self.assertEqual(layout.find("view/video[@name='system_video']/visible").text,'false')
            self.assertEqual(layout.find("view/image[@name='logo']/path").text,'./family-tiles/${family.skin}/${system.theme}.svg')
            for palette, colors in module.SURFACES.items():
                self.assertIn('#'+colors[0],(root/f'family-tiles/{palette}/ps2.svg').read_text())
                self.assertIn('#'+colors[3],(root/f'family-tiles/{palette}/waves.svg').read_text())
            elements=layout.find("view[@name='system']")
            self.assertEqual(len(elements),len({node.get('name') for node in elements}))
            self.assertIn('{binding:total}',layout.find("view/text[@name='family-stats']/text").text)
            self.assertEqual(layout.find("view/text[@name='friends']/visible").text,'false')
            self.assertEqual(layout.find("view/text[@name='trophy-level']/visible").text,'false')
            self.assertNotIn('Osman',before.decode())
            self.assertNotIn('Historical sales',before.decode())
            self.assertIn('4 March 2000',before.decode())
            self.assertTrue((root/'family-tiles/surface.svg').is_file())
            self.assertTrue((root/'family-tiles/waves.svg').is_file())
            self.assertIn('#FFFFFF',(root/'family-tiles/ps2.svg').read_text())


if __name__=='__main__':unittest.main()
