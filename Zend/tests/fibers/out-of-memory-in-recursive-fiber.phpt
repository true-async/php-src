--TEST--
Out of Memory from recursive fiber creation
--INI--
memory_limit=2M
--SKIPIF--
<?php
if (getenv("USE_ZEND_ALLOC") === "0") {
    die("skip Zend MM disabled");
}
if (!function_exists("Async\\spawn")) {
    die("skip TrueAsync runtime required");
}
?>
--FILE--
<?php

function create_fiber(): Fiber
{
    $fiber = new Fiber('create_fiber');
    $fiber->start();
    return $fiber;
}

$fiber = new Fiber('create_fiber');
$fiber->start();

?>
--EXPECTF--
Fatal error: Allowed memory size of %d bytes exhausted%s(tried to allocate %d bytes) in %s on line %d
%A