import os

def read_file(path):
    with open(path, 'rb') as f:
        return f.read()

def to_c_array(name, data):
    lines = []
    lines.append(f'const uint8_t {name}[] = {{')
    s = ""
    for i, byte in enumerate(data):
        s += f"0x{byte:02x}, "
        if (i + 1) % 12 == 0:
            lines.append("  " + s.strip())
            s = ""
    if s:
        lines.append("  " + s.strip())
    lines.append('};')
    lines.append(f'const uint64_t {name}_len = {len(data)};')
    return "\n".join(lines)

files = [
    ('user_image_hello', 'build-arm/hello'),
    ('user_image_init', 'build-arm/init'),
    ('user_image_shell', 'build-arm/shell'),
]

dummies = [
    'user_image_echo', 'user_image_ls', 'user_image_cat', 'user_image_fuzz'
]

# Just output the arrays
for name, path in files:
    try:
        data = read_file(path)
        print(to_c_array(name, data))
        print("")
    except FileNotFoundError:
        print(f"/* {path} not found */")
        # Fallback to dummy
        print(f'const uint8_t {name}[] = {{ 0 }};')
        print(f'const uint64_t {name}_len = 0;')
        print("")

for name in dummies:
    print(f'const uint8_t {name}[] = {{ 0 }};')
    print(f'const uint64_t {name}_len = 0;')
    print("")
