--TEST--
register_dsl(): userland handler compiles a DSL literal in later-compiled code
--FILE--
<?php
register_dsl('json', function (string $body): string {
    $decoded = json_decode($body);
    if ($decoded === null) {
        throw new Exception('invalid JSON: ' . json_last_error_msg());
    }
    return 'json_decode(' . var_export($body, true) . ')';
});

/* eval() of a constant string: a userland DSL handler only applies to code
 * compiled after registration, and eval is the in-request way to compile */
$v = eval('return json`{"a": 1, "b": [2, 3]}`;');
var_dump($v->a, $v->b);
?>
--EXPECT--
int(1)
array(2) {
  [0]=>
  int(2)
  [1]=>
  int(3)
}
