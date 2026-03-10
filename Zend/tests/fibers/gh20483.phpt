--TEST--
GH-20483 (ASAN stack overflow with small fiber.stack_size INI value)
--SKIPIF--
<?php if (extension_loaded("true_async")) die("skip TrueAsync has different stack size handling"); ?>
--INI--
fiber.stack_size=1024
--FILE--
<?php
$callback = function () {};
$fiber = new Fiber($callback);
try {
    $fiber->start();
} catch (Exception $e) {
    echo $e->getMessage() . "\n";
}
?>
--EXPECTF--
Fiber stack size is too small, it needs to be at least %d bytes
