"""Execute the native workload; null/fake runs are protocol conformance, never NPU certification."""
import argparse
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
EXECUTABLE=None


class MonitorContractTests(unittest.TestCase):
    def run_workload(self,*args,expected=0):
        result=subprocess.run([str(EXECUTABLE),*map(str,args)],text=True,capture_output=True,timeout=15)
        self.assertEqual(result.returncode,expected,result.stderr+'\n'+result.stdout)
        return [json.loads(line) for line in result.stdout.splitlines() if line.strip()]

    def model(self,name='kws01'):
        return ['--profile',name,'--backend','null','--input-manifest',ROOT/f'profiles/{name}/manifest.json',
                '--duration','0','--inferences','10','--warmup-inferences','2']

    def test_capabilities_do_not_initialize_missing_runtime(self):
        records=self.run_workload('--capabilities','--runtime-library','/nonexistent','--delegate-library','/nonexistent')
        self.assertEqual(len(records),1)
        self.assertEqual(records[0]['contract_version'],2)
        self.assertEqual(records[0]['workload'],'npu')
        self.assertEqual(records[0]['success_log_min_period_ms'],1000)
        self.assertEqual(set(records[0]['profiles']),{'kws01','ic01','ad01','sww01','framework_smoke'})

    def test_actual_check_count_excludes_warmup(self):
        for interval in (1,2,3,11):
            with self.subTest(interval=interval):
                summary=self.run_workload(*self.model(),'--verify-interval',interval)[-1]
                self.assertEqual(summary['inference_count'],10)
                self.assertEqual(summary['verify_count'],10//interval)
                self.assertEqual(summary['warmup_inferences'],2)
                self.assertEqual(summary['verify_interval'],interval)

    def test_success_sampling_and_rate_limit_do_not_change_checks(self):
        records=self.run_workload(*self.model(),'--success-log-interval','1')
        self.assertEqual(records[-1]['verify_count'],10)
        self.assertLessEqual(len([r for r in records if r['type']=='verify']),1)
        for record in records:
            if record['type']=='verify': self.assertIs(record['pass'],True)
        records=self.run_workload(*self.model(),'--success-log-interval','0')
        self.assertFalse(any(r['type']=='verify' for r in records))
        self.assertEqual(records[-1]['verify_count'],10)

    def test_wrong_golden_failure_is_immediate_even_when_logs_disabled(self):
        records=self.run_workload(*self.model(),'--golden-checksum','deadbeef','--success-log-interval','0',expected=1)
        failures=[r for r in records if r['type']=='verify' and not r['pass']]
        self.assertEqual(len(failures),1)
        self.assertEqual(records[-1]['verify_count'],1)
        self.assertEqual(records[-1]['verify_fail_count'],1)
        self.assertEqual(records[-1]['inference_count'],1)

    def test_summary_only_does_not_hide_failed_verification(self):
        records=self.run_workload(*self.model(),'--golden-checksum','deadbeef','--summary-only',expected=1)
        self.assertEqual(records[0]['type'],'verify')
        self.assertIs(records[0]['pass'],False)

    def test_golden_generation_is_not_a_measured_or_verified_inference(self):
        records=self.run_workload(*self.model(),'--generate-golden')
        self.assertEqual(len([r for r in records if r['type']=='golden']),1)
        self.assertEqual(records[-1]['verify_count'],0)
        self.assertEqual(records[-1]['inference_count'],0)

    def test_verification_disabled_has_zero_actual_checks(self):
        summary=self.run_workload(*self.model(),'--verify-mode','none','--verify-interval','0')[-1]
        self.assertEqual(summary['verify_count'],0)
        self.assertEqual(summary['inference_count'],10)

    def test_all_models_execute_null_contract(self):
        for model in ('kws01','ic01','ad01','sww01'):
            summary=self.run_workload(*self.model(model))[-1]
            self.assertEqual(summary['verify_count'],10)
            self.assertFalse(summary['device_time_valid'])

    def test_strict_config_rejects_duplicate_malformed_alias_and_wrong_types(self):
        documents=['{"duration":1,"duration":2}', '{"duration":true}', '{"duration":"2"}',
                   '{"duration":1,"duration_s":2}', '{"verify_interval":1,"checksum_interval":2}',
                   '{"duration":NaN}', '{"unknown":1}', '{"duration":2 garbage}',
                   '{"summary_only":"false"}', '{"inferences":-1}', '{"verify-interval":1}']
        with tempfile.TemporaryDirectory() as temp:
            for i,doc in enumerate(documents):
                path=Path(temp)/f'{i}.json';path.write_text(doc)
                result=subprocess.run([str(EXECUTABLE),'--config',str(path)],capture_output=True,text=True,timeout=5)
                self.assertNotEqual(result.returncode,0,doc)

    def test_json_escaped_manifest_path_is_decoded(self):
        with tempfile.TemporaryDirectory() as temp:
            config=Path(temp)/'config.json'
            config.write_text(json.dumps({'profile':'kws01','backend':'null','input_manifest':str(ROOT/'profiles/kws01/manifest.json'),
                                         'duration':0,'inferences':2,'warmup_inferences':0}))
            self.assertEqual(self.run_workload('--config',config)[-1]['verify_count'],2)

    def test_fake_delegate_runs_are_only_protocol_evidence(self):
        suffix='npu-fake-tflite-runtime.dll' if os.name=='nt' else 'libnpu-fake-tflite-runtime.so'
        library=EXECUTABLE.parent/suffix
        if not library.exists(): self.skipTest('fake runtime is not built')
        for model in ('kws01','ic01','ad01','sww01'):
            records=self.run_workload(*self.model(model),'--backend','tflite_delegate','--runtime-library',library,'--delegate-library',library)
            self.assertEqual(records[-1]['verify_count'],10)
            self.assertEqual(records[-1]['backend'],'tflite_delegate')


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--executable',type=Path,required=True)
    args,rest=parser.parse_known_args()
    EXECUTABLE=args.executable.resolve(strict=True)
    unittest.main(argv=[__file__,*rest])
