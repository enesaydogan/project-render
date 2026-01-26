
b = 0
with open('src/main.cpp', 'r') as f:
    for line_num, line in enumerate(f, 1):
        for char in line:
            if char == '{':
                b += 1
            elif char == '}':
                b -= 1
        if line_num in [1671, 1777]:
             print(f"Line {line_num}: balance {b}")
print(f"Final balance: {b}")
