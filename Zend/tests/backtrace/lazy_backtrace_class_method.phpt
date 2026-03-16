--TEST--
Lazy backtrace: static and instance method calls
--FILE--
<?php
class Foo {
    static function bar() { return new Exception("test"); }
    function baz() { return self::bar(); }
}

$e = (new Foo)->baz();
echo $e->getTraceAsString();
?>
--EXPECTF--
#0 %s(%d): Foo::bar()
#1 %s(%d): Foo->baz()
#2 {main}
