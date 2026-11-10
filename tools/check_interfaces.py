"""Verify cross-stage varying locations and preserve baseline interface compatibility."""
from pathlib import Path
import argparse,json,struct
ROOT=Path(__file__).resolve().parents[1]
def locations(path):
    data=path.read_bytes()
    if len(data)%4:raise ValueError('Unaligned SPIR-V')
    words=struct.unpack('<'+str(len(data)//4)+'I',data)
    if words[0]!=0x07230203:raise ValueError('Invalid SPIR-V magic')
    names={};decorations={};offset=5
    while offset<len(words):
        length,opcode=words[offset]>>16,words[offset]&65535
        if not length or offset+length>len(words):raise ValueError('Invalid SPIR-V instruction size')
        args=words[offset+1:offset+length]
        if opcode==5:
            names[args[0]]=struct.pack('<'+str(len(args)-1)+'I',*args[1:]).split(b'\0')[0].decode()
        elif opcode==71 and len(args)>=3 and args[1]==30:
            decorations[args[0]]=args[2]
        offset+=length
    return {names.get(key,str(key)):value for key,value in decorations.items()}

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--input',type=Path,default=ROOT/'analysis/translation-probe/spirv')
    parser.add_argument('--report',type=Path,default=ROOT/'analysis/translation-probe/interface-validation.json')
    args=parser.parse_args()
    out=ROOT/'analysis/translation-probe'
    fog=[];comparisons=0;changed=[]
    for path in sorted(args.input.glob('*.spv')):
        current=locations(path)
        for name,value in current.items():
            if 'FOG0' in name.upper():
                if value!=28:raise ValueError('Fog varying location mismatch')
                fog.append(dict(shader=path.name,name=name,location=value))
            if 'POINTCOORD' in name.upper() and value!=23:raise ValueError('Point coordinate location moved')
            if 'POINTSIZE' in name.upper() and value!=24:raise ValueError('Point size location moved')
        baseline=out/'baseline-spirv'/path.name
        if baseline.exists():
            old=locations(baseline)
            for name,value in old.items():
                if current.get(name)!=value:changed.append(dict(shader=path.name,name=name,before=value,after=current.get(name)))
            comparisons+=1
    if changed:raise ValueError('Existing varying location moved: '+str(changed[:3]))
    if not any(row['shader'].startswith('vs_') for row in fog) or not any(row['shader'].startswith('ps_') for row in fog):
        raise ValueError('Missing actual vertex/pixel Fog0 proof')
    report=dict(success=True,existing_shader_interfaces_preserved=comparisons,fog_varyings=fog,
                fog_location=28,point_coordinate_location=23,point_size_location=24,
                limitation='Interface consistency and compiler acceptance; no physical rendering proof')
    args.report.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS:',comparisons,'baseline interfaces preserved;',len(fog),'Fog0 declarations use location 28')
if __name__=='__main__':main()
