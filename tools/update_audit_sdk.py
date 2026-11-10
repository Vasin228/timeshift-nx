path = 'tools/audit_private_trees.py'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

target = "'include/rex/audio/xma/context.h': '3cbe892635b066fafcaaceae7e9e7d10675f1a2416d3f97a2fd74dbaca7a7fed',"
replacement = ("'include/rex/audio/xma/context.h': '3cbe892635b066fafcaaceae7e9e7d10675f1a2416d3f97a2fd74dbaca7a7fed',\n"
               "    'src/ui/switch/rex_app_switch.cpp': '372815ee0b9e0ab30b0e0a6610e0407e37b2e283cc56f3b3022f5c6a27b84c5e',")

assert target in text, 'Target not found'
text = text.replace(target, replacement, 1)

with open(path, 'w', encoding='utf-8') as f:
    f.write(text)

print('Updated audit_private_trees.py successfully!')
