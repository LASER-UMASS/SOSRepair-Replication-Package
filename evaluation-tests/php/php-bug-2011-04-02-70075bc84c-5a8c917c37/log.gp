====begin test:1====
Outer function catenates 'hello' onto $var to give hello
Outer function catenates 'bar' onto $var to give hellobar
Inner function reckons $var is hello
Outer function catenates 'foo' onto $var to give hellobarfoo
Outer function catenates 'bar' onto $var to give hellobarfoobar
Inner function reckons $var is ���ofoo
====end test====
====begin test:2====
Test 2
Outer function increments $v to 2 after adding 2
Inner function reckons $v is 0
Outer function increments $v to 5 after adding 3
Inner function reckons $v is 2
====end test====
====begin test:3====
Test 3
Dmitry2
Dmitry2
====end test====
====begin test:4====
Test 4
id: 1 | person: Tyler
id: 20 | person: Bill
id: 4 | person: Marc
====end test====
====begin test:5====
Test 2
string(3) "md5"
string(3) "md5"
string(3) "md5"
====end test====
====begin test:6====
Test 6
NULL
int(2)

====end test====
====begin test:7====
Test 7

Warning: Invalid callback Array, array must have exactly two members in /experiment/src/sapi/cli/test7.php on line 25

Warning: Foo in /experiment/src/sapi/cli/test7.php on line 25
Caught on second level: 'Foo'

====end test====
====begin test:8====
Test 8
array(1) {
  [0]=>
  int(1)
}
array(0) {
}
array(0) {
}
array(1) {
  [0]=>
  object(Closure)#1 (0) {
  }
}
int(2)
array(1) {
  [0]=>
  object(Closure)#2 (1) {
    ["static"]=>
    array(1) {
      ["h"]=>
      &array(1) {
        [0]=>
        object(Closure)#1 (0) {
        }
      }
    }
  }
}
object(Closure)#2 (1) {
  ["static"]=>
  array(1) {
    ["h"]=>
    &array(1) {
      [0]=>
      object(Closure)#1 (0) {
      }
    }
  }
}
array(0) {
}
array(2) {
  [0]=>
  object(Closure)#3 (0) {
  }
  [1]=>
  int(5)
}

====end test====

