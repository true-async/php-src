<?php
$env = [
    'MYSQL_TEST_HOST' => false !== getenv('MYSQL_TEST_HOST') ? getenv('MYSQL_TEST_HOST') : 'localhost',
    'MYSQL_TEST_PORT' => false !== getenv('MYSQL_TEST_PORT') ? getenv('MYSQL_TEST_PORT') : '3306',
    'MYSQL_TEST_USER' => false !== getenv('MYSQL_TEST_USER') ? getenv('MYSQL_TEST_USER') : 'test',
    'MYSQL_TEST_PASSWD' => false !== getenv('MYSQL_TEST_PASSWD') ? getenv('MYSQL_TEST_PASSWD') : 'test',
    'MYSQL_TEST_DB' => false !== getenv('MYSQL_TEST_DB') ? getenv('MYSQL_TEST_DB') : 'test',
    'MYSQL_TEST_SOCKET' => false !== getenv('MYSQL_TEST_SOCKET') ? getenv('MYSQL_TEST_SOCKET') : '/var/run/mysqld/mysqld.sock',
    'MYSQL_TEST_CHARSET' => false !== getenv('MYSQL_TEST_CHARSET') ? getenv('MYSQL_TEST_CHARSET') : 'utf8',
    'MYSQL_TEST_ENGINE' => false !== getenv('MYSQL_TEST_ENGINE') ? getenv('MYSQL_TEST_ENGINE') : 'InnoDB',
];

foreach ($env as $k => $v) {
    define($k, $v);
}
?>
