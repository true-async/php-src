<?php
require_once __DIR__ . '/config.inc';

use function Async\spawn;
use function Async\await;

class PDOPoolTest {

    static function factory($options = []) {
        $dsn = "mysql:host=" . MYSQL_TEST_HOST;
        if (MYSQL_TEST_PORT) {
            $dsn .= ";port=" . MYSQL_TEST_PORT;
        }
        if (MYSQL_TEST_SOCKET) {
            $dsn .= ";unix_socket=" . MYSQL_TEST_SOCKET;
        }
        if (MYSQL_TEST_DB) {
            $dsn .= ";dbname=" . MYSQL_TEST_DB;
        }
        if (MYSQL_TEST_CHARSET) {
            $dsn .= ";charset=" . MYSQL_TEST_CHARSET;
        }

        $defaults = [
            PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
            PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
        ];

        return new PDO($dsn, MYSQL_TEST_USER, MYSQL_TEST_PASSWD, $options + $defaults);
    }

    static function poolFactory($poolMax = 3, $extra = []) {
        return self::factory([
            PDO::ATTR_POOL_ENABLED => true,
            PDO::ATTR_POOL_MIN => 0,
            PDO::ATTR_POOL_MAX => $poolMax,
        ] + $extra);
    }

    static function skip() {
        try {
            $pdo = self::factory();
            $pdo->query("SELECT 1");
        } catch (Exception $e) {
            die('skip MySQL server not available: ' . $e->getMessage());
        }
    }
}
?>
