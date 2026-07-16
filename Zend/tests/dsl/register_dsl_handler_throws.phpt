--TEST--
register_dsl(): an exception in the handler aborts compilation with its message
--FILE--
<?php
register_dsl('boom', function (string $body): string {
    throw new Exception('kaboom: ' . $body);
});

/* eval() of a constant string: compiles after registration on purpose */
eval('return boom`payload`;');
?>
--EXPECTF--
Fatal error: DSL "boom": kaboom: payload in %s on line %d
