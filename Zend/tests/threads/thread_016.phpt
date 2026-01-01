--TEST--
Thread: Thread final class cannot be extended
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
