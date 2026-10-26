import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('build_entry',ROOT/'scripts/build.py');build=importlib.util.module_from_spec(spec);spec.loader.exec_module(build)

class BuildEntryTests(unittest.TestCase):
    def test_components_are_explicit_and_deduplicated(self):
        self.assertEqual(build.selected('cpu,npu,cpu'),['cpu','npu'])
        self.assertIn('relay',build.selected('all'))
        with self.assertRaises(ValueError):build.selected('invalid')

    def test_missing_ndk_cannot_fall_back_to_host(self):
        args=build.parser().parse_args(['--target','android-arm64'])
        with patch.dict('os.environ',{},clear=True),self.assertRaises(ValueError):build.ndk_paths(args)

    def test_cross_target_never_executes_host_tests(self):
        args=build.parser().parse_args(['--target','ohos-arm64','--test'])
        with self.assertRaises(ValueError):build.Build(args)

    def test_dry_run_creates_planned_manifest_without_success_or_binary(self):
        with tempfile.TemporaryDirectory(prefix='build with spaces ') as temp:
            args=build.parser().parse_args(['--components','cpu','--engine','direct','--compiler','not-executed','--dry-run','--output-root',temp])
            item=build.Build(args)
            with patch('subprocess.run',side_effect=AssertionError('dry-run must not invoke subprocess')):
                self.assertEqual(item.run(),0)
            import json
            record=json.loads((item.out/'build-manifest.json').read_text(encoding='utf-8'))
            self.assertEqual(record['status'],'planned')
            self.assertNotIn('sha256',record['components']['cpu'])

    def test_workload_verify_headers_are_identical_for_all_three_targets(self):
        import hashlib
        hashes=[hashlib.sha256((ROOT/f'{k}workload/include/avs/verify_schedule.h').read_bytes().replace(b'\r\n',b'\n')).hexdigest() for k in ('cpu','gpu','npu')]
        self.assertEqual(len(set(hashes)),1)
