--TEST--
DSL literal scanner errors are catchable ParseErrors via eval()
--FILE--
<?php
/* eval() of constant strings: the parse of the string is what is under test */
try {
    eval('return x`abc;');
} catch (ParseError $e) {
    echo $e->getMessage(), "\n";
}

try {
    eval("return x`\n  a\n    `;");
} catch (ParseError $e) {
    echo $e->getMessage(), "\n";
}

try {
    eval("return x`\n\ta\n    `;");
} catch (ParseError $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
Unterminated DSL literal
Invalid body indentation level (expecting an indentation level of at least 4)
Invalid indentation - tabs and spaces cannot be mixed
