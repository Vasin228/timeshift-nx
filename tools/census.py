from pathlib import Path
import argparse,hashlib,json,re,struct,zipfile
ap=argparse.ArgumentParser();ap.add_argument('--xex',type=Path,default=Path('D:/Anti/timeshift-recomp/dump360/default.xex'));ap.add_argument('--app-archive',type=Path,default=Path('D:/Anti/timeshift-recomp/frozen/vulkan-part181/actual-app-source.zip'));ap.add_argument('--out',type=Path,default=Path(__file__).resolve().parents[1]/'analysis');a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
b=a.xex.read_bytes()
if b[:4]!=b'XEX2':raise ValueError('Expected XEX2')
count=struct.unpack_from('>I',b,20)[0]
if count>4096 or 24+count*8>len(b):raise ValueError('Invalid optional header bounds')
libraries=[]
for i in range(count):
 key,offset=struct.unpack_from('>II',b,24+i*8)
 if key!=0x200FF:continue
 if offset+4>len(b):raise ValueError('Library table outside input')
 size=struct.unpack_from('>I',b,offset)[0]
 if size<4 or (size-4)%16 or offset+size>len(b):raise ValueError('Invalid library table size')
 for pos in range(offset+4,offset+size,16):
  name=b[pos:pos+8].split(bytes([0]))[0].decode('ascii');major,minor,build,flags=struct.unpack_from('>4H',b,pos+8)
  libraries.append(dict(name=name,major=major,minor=minor,build=build,flags_hex=f'{flags:04X}'))
functions={};video=[];callers={}
pattern=re.compile(r'DEFINE_REX_FUNC\((sub_[0-9A-Fa-f]{8})\)\s*\{')
with zipfile.ZipFile(a.app_archive) as z:
 for name in z.namelist():
  if not re.search(r'_recomp\.\d+\.cpp$',name):continue
  text=z.read(name).decode(errors='replace');matches=list(pattern.finditer(text))
  for i,m in enumerate(matches):
   body=text[m.end():matches[i+1].start() if i+1<len(matches) else len(text)]
   symbol=m[1];calls=sorted(set(re.findall(r'\b(?:__imp__)?(sub_[0-9A-Fa-f]{8}|Vd\w+)\s*\(',body)))
   record=dict(symbol=symbol,file=name,line=text.count('\n',0,m.start())+1,calls=calls)
   functions[symbol]=record
   for callee in calls:callers.setdefault(callee,[]).append(symbol)
   imports=[v for v in calls if v.startswith('Vd')]
   if imports:video.append(dict(symbol=symbol,file=name,line=record['line'],imports=imports,verification='candidate anchor only, not an installed hook'))
for v in video:v['callers']=sorted(callers.get(v['symbol'],[]))
summary=dict(xex_sha256=hashlib.sha256(b).hexdigest(),source_archive_sha256=hashlib.sha256(a.app_archive.read_bytes()).hexdigest(),libraries=libraries,function_count=len(functions),video_anchor_count=len(video),video_anchors=video,source_to_xex_full_image_match_verified=False)
(a.out/'census.json').write_text(json.dumps(summary,indent=2)+'\n')
(a.out/'function-index.json').write_text(json.dumps(functions,separators=(',',':'))+'\n')
print('Functions:',len(functions),'video anchors:',len(video))
print('Libraries:',[(v['name'],f"{v['major']}.{v['minor']}.{v['build']}") for v in libraries])
for v in video:print(v['symbol'],','.join(v['imports']),'callers',','.join(v['callers'][:5]))
