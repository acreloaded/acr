import sys

sums_file_name, version = sys.argv[1:]

HASH_LEN = 64

hashes = {}

with open(sums_file_name) as f:
    for line in f:
        if len(line) >= HASH_LEN + 3:
            hashes[line[HASH_LEN + 2:].rstrip()] = line[:HASH_LEN]

def get_file_name(base, version):
    return 'acr{}-{}.zip'.format(version, base)

print('''auto-generated

| Target             | File                             | SHA-256                                                          |
|--------------------|----------------------------------|------------------------------------------------------------------|''')

for base, target in (
    ('w', 'Windows'),
    ('l', 'Linux'),
    ('serv', 'Server Pack'),
    ('src', 'Source Only (/src)'),
    ('w_bin', 'Windows (Binaries)'),
    ('l_bin', 'Linux (Binaries)'),
):
    file_name = get_file_name(base, version)
    print('| {: <18} | {: <32} | {: <{}} |'.format(
            target,
            file_name,
            hashes.pop(file_name, '?' * HASH_LEN),
            HASH_LEN))

if hashes:
    print('\nExtra:')
    for k in sorted(hashes.keys()):
        print('{} ({})'.format(k, hashes[k]))
