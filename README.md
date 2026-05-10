# X Programming Language 🔥

> Simple like Python, fast like C, safe like Rust!

```x
name = input("Your name: ")
print("Hello", name)
```

---

## Install

```bash
git clone https://github.com/Natarizki/xlang.git
cd xlang
make
make install
```

---

## Tools

| Tool | Description |
|------|-------------|
| `xlang` | X Compiler |
| `xpm` | Package manager |
| `xconv` | Convert other languages → X |

---

## Syntax

### Variables
```x
name = "Budi"
age = 19
active = yes
empty = null
```

### Print & Input
```x
print("Hello", name)
name = input("Your name: ")
```

### Conditions
```x
if age >= 18
    print("Adult!")
or if age >= 13
    print("Teenager!")
or
    print("Kid!")
```

### Loops
```x
repeat 5
    print("Hello!")

for item in list
    print(item)

while x > 0
    x = x - 1
```

### Functions
```x
make add(a, b)
    give a + b

result = add(10, 5)
print(result)
```

### Error Handling
```x
maybe
    result = 10 / 0
nope
    print("An error occurred!")
```

### Import Package
```x
import math
print(math.sqrt(16))
```

---

## Package Manager (xpm)

```bash
xpm install math
xpm install http
xpm list
xpm search json
xpm update
```

---

## Converter (xconv)

Convert other languages to X:

```bash
xconv main.py main.x
xconv main.js main.x
xconv main.go main.x
xconv main.rs main.x
xconv main.c  main.x
```

Supported: Python, JavaScript, TypeScript, C, C++, Go, Rust

---

## Package Registry

Submit your package to [xpm-packages](https://github.com/Natarizki/xpm-packages)!

---

## License

X Language License v1.0

