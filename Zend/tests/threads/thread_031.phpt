--TEST--
Thread: Boolean values in arguments
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

$thread->run(function($t, $f) {
    echo "True: " . ($t ? 'yes' : 'no') . "\n";
    echo "False: " . ($f ? 'yes' : 'no') . "\n";
    echo "True type: " . gettype($t) . "\n";
    echo "False type: " . gettype($f) . "\n";
}, [true, false]);

$thread->join();
echo "Done\n";
?>
--EXPECT--
True: yes
False: no
True type: boolean
False type: boolean
Done
