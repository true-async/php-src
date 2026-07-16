--TEST--
A DSL literal without any registered handler is a compile error
--FILE--
<?php
$x = nosuch`body`;
?>
--EXPECTF--
Fatal error: No DSL handler registered for tag "nosuch" (load the providing extension or call register_dsl() before this code is compiled) in %s on line %d
