--TEST--
register_dsl(): argument validation and duplicate registration
--FILE--
<?php
try {
    register_dsl('not a tag', fn(string $b): string => "''");
} catch (ValueError $e) {
    echo $e->getMessage(), "\n";
}

try {
    register_dsl('tag', 'no_such_function');
} catch (TypeError $e) {
    echo $e->getMessage(), "\n";
}

register_dsl('twice', fn(string $b): string => "new ArrayObject([])");
try {
    register_dsl('twice', fn(string $b): string => "new ArrayObject([])");
} catch (Error $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
register_dsl(): Argument #1 ($tag) must be a valid DSL tag ([a-zA-Z_][a-zA-Z0-9_]*)
register_dsl(): Argument #2 ($handler) must be a valid callback
DSL tag "twice" is already registered
