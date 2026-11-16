"""Where the exporters read Epson's installer and write the app's resources (set by build.py)."""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import macres  # noqa: E402

PAYLOAD = os.environ.get('ES_PAYLOAD', '')
OUT = os.environ.get('ES_OUT', '')
DS = os.path.join(PAYLOAD, 'Library/Image Capture/TWAIN Data Sources/EPSON Perfection V550.ds/Contents')
PLUGINS = os.path.join(DS, 'PlugIns')
FILE_FORMAT = os.path.join(PLUGINS, 'File Format')
UI_LOCALIZED = os.path.join(PLUGINS, 'UI.bundle/Contents/Resources/English.lproj/Localized.rsrc')
UI_RSRC = os.path.join(PLUGINS, 'UI.bundle/Contents/Resources/UI.rsrc')
IC_LOCALIZED = os.path.join(PLUGINS, 'Image Control.bundle/Contents/Resources/American.lproj/Localized.rsrc')
IC_RSRC = os.path.join(PLUGINS, 'Image Control.bundle/Contents/Resources/Image Control.rsrc')


def rsrc(path):
    return macres.open_rsrc(path)


_STRINGS = {}


def STR(tag, rid, index, **spec):
    """an Epson text, read from the installer's string resources (gen/strfiles.json says where)"""
    import json
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    sys.path.insert(0, os.path.join(here, 'gen'))
    import strings_gen
    if tag not in _STRINGS:
        files = json.load(open(os.path.join(here, 'gen', 'strfiles.json')))
        _STRINGS[tag] = strings_gen.string_lists(PAYLOAD, {tag: files[tag]})[tag]
    return strings_gen.transform(_STRINGS[tag][rid][index - 1], spec)
