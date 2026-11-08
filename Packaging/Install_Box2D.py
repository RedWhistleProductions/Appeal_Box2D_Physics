"""Download pinned portable Box2D sources and build a local static library."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import hashlib,json,os,platform,subprocess,tarfile,urllib.request
base=Path(__file__).resolve().parents[1];deps=base/'.deps/box2d';deps.mkdir(parents=True,exist_ok=True)
tag='v3.2.0'
commit='178495ba35695aefe9c321e569d2277f06db3b90'
sha='f226d0684c46d06ddbc947c2ab33758cb220eca6624549778250e94f845097c9'
url=f'https://codeload.github.com/erincatto/box2d/tar.gz/{commit}'
archive=deps/'box2d.tar.gz'
if not archive.exists():archive.write_bytes(urllib.request.urlopen(url,timeout=60).read())
if hashlib.sha256(archive.read_bytes()).hexdigest()!=sha:raise RuntimeError('Box2D archive checksum mismatch')
source=deps/'source';source.mkdir(exist_ok=True)
manifest=deps/'SOURCE.json'
installed=json.loads(manifest.read_text()) if manifest.exists() else {}
if installed.get('commit')!=commit or not (source/'include/box2d/box2d.h').exists():
 with tarfile.open(archive) as tar:
     for item in tar.getmembers():
         if not (item.isdir() or item.isfile()):continue
         parts=Path(item.name).parts[1:]
         if not parts:continue
         target=source.joinpath(*parts).resolve()
         if not target.is_relative_to(source.resolve()):raise RuntimeError('Unsafe archive path')
         if item.isdir():target.mkdir(parents=True,exist_ok=True)
         else:
             target.parent.mkdir(parents=True,exist_ok=True)
             with tar.extractfile(item) as stream:target.write_bytes(stream.read())
(deps/'SOURCE.json').write_text(json.dumps({'version':tag,'commit':commit,'url':url,'sha256':hashlib.sha256(archive.read_bytes()).hexdigest()},indent=2))
if '--download-only' not in __import__('sys').argv:
    system=platform.system().lower();out=deps/'lib'/system;out.mkdir(parents=True,exist_ok=True)
    sources=sorted((source/'src').glob('*.c'))
    def compile(file):
        obj=out/(file.stem+'.o')
        command=['gcc','-std=c17','-O2','-DNDEBUG','-I'+str(source/'include'),'-I'+str(source/'src')]
        if os.name!='nt':command+=['-fPIC']
        subprocess.run(command+['-c',str(file),'-o',str(obj)],check=True)
        return str(obj)
    with ThreadPoolExecutor(max_workers=4) as pool:objects=list(pool.map(compile,sources))
    subprocess.run(['ar','rcs',str(out/'libbox2d.a'),*objects],check=True)
    print('Box2D installed:',out)
