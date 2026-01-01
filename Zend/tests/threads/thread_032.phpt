--TEST--
Thread: Negative numbers and float precision
--EXTENSIONS--
zts
--SKIPIF--
<?php
if (!class_exists('Thread')) {
    die('skip Thread class not available');
}
?>
--FILE--
<?php
$thread = new Thread();

$thread->run(function($neg, $pi, $large) {
    echo "Negative: $neg\n";
    printf("Pi: %.10f\n", $pi);
    echo "Large: $large\n";
}, [-42, 3.1415926535, 1.23e10]);

$thread->join();
echo "Done\n";
?>
--EXPECT--
Negative: -42
Pi: 3.1415926535
Large: 12300000000
Done
