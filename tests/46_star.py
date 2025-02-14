def f(a, b, *args):
    return a + b + sum(args)

assert f(1, 2, 3, 4) == 10

a = [5, 6, 7, 8]
assert f(*a) == 26

def g(*args):
    return f(*args)

assert g(1, 2, 3, 4) == 10
assert g(*a) == 26

def f(a, b, *args, c=16):
    return a + b + sum(args) + c

assert f(1, 2, 3, 4) == 26
assert f(1, 2, 3, 4, c=32) == 42

assert f(*a, c=-26) == 0


a, b = 1, 2
a, b = b, a
assert a == 2
assert b == 1

a, *b = 1, 2, 3, 4
assert a == 1
assert b == [2, 3, 4]

a, *b = [1]
assert a == 1
assert b == []