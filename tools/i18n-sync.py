#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Outil de synchronisation des traductions d'EvolutionWin.

Les textes de l'interface sont identifiés par leur libellé français : la clé de
ressource est « T_ » + FNV-1a 32 bits (unités UTF-16) du libellé, comme dans
winui/app/I18n.h (I18n::KeyFor). Les catalogues sont winui/app/Strings/<langue>/Resources.resw.

Commandes :
  extract [fichiers...]  liste en JSON les textes source des fichiers donnés
                         (tous les fichiers de l'application sans argument) ;
  check                  vérifie que chaque texte source a une entrée dans les 7 catalogues
                         (et que les marqueurs {0}, {1}… sont cohérents) ; code 1 sinon ;
  merge fichier.json...  ajoute aux catalogues les traductions d'un fichier JSON
                         {"texte fr": {"en": "...", "de": "...", "it": "...", "es": "...",
                         "pt": "...", "nl": "..."}} ;
  missing                liste en JSON les textes source absents d'au moins un catalogue.
"""
import html
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, 'winui', 'app')
LANGS = {'fr': 'fr-FR', 'en': 'en-US', 'de': 'de-DE', 'it': 'it-IT', 'es': 'es-ES', 'pt': 'pt-PT', 'nl': 'nl-NL'}

XAML_ATTRS = ['Text', 'Content', 'Label', 'Header', 'PlaceholderText', 'Title', 'Message', 'Subtitle',
              'PaneTitle', 'OnContent', 'OffContent', 'ToolTipService.ToolTip', 'AutomationProperties.Name']
XAML_ELEMENT_TEXT = ['TextBlock', 'Run', 'Button', 'HyperlinkButton', 'CheckBox', 'RadioButton',
                     'ToggleButton', 'ComboBoxItem', 'ListViewItem', 'NavigationViewItem', 'MenuFlyoutItem']


def key_for(fr):
    """Clé de ressource d'un texte français (identique à I18n::KeyFor)."""
    h = 2166136261
    data = fr.encode('utf-16-le')
    for i in range(0, len(data), 2):
        h ^= data[i] | (data[i + 1] << 8)
        h = (h * 16777619) & 0xFFFFFFFF
    return 'T_%08X' % h


def translatable(text, cpp=False):
    """Vrai si le texte est un libellé à traduire (et non un symbole, une liaison, un identifiant)."""
    if not text or text.startswith('ms-'):
        return False
    if text.startswith('{') and not re.match(r'\{\d+\}', text):
        return False
    if not re.search('[A-Za-zÀ-ÿ]{1}' if cpp else '[A-Za-zÀ-ÿ]{2}', text):
        return False
    # glyphe d'icône (zone d'usage privé) ou identifiant technique
    if re.search('[-]', text) and not re.search('[A-Za-z]{3}', text):
        return False
    return True


def unescape_cpp(literal):
    """Décode une chaîne littérale C++ (séquences d'échappement usuelles)."""
    out = []
    i = 0
    while i < len(literal):
        c = literal[i]
        if c == '\\' and i + 1 < len(literal):
            n = literal[i + 1]
            if n == 'n':
                out.append('\n')
            elif n == 't':
                out.append('\t')
            elif n == 'r':
                out.append('\r')
            elif n in '"\\\'':
                out.append(n)
            elif n == 'u' and i + 5 < len(literal) + 0:
                out.append(chr(int(literal[i + 2:i + 6], 16)))
                i += 4
            else:
                out.append(n)
            i += 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


LIT = r'((?:L"(?:[^"\\]|\\.)*"\s*)+)'
TR_CALL = re.compile(r'\bTr\(\s*' + LIT + r'(?:,\s*' + LIT + r')?(?:,\s*' + LIT + r')?\s*\)')


def join_literals(group):
    if group is None:
        return None
    parts = re.findall(r'L"((?:[^"\\]|\\.)*)"', group)
    return unescape_cpp(''.join(parts))


def extract_cpp(path):
    """Textes des appels Tr(...) sans clé explicite : {fr: en|None}."""
    found = {}
    text = open(path, encoding='utf-8', errors='replace').read()
    for m in TR_CALL.finditer(text):
        fr = join_literals(m.group(1))
        en = join_literals(m.group(2))
        res = join_literals(m.group(3))
        if res:
            continue
        if translatable(fr, True):
            found.setdefault(fr, en)
    return found


def extract_xaml(path):
    """Textes statiques d'un fragment XAML : {fr: None}."""
    raw = open(path, encoding='utf-8', errors='replace').read()
    raw = re.sub(r'<\?xml[^>]*\?>', '', raw)
    raw = re.sub(r'<!--#include[^>]*-->', '', raw)
    prefixes = set(re.findall(r'</?([A-Za-z_][\w.-]*):', raw)) | set(re.findall(r'\s([A-Za-z_][\w.-]*):[\w.]+=', raw))
    prefixes -= {'xml', 'xmlns'}
    decl = ' '.join('xmlns:%s="urn:%s"' % (p, p) for p in sorted(prefixes))
    found = {}
    try:
        root = ET.fromstring('<root xmlns="urn:default" %s>%s</root>' % (decl, raw))
    except ET.ParseError as e:
        sys.stderr.write('XAML illisible %s: %s\n' % (path, e))
        return found
    for el in root.iter():
        tag = el.tag.split('}')[-1]
        for name, value in el.attrib.items():
            local = name.split('}')[-1]
            if local in XAML_ATTRS and translatable(value):
                found.setdefault(value, None)
        if tag in XAML_ELEMENT_TEXT and el.text and translatable(el.text.strip()) and len(el) == 0:
            found.setdefault(el.text, None) if el.text == el.text.strip() else found.setdefault(el.text.strip(), None)
    return found


def source_files():
    files = []
    for name in sorted(os.listdir(APP)):
        if name.endswith(('.cpp', '.h')):
            files.append(os.path.join(APP, name))
    xaml_dir = os.path.join(APP, 'xaml')
    for name in sorted(os.listdir(xaml_dir)):
        if name.endswith(('.xamlpart', '.xaml.in')):
            files.append(os.path.join(xaml_dir, name))
    for name in sorted(os.listdir(APP)):
        if name.endswith('.xaml') and name != 'MainWindow.xaml':
            files.append(os.path.join(APP, name))
    return files


def extract(paths):
    result = {}
    for p in paths:
        p = p if os.path.isabs(p) else os.path.abspath(p)
        if p.endswith(('.cpp', '.h')):
            found = extract_cpp(p)
        else:
            found = extract_xaml(p)
        for fr, en in found.items():
            entry = result.setdefault(fr, {'en': None, 'files': []})
            if en and not entry['en']:
                entry['en'] = en
            entry['files'].append(os.path.relpath(p, ROOT).replace('\\', '/'))
    return result


DATA = re.compile(r'<data name="([^"]+)"[^>]*>\s*<value>(.*?)</value>', re.S)


def read_catalog(lang):
    path = os.path.join(APP, 'Strings', LANGS[lang], 'Resources.resw')
    text = open(path, encoding='utf-8').read()
    return {m.group(1): html.unescape(m.group(2)) for m in DATA.finditer(text)}


def merge(files):
    additions = {}
    for f in files:
        data = json.load(open(f, encoding='utf-8'))
        for fr, tr in data.items():
            additions.setdefault(fr, {}).update({k: v for k, v in tr.items() if v})
    for lang in LANGS:
        path = os.path.join(APP, 'Strings', LANGS[lang], 'Resources.resw')
        raw = open(path, 'rb').read().decode('utf-8')
        crlf = '\r\n' in raw
        text = raw.replace('\r\n', '\n')
        existing = {m.group(1) for m in DATA.finditer(text)}
        block = ''
        for fr, tr in additions.items():
            key = key_for(fr)
            value = fr if lang == 'fr' else tr.get(lang)
            if key in existing or not value:
                continue
            existing.add(key)
            block += '  <data name="%s" xml:space="preserve">\n    <value>%s</value>\n    <comment>%s</comment>\n  </data>\n' % (
                key, html.escape(value, quote=False), html.escape(fr, quote=False).replace('--', '- -'))
        if block:
            i = text.rindex('</root>')
            text = text[:i] + block + text[i:]
        if crlf:
            text = text.replace('\n', '\r\n')
        open(path, 'wb').write(text.encode('utf-8'))


def placeholders(s):
    return sorted(re.findall(r'\{\d+\}', s))


def compute_missing():
    sources = extract(source_files())
    catalogs = {lang: read_catalog(lang) for lang in LANGS}
    missing = {}
    problems = []
    keys = {}
    for fr, info in sources.items():
        key = key_for(fr)
        if key in keys and keys[key] != fr:
            problems.append('collision de clé %s : %r / %r' % (key, keys[key], fr))
        keys[key] = fr
        for lang, cat in catalogs.items():
            if key not in cat:
                missing.setdefault(fr, {'en': info['en'], 'files': info['files'], 'langs': []})['langs'].append(lang)
            elif lang != 'fr' and placeholders(cat[key]) != placeholders(fr):
                problems.append('marqueurs incohérents (%s) pour %r' % (lang, fr))
    return sources, missing, problems


def main(argv):
    sys.stdout.reconfigure(encoding='utf-8')
    sys.stderr.reconfigure(encoding='utf-8')
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == 'extract':
        paths = argv[2:] or source_files()
        json.dump(extract(paths), sys.stdout, ensure_ascii=False, indent=1)
        return 0
    if cmd == 'merge':
        merge(argv[2:])
        return 0
    if cmd in ('check', 'missing'):
        sources, missing, problems = compute_missing()
        if cmd == 'missing':
            json.dump(missing, sys.stdout, ensure_ascii=False, indent=1)
            return 0
        for p in problems:
            print('ERREUR:', p)
        for fr, info in list(missing.items())[:40]:
            print('MANQUE (%s): %r  [%s]' % (','.join(info['langs']), fr, info['files'][0]))
        print('%d textes sources, %d sans traduction complète, %d problèmes' % (len(sources), len(missing), len(problems)))
        return 1 if missing or problems else 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
