# SPDX-License-Identifier: GPL-2.0-only
import re
from pathlib import Path


root = Path(__file__).resolve().parents[3]
source = (root / 'driver/px4_card.c').read_text(encoding='utf-8')
table = source.split('px4_card_guids[] = {', 1)[1].split('};', 1)[0]
entries = re.findall(r'\{\s*0x([0-9a-f]{4}),\s*"(\{[0-9a-f-]+\})"\s*\}', table)
actual = {int(pid, 16): guid for pid, guid in entries}
assert len(actual) == len(entries), 'Duplicate PID in px4_card_guids'

# INF は同じ PID をアーキテクチャごとに記載しているため、重複をまとめて照合する
expected = {}
for inf in sorted((root / 'winusb/pkg/inf').glob('*.inf')):
    text = inf.read_text(encoding='utf-8-sig')
    pids = set(re.findall(r'USB\\VID_0511&PID_([0-9a-f]{4})', text, re.IGNORECASE))
    guids = set(re.findall(r'DeviceInterfaceGUIDs,[^"\n]*"(\{[0-9a-f-]+\})"', text, re.IGNORECASE))
    assert pids and len(guids) == 1, f'{inf.name}: expected PIDs and one interface GUID'
    guid = guids.pop().lower()
    for pid in pids:
        product = int(pid, 16)
        assert product not in expected or expected[product] == guid, f'{inf.name}: conflicting GUID for PID {pid}'
        expected[product] = guid

assert actual == expected, f'PID/GUID mismatch: kernel-only {actual.items() - expected.items()}, INF-only {expected.items() - actual.items()}'
print(f'check_card_guids: {len(actual)} PID/GUID pairs match the Windows INFs')
