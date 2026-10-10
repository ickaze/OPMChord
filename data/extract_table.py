"""Read the supplied XLSX without spreadsheet dependencies. No pitch fitting."""
import csv
import sys
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

source = Path(sys.argv[1])
output = Path(sys.argv[2])
ns = {'m': 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}
names = ['Major', 'Minor', 'dim', 'aug', 'sus4', '7', 'maj7', 'm7', 'm7b5', 'dim7']
records = []
with zipfile.ZipFile(source) as archive:
    strings = [''.join(element.itertext()) for element in ET.fromstring(
        archive.read('xl/sharedStrings.xml')).findall('m:si', ns)]
    rows = ET.fromstring(archive.read('xl/worksheets/sheet1.xml')).findall('m:sheetData/m:row', ns)
    for row in rows[1:]:
        cells = {}
        for cell in row:
            value = cell.find('m:v', ns)
            if value is not None:
                text = strings[int(value.text)] if cell.get('t') == 's' else value.text
                cells[cell.get('r').rstrip('0123456789')] = text
        if not cells.get('A') or not cells.get('B'):
            continue
        chord = names.index(cells['A'])
        def numeric(column):
            value = cells.get(column, '0')
            return int(value, 16) if value.startswith('0x') else int(float(value))
        count = 3 if chord < 5 else 4
        record = [chord, numeric('B'), numeric('D'), numeric('E'), count]
        record += [numeric(chr(ord('F') + index)) for index in range(12)]
        records.append(record)
assert len(records) == 739
assert len({(r[0], r[1]) for r in records}) == len(records)
output.mkdir(parents=True, exist_ok=True)
with (output / 'chords.csv').open('w', newline='', encoding='utf-8') as file:
    writer = csv.writer(file)
    writer.writerow(['chord_index', 'note', 'KC', 'KF', 'carriers'] +
                    [f'C{c}_{p}' for c in range(1, 5) for p in ['ML', 'DT1', 'DT2']])
    writer.writerows(records)
lines = ['// Generated verbatim from YM2151_ALG_5_6_7_Code_4MHz.xlsx.', '#pragma once',
         '#include <cstdint>', 'namespace chord {',
         'struct PitchRow { int chord, note, kc, kf, count; int carrier[4][3]; };',
         'inline constexpr PitchRow pitchRows[] = {']
for row in records:
    head = ','.join(map(str, row[:5]))
    carriers = ','.join('{' + ','.join(map(str, row[5+i*3:8+i*3])) + '}' for i in range(4))
    lines.append('    {' + head + ',{' + carriers + '}},')
lines += ['};', '}']
(output.parent / 'src' / 'pitch_table.h').write_text('\n'.join(lines) + '\n')
print(f'{len(records)} rows extracted')
