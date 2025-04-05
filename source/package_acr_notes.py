import os
import sys

sums_file_name, version, attest256, attest512 = sys.argv[1:]

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
    file_name = 'acr_{}-{}.zip'.format(version, base)
    print('| {: <18} | {: <32} |'.format(target, file_name))

print('')
print(os.path.basename(sums_file_name))
print('```hash')
print(sums_file_content, end='```')
print('')
print('Build provenance attestations')
print('')
print('- SHA256: ' + attest256)
print('- SHA512: ' + attest512)
