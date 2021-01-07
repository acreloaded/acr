import os
import sys

sums_file_name, version = sys.argv[1:]

sums_file_content = '<unknown>'
with open(sums_file_name) as f:
    sums_file_content = f.read()

print('''auto-generated

| Target             | File                             |
|--------------------|----------------------------------|''')

for base, target in (
    ('w', 'Windows'),
    ('l', 'Linux'),
    ('serv', 'Server Pack'),
    ('src', 'Source Only (/src)'),
    ('w_bin', 'Windows (Binaries)'),
    ('l_bin', 'Linux (Binaries)'),
):
    file_name = 'acr{}-{}.zip'.format(version, base)
    print('| {: <18} | {: <32} |'.format(target, file_name))

print('')
print(os.path.basename(sums_file_name))
print('```hash')
print(sums_file_content, end='```')
