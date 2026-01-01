--TEST--
Thread: Object passing should fail (not supported)
--SKIPIF--
<?php
if (!Thread::isSupported()) {
    die('skip Thread support not available (requires ZTS build)');
}
if (!class_exists('Thread')) {
    die('skip Thread class not available');
}
?>
--FILE--
<?php
class TestClass {
    public $value = 42;
}

$thread = new Thread();
$obj = new TestClass();

try {
    $thread->run(function($o) {
        echo "Value: " . $o->value . "\n";
    }, [$obj]);
    $thread->join();
} catch (Error $e) {
    echo "Error caught: Cannot pass object\n";
}

echo "Done\n";
?>
--EXPECTF--
Error caught: Cannot pass object
Done
