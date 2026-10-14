import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser(description='Optionally regenerate the checked-in import names from a local resolution report.')
parser.add_argument('report', type=Path)
parser.add_argument('--out', type=Path, default=Path(__file__).resolve().parents[1]/'import_names.inc')
args = parser.parse_args()
data = json.loads(args.report.read_text())
rows = []
for item in data['imports']:
    name = item['names'][0] if len(item['names']) == 1 else '<unknown disc-map function>'
    rows.append('{'+json.dumps(item['encoded'])+','+json.dumps(name)+'},')
args.out.write_text('\n'.join(rows)+'\n')
