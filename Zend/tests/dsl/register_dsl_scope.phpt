--TEST--
register_dsl(): the generated expression compiles in the enclosing scope
--FILE--
<?php
/* The handler splices the raw body into the generated code, so the DSL
 * can reference variables of the scope where the literal appears. */
register_dsl('wrap', fn(string $body): string => "new ArrayObject([{$body}])");

function f(): array {
    $x = 5;
    /* eval() of a constant string: compiles after registration on purpose */
    $o = eval('return wrap`$x * 2, $x`;');
    return $o->getArrayCopy();
}

var_dump(f());
?>
--EXPECT--
array(2) {
  [0]=>
  int(10)
  [1]=>
  int(5)
}
