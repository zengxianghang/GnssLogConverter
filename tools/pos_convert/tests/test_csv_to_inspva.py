#!/usr/bin/env python3
"""Regression tests for pos_convert CSV/TSV -> OEM7 INSPVAA mode.

Usage: python3 test_csv_to_inspva.py <pos_convert_executable>
"""
from __future__ import annotations
import datetime as dt
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

EXE = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else None
sys.argv[:] = sys.argv[:1]
HEAD = [
    'TOW', 'UTC', 'Leap', 'Q', 'Lat', 'Lon', 'Height',
    'Pos_x', 'Pos_y', 'Pos_z', 'Vel_x', 'Vel_y', 'Vel_z',
    'Vel_e', 'Vel_n', 'Vel_u', 'Heading'
]


def make_row(tow, utc='12:00:00', leap='18', q='1', **kwargs):
    values = dict(TOW=f'{tow:.9f}', UTC=utc, Leap=str(leap), Q=q,
                  Lat='22.5', Lon='113.6', Height='89.1234',
                  Pos_x='-2400000', Pos_y='5000000', Pos_z='2400000',
                  Vel_x='7', Vel_y='8', Vel_z='9', Vel_e='1.1',
                  Vel_n='2.2', Vel_u='-0.3', Heading='-105.5')
    values.update(kwargs)
    return [str(values[k]) for k in HEAD]


def reference_crc(payload):
    crc = 0
    for b in payload:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
    return crc


def parse_inspva(record):
    assert record.startswith('#INSPVAA,')
    data, crc = record[1:].rsplit('*', 1)
    assert reference_crc(data.encode('ascii')) == int(crc, 16)
    head, body = data.split(';')
    return head.split(','), body.split(',')


class CsvConversionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)

    def write_csv(self, rows, delimiter=',', bom=False, header=None):
        p = self.root / ('input.tsv' if delimiter == '\t' else 'input.csv')
        with p.open('w', encoding='utf-8-sig' if bom else 'utf-8', newline='') as f:
            f.write(delimiter.join(header or HEAD) + '\r\n')
            for row in rows:
                f.write(delimiter.join(row) + '\r\n')
        return p

    def run_converter(self, input_path, *opts, expect=0):
        out = self.root / 'output.inspva'
        proc = subprocess.run([str(EXE), '--csv-to-inspva', str(input_path), str(out), *map(str, opts)],
                              capture_output=True, text=True)
        self.assertEqual(proc.returncode, expect, proc.stdout + '\n' + proc.stderr)
        records = out.read_text(encoding='ascii').splitlines() if out.exists() else []
        return proc, records

    def test_csv_fields_and_crc(self):
        p = self.write_csv([make_row(99000.0), make_row(99000.1, q='0')])
        _, records = self.run_converter(p, '--week', '2362')
        self.assertEqual(len(records), 2)
        head, body = parse_inspva(records[0])
        self.assertEqual(head[5], '2362')
        self.assertEqual(float(head[6]), 99000)
        self.assertEqual(body[:5], ['2362', '99000.000000000', '22.50000000000',
                                    '113.60000000000', '89.1234'])
        self.assertEqual(list(map(float, body[5:8])), [2.2, 1.1, -0.3])
        self.assertEqual(list(map(float, body[8:11])), [0.0, 0.0, 254.5])
        self.assertEqual(body[11], 'INS_SOLUTION_GOOD')
        self.assertEqual(parse_inspva(records[1])[1][11], 'INS_INACTIVE')

    def test_tsv_bom_and_explicit_attitude(self):
        p = self.write_csv([make_row(123.456)], '\t', bom=True)
        _, records = self.run_converter(p, '--week', '2362', '--roll', '-1.5', '--pitch', '2.5')
        body = parse_inspva(records[0])[1]
        self.assertEqual([float(x) for x in body[8:11]], [-1.5, 2.5, 254.5])

    def test_date_and_leap_infer_week(self):
        utc = dt.datetime(2026, 11, 27, 10, 0, 0, tzinfo=dt.timezone.utc)
        seconds = (utc - dt.datetime(1980, 1, 6, tzinfo=dt.timezone.utc)).total_seconds() + 18
        week, tow = divmod(seconds, 604800)
        p = self.write_csv([make_row(tow, utc='2026-11-27 10:00:00', leap='18')])
        _, records = self.run_converter(p)
        head, body = parse_inspva(records[0])
        self.assertEqual(int(body[0]), int(week))
        self.assertAlmostEqual(float(body[1]), tow, places=5)
        self.assertEqual(int(head[5]), int(week))

    def test_missing_week_without_date_rejected(self):
        p = self.write_csv([make_row(100.0)])
        self.run_converter(p, expect=1)
        self.assertFalse((self.root / 'output.inspva').exists())

    def test_mismatch_between_utc_date_and_tow_rejected(self):
        p = self.write_csv([make_row(100.0, utc='2026-11-27 10:00:00', leap='18')])
        self.run_converter(p, expect=3)

    def test_rate_auto_100_to_10(self):
        p = self.write_csv([make_row(100.0 + i * 0.01) for i in range(101)])
        _, records = self.run_converter(p, '--week', '2447', '--rate', '10')
        self.assertEqual(len(records), 11)
        self.assertAlmostEqual(float(parse_inspva(records[-1])[1][1]), 101.0, places=6)

    def test_rate_manual_nondivisor_rejected(self):
        p = self.write_csv([make_row(100 + i * .01) for i in range(10)])
        self.run_converter(p, '--week', '2447', '--rate', '100', '30', expect=1)

    def test_rollover_with_week_option(self):
        p = self.write_csv([make_row(604799.9), make_row(0.0), make_row(0.1)])
        _, records = self.run_converter(p, '--week', '2400')
        self.assertEqual([int(parse_inspva(x)[1][0]) for x in records], [2400, 2401, 2401])

    def test_missing_csv_column_rejected(self):
        p = self.write_csv([], header=HEAD[:-1])
        self.run_converter(p, '--week', '2400', expect=1)

    def test_ignored_cartesian_columns_may_be_blank(self):
        p = self.write_csv([make_row(900, Pos_x='', Pos_y='', Pos_z='',
                                     Vel_x='', Vel_y='', Vel_z='')])
        _, records = self.run_converter(p, '--week', '2362')
        self.assertEqual(len(records), 1)
        self.assertEqual([float(x) for x in parse_inspva(records[0])[1][5:8]],
                         [2.2, 1.1, -0.3])

    def test_quoted_csv_and_reordered_header(self):
        import csv
        p = self.root / 'quoted.csv'
        new_head = list(reversed(HEAD))
        row = dict(zip(HEAD, make_row(98.5)))
        row['UTC'] = '03:30:00.00'
        with p.open('w', encoding='utf-8', newline='') as f:
            w = csv.writer(f, quoting=csv.QUOTE_ALL)
            w.writerow(new_head)
            w.writerow([row[k] for k in new_head])
        _, records = self.run_converter(p, '--week', '2362')
        self.assertEqual(float(parse_inspva(records[0])[1][1]), 98.5)

    def test_malformed_row_reports_nonzero_but_writes_valid_rows(self):
        rows = [make_row(100.0), make_row(100.1, Lat='not-a-number'),
                make_row(100.2)]
        p = self.write_csv(rows)
        _, records = self.run_converter(p, '--week', '2362', expect=3)
        self.assertEqual(len(records), 2)

    def test_first_row_reports_bad_column_value_and_line(self):
        p = self.write_csv([make_row(100, Lat='bad-latitude')])
        proc, records = self.run_converter(p, '--week', '2362', expect=3)
        self.assertEqual(records, [])
        self.assertIn('file line 2', proc.stderr)
        self.assertIn('Lat', proc.stderr)
        self.assertIn('bad-latitude', proc.stderr)

    def test_first_row_reports_column_count_mismatch(self):
        row = make_row(100.0)
        p = self.write_csv([row + ['unexpected-extra-value']])
        proc, records = self.run_converter(p, '--week', '2362', expect=3)
        self.assertEqual(records, [])
        self.assertIn('expected 17, got 18', proc.stderr)
        self.assertIn('file line 2', proc.stderr)

    def test_first_row_reports_utc_tow_disagreement(self):
        p = self.write_csv([make_row(100, utc='2026-11-27 10:00:00')])
        proc, _ = self.run_converter(p, expect=3)
        self.assertIn('UTC + Leap gives TOW', proc.stderr)
        self.assertIn('CSV TOW', proc.stderr)

    def test_first_row_reports_invalid_q(self):
        p = self.write_csv([make_row(100, q='3.25')])
        proc, _ = self.run_converter(p, '--week', '2362', expect=3)
        self.assertIn('Q must be an integer', proc.stderr)

    def test_blank_lines_before_first_record(self):
        p = self.write_csv([make_row(100.0)])
        txt = p.read_text(encoding='utf-8')
        header, rest = txt.split('\n', 1)
        p.write_text(header + '\n   \t   \n' + rest, encoding='utf-8')
        _, records = self.run_converter(p, '--week', '2362')
        self.assertEqual(len(records), 1)

    def test_later_malformed_row_reports_specific_line(self):
        p = self.write_csv([make_row(100), make_row(100.1, Vel_n='bad'),
                            make_row(100.2)])
        proc, records = self.run_converter(p, '--week', '2362', expect=3)
        self.assertEqual(len(records), 2)
        self.assertIn('file line 3', proc.stderr)
        self.assertIn('Vel_n', proc.stderr)

    def test_non_csv_legacy_help(self):
        for opt in ('-h', '--help'):
            proc = subprocess.run([str(EXE), opt], capture_output=True, text=True)
            self.assertEqual(proc.returncode, 0)
            self.assertIn('--csv-to-inspva', proc.stdout)


if __name__ == '__main__':
    if EXE is None or not EXE.is_file():
        print('Usage: test_csv_to_inspva.py <pos_convert_executable>', file=sys.stderr)
        sys.exit(2)
    unittest.main()
