--TEST--
GH-10249 (Assertion `size >= page_size + 1 * page_size' failed.)
--SKIPIF--
<?php if (extension_loaded("true_async")) die("skip TrueAsync has different stack size handling"); ?>
--FILE--
<?php

$callback = function () {};
ini_set("fiber.stack_size", "");
$fiber = new Fiber($callback);
try {
    $fiber->start();
} catch (Exception $e) {
    echo $e->getMessage() . "\n";
}

?>
--EXPECTF--
Fiber stack size is too small, it needs to be at least %d bytes
