"""Install an official Qt kit when aqt cannot read the nested repository layout.

Archives are selected from Qt's metadata and checked before extraction. The
destination is the kit directory, since these archives contain its contents.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import time
import urllib.request
import xml.etree.ElementTree as ET


def fetch(url):
    for attempt in range(4):
        try:
            with urllib.request.urlopen(url, timeout=120) as response:
                return response.read()
        except OSError:
            if attempt == 3:
                raise
            time.sleep(2 ** attempt)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', default='6.11.2')
    parser.add_argument('--kit', required=True, choices=['msvc2022_64', 'gcc_64', 'gcc_arm64', 'clang_64', 'android_arm64_v8a', 'android_x86_64'])
    parser.add_argument('--root', type=Path, default=Path('C:/Qt'))
    parser.add_argument('--cache', type=Path, default=Path.home() / '.cache/fruity-qt')
    args = parser.parse_args()
    release = 'qt6_' + args.version.replace('.', '')
    android = args.kit.startswith('android_')
    suffix = args.kit.removeprefix('android_') if android else args.kit
    hosts = {'gcc_64': 'linux_x64', 'gcc_arm64': 'linux_arm64', 'clang_64': 'mac_x64'}
    host = hosts.get(args.kit, 'windows_x86')
    platform = 'all_os/android' if android else host + '/desktop'
    nested = release if host != 'windows_x86' and not android else release + '_' + suffix
    base = f'https://download.qt.io/online/qtsdkrepository/{platform}/{release}/{nested}'
    package_suffix = 'win64_' + args.kit if args.kit == 'msvc2022_64' else 'linux_' + args.kit if args.kit.startswith('gcc_') else args.kit
    package_name = 'qt.qt6.' + args.version.replace('.', '') + '.' + package_suffix
    metadata = ET.fromstring(fetch(base + '/Updates.xml'))
    package = next(p for p in metadata.findall('PackageUpdate') if p.findtext('Name') == package_name)
    destination = args.root / args.version / args.kit
    destination.mkdir(parents=True, exist_ok=True)
    args.cache.mkdir(parents=True, exist_ok=True)
    extractor = shutil.which('7z') or shutil.which('7zz') or 'C:/Program Files/7-Zip/7z.exe'
    for archive in package.findtext('DownloadableArchives').split(','):
        archive = archive.strip()
        if archive.startswith('qtdoc'):
            continue
        url = f"{base}/{package_name}/{package.findtext('Version')}{archive}"
        expected = fetch(url + '.sha1').decode().split()[0]
        path = args.cache / archive
        if not path.exists() or hashlib.sha1(path.read_bytes()).hexdigest() != expected:
            print('Downloading', archive, flush=True)
            path.write_bytes(fetch(url))
        if hashlib.sha1(path.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f'Checksum mismatch: {archive}')
        # The ICU archive is flat, unlike the Qt archives' kit-relative trees.
        archive_destination = destination / 'lib' if archive.startswith('icu-') else destination
        archive_destination.mkdir(parents=True, exist_ok=True)
        subprocess.run([extractor, 'x', '-y', f'-o{archive_destination}', str(path)], check=True, stdout=subprocess.DEVNULL)
        print('Installed', archive, flush=True)
    print(destination, flush=True)


if __name__ == '__main__':
    main()
