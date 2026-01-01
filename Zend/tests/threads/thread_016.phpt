--TEST--
Thread: Thread final class cannot be extended
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
try {
    class MyThread extends Thread {
    }
    echo "Should not reach here\n";
} catch (Error $e) {
    echo "Error: Class Thread is final\n";
}
?>
--EXPECT--
Error: Class Thread is final
