--TEST--
Thread: isSupported() static method
--FILE--
<?php
$supported = Thread::isSupported();

echo "Thread support: " . ($supported ? 'yes' : 'no') . "\n";
echo "Is bool: " . (is_bool($supported) ? 'yes' : 'no') . "\n";

// Should be callable statically
$result = call_user_func(['Thread', 'isSupported']);
echo "Callable: " . ($result === $supported ? 'yes' : 'no') . "\n";
?>
--EXPECTF--
Thread support: %s
Is bool: yes
Callable: yes
