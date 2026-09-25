#!/usr/bin/env python3
"""Generate src/game/PlayerAiMethods.inc: the private method declarations of PlayerAi, from its .cpp definitions."""
import re,glob,os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'game'))
public={'PlayerAi','Reset','InitializeAtLoad','InitializeAtSpawn','ProcessInput','Process','OnTakeDamage','InitializeGlobals','UpdateVisibilityAndGlobals','pathDescription','IsPopulated'}
static={'UpdateVisibility','UpdateGlobals','RemovePlayerFromGlobals','GetBeamType','GetWeaponIndex','TargetPosition','GetClosestNodeInList'}
defaults={'FindClosestPopulatedItemSpawnOfTypeToPosition':('ItemType type2, ItemType type3','ItemType type2 = kItemNone, ItemType type3 = kItemNone'),'FindClosestItemOfTypeToPosition':('ItemType type2, ItemType type3','ItemType type2 = kItemNone, ItemType type3 = kItemNone'),'PressButton':('int frames','int frames = 0'),'PressL':('int frames','int frames = 0')}
decls=[]; seen=set()
for f in sorted(glob.glob('PlayerAi*.cpp')):
    s=open(f).read()
    for m in re.finditer(r'^FUNC3\((\w+)\)',s,re.M):
        n=m.group(1)
        if n not in seen: seen.add(n); decls.append(f'    int {n}(AiContext& context, const AiPersonalityData5& param);')
    for m in re.finditer(r'^([A-Za-z_][\w:<>,\*& ]*?)\s*\bPlayerAi::(\w+)\(([^)]*)\)(\s*const)?\s*\n?\{',s,re.M):
        ret,name,params,const=m.group(1).strip(),m.group(2),m.group(3),m.group(4) or ''
        if name in public or name in seen: continue
        if 'AiEntityRefs' in ret: continue
        seen.add(name)
        params=' '.join(params.split())
        ret=ret.replace('PlayerAi::','')
        if name in defaults: params=params.replace(*defaults[name])
        pre='static ' if name in static else ''
        decls.append(f'    {pre}{ret} {name}({params}){const.strip() and " const" or ""};')
open('PlayerAiMethods.inc','w').write('// Generated from the PlayerAi*.cpp definitions (tools/gen_ai_decls.py).\n'+'\n'.join(decls)+'\n')
print(len(decls))
