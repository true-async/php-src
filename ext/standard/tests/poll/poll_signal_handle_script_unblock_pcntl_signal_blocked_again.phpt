--TEST--
Io\Poll\SignalHandle: a blocked signal stays blocked after pcntl_signal() and a block during the watch
--EXTENSIONS--
pcntl
--SKIPIF--
<?php
if (!Io\Poll\Backend::Auto->supportsSignalHandles()) die("skip no signal handle source on this platform");
// Without Zend signals, installing a handler leaves the mask as it is
if (ini_get('zend.signal_check') === false) die("skip Zend signals are disabled");
?>
--FILE--
<?php
// SIGCHLD only because the call refuses an empty list
function is_blocked(int $signo): bool
{
    pcntl_sigprocmask(SIG_BLOCK, [SIGCHLD], $mask);
    pcntl_sigprocmask(SIG_SETMASK, $mask);
    return in_array($signo, $mask, true);
}

$ctx = new Io\Poll\Context();
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1]);
$watcher = $ctx->add(new Io\Poll\SignalHandle([SIGUSR1]), [Io\Poll\Event::Signal]);
pcntl_signal(SIGUSR1, SIG_DFL);
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1]);
$watcher->remove();
var_dump(is_blocked(SIGUSR1));
?>
--EXPECT--
bool(true)
