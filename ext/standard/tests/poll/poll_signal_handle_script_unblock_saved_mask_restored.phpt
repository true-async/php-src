--TEST--
Io\Poll\SignalHandle: a blocked signal stays blocked after the mask is saved and restored during the watch
--EXTENSIONS--
pcntl
--SKIPIF--
<?php
if (!Io\Poll\Backend::Auto->supportsSignalHandles()) die("skip no signal handle source on this platform");
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
pcntl_sigprocmask(SIG_UNBLOCK, [SIGUSR2]);
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR1]);
$watcher = $ctx->add(new Io\Poll\SignalHandle([SIGUSR1]), [Io\Poll\Event::Signal]);
pcntl_sigprocmask(SIG_BLOCK, [SIGUSR2], $saved);
pcntl_sigprocmask(SIG_SETMASK, $saved);
$watcher->remove();
var_dump(is_blocked(SIGUSR1), is_blocked(SIGUSR2));
?>
--EXPECT--
bool(true)
bool(false)
