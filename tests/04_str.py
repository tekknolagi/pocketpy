assert 'testing' == 'test' + 'ing'
assert 'testing' != 'test' + 'ing2'
assert 'testing' < 'test' + 'ing2'
assert 'testing5' > 'test' + 'ing1'

# test + *=
assert 'abc' + 'def' == 'abcdef'
assert 'abc' * 3 == 'abcabcabc'

a = ''
b = 'test'
c ='test'
assert len(a) == 0
assert len(b) == 4
assert b == c

# upper and lower not work for utf-8
# assert ''.lower() == '' and ''.upper() == ''
# assert 'already+lower '.lower() == 'already+lower '
# assert 'ALREADY+UPPER '.upper() == 'ALREADY+UPPER '
# assert 'tEST+InG'.lower() == 'test+ing'
# assert 'tEST+InG'.upper() == 'TEST+ING'

s = "football"
q = "abcd"
r = "zoo"
t = "this is string example....wow!!!"
assert s[0] == 'f'
assert s[1:4] == 'oot'
assert s[:-1] == 'footbal'
assert s[:10] == 'football'
assert s[-3] == 'a'
assert t[-5:] == 'ow!!!'
assert t[3:-3] == 's is string example....wow'
assert s > q;assert s < r
assert s.replace("foo","ball") == "balltball"
assert s.startswith('f') == True;assert s.endswith('o') == False
assert t.startswith('this') == True;


assert t.split('w') == ['this is string example....', 'o', '!!!']
assert "a,b,c".split(',') == ['a', 'b', 'c']
assert 'a,'.split(',') == ['a', '']
assert 'foo!!bar!!baz'.split('!!') == ['foo', 'bar', 'baz']

t = "*****this is **string** example....wow!!!*****"
s = "123abcrunoob321"
assert t.strip( '*' ) == "this is **string** example....wow!!!"
assert s.strip( '12' ) == "3abcrunoob3"

assert t.strip( '*' ) == "this is **string** example....wow!!!"
assert s.strip( '12' ) == "3abcrunoob3"

s = ' asd\n  asd \n'
assert s.strip() == 'asd\n  asd'

s1 = "-"
s2 = ""
seq = ["r","u","n","o","o","b"]
assert s1.join( seq ) == "r-u-n-o-o-b"
assert s2.join( seq ) == "runoob"

def test(*seq):
    return s1.join(seq)
assert test("r", "u", "n", "o", "o", "b") == "r-u-n-o-o-b"

def f():
    for i in range(5):
        yield str(i)
assert '|'.join(f()) == '0|1|2|3|4'

num = 6
assert str(num) == '6'

# test Lo group names

测试 = "test"
assert 测试 == "test"