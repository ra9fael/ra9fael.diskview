"""Validate QML release source identity and packaged artifacts (stdlib only)."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / 'qmlapp' / 'plugin.json'


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True, encoding='utf-8').strip()


def manifest():
    value = json.loads(MANIFEST.read_text(encoding='utf-8-sig'))
    if (value['id'] != 'ra9fael.diskview.qml'
            or value['runtime']['program'] != 'diskview-qml.exe'
            or value['runtime']['qt'] != 'host'
            or value['runtime']['qtVersion'] != '6.12.0'):
        raise ValueError('Expected the host-Qt QML plugin variant')
    return value


def release_notes(version):
    sections = []
    for name in ('CHANGELOG.en.md', 'CHANGELOG.md'):
        text = (ROOT / name).read_text(encoding='utf-8')
        match = re.search(r'^## \[(?:QML )?' + re.escape(version)
                          + r'\][^\n]*\n(.*?)(?=^## |\Z)', text, re.M | re.S)
        if not match or not match[1].strip():
            raise ValueError(f'Missing nonempty {version} section in {name}')
        sections.append(match[1].strip())
    return '\n\n'.join(sections) + '\n'


def validate():
    event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text(encoding='utf-8'))
    name = os.environ['GITHUB_EVENT_NAME']
    if name not in ('push', 'workflow_dispatch'):
        raise ValueError('Only a new version tag or manual dispatch is allowed')
    value = manifest()
    version = value['version']
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('Version must be X.Y.Z')
    inputs = event.get('inputs', {})
    publish = name == 'push' or str(inputs.get('publish', 'false')).lower() == 'true'
    sha = git('rev-parse', 'HEAD')
    tag = f'v{version}'
    if name == 'push':
        if (not event.get('created') or event.get('deleted')
                or os.environ.get('GITHUB_REF_TYPE') != 'tag'
                or not re.fullmatch(r'refs/tags/v\d+\.\d+\.\d+', event.get('ref', ''))):
            raise ValueError('Only newly created version tags can publish automatically')
        if event['ref'] != f'refs/tags/{tag}':
            raise ValueError(f'Publish requires version tag {tag}, got {event["ref"]!r}')
    elif not str(inputs.get('source_ref', '')).strip():
        raise ValueError('source_ref is required')
    if publish:
        if git('rev-parse', '--verify', f'refs/tags/{tag}^{{commit}}') != sha:
            raise ValueError('Checked out source does not match tag')
        remote = git('ls-remote', '--tags', 'origin', f'refs/tags/{tag}', f'refs/tags/{tag}^{{}}')
        refs = dict((line.split()[1], line.split()[0]) for line in remote.splitlines())
        if refs.get(f'refs/tags/{tag}^{{}}', refs.get(f'refs/tags/{tag}')) != sha:
            raise ValueError('Remote version tag does not point to the expected commit')
        subprocess.run(['git', 'merge-base', '--is-ancestor', sha, 'origin/qml'], cwd=ROOT, check=True)
    release_notes(version)
    title = git('log', '-1', '--format=%s')
    if not re.fullmatch(r'(feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert)(\([a-zA-Z0-9_.-]+\))?!?: [\x20-\x7e]+', title):
        raise ValueError('Release source commit must use an English Angular subject')
    values = dict(sha=sha, tag=tag, publish=str(publish).lower(), version=version)
    with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as output:
        for key, item in values.items():
            output.write(f'{key}={item}\n')
    print(f'Validated {value["id"]} {version} at {sha}; publish={publish}')


def verify_package(folder):
    files = list(Path(folder).glob('*.wsplugin'))
    if len(files) != 1:
        raise ValueError('Expected exactly one package')
    package = files[0]
    digest = hashlib.sha256(package.read_bytes()).hexdigest()
    if package.with_suffix('.wsplugin.sha256').read_text(encoding='ascii').split()[0] != digest:
        raise ValueError('Package checksum mismatch')
    expected = manifest()
    if package.name != f'{expected["id"]}-{expected["version"]}.wsplugin':
        raise ValueError('Package filename differs from QML identity')
    with zipfile.ZipFile(package) as archive:
        actual = json.loads(archive.read('plugin.json').decode('utf-8-sig'))
        if actual != expected:
            raise ValueError('Packaged manifest differs from QML source')
        for name in (actual['runtime']['program'], 'DiskViewQmlApp.dll', actual['icon'],
                     'qml/QtQuick/Controls/qmldir', 'qml/QtQuick/Controls/qtquickcontrols2plugin.dll',
                     'qml/QtQuick/Controls/plugins.qmltypes'):
            if not archive.read(name):
                raise ValueError(f'Empty runtime asset {name}')
        if 'diskview.exe' in archive.namelist():
            raise ValueError('Native executable found in QML package')
    (Path(folder) / 'release-notes.md').write_text(release_notes(expected['version']), encoding='utf-8')
    print(f'Verified {package.name}: SHA256 {digest}')


if __name__ == '__main__':
    if len(sys.argv) > 1:
        verify_package(sys.argv[1])
    else:
        validate()
