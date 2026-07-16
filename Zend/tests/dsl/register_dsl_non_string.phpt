--TEST--
register_dsl(): a handler returning a non-string is a compile error
--FILE--
<?php
register_dsl('answer', fn(string $body) => 42);

/* eval() of a constant string: compiles after registration on purpose */
eval('return answer`x`;');
?>
--EXPECTF--
Fatal error: DSL "answer": DSL handler must return a string, int returned in %s on line %d
