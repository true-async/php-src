--TEST--
Function JIT: a delayed OOM error stops a recursion without loops
--EXTENSIONS--
opcache
--SKIPIF--
<?php
if (getenv("USE_ZEND_ALLOC") === "0") {
    die("skip Zend MM disabled");
}
?>
--INI--
opcache.enable=1
opcache.enable_cli=1
opcache.jit=function
opcache.jit_buffer_size=64M
opcache.jit_hot_func=0
memory_limit=32M
--FILE--
<?php
function recurse($depth) {
    return recurse($depth + 1);
}

register_shutdown_function(function () {
    echo "Shutdown function called\n";
});

recurse(0);
?>
--EXPECTF--
Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
%AShutdown function called
