import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import run_corpus

class HarnessTests(unittest.TestCase):
    def test_target_expectations(self):
        accepted=SimpleNamespace(RuntimeEffect=SimpleNamespace(MakeForShader=lambda source:object()))
        def reject(source): raise RuntimeError('error: loop bound must be constant')
        rejected=SimpleNamespace(RuntimeEffect=SimpleNamespace(MakeForShader=reject))
        self.assertIn('loop bound',run_corpus.compile_target(rejected,'source','reject'))
        with self.assertRaises(RuntimeError):run_corpus.compile_target(accepted,'source','reject')
        with self.assertRaises(RuntimeError):run_corpus.compile_target(rejected,'source','accept')
    def test_target_infrastructure_exception_not_rejection(self):
        def broken(source): raise OSError('library unavailable')
        fake=SimpleNamespace(RuntimeEffect=SimpleNamespace(MakeForShader=broken))
        with self.assertRaises(OSError):run_corpus.compile_target(fake,'source','reject')
    def test_manifest_real(self):
        cases=run_corpus.validate_manifest(Path(__file__).parent/'corpus')
        self.assertGreaterEqual(len(cases),40)
    def test_manifest_rejects_missing_and_traversal(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            for name in ('missing.frag','../escape.frag'):
                (root/'manifest.json').write_text(json.dumps([dict(name='x',file=name,expect='accept')]))
                with self.assertRaises(ValueError):run_corpus.validate_manifest(root)
    def test_compiler_noop_rejected(self):
        fake=SimpleNamespace(RuntimeEffect=SimpleNamespace(MakeForShader=lambda source:object()))
        with self.assertRaisesRegex(RuntimeError,'invalid-source'):run_corpus.compiler_controls(fake)
    def test_compiler_valid_control_rejected(self):
        fake=SimpleNamespace(RuntimeEffect=SimpleNamespace(MakeForShader=lambda source:None))
        with self.assertRaisesRegex(RuntimeError,'valid-source'):run_corpus.compiler_controls(fake)
    def test_compiler_controls_success(self):
        def compile(source):
            if 'unknown_identifier' in source:raise RuntimeError('invalid')
            return object()
        run_corpus.compiler_controls(SimpleNamespace(RuntimeEffect=SimpleNamespace(MakeForShader=compile)))
    def test_missing_compiler_is_failure(self):
        with patch.dict('sys.modules',{'skia':None}),patch('sys.argv',['runner','--transpiler','unused']):
            with contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit) as error:run_corpus.main()
            self.assertEqual(error.exception.code,2)
    def test_failed_transpiler_does_not_pass(self):
        case=dict(name='x',file='x.frag',expect='accept')
        with tempfile.TemporaryDirectory() as folder,patch.dict('sys.modules',{'skia':None}),patch('sys.argv',['runner','--transpiler','unused','--allow-missing-skia','--artifacts',folder]),patch.object(run_corpus,'cli_checks'),patch.object(run_corpus,'validate_manifest',return_value=[case]),patch.object(run_corpus.subprocess,'run',return_value=SimpleNamespace(returncode=2,stdout='',stderr='failed')):
            with contextlib.redirect_stdout(io.StringIO()):self.assertTrue(run_corpus.main())

if __name__=='__main__':unittest.main()
