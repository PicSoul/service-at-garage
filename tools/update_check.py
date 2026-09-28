"""
Service At Garage: game update checker / signature repair tool.

After a game update, run:   tools\\update_check.bat            (check only)
                            tools\\update_check.bat --write    (also repair the installed ini)

Every installed SCS truck game (American Truck Simulator, Euro Truck Simulator 2) is checked.
For every signature it checks that the pattern matches exactly once in the game executable and that the
values read from it are plausible. If a signature no longer matches, it looks for the most similar code in the
new executable (usually the game was recompiled and a few bytes moved), proposes a repaired signature and, with
--write, stores it in the ini (a backup is kept). The plugin reads its signatures from the ini, so repairs need
no recompiling.

Only the Python standard library is used.
"""
import argparse
import configparser
import datetime
import json
import os
import re
import shutil
import struct
import sys

GAMES = [('ATS', os.path.join('steamapps', 'common', 'American Truck Simulator', 'bin', 'win_x64', 'amtrucks.exe')),
         ('ETS2', os.path.join('steamapps', 'common', 'Euro Truck Simulator 2', 'bin', 'win_x64', 'eurotrucks2.exe'))]
DLL = 'service_at_garage.dll'
INI = 'service_at_garage.ini'


def find_game_exes():
    """Installed SCS truck games found through Steam's registry entry and library list: [(id, exe path)]."""
    roots = []
    try:
        import winreg
        for hive, key in ((winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam'),
                          (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\WOW6432Node\Valve\Steam')):
            try:
                with winreg.OpenKey(hive, key) as k:
                    for value in ('SteamPath', 'InstallPath'):
                        try:
                            roots.append(winreg.QueryValueEx(k, value)[0])
                        except OSError:
                            pass
            except OSError:
                pass
    except ImportError:
        pass
    roots.append(r'C:\Program Files (x86)\Steam')
    libraries = []
    for root in roots:
        libraries.append(root)
        vdf = os.path.join(root, 'steamapps', 'libraryfolders.vdf')
        if os.path.exists(vdf):
            libraries += [p.replace('\\\\', '\\') for p in
                          re.findall(r'"path"\s+"([^"]+)"', open(vdf, encoding='utf-8', errors='replace').read())]
    found = []
    for game, rel in GAMES:
        for lib in libraries:
            exe = os.path.join(lib, rel)
            if os.path.exists(exe):
                found.append((game, os.path.normpath(exe)))
                break
    return found


# Operands read from each signature: (name, byte position, size, kind). They must match src/signatures.cpp.
# kind: 'off' = struct offset, 'rva' = image-relative address of an activation path table.
SIGNATURES = {
    'StartActivation': [('slot_type', 34, 1, 'off'), ('stop_vslot', 51, 4, 'off'), ('slot_item', 61, 1, 'off')],
    'StopActivation': [],
    'PerformActivation': [('slot_type_2', 21, 1, 'off')],
    'GarageItemUpdate': [('item_node', 14, 1, 'off')],
    'GarageItemLayout': [('item_marker', 3, 4, 'off'), ('item_garage', 38, 4, 'off')],
    'GarageItemDraw': [],
    'GarageStatus': [('garage_status', 2, 4, 'off')],
    'TriggerUpdate': [('trigger_manager', 42, 4, 'off'), ('trigger_list_head', 52, 1, 'off'),
                      ('trigger_list_tail', 66, 1, 'off')],
    'IconTables': [('icon_anim_table', 3, 4, 'rva'), ('icon_model_table', 27, 4, 'rva')],
    'MarkerCreate': [],
    'MarkerPlace': [('marker_flags', 34, 4, 'off')],
    'MarkerDraw': [],
    'MarkerRelease': [],
}
# Data-only signatures that may match several identical copies of the same code, as long as every match
# gives the same values (the game has two identical icon functions).
SAME_VALUES_OK = {'IconTables'}
REQUIRED = {'StartActivation', 'StopActivation', 'PerformActivation', 'GarageItemUpdate', 'GarageItemLayout',
            'GarageStatus'}
OPTIONAL_NOTE = {
    'TriggerUpdate': 'without it there is no service icon, and leaving the bay is detected by distance only',
    'GarageItemDraw': 'without it there is no service icon',
    'IconTables': 'without it there is no service icon',
    'MarkerCreate': 'without it there is no service icon',
    'MarkerPlace': 'without it there is no service icon',
    'MarkerDraw': 'without it there is no service icon',
    'MarkerRelease': 'without it there is no service icon',
}
ICON_PATHS = {'icon_anim_table': '/model/activation/service.pma', 'icon_model_table': '/model/activation/service.pmd'}
# Values found for 1.61 (ATS and ETS2), shown for comparison only.
KNOWN_161 = {'slot_type': 0x14, 'stop_vslot': 0x198, 'slot_item': 0x18, 'slot_type_2': 0x14, 'item_node': 0x48,
             'item_marker': 0xA0, 'item_garage': 0x90, 'garage_status': 0x88, 'trigger_manager': 0x958,
             'trigger_list_head': 0x10, 'trigger_list_tail': 0x20, 'marker_flags': 0x13C}
ALIGN = {'slot_type': 4, 'slot_type_2': 4, 'garage_status': 4, 'marker_flags': 4}  # others: 8


# ------------------------------------------------------------------------------------------------ PE
class Exe:
    def __init__(self, path):
        self.path = path
        self.data = open(path, 'rb').read()
        d = self.data
        pe = struct.unpack_from('<I', d, 0x3C)[0]
        nsec = struct.unpack_from('<H', d, pe + 6)[0]
        optsz = struct.unpack_from('<H', d, pe + 20)[0]
        self.size_of_image = struct.unpack_from('<I', d, pe + 24 + 56)[0]
        self.image_base = struct.unpack_from('<Q', d, pe + 24 + 24)[0]
        # Same format as GameBuildId() in src/common.cpp
        self.build_id = f'{struct.unpack_from("<I", d, pe + 8)[0]:08X}-{self.size_of_image:X}'
        self.sections = []
        for i in range(nsec):
            o = pe + 24 + optsz + i * 40
            name = d[o:o + 8].rstrip(b'\0').decode(errors='replace')
            vsz, va, rsz, ro = struct.unpack_from('<IIII', d, o + 8)
            self.sections.append((name, va, vsz, ro, rsz))
        text = [s for s in self.sections if s[0] == '.text'][0]
        self.text_va, self.text = text[1], d[text[3]:text[3] + text[4]]

    def section_of(self, rva):
        for name, va, vsz, ro, rsz in self.sections:
            if va <= rva < va + max(vsz, rsz):
                return name
        return None

    def read(self, rva, size):
        for name, va, vsz, ro, rsz in self.sections:
            if va <= rva < va + rsz:
                return self.data[ro + rva - va:ro + rva - va + size]
        return None

    def c_string(self, va_abs):
        b = self.read(va_abs - self.image_base, 64) if va_abs > self.image_base else None
        return b.split(b'\0')[0].decode(errors='replace') if b else None


# ------------------------------------------------------------------------------------------------ patterns
def parse(sig):
    return [None if t in ('?', '??') else int(t, 16) for t in sig.split()]


def fmt(pat):
    return ' '.join('?' if b is None else f'{b:02X}' for b in pat)


def to_regex(pat):
    return re.compile(b''.join(b'.' if b is None else re.escape(bytes([b])) for b in pat), re.S)


def find_all(text, pat, limit=8):
    return [m.start() for _, m in zip(range(limit), to_regex(pat).finditer(text))]


def read_operand(exe, off, pos, size):
    b = exe.text[off + pos:off + pos + size]
    return int.from_bytes(b, 'little') if len(b) == size else None


def extract(exe, name, off):
    """Returns ({value_name: value}, [problems]) for a match at .text offset `off`."""
    values, problems = {}, []
    for vname, pos, size, kind in SIGNATURES[name]:
        v = read_operand(exe, off, pos, size)
        if v is None:
            problems.append(f'{vname}: out of range')
            continue
        values[vname] = v
        if kind == 'rva':
            entry = exe.read(v + 8, 8)
            path = exe.c_string(struct.unpack('<Q', entry)[0]) if entry else None
            if path != ICON_PATHS[vname]:
                problems.append(f'{vname}: table at 0x{v:X} does not hold {ICON_PATHS[vname]} (found {path!r})')
        elif v == 0 or v > 0x10000 or v % ALIGN.get(vname, 8):
            problems.append(f'{vname} = 0x{v:X} is implausible')
    if name == 'TriggerUpdate' and values.get('trigger_list_tail') != values.get('trigger_list_head', -1) + 0x10:
        problems.append('trigger list head/tail offsets are not 0x10 apart')
    return values, problems


def unique_enough(exe, name, pat, hits):
    """Exactly one match, or (for SAME_VALUES_OK) several matches that all give the same values."""
    if len(hits) == 1:
        return True
    if name not in SAME_VALUES_OK or not hits:
        return False
    return len({tuple(sorted(extract(exe, name, h)[0].items())) for h in hits}) == 1


def operand_positions(name):
    s = set()
    for _, pos, size, _ in SIGNATURES[name]:
        s.update(range(pos, pos + size))
    return s


def fuzzy_find(exe, name, pat, known):
    """Find the most similar code for a signature that no longer matches.

    Candidates come from distinctive byte runs of the old signature and from places where the last known
    operand values (struct offsets rarely change between updates) appear at the right position.
    Returns a list of (score, offset, values, problems), best first."""
    fixed = [i for i, b in enumerate(pat) if b is not None]
    if not fixed:
        return []
    candidates = set()
    run = []
    for i, b in enumerate(pat + [None]):
        if b is not None:
            run.append(i)
            continue
        if len(run) >= 4:
            chunk = bytes(pat[run[0]:run[-1] + 1])
            for k in range(0, len(chunk) - 3, 2):
                sub, pos, hits = chunk[k:k + 4], -1, 0
                while hits < 4000:
                    pos = exe.text.find(sub, pos + 1)
                    if pos < 0:
                        break
                    hits += 1
                    c = pos - (run[0] + k)
                    if 0 <= c <= len(exe.text) - len(pat):
                        candidates.add(c)
        run = []
    for vname, opos, size, kind in SIGNATURES[name]:
        if kind != 'off' or vname not in known or size != 4:
            continue
        needle, pos, hits = struct.pack('<I', known[vname]), -1, 0
        while hits < 20000:
            pos = exe.text.find(needle, pos + 1)
            if pos < 0:
                break
            hits += 1
            c = pos - opos
            if 0 <= c <= len(exe.text) - len(pat):
                candidates.add(c)
    results = []
    for c in candidates:
        similarity = sum(1 for i in fixed if exe.text[c + i] == pat[i]) / len(fixed)
        if similarity < 0.5:
            continue
        values, problems = extract(exe, name, c)
        same_as_known = all(values.get(k) == v for k, v in known.items() if k in values)
        score = similarity + (0.10 if same_as_known and not problems else 0.0)
        results.append((score, c, values, problems))
    results.sort(key=lambda r: -r[0])
    return results


def repair(exe, name, pat, off):
    """Build a signature at `off`: keep matching bytes, take the new bytes where the old ones differ, grow at
    the end until it is unique (operand positions stay the same)."""
    ops = operand_positions(name)
    new = []
    for i, b in enumerate(pat):
        new.append(None if (b is None or i in ops) else exe.text[off + i])
    for extra in range(0, 33):
        if extra:
            new.append(exe.text[off + len(pat) + extra - 1])
        hits = find_all(exe.text, new)
        if off in hits and unique_enough(exe, name, new, hits):
            return new
    return None


# ------------------------------------------------------------------------------------------------ built-ins
def builtin_signatures(dll_paths, source_path):
    """Signatures a plugin build uses when the ini has none: read from the DLL's SAGSIG: markers,
    else from src/signatures.cpp. Returns (dict, where_from)."""
    for dll in dll_paths:
        if dll and os.path.exists(dll):
            data = open(dll, 'rb').read()
            sigs = {m.group(1).decode(): m.group(2).decode()
                    for m in re.finditer(rb'SAGSIG:(\w+)=([0-9A-Fa-f? ]+)\x00', data)}
            if sigs:
                return sigs, dll
    if os.path.exists(source_path):
        src = open(source_path, encoding='utf-8').read()
        joined = re.sub(r'"\s*\n\s*"', '', src)   # join C string literal continuations
        sigs = dict(re.findall(r'"SAGSIG:(\w+)=([0-9A-Fa-f? ]+)"', joined))
        if sigs:
            return sigs, source_path
    return {}, None


def write_overrides(ini_path, build_id, sigs):
    """Replace the ini's [Signatures] section with a GameBuild-stamped one containing only `sigs`.
    User settings and comments elsewhere are kept. Returns the backup path (or None for a new file)."""
    backup = None
    lines = []
    if os.path.exists(ini_path):
        backup = ini_path + f'.bak_{datetime.datetime.now():%Y%m%d_%H%M%S}'
        shutil.copy2(ini_path, backup)
        lines = open(ini_path, encoding='utf-8').read().splitlines()
    out, skipping = [], False
    for line in lines:
        header = re.match(r'^\s*\[(\w+)\]', line)
        if header:
            skipping = header.group(1) == 'Signatures'
        if not skipping:
            out.append(line)
    while out and not out[-1].strip():
        out.pop()
    out += ['', '[Signatures]',
            '; Written by tools\\update_check.py. Only used while this exact game build is running.',
            f'GameBuild={build_id}']
    out += [f'{name}={sig}' for name, sig in sigs.items()]
    open(ini_path, 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    return backup


# ------------------------------------------------------------------------------------------------ main
def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe', default=None, help='check only this game executable (default: all games found through Steam)')
    ap.add_argument('--print-exe', action='store_true', help='only print the detected game executables, one per line')
    ap.add_argument('--ini', default=None, help=f'{INI} (default: the installed one of each game)')
    ap.add_argument('--dll', default=None, help=f'{DLL} to read built-in signatures from (default: the installed one)')
    ap.add_argument('--write', action='store_true', help='store repaired signatures in the ini (keeps a backup)')
    args = ap.parse_args()
    games = [('custom', args.exe)] if args.exe else find_game_exes()
    if args.print_exe:
        for _, exe in games:
            print(exe)
        return 0 if games else 2
    if not games or not all(os.path.exists(exe) for _, exe in games):
        print('Could not find American Truck Simulator or Euro Truck Simulator 2 through Steam. '
              'Pass the game executable with --exe "<path>\\amtrucks.exe" (or eurotrucks2.exe).')
        return 2
    worst = 0
    for i, (game, exe_path) in enumerate(games):
        if i:
            print('\n' + '=' * 110 + '\n')
        worst = max(worst, check_game(args, here, game, exe_path))
    return worst


def check_game(args, here, game, exe_path):
    """Checks (and with --write repairs) one game. Returns 0 OK, 1 needs attention, 2 plugin inactive."""
    plugins = os.path.join(os.path.dirname(exe_path), 'plugins')
    ini_path = args.ini or os.path.join(plugins, INI)
    exe = Exe(exe_path)
    installed_dll = os.path.join(plugins, DLL)
    builtins, builtins_from = builtin_signatures(
        [args.dll, installed_dll, os.path.join(here, '..', DLL), os.path.join(here, '..', 'bin', DLL)],
        os.path.join(here, '..', 'src', 'signatures.cpp'))
    if not builtins:
        print(f'Could not find {DLL} (or the source) to read the built-in signatures from. Use --dll.')
        return 2

    cfg = configparser.ConfigParser(inline_comment_prefixes=(';',), strict=False)
    cfg.optionxform = str
    if os.path.exists(ini_path):
        cfg.read(ini_path, encoding='utf-8')
    ini_build = cfg.get('Signatures', 'GameBuild', fallback='')
    overrides = {k: v for k, v in (cfg.items('Signatures') if cfg.has_section('Signatures') else [])
                 if k in SIGNATURES and v.strip()}
    use_overrides = bool(ini_build) and ini_build == exe.build_id

    print(f'Game       : {game}  {exe_path}')
    print(f'             build {exe.build_id}, modified '
          f'{datetime.datetime.fromtimestamp(os.path.getmtime(exe_path)):%Y-%m-%d %H:%M}')
    print(f'Plugin     : {installed_dll}' + ('' if os.path.exists(installed_dll) else ' (not installed in this game)'))
    print(f'Built-ins  : {os.path.abspath(builtins_from)}')
    print(f'Config     : {os.path.abspath(ini_path)}' + ('' if os.path.exists(ini_path) else ' (does not exist)'))
    if overrides and not use_overrides:
        print(f'             has {len(overrides)} override(s) for game build {ini_build or "?"} - ignored for this build')
    elif overrides:
        print(f'             has {len(overrides)} override(s) for this build')
    print()

    known_path = os.path.join(here, 'known_good.json')
    known = json.load(open(known_path)) if os.path.exists(known_path) else {}
    for name, ops in SIGNATURES.items():   # first run: values found for 1.61
        known.setdefault(name, {v: KNOWN_161[v] for v, _, _, k in ops if v in KNOWN_161})
    new_known = {}
    effective_overrides = dict(overrides) if use_overrides else {}
    repairs, broken = {}, []
    for name in SIGNATURES:
        source = 'ini' if name in effective_overrides else 'built-in'
        sig = effective_overrides.get(name) or builtins.get(name)
        if not sig:
            print(f'[MISSING   ] {name}: no built-in signature in this plugin build')
            broken.append(name)
            continue
        pat = parse(sig)
        hits = find_all(exe.text, pat)
        if hits and unique_enough(exe, name, pat, hits):
            values, problems = extract(exe, name, hits[0])
            status = 'OK' if not problems else 'SUSPICIOUS'
            print(f'[{status:10}] {name:17} ({source}) ' + ', '.join(
                f'{k}=0x{v:X}' + ('' if KNOWN_161.get(k, v) == v else f' (1.61: 0x{KNOWN_161[k]:X})')
                for k, v in values.items()))
            for p in problems:
                print(f'             ! {p}')
            if problems:
                broken.append(name)
            else:
                new_known[name] = values
            continue

        why = 'no longer matches' if not hits else 'matches more than once'
        print(f'[BROKEN    ] {name:17} ({source}) {why} - searching for the new location...')
        results = fuzzy_find(exe, name, pat, known.get(name, {}))
        good = [r for r in results if not r[3]]
        chosen = None
        if good and good[0][0] >= 0.75:
            best = good[0]
            ties = [r for r in good if best[0] - r[0] < 0.05]
            same = all(r[2] == best[2] for r in ties)
            print(f'             best candidate exe+0x{exe.text_va + best[1]:X}: match score {best[0]:.2f} '
                  f'(1.00 = identical bytes, +0.10 when the offsets are unchanged), '
                  f'{len(ties)} candidate(s) within 5%' + (' (all give the same values)' if len(ties) > 1 and same else ''))
            # several equally good candidates are only acceptable for data-only signatures
            if len(ties) == 1 or (same and name in SAME_VALUES_OK):
                chosen = best
        elif results:
            print(f'             best candidate only scores {results[0][0]:.2f} - too different to trust')
        new = repair(exe, name, pat, chosen[1]) if chosen else None
        if new:
            if chosen[2]:
                print('             values: ' + ', '.join(f'{k}=0x{v:X}' for k, v in chosen[2].items()))
            print(f'             repaired signature: {fmt(new)}')
            repairs[name] = fmt(new)
            new_known[name] = dict(chosen[2])
            continue
        print('             could not repair automatically')
        broken.append(name)

    # cross-signature consistency
    sk = new_known.get('StartActivation', {}).get('slot_type')
    pk = new_known.get('PerformActivation', {}).get('slot_type_2')
    if sk is not None and pk is not None and sk != pk:
        print(f'             ! slot_type differs between StartActivation (0x{sk:X}) and PerformActivation (0x{pk:X})')
        broken.append('PerformActivation')

    print()
    fatal = [b for b in broken if b in REQUIRED]
    if repairs:
        if args.write:
            # keep still-valid overrides for this build and add the repairs; a stale section is replaced
            to_write = dict(effective_overrides)
            to_write.update(repairs)
            backup = write_overrides(ini_path, exe.build_id, to_write)
            print(f'Wrote {len(repairs)} repaired signature(s) for build {exe.build_id} to the ini'
                  + (f' (backup: {os.path.basename(backup)}).' if backup else '.'))
        else:
            print(f'{len(repairs)} signature(s) can be repaired. Run again with --write to store them.')
    if new_known and (args.write or not repairs):
        merged = dict(known)
        merged.update({k: {n: v for n, v in vals.items() if not n.startswith('icon_')} for k, vals in new_known.items()})
        json.dump(merged, open(known_path, 'w'), indent=1)
    for b in dict.fromkeys(broken):
        print(f'NEEDS MANUAL WORK: {b}' + (f' ({OPTIONAL_NOTE[b]})' if b in OPTIONAL_NOTE else ' (plugin stays inactive)'))
    if not broken and not repairs:
        print('Everything matches - the plugin supports this game version as is.')
    if fatal:
        print('\nThe game code changed shape - see "After a game update" in README.md.')
    return 2 if fatal else (1 if broken or (repairs and not args.write) else 0)


if __name__ == '__main__':
    sys.exit(main())
