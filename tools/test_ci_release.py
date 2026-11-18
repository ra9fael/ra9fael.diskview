"""Regression tests for the QML release identity and event contract."""
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import ci_release as release


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.manifest = release.manifest()
        self.version = self.manifest['version']
        self.tag = 'v' + self.version
        self.sha = '1' * 40
        for name in ('CHANGELOG.md', 'CHANGELOG.en.md'):
            (self.root / name).write_text(f'## [{self.version}]\n\n### Fixed\n- QML release.\n', encoding='utf-8')
        self.event_path = self.root / 'event.json'
        self.output = self.root / 'output'
        self.addCleanup(patch.stopall)
        patch.object(release, 'ROOT', self.root).start()
        self.run = patch.object(release.subprocess, 'run').start()
        self.git = patch.object(release, 'git', side_effect=self.git_result).start()
        patch.dict(os.environ, GITHUB_EVENT_NAME='push', GITHUB_EVENT_PATH=str(self.event_path),
                   GITHUB_REF_TYPE='tag', GITHUB_OUTPUT=str(self.output)).start()

    def git_result(self, *args):
        if args[0] == 'ls-remote':
            return f'{self.sha}\trefs/tags/{self.tag}'
        if args[0] == 'log':
            return 'ci(qml): publish the QML variant'
        return self.sha

    def validate(self, **event):
        self.event_path.write_text(json.dumps(dict(created=True, deleted=False,
                                                   ref='refs/tags/' + self.tag, **event)), encoding='utf-8')
        with contextlib.redirect_stdout(io.StringIO()):
            release.validate()

    def test_qml_manifest_not_native(self):
        self.assertEqual(release.manifest()['id'], 'ra9fael.diskview.qml')
        self.validate()
        self.assertIn('version=' + self.version, self.output.read_text())
        self.run.assert_called_once_with(
            ['git', 'merge-base', '--is-ancestor', self.sha, 'origin/qml'], cwd=self.root, check=True)

    def test_native_tag_rejected(self):
        self.event_path.write_text(json.dumps(dict(created=True, deleted=False, ref='refs/tags/v0.3.0')))
        with self.assertRaisesRegex(ValueError, 'version tag'):
            release.validate()

    def test_deleted_tag_rejected(self):
        self.event_path.write_text(json.dumps(dict(created=False, deleted=True, ref='refs/tags/' + self.tag)))
        with self.assertRaisesRegex(ValueError, 'newly created'):
            release.validate()

    def test_dispatch_dry_run_does_not_require_remote_tag(self):
        os.environ['GITHUB_EVENT_NAME'] = 'workflow_dispatch'
        self.event_path.write_text(json.dumps({'inputs': {'source_ref': 'qml', 'publish': False}}))
        release.validate()
        self.assertFalse(any(call.args[0] == 'ls-remote' for call in self.git.call_args_list))
        self.assertIn('publish=false', self.output.read_text())

    def test_remote_mismatch_rejected(self):
        self.git.side_effect = lambda *args: '2' * 40 + '\trefs/tags/' + self.tag if args[0] == 'ls-remote' else self.sha
        with self.assertRaisesRegex(ValueError, 'Remote version tag'):
            self.validate()

    def test_package_identity(self):
        package = self.root / f'{self.manifest["id"]}-{self.version}.wsplugin'
        with zipfile.ZipFile(package, 'w') as archive:
            archive.writestr('plugin.json', json.dumps(self.manifest))
            for name in ('diskview-qml.exe', 'DiskViewQmlApp.dll', 'app.ico',
                         'qml/QtQuick/Controls/qmldir', 'qml/QtQuick/Controls/qtquickcontrols2plugin.dll',
                         'qml/QtQuick/Controls/plugins.qmltypes'):
                archive.writestr(name, b'test')
        package.with_suffix('.wsplugin.sha256').write_text(hashlib.sha256(package.read_bytes()).hexdigest())
        release.verify_package(self.root)
        self.assertTrue((self.root / 'release-notes.md').is_file())
        with zipfile.ZipFile(package, 'a') as archive:
            archive.writestr('diskview.exe', b'native')
        package.with_suffix('.wsplugin.sha256').write_text(hashlib.sha256(package.read_bytes()).hexdigest())
        with self.assertRaisesRegex(ValueError, 'Native executable'):
            release.verify_package(self.root)


if __name__ == '__main__':
    unittest.main()
