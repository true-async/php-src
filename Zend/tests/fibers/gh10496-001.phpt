--TEST--
Bug GH-10496 001 (Segfault when garbage collector is invoked inside of fiber)
--SKIPIF--
<?php
if (!function_exists("Async\\spawn")) die("skip TrueAsync runtime required");
?>
--FILE--
<?php

function x(&$ref) {
	$ref = new class() {
		function __destruct() {
			print "Dtor x()\n";
		}
	};
}
function suspend($x) {
	Fiber::suspend();
}
$f = new Fiber(function() use (&$f) {
	try {
		x($var);
		\ord(suspend(1));
	} finally {
		print "Cleaned\n";
	}
});
$f->start();
unset($f);
gc_collect_cycles();

// In TrueAsync, destructors from GC run in a separate coroutine.
Async\spawn(function () {
    echo "2\n";
});

print "Collected\n";

?>
--EXPECT--
Collected
2
Cleaned
Dtor x()
