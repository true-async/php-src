/*
   +----------------------------------------------------------------------+
   | Copyright © The PHP Group and Contributors.                          |
   +----------------------------------------------------------------------+
   | This source file is subject to the Modified BSD License that is      |
   | bundled with this package in the file LICENSE, and is available      |
   | through the World Wide Web at <https://www.php.net/license/>.        |
   |                                                                      |
   | SPDX-License-Identifier: BSD-3-Clause                                |
   +----------------------------------------------------------------------+
   | Authors: Wez Furlong <wez@thebrainroom.com>                          |
   +----------------------------------------------------------------------+
 */

#include "php.h"
#include "php_globals.h"
#include "php_network.h"
#include "php_open_temporary_file.h"
#include "ext/standard/file.h"
#include "ext/standard/flock_compat.h"
#include "ext/standard/php_filestat.h"
#include <stddef.h>
#include <fcntl.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#include "SAPI.h"

#include "php_streams_int.h"
#include "zend_exceptions.h"
#ifdef PHP_WIN32
# include "win32/winutil.h"
# include "win32/time.h"
# include "win32/ioutil.h"
# include "win32/readdir.h"
# include <limits.h>
#endif

#ifdef __linux__
# include <sys/sysmacros.h>
#endif

#define php_stream_fopen_from_fd_int(fd, mode, persistent_id)	_php_stream_fopen_from_fd_int((fd), (mode), (persistent_id) STREAMS_CC)
#define php_stream_fopen_from_fd_int_rel(fd, mode, persistent_id)	 _php_stream_fopen_from_fd_int((fd), (mode), (persistent_id) STREAMS_REL_CC)
#define php_stream_fopen_from_file_int(file, mode)	_php_stream_fopen_from_file_int((file), (mode) STREAMS_CC)
#define php_stream_fopen_from_file_int_rel(file, mode)	 _php_stream_fopen_from_file_int((file), (mode) STREAMS_REL_CC)

#ifndef PHP_WIN32
extern int php_get_uid_by_name(const char *name, uid_t *uid);
extern int php_get_gid_by_name(const char *name, gid_t *gid);
#endif

#if defined(PHP_WIN32)
# define PLAIN_WRAP_BUF_SIZE(st) ((unsigned int)(st > INT_MAX ? INT_MAX : st))
# define fsync _commit
# define fdatasync fsync
# define php_fd_set_nonblock(fd)	((void)0)
# define php_fd_set_block(fd)		((void)0)
#else
# define PLAIN_WRAP_BUF_SIZE(st) (st)
# define php_fd_set_nonblock(fd)	fcntl((fd), F_SETFL, fcntl((fd), F_GETFL, 0) | O_NONBLOCK)
# define php_fd_set_block(fd)		fcntl((fd), F_SETFL, fcntl((fd), F_GETFL, 0) & ~O_NONBLOCK)
# if !defined(HAVE_FDATASYNC)
#  define fdatasync fsync
# elif defined(__APPLE__)
  // The symbol is present, however not in the headers
  extern int fdatasync(int);
# endif
#endif

/* parse standard "fopen" modes into open() flags */
PHPAPI int php_stream_parse_fopen_modes(const char *mode, int *open_flags)
{
	int flags;

	switch (mode[0]) {
		case 'r':
			flags = 0;
			break;
		case 'w':
			flags = O_TRUNC|O_CREAT;
			break;
		case 'a':
			flags = O_CREAT|O_APPEND;
			break;
		case 'x':
			flags = O_CREAT|O_EXCL;
			break;
		case 'c':
			flags = O_CREAT;
			break;
		default:
			/* unknown mode */
			return FAILURE;
	}

	if (strchr(mode, '+')) {
		flags |= O_RDWR;
	} else if (flags) {
		flags |= O_WRONLY;
	} else {
		flags |= O_RDONLY;
	}

#if defined(O_CLOEXEC)
	if (strchr(mode, 'e')) {
		flags |= O_CLOEXEC;
	}
#endif

#if defined(O_NONBLOCK)
	if (strchr(mode, 'n')) {
		flags |= O_NONBLOCK;
	}
#endif

#if defined(_O_TEXT) && defined(O_BINARY)
	if (strchr(mode, 't')) {
		flags |= _O_TEXT;
	} else {
		flags |= O_BINARY;
	}
#endif

	*open_flags = flags;
	return SUCCESS;
}


/* {{{ ------- STDIO stream implementation -------*/

typedef struct {
	FILE *file;
	int fd;					/* underlying file descriptor */
	unsigned is_process_pipe:1;	/* use pclose instead of fclose */
	unsigned is_pipe:1;		/* stream is an actual pipe, currently Windows only*/
	unsigned cached_fstat:1;	/* sb is valid */
	unsigned is_pipe_blocking:1; /* allow blocking read() on pipes, currently Windows only */
	unsigned no_forced_fstat:1;  /* Use fstat cache even if forced */
	unsigned is_seekable:1;		/* don't try and seek, if not set */
	unsigned is_blocked:1;		/* true (default) = blocking mode; false = non-blocking */
	unsigned sync_io_fallback:1;	/* fd temporarily set to blocking for scheduler context */
	unsigned _reserved:24;

	zend_async_io_t *async_io;
	zend_async_poll_event_t *poll_event;

	struct timeval timeout;		/* read timeout; tv_sec == -1 means "not set" */
	bool timeout_event;			/* true if last read timed out */

	int lock_flag;			/* stores the lock state */
	zend_string *temp_name;	/* if non-null, this is the path to a temporary file that
							 * is to be deleted when the stream is closed */
#ifdef HAVE_FLUSHIO
	char last_op;
#endif

#ifdef HAVE_MMAP
	char *last_mapped_addr;
	size_t last_mapped_len;
#endif
#ifdef PHP_WIN32
	char *last_mapped_addr;
	HANDLE file_mapping;
#endif

	zend_stat_t sb;
} php_stdio_stream_data;
#define PHP_STDIOP_GET_FD(anfd, data)	anfd = (data)->file ? fileno((data)->file) : (data)->fd

static uint32_t php_stdiop_mode_to_io_state(const char *mode)
{
	uint32_t state = 0;

	if (strchr(mode, 'r') || strchr(mode, '+')) {
		state |= ZEND_ASYNC_IO_READABLE;
	}
	if (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, 'x') || strchr(mode, 'c') || strchr(mode, '+')) {
		state |= ZEND_ASYNC_IO_WRITABLE;
	}
	if (strchr(mode, 'a')) {
		state |= ZEND_ASYNC_IO_APPEND;
	}

	return state;
}

/* Called by the reactor during shutdown to detach async IO from this stream.
 * After this, the stream continues working synchronously. */
static void php_stdiop_on_async_detach(zend_async_io_t *io, void *arg)
{
	php_stdio_stream_data *data = (php_stdio_stream_data *) arg;
	data->async_io = NULL;
}

static void php_stdiop_init_async_io(php_stdio_stream_data *self, const char *mode)
{
	if (UNEXPECTED(ZEND_ASYNC_IS_OFF)) {
		return;
	}

	zend_async_io_type type;
#ifdef PHP_WIN32
	/* On Windows, use GetFileType + GetConsoleMode for reliable type detection.
	 * isatty() can return true for CRT fd even when the underlying OS handle
	 * is not a real console (e.g. NUL device, MSYS pipes). */
	const intptr_t os_handle = _get_osfhandle(self->fd);

	if (os_handle == -1 || os_handle == (intptr_t)INVALID_HANDLE_VALUE) {
		return;
	}

	const DWORD file_type = GetFileType((HANDLE)os_handle);

	if (file_type == FILE_TYPE_PIPE) {
		/* GetFileType returns FILE_TYPE_PIPE for both named pipes and sockets.
		 * GetNamedPipeInfo succeeds only for real pipes, not Winsock sockets. */
		if (self->is_pipe || GetNamedPipeInfo((HANDLE)os_handle, NULL, NULL, NULL, NULL)) {
			type = ZEND_ASYNC_IO_TYPE_PIPE;
		} else {
			type = ZEND_ASYNC_IO_TYPE_TCP;
		}
	} else if (file_type == FILE_TYPE_CHAR) {
		DWORD console_mode;
		if (GetConsoleMode((HANDLE)os_handle, &console_mode)) {
			type = ZEND_ASYNC_IO_TYPE_TTY;
		} else {
			type = ZEND_ASYNC_IO_TYPE_FILE;
		}
	} else {
		type = ZEND_ASYNC_IO_TYPE_FILE;
	}

	zend_file_descriptor_t fd = self->fd;

#else
	if (self->is_pipe) {
		type = ZEND_ASYNC_IO_TYPE_PIPE;
	} else if (self->fd >= 0 && isatty(self->fd)) {
		type = ZEND_ASYNC_IO_TYPE_TTY;
	} else {
		type = ZEND_ASYNC_IO_TYPE_FILE;
	}

	const zend_file_descriptor_t fd = self->fd;
#endif
	const uint32_t state = php_stdiop_mode_to_io_state(mode);

	self->async_io = ZEND_ASYNC_IO_CREATE(fd, type, state);

	if (self->async_io != NULL) {
		self->async_io->on_detach = php_stdiop_on_async_detach;
		self->async_io->on_detach_arg = self;
	}
}

static int do_fstat(php_stdio_stream_data *d, int force)
{
	if (!d->cached_fstat || (force && !d->no_forced_fstat)) {
		int fd;
		int r;

		PHP_STDIOP_GET_FD(fd, d);
		r = zend_fstat(fd, &d->sb);
		d->cached_fstat = r == 0;

		return r;
	}
	return 0;
}

static php_stream *_php_stream_fopen_from_fd_int(int fd, const char *mode, const char *persistent_id STREAMS_DC)
{
	php_stdio_stream_data *self;

	self = pemalloc_rel_orig(sizeof(*self), persistent_id);
	memset(self, 0, sizeof(*self));
	self->file = NULL;
	self->is_seekable = 1;
	self->is_pipe = 0;
	self->is_blocked = 1;
	self->lock_flag = LOCK_UN;
	self->is_process_pipe = 0;
	self->temp_name = NULL;
	self->fd = fd;
	self->timeout.tv_sec = -1;
#ifdef PHP_WIN32
	self->is_pipe_blocking = 0;
#endif

	return php_stream_alloc_rel(&php_stream_stdio_ops, self, persistent_id, mode);
}

static php_stream *_php_stream_fopen_from_file_int(FILE *file, const char *mode STREAMS_DC)
{
	php_stdio_stream_data *self;

	self = emalloc_rel_orig(sizeof(*self));
	memset(self, 0, sizeof(*self));
	self->file = file;
	self->is_seekable = 1;
	self->is_pipe = 0;
	self->is_blocked = 1;
	self->lock_flag = LOCK_UN;
	self->is_process_pipe = 0;
	self->temp_name = NULL;
	self->fd = fileno(file);
#ifdef PHP_WIN32
	self->is_pipe_blocking = 0;
#endif

	return php_stream_alloc_rel(&php_stream_stdio_ops, self, 0, mode);
}

PHPAPI php_stream *_php_stream_fopen_temporary_file(const char *dir, const char *pfx, zend_string **opened_path_ptr STREAMS_DC)
{
	zend_string *opened_path = NULL;
	int fd;

	fd = php_open_temporary_fd(dir, pfx, &opened_path);
	if (fd != -1)	{
		php_stream *stream;

		if (opened_path_ptr) {
			*opened_path_ptr = opened_path;
		}

		stream = php_stream_fopen_from_fd_int_rel(fd, "r+b", NULL);
		if (stream) {
			php_stdio_stream_data *self = (php_stdio_stream_data*)stream->abstract;
			stream->wrapper = (php_stream_wrapper*)&php_plain_files_wrapper;
			stream->orig_path = estrndup(ZSTR_VAL(opened_path), ZSTR_LEN(opened_path));

			self->temp_name = opened_path;
			self->lock_flag = LOCK_UN;

			return stream;
		}
		close(fd);

		php_error_docref(NULL, E_WARNING, "Unable to allocate stream");

		return NULL;
	}
	return NULL;
}

PHPAPI php_stream *_php_stream_fopen_tmpfile(int dummy STREAMS_DC)
{
	return php_stream_fopen_temporary_file(NULL, "php", NULL);
}

static void detect_is_seekable(php_stdio_stream_data *self) {
#if defined(S_ISFIFO) && defined(S_ISCHR)
	if (self->fd >= 0 && do_fstat(self, 0) == 0) {
#ifdef __linux__
		if (S_ISCHR(self->sb.st_mode)) {
			/* Some character devices are exceptions, check their major/minor ID
			 * https://www.kernel.org/doc/Documentation/admin-guide/devices.txt */
			if (major(self->sb.st_rdev) == 1) {
				unsigned m = minor(self->sb.st_rdev);
				self->is_seekable =
					m == 1 ||   /* /dev/mem   */
					m == 2 ||   /* /dev/kmem  */
					m == 3 ||   /* /dev/null  */
					m == 4 ||   /* /dev/port  (seekable, offset = I/O port) */
					m == 5 ||   /* /dev/zero  */
					m == 7;     /* /dev/full  */
			} else {
				self->is_seekable = false;
			}
		} else {
			self->is_seekable = !S_ISFIFO(self->sb.st_mode);
		}
#else
		self->is_seekable = !(S_ISFIFO(self->sb.st_mode) || S_ISCHR(self->sb.st_mode));
#endif
		self->is_pipe = S_ISFIFO(self->sb.st_mode);
	}
#elif defined(PHP_WIN32)
	uintptr_t handle = _get_osfhandle(self->fd);

	if (handle != (uintptr_t)INVALID_HANDLE_VALUE) {
		DWORD file_type = GetFileType((HANDLE)handle);

		self->is_seekable = !(file_type == FILE_TYPE_PIPE || file_type == FILE_TYPE_CHAR);
		self->is_pipe = file_type == FILE_TYPE_PIPE;

		/* Additional check needed to distinguish between pipes and sockets. */
		if (self->is_pipe && !GetNamedPipeInfo((HANDLE) handle, NULL, NULL, NULL, NULL)) {
			self->is_pipe = 0;
		}
	}
#endif
}

PHPAPI php_stream *_php_stream_fopen_from_fd(int fd, const char *mode, const char *persistent_id, bool zero_position STREAMS_DC)
{
	php_stream *stream = php_stream_fopen_from_fd_int_rel(fd, mode, persistent_id);

	if (stream) {
		php_stdio_stream_data *self = (php_stdio_stream_data*)stream->abstract;

		detect_is_seekable(self);
		if (!self->is_seekable) {
			stream->flags |= PHP_STREAM_FLAG_NO_SEEK;
			stream->position = -1;
		} else if (zero_position) {
			ZEND_ASSERT(zend_lseek(self->fd, 0, SEEK_CUR) == 0);
			stream->position = 0;
		} else {
			stream->position = zend_lseek(self->fd, 0, SEEK_CUR);
#ifdef ESPIPE
			/* FIXME: Is this code still needed? */
			if (stream->position == (zend_off_t)-1 && errno == ESPIPE) {
				stream->flags |= PHP_STREAM_FLAG_NO_SEEK;
				self->is_seekable = 0;
			}
#endif
		}

		php_stdiop_init_async_io(self, mode);
	}

	return stream;
}

PHPAPI php_stream *_php_stream_fopen_from_file(FILE *file, const char *mode STREAMS_DC)
{
	php_stream *stream = php_stream_fopen_from_file_int_rel(file, mode);

	if (stream) {
		php_stdio_stream_data *self = (php_stdio_stream_data*)stream->abstract;

		detect_is_seekable(self);
		if (!self->is_seekable) {
			stream->flags |= PHP_STREAM_FLAG_NO_SEEK;
			stream->position = -1;
		} else {
			stream->position = zend_ftell(file);
		}

		php_stdiop_init_async_io(self, mode);
	}

	return stream;
}

PHPAPI php_stream *_php_stream_fopen_from_pipe(FILE *file, const char *mode STREAMS_DC)
{
	php_stdio_stream_data *self;
	php_stream *stream;

	self = emalloc_rel_orig(sizeof(*self));
	memset(self, 0, sizeof(*self));
	self->file = file;
	self->is_seekable = 0;
	self->is_pipe = 1;
	self->is_blocked = 1;
	self->lock_flag = LOCK_UN;
	self->is_process_pipe = 1;
	self->fd = fileno(file);
	self->temp_name = NULL;
	self->timeout.tv_sec = -1;
#ifdef PHP_WIN32
	self->is_pipe_blocking = 0;
#endif

	stream = php_stream_alloc_rel(&php_stream_stdio_ops, self, 0, mode);
	stream->flags |= PHP_STREAM_FLAG_NO_SEEK;

	/* TODO: popen() should be reimplemented using fork()/exec()/pipe() like proc_open()
	 * instead of libc popen(), so the child PID is stored explicitly. This would allow
	 * proper async waitpid() without the dup() workaround below.
	 *
	 * Current workaround: dup the fd so libuv owns the copy and the original stays
	 * in FILE* for pclose() to do fclose() + waitpid(). */
	int dup_fd = dup(self->fd);
	if (dup_fd >= 0) {
		self->fd = dup_fd;
		php_stdiop_init_async_io(self, mode);
	}

	return stream;
}

static ssize_t php_stdiop_write(php_stream *stream, const char *buf, size_t count)
{
	php_stdio_stream_data *data = (php_stdio_stream_data*)stream->abstract;
	ssize_t bytes_written;

	assert(data != NULL);

	if (data->async_io != NULL && !ZEND_ASYNC_IS_OFF && data->is_blocked && !ZEND_ASYNC_IS_SCHEDULER_CONTEXT) {
		ZEND_ASYNC_SCHEDULER_INIT();
		if (UNEXPECTED(EG(exception))) {
			return -1;
		}

		if (UNEXPECTED(data->sync_io_fallback)) {
			php_fd_set_nonblock(data->fd);
			data->sync_io_fallback = 0;
		}

		zend_async_io_req_t *req = ZEND_ASYNC_IO_WRITE(data->async_io, buf, count);
		if (UNEXPECTED(req == NULL)) {
			return -1;
		}

		if (!req->completed) {
			zend_coroutine_t *const coroutine = ZEND_ASYNC_CURRENT_COROUTINE;
			/* The async IO event is shared by every coroutine writing this
			 * descriptor, so any write's completion notifies them all. Keep
			 * suspending until THIS request is the one that finished —
			 * otherwise a spuriously woken coroutine would dispose a request
			 * whose uv_write is still in flight, and libuv would later touch
			 * freed memory. */
			do {
				ZEND_ASYNC_WAKER_NEW(coroutine);
				zend_async_resume_when(coroutine, &data->async_io->event, false,
						zend_async_waker_callback_resolve, NULL);
				ZEND_ASYNC_SUSPEND();
				zend_async_waker_clean(coroutine);
			} while (!req->completed && EG(exception) == NULL);
		}

		/* IO closed externally while parked — stream/data may be freed. */
		if (UNEXPECTED(req->io_closed)) {
			if (EG(exception)) {
				zend_clear_exception();
			}
			if (req->exception != NULL) {
				OBJ_RELEASE(req->exception);
				req->exception = NULL;
			}
			req->dispose(req);
			return -1;
		}

		if (UNEXPECTED(EG(exception)) || UNEXPECTED(req->exception != NULL)) {
			if (!(stream->flags & PHP_STREAM_FLAG_SUPPRESS_ERRORS)) {
				zend_object *exception = EG(exception) ? EG(exception) : req->exception;
				zval rv;
				const zval *message =
						zend_read_property_ex(exception->ce, exception, zend_known_strings[ZEND_STR_MESSAGE], 0, &rv);

				php_error_docref(NULL, E_NOTICE, "Write of %zu bytes failed with async IO error: %s",
						count, message ? Z_STRVAL_P(message) : "empty error");
			}
			if (EG(exception)) {
				zend_clear_exception();
			}
			if (req->exception != NULL) {
				OBJ_RELEASE(req->exception);
				req->exception = NULL;
			}
			req->dispose(req);
			return -1;
		}

		const ssize_t transferred = req->transferred;
		req->dispose(req);
		return transferred;
	}

	if (data->async_io != NULL && data->is_blocked && !data->sync_io_fallback) {
		php_fd_set_block(data->fd);
		data->sync_io_fallback = 1;
	}

	if (data->fd >= 0) {
#ifdef PHP_WIN32
		bytes_written = _write(data->fd, buf, PLAIN_WRAP_BUF_SIZE(count));
#else
		bytes_written = write(data->fd, buf, count);
#endif
		if (bytes_written < 0) {
			if (PHP_IS_TRANSIENT_ERROR(errno)) {
				return 0;
			}
			if (errno == EINTR) {
				/* TODO: Should this be treated as a proper error or not? */
				return bytes_written;
			}
			if (!(stream->flags & PHP_STREAM_FLAG_SUPPRESS_ERRORS)) {
				char errstr[256];
				php_error_docref(NULL, E_NOTICE, "Write of %zu bytes failed with errno=%d %s",
						count, errno, php_socket_strerror_s(errno, errstr, sizeof(errstr)));
			}
		}
	} else {

#ifdef HAVE_FLUSHIO
		if (data->is_seekable && data->last_op == 'r') {
			zend_fseek(data->file, 0, SEEK_CUR);
		}
		data->last_op = 'w';
#endif

		bytes_written = (ssize_t) fwrite(buf, 1, count, data->file);
	}

	if (EG(active)) {
		/* clear stat cache as mtime and ctime got changed */
		php_clear_stat_cache(0, NULL, 0);
	}

	return bytes_written;
}

static ssize_t php_stdiop_read(php_stream *stream, char *buf, size_t count)
{
	php_stdio_stream_data *data = (php_stdio_stream_data*)stream->abstract;
	ssize_t ret;

	assert(data != NULL);

	/* Scheduler context cannot suspend coroutines, so async IO is not possible.
	 * Skip this block entirely and fall through to the sync path below,
	 * which will set the fd to blocking mode if needed. */
	if (data->async_io != NULL && !ZEND_ASYNC_IS_OFF && data->is_blocked && !ZEND_ASYNC_IS_SCHEDULER_CONTEXT) {
		ZEND_ASYNC_SCHEDULER_INIT();
		if (UNEXPECTED(EG(exception))) {
			return -1;
		}

		data->timeout_event = false;

		/* Restore non-blocking mode if a previous scheduler-context call
		 * temporarily switched the fd to blocking. */
		if (UNEXPECTED(data->sync_io_fallback)) {
			php_fd_set_nonblock(data->fd);
			data->sync_io_fallback = 0;
		}

		zend_async_io_req_t *req = ZEND_ASYNC_IO_READ(data->async_io, buf, count);
		if (UNEXPECTED(req == NULL)) {
			return -1;
		}

		if (!req->completed) {
			zend_coroutine_t *const coroutine = ZEND_ASYNC_CURRENT_COROUTINE;
			const zend_ulong timeout_ms = data->timeout.tv_sec >= 0
				? (zend_ulong)data->timeout.tv_sec * 1000 + (zend_ulong)data->timeout.tv_usec / 1000
				: 0;

			/* The async IO event is shared by every coroutine using this
			 * descriptor, so an unrelated completion can wake us. Keep
			 * suspending until THIS request finished — otherwise we would use
			 * and dispose a request whose libuv operation is still in flight. */
			do {
				ZEND_ASYNC_WAKER_NEW(coroutine);

				if (timeout_ms) {
					zend_async_resume_when(coroutine, &ZEND_ASYNC_NEW_TIMER_EVENT(timeout_ms, false)->base, true,
							zend_async_waker_callback_timeout, NULL);
				}

				zend_async_resume_when(coroutine, &data->async_io->event, false,
						zend_async_waker_callback_resolve, NULL);

				if (!ZEND_ASYNC_SUSPEND()) {
					zend_async_waker_clean(coroutine);
					if (EG(exception) != NULL
						&& instanceof_function(EG(exception)->ce, ZEND_ASYNC_GET_EXCEPTION_CE(ZEND_ASYNC_EXCEPTION_TIMEOUT))) {
						/* Clear the timeout exception, as we will handle it via return value. */
						zend_clear_exception();

						if (!req->completed) {
							data->timeout_event = true;
							req->dispose(req);
							return -1;
						}
					}
					break;
				}

				zend_async_waker_clean(coroutine);
			} while (!req->completed && EG(exception) == NULL);
		}

		/* IO closed externally while parked — stream/data may be freed. */
		if (UNEXPECTED(req->io_closed)) {
			if (EG(exception)) {
				zend_clear_exception();
			}
			if (req->exception != NULL) {
				OBJ_RELEASE(req->exception);
				req->exception = NULL;
			}
			req->dispose(req);
			return -1;
		}

		if (UNEXPECTED(EG(exception)) || UNEXPECTED(req->exception != NULL)) {
			if (!(stream->flags & PHP_STREAM_FLAG_SUPPRESS_ERRORS)) {
				zend_object *exception = EG(exception) ? EG(exception) : req->exception;
				zval rv;
				const zval *message =
						zend_read_property_ex(exception->ce, exception, zend_known_strings[ZEND_STR_MESSAGE], 0, &rv);
				php_error_docref(NULL, E_NOTICE, "Read of %zu bytes failed with async IO error: %s",
						count, message ? Z_STRVAL_P(message) : "empty error");
			}
			if (EG(exception)) {
				zend_clear_exception();
			}
			if (req->exception != NULL) {
				OBJ_RELEASE(req->exception);
				req->exception = NULL;
			}
			req->dispose(req);
			return -1;
		}

		const ssize_t transferred = req->transferred;
		if (transferred == 0) {
			stream->eof = 1;
		}
		req->dispose(req);
		return transferred;
	}

	if (data->async_io != NULL && data->is_blocked && !data->sync_io_fallback) {
		php_fd_set_block(data->fd);
		data->sync_io_fallback = 1;
	}

	if (data->fd >= 0) {
#ifdef PHP_WIN32
		php_stdio_stream_data *self = (php_stdio_stream_data*)stream->abstract;

		if ((self->is_pipe || self->is_process_pipe) && !self->is_pipe_blocking) {
			HANDLE ph = (HANDLE)_get_osfhandle(data->fd);
			int retry = 0;
			DWORD avail_read = 0;

			do {
				/* Look ahead to get the available data amount to read. Do the same
					as read() does, however not blocking forever. In case it failed,
					no data will be read (better than block). */
				if (!PeekNamedPipe(ph, NULL, 0, NULL, &avail_read, NULL)) {
					break;
				}
				/* If there's nothing to read, wait in 10us periods. */
				if (0 == avail_read) {
					if (!self->is_blocked) {
						return 0;
					}
					usleep(10);
				}
			} while (0 == avail_read && retry++ < 3200000);

			/* Reduce the required data amount to what is available, otherwise read()
				will block.*/
			if (avail_read < count) {
				count = avail_read;
			}
		}
#endif
		ret = read(data->fd, buf,  PLAIN_WRAP_BUF_SIZE(count));

		if (ret == (size_t)-1 && errno == EINTR) {
			/* Read was interrupted, retry once,
			   If read still fails, give up with feof==0
			   so script can retry if desired */
			ret = read(data->fd, buf,  PLAIN_WRAP_BUF_SIZE(count));
		}

		if (ret < 0) {
			if (PHP_IS_TRANSIENT_ERROR(errno)) {
				/* Not an error. */
				ret = 0;
			} else if (errno == EINTR) {
				/* TODO: Should this be treated as a proper error or not? */
			} else {
				if (!(stream->flags & PHP_STREAM_FLAG_SUPPRESS_ERRORS)) {
					char errstr[256];
					php_error_docref(NULL, E_NOTICE, "Read of %zu bytes failed with errno=%d %s",
							count, errno, php_socket_strerror_s(errno, errstr, sizeof(errstr)));
				}

				/* TODO: Remove this special-case? */
				if (errno != EBADF) {
					stream->eof = 1;
				}
			}
		} else if (ret == 0) {
			stream->eof = 1;
		}

	} else {
#ifdef HAVE_FLUSHIO
		if (data->is_seekable && data->last_op == 'w')
			zend_fseek(data->file, 0, SEEK_CUR);
		data->last_op = 'r';
#endif

		ret = fread(buf, 1, count, data->file);

		stream->eof = feof(data->file);
	}

	if (EG(active)) {
		/* clear stat cache as atime got changed */
		php_clear_stat_cache(0, NULL, 0);
	}

	return ret;
}

static int php_stdiop_close(php_stream *stream, int close_handle)
{
	int ret;
	php_stdio_stream_data *data = (php_stdio_stream_data*)stream->abstract;

	assert(data != NULL);

	if (data->poll_event) {
		data->poll_event->base.dispose(&data->poll_event->base);
		data->poll_event = NULL;
	}

	if (data->async_io != NULL) {
		if (!close_handle) {
			data->async_io->state |= ZEND_ASYNC_IO_PRESERVE_FD;
		}
		/* Clear on_detach before dispose — data is about to be freed. */
		data->async_io->on_detach = NULL;
		const bool is_stream = ZEND_ASYNC_IO_IS_STREAM(data->async_io->type);
		ZEND_ASYNC_IO_CLOSE(data->async_io);
		data->async_io->event.dispose(&data->async_io->event);
		data->async_io = NULL;
		if (is_stream && !data->is_process_pipe) {
			data->fd = -1;
		}
		/* If FILE* was created via fdopen(dup(fd)) in php_stdiop_cast,
		 * it owns a separate fd copy that must be closed explicitly.
		 * The original fd (data->fd) will be closed by normal logic below.
		 * When close_handle is 0 (preserve mode), do NOT close the C FILE*
		 * as it may be stdout/stderr/stdin needed for error display during shutdown. */
		if (data->file != NULL && close_handle) {
			fclose(data->file);
			data->file = NULL;
		}
	}

#ifdef HAVE_MMAP
	if (data->last_mapped_addr) {
		munmap(data->last_mapped_addr, data->last_mapped_len);
		data->last_mapped_addr = NULL;
	}
#elif defined(PHP_WIN32)
	if (data->last_mapped_addr) {
		UnmapViewOfFile(data->last_mapped_addr);
		data->last_mapped_addr = NULL;
	}
	if (data->file_mapping) {
		CloseHandle(data->file_mapping);
		data->file_mapping = NULL;
	}
#endif

	if (close_handle) {
		if (data->file) {
			if (data->is_process_pipe) {
				errno = 0;
				ret = pclose(data->file);

#ifdef HAVE_SYS_WAIT_H
				if (WIFEXITED(ret)) {
					ret = WEXITSTATUS(ret);
				}
#endif
			} else {
				ret = fclose(data->file);
				data->file = NULL;
			}
		} else if (data->fd != -1) {
			ret = close(data->fd);
			data->fd = -1;
		} else {
			ret = 0; /* everything should be closed already -> success */
		}
		if (data->temp_name) {
#ifdef PHP_WIN32
			php_win32_ioutil_unlink(ZSTR_VAL(data->temp_name));
#else
			unlink(ZSTR_VAL(data->temp_name));
#endif
			/* temporary streams are never persistent */
			zend_string_release_ex(data->temp_name, 0);
			data->temp_name = NULL;
		}
	} else {
		ret = 0;
		data->file = NULL;
		data->fd = -1;
	}

	pefree(data, stream->is_persistent);

	return ret;
}

static int php_stdiop_flush(php_stream *stream)
{
	php_stdio_stream_data *data = (php_stdio_stream_data*)stream->abstract;

	assert(data != NULL);

	/*
	 * stdio buffers data in user land. By calling fflush(3), this
	 * data is sent to the kernel using write(2). fsync'ing is
	 * something completely different.
	 */
	if (data->file) {
		if (EG(active)) {
			/* clear stat cache as there might be a write so mtime and ctime might have changed */
			php_clear_stat_cache(0, NULL, 0);
		}
		return fflush(data->file);
	}
	return 0;
}


static int php_stdiop_sync(php_stream *stream, bool dataonly)
{
	php_stdio_stream_data *data = (php_stdio_stream_data*)stream->abstract;

	/* Async IO path: flush via async reactor, avoid fdopen() which creates
	 * a FILE* that conflicts with reactor's fd ownership. */
	if (data->async_io != NULL) {
		/* fsync/fdatasync is only meaningful for regular files */
		if (data->async_io->type == ZEND_ASYNC_IO_TYPE_PIPE
				|| data->async_io->type == ZEND_ASYNC_IO_TYPE_TTY) {
			return -1;
		}

		zend_async_io_req_t *req = ZEND_ASYNC_IO_FLUSH(data->async_io);
		if (UNEXPECTED(req == NULL)) {
			return -1;
		}

		if (!req->completed) {
			zend_coroutine_t *const coroutine = ZEND_ASYNC_CURRENT_COROUTINE;
			ZEND_ASYNC_WAKER_NEW(coroutine);
			zend_async_resume_when(coroutine, &data->async_io->event, false,
					zend_async_waker_callback_resolve, NULL);
			ZEND_ASYNC_SUSPEND();
			ZEND_ASYNC_WAKER_DESTROY(coroutine);
		}

		const int result = (int) req->result;
		req->dispose(req);
		return result;
	}

	FILE *fp;
	int fd;

	if (php_stream_cast(stream, PHP_STREAM_AS_STDIO, (void**)&fp, REPORT_ERRORS) == FAILURE) {
		return -1;
	}

	if (php_stdiop_flush(stream) == 0) {
		PHP_STDIOP_GET_FD(fd, data);
		if (dataonly) {
			return fdatasync(fd);
		} else {
			return fsync(fd);
		}
	}
	return -1;
}

static int php_stdiop_seek(php_stream *stream, zend_off_t offset, int whence, zend_off_t *newoffset)
{
	php_stdio_stream_data *data = (php_stdio_stream_data*)stream->abstract;

	assert(data != NULL);

	if (!data->is_seekable) {
		php_error_docref(NULL, E_WARNING, "Cannot seek on this stream");
		return -1;
	}

	if (data->fd >= 0) {
		const zend_off_t result = data->async_io != NULL
			? ZEND_ASYNC_IO_SEEK(data->async_io, offset, whence)
			: zend_lseek(data->fd, offset, whence);

		if (result == (zend_off_t)-1) {
			return -1;
		}

		*newoffset = result;
		return 0;

	} else {
		const int ret = zend_fseek(data->file, offset, whence);
		*newoffset = zend_ftell(data->file);
		return ret;
	}
}

static int php_stdiop_cast(php_stream *stream, int castas, void **ret)
{
	php_socket_t fd;
	php_stdio_stream_data *data = (php_stdio_stream_data*) stream->abstract;

	assert(data != NULL);

	/* as soon as someone touches the stdio layer, buffering may ensue,
	 * so we need to stop using the fd directly in that case */

	switch (castas)	{
		case PHP_STREAM_AS_STDIO:
			if (ret) {
				if (data->file == NULL) {
					int fd_for_fdopen = data->fd;

					/* TEMPORARY: When async IO owns the fd, dup() before fdopen()
					 * to avoid dual ownership.  fdopen() wraps the fd, but libuv
					 * also holds it; on close libuv calls _close(fd), orphaning
					 * the FILE* in CRT — which crashes on Windows during exit().
					 * With dup, FILE* gets its own fd copy.
					 * TODO: analyze all PHP_STREAM_AS_STDIO call sites for a
					 * proper long-term solution (see ext/async/TODO.md). */
					if (data->async_io != NULL) {
#ifdef PHP_WIN32
						fd_for_fdopen = _dup(data->fd);
#else
						fd_for_fdopen = dup(data->fd);
#endif
						if (fd_for_fdopen < 0) {
							return FAILURE;
						}
					}

					char fixed_mode[5];
					php_stream_mode_sanitize_fdopen_fopencookie(stream, fixed_mode);
					data->file = fdopen(fd_for_fdopen, fixed_mode);
					if (data->file == NULL) {
						if (data->async_io != NULL) {
#ifdef PHP_WIN32
							_close(fd_for_fdopen);
#else
							close(fd_for_fdopen);
#endif
						}
						return FAILURE;
					}
				}

				*(FILE**)ret = data->file;
				/* When async IO dup'd the fd, the original fd is still valid
				 * and must remain tracked so plain_wrapper can close it. */
				if (data->async_io == NULL) {
					data->fd = SOCK_ERR;
				}
			}
			return SUCCESS;

		case PHP_STREAM_AS_FD_FOR_SELECT:
			PHP_STDIOP_GET_FD(fd, data);
			if (SOCK_ERR == fd) {
				return FAILURE;
			}
			if (ret) {
				*(php_socket_t *)ret = fd;
			}
			return SUCCESS;

		case PHP_STREAM_AS_FD:
			PHP_STDIOP_GET_FD(fd, data);

			if (SOCK_ERR == fd) {
				return FAILURE;
			}
			if (data->file) {
				fflush(data->file);
			}
			if (ret) {
				*(php_socket_t *)ret = fd;
			}
			return SUCCESS;
		default:
			return FAILURE;
	}
}

static int php_stdiop_stat(php_stream *stream, php_stream_statbuf *ssb)
{
	int ret;
	php_stdio_stream_data *data = (php_stdio_stream_data*) stream->abstract;

	assert(data != NULL);
	if((ret = do_fstat(data, 1)) == 0) {
		memcpy(&ssb->sb, &data->sb, sizeof(ssb->sb));
	}

	return ret;
}

/* Thread pool flock support */
typedef struct {
	int fd;
	int operation;
	int result;
	int error_code;
} php_stdiop_flock_task_data_t;

static void php_stdiop_flock_task_run(zend_async_task_t *task)
{
	php_stdiop_flock_task_data_t *flock_data = (php_stdiop_flock_task_data_t *) task->data;
	flock_data->result = flock(flock_data->fd, flock_data->operation);

	if (flock_data->result != 0) {
		flock_data->error_code = errno;
	}
}

static int php_stdiop_set_option(php_stream *stream, int option, int value, void *ptrparam)
{
	php_stdio_stream_data *data = (php_stdio_stream_data*) stream->abstract;
	size_t size;
	int fd;
#ifdef O_NONBLOCK
	/* FIXME: make this work for win32 */
	int flags;
	int oldval;
#endif

	PHP_STDIOP_GET_FD(fd, data);

	switch(option) {
		case PHP_STREAM_OPTION_BLOCKING:
			if (fd == -1)
				return -1;
#ifdef O_NONBLOCK
			flags = fcntl(fd, F_GETFL, 0);
			oldval = (flags & O_NONBLOCK) ? 0 : 1;
			if (value)
				flags &= ~O_NONBLOCK;
			else
				flags |= O_NONBLOCK;

			if (-1 == fcntl(fd, F_SETFL, flags))
				return -1;
			data->is_blocked = value ? 1 : 0;
			return oldval;
#else
			/* Windows has no fcntl/O_NONBLOCK, but when async IO is active
			 * the is_blocked flag controls whether reads suspend the coroutine
			 * (blocking) or return immediately (non-blocking). */
			if (data->async_io != NULL) {
				const int was_blocked = data->is_blocked;
				data->is_blocked = value ? 1 : 0;
				return was_blocked;
			}
			return -1;
#endif

		case PHP_STREAM_OPTION_WRITE_BUFFER:

			if (data->file == NULL) {
				return -1;
			}

			if (ptrparam)
				size = *(size_t *)ptrparam;
			else
				size = BUFSIZ;

			switch(value) {
				case PHP_STREAM_BUFFER_NONE:
					return setvbuf(data->file, NULL, _IONBF, 0);

				case PHP_STREAM_BUFFER_LINE:
					return setvbuf(data->file, NULL, _IOLBF, size);

				case PHP_STREAM_BUFFER_FULL:
					return setvbuf(data->file, NULL, _IOFBF, size);

				default:
					return -1;
			}
			break;

		case PHP_STREAM_OPTION_LOCKING:
			if (fd == -1) {
				return -1;
			}

			if ((uintptr_t) ptrparam == PHP_STREAM_LOCK_SUPPORTED) {
				return 0;
			}

			/* Use thread pool for potentially blocking lock operations inside coroutines.
			 * LOCK_UN (unlock) and LOCK_NB (non-blocking) never block, so skip the thread pool. */
			if (!(value & LOCK_NB) && (value & ~LOCK_NB) != LOCK_UN
					&& !ZEND_ASYNC_IS_OFF && !ZEND_ASYNC_IS_SCHEDULER_CONTEXT
					&& zend_async_thread_pool_is_enabled()) {

				ZEND_ASYNC_SCHEDULER_INIT();

				if (UNEXPECTED(EG(exception))) {
					return -1;
				}

				/* Inline-tail so flock_data outlives caller on cancel —
				 * worker keeps writing after AsyncCancellation unwinds the frame. */
				zend_async_task_t *task = ZEND_ASYNC_NEW_TASK_EX(
					php_stdiop_flock_task_run, NULL,
					sizeof(php_stdiop_flock_task_data_t));
				if (UNEXPECTED(task == NULL)) {
					return -1;
				}
				php_stdiop_flock_task_data_t *flock_data =
					(php_stdiop_flock_task_data_t *)
						((char *)task + task->base.extra_offset);
				flock_data->fd = fd;
				flock_data->operation = value;
				task->data = flock_data;

				zend_coroutine_t *const coroutine = ZEND_ASYNC_CURRENT_COROUTINE;
				ZEND_ASYNC_WAKER_NEW(coroutine);

				if (UNEXPECTED(!zend_async_resume_when(coroutine, &task->base, true,
					zend_async_waker_callback_resolve, NULL))) {
					ZEND_ASYNC_WAKER_DESTROY(coroutine);
					return -1;
				}

				if (UNEXPECTED(!ZEND_ASYNC_QUEUE_TASK(task))) {
					ZEND_ASYNC_WAKER_DESTROY(coroutine);
					return -1;
				}

				/* Pin task across SUSPEND: waker cleanup disposes it before SUSPEND returns,
				 * freeing the inline-tail flock_data we still need to read. */
				ZEND_ASYNC_EVENT_ADD_REF(&task->base);

				if (UNEXPECTED(!ZEND_ASYNC_SUSPEND())) {
					ZEND_ASYNC_WAKER_DESTROY(coroutine);
					ZEND_ASYNC_EVENT_RELEASE(&task->base);
					return -1;
				}

				const int flock_result = flock_data->result;
				const int flock_errno = flock_data->error_code;
				ZEND_ASYNC_EVENT_RELEASE(&task->base);

				if (flock_result == 0) {
					data->lock_flag = value;
					return 0;
				}

				errno = flock_errno;
				return -1;
			}

			if (!flock(fd, value)) {
				data->lock_flag = value;
				return 0;
			} else {
				return -1;
			}
			break;

		case PHP_STREAM_OPTION_MMAP_API:
#ifdef HAVE_MMAP
			{
				php_stream_mmap_range *range = (php_stream_mmap_range*)ptrparam;
				int prot, flags;

				switch (value) {
					case PHP_STREAM_MMAP_SUPPORTED:
						return fd == -1 ? PHP_STREAM_OPTION_RETURN_ERR : PHP_STREAM_OPTION_RETURN_OK;

					case PHP_STREAM_MMAP_MAP_RANGE:
						if (do_fstat(data, 1) != 0) {
							return PHP_STREAM_OPTION_RETURN_ERR;
						}
						if (range->offset > data->sb.st_size) {
							range->offset = data->sb.st_size;
						}
						if (range->length == 0 ||
								range->length > data->sb.st_size - range->offset) {
							range->length = data->sb.st_size - range->offset;
						}
						switch (range->mode) {
							case PHP_STREAM_MAP_MODE_READONLY:
								prot = PROT_READ;
								flags = MAP_PRIVATE;
								break;
							case PHP_STREAM_MAP_MODE_READWRITE:
								prot = PROT_READ | PROT_WRITE;
								flags = MAP_PRIVATE;
								break;
							case PHP_STREAM_MAP_MODE_SHARED_READONLY:
								prot = PROT_READ;
								flags = MAP_SHARED;
								break;
							case PHP_STREAM_MAP_MODE_SHARED_READWRITE:
								prot = PROT_READ | PROT_WRITE;
								flags = MAP_SHARED;
								break;
							default:
								return PHP_STREAM_OPTION_RETURN_ERR;
						}
						range->mapped = (char*)mmap(NULL, range->length, prot, flags, fd, range->offset);
						if (range->mapped == (char*)MAP_FAILED) {
							range->mapped = NULL;
							return PHP_STREAM_OPTION_RETURN_ERR;
						}
						/* remember the mapping */
						data->last_mapped_addr = range->mapped;
						data->last_mapped_len = range->length;
						return PHP_STREAM_OPTION_RETURN_OK;

					case PHP_STREAM_MMAP_UNMAP:
						if (data->last_mapped_addr) {
							munmap(data->last_mapped_addr, data->last_mapped_len);
							data->last_mapped_addr = NULL;

							return PHP_STREAM_OPTION_RETURN_OK;
						}
						return PHP_STREAM_OPTION_RETURN_ERR;
				}
			}
#elif defined(PHP_WIN32)
			{
				php_stream_mmap_range *range = (php_stream_mmap_range*)ptrparam;
				HANDLE hfile = (HANDLE)_get_osfhandle(fd);
				DWORD prot, acc, loffs = 0, hoffs = 0, delta = 0;
				LARGE_INTEGER file_size;

				switch (value) {
					case PHP_STREAM_MMAP_SUPPORTED:
						return hfile == INVALID_HANDLE_VALUE ? PHP_STREAM_OPTION_RETURN_ERR : PHP_STREAM_OPTION_RETURN_OK;

					case PHP_STREAM_MMAP_MAP_RANGE:
						switch (range->mode) {
							case PHP_STREAM_MAP_MODE_READONLY:
								prot = PAGE_READONLY;
								acc = FILE_MAP_READ;
								break;
							case PHP_STREAM_MAP_MODE_READWRITE:
								prot = PAGE_READWRITE;
								acc = FILE_MAP_READ | FILE_MAP_WRITE;
								break;
							case PHP_STREAM_MAP_MODE_SHARED_READONLY:
								prot = PAGE_READONLY;
								acc = FILE_MAP_READ;
								/* TODO: we should assign a name for the mapping */
								break;
							case PHP_STREAM_MAP_MODE_SHARED_READWRITE:
								prot = PAGE_READWRITE;
								acc = FILE_MAP_READ | FILE_MAP_WRITE;
								/* TODO: we should assign a name for the mapping */
								break;
							default:
								return PHP_STREAM_OPTION_RETURN_ERR;
						}

						/* create a mapping capable of viewing the whole file (this costs no real resources) */
						data->file_mapping = CreateFileMapping(hfile, NULL, prot, 0, 0, NULL);

						if (data->file_mapping == NULL) {
							return PHP_STREAM_OPTION_RETURN_ERR;
						}

						if (!GetFileSizeEx(hfile, &file_size)) {
							CloseHandle(data->file_mapping);
							data->file_mapping = NULL;
							return PHP_STREAM_OPTION_RETURN_ERR;
						}
# if defined(_WIN64)
						size = file_size.QuadPart;
# else
						if (file_size.HighPart) {
							CloseHandle(data->file_mapping);
							data->file_mapping = NULL;
							return PHP_STREAM_OPTION_RETURN_ERR;
						} else {
							size = file_size.LowPart;
						}
# endif
						if (range->offset > size) {
							range->offset = size;
						}
						if (range->length == 0 || range->length > size - range->offset) {
							range->length = size - range->offset;
						}

						/* figure out how big a chunk to map to be able to view the part that we need */
						if (range->offset != 0) {
							SYSTEM_INFO info;
							DWORD gran;

							GetSystemInfo(&info);
							gran = info.dwAllocationGranularity;
							ZEND_ASSERT(gran != 0 && (gran & (gran - 1)) == 0);
							size_t rounded_offset = (range->offset / gran) * gran;
							delta = range->offset - rounded_offset;
							loffs = (DWORD)rounded_offset;
#ifdef _WIN64
							hoffs = (DWORD)(rounded_offset >> 32);
#else
							hoffs = 0;
#endif
						}

						/* MapViewOfFile()ing zero bytes would map to the end of the file; match *nix behavior instead */
						if (range->length + delta == 0) {
							return PHP_STREAM_OPTION_RETURN_ERR;
						}

						data->last_mapped_addr = MapViewOfFile(data->file_mapping, acc, hoffs, loffs, range->length + delta);

						if (data->last_mapped_addr) {
							/* give them back the address of the start offset they requested */
							range->mapped = data->last_mapped_addr + delta;
							return PHP_STREAM_OPTION_RETURN_OK;
						}

						CloseHandle(data->file_mapping);
						data->file_mapping = NULL;

						return PHP_STREAM_OPTION_RETURN_ERR;

					case PHP_STREAM_MMAP_UNMAP:
						if (data->last_mapped_addr) {
							UnmapViewOfFile(data->last_mapped_addr);
							data->last_mapped_addr = NULL;
							CloseHandle(data->file_mapping);
							data->file_mapping = NULL;
							return PHP_STREAM_OPTION_RETURN_OK;
						}
						return PHP_STREAM_OPTION_RETURN_ERR;

					default:
						return PHP_STREAM_OPTION_RETURN_ERR;
				}
			}

#endif
			return PHP_STREAM_OPTION_RETURN_NOTIMPL;

		case PHP_STREAM_OPTION_SYNC_API:
			switch (value) {
				case PHP_STREAM_SYNC_SUPPORTED:
					return fd == -1 ? PHP_STREAM_OPTION_RETURN_ERR : PHP_STREAM_OPTION_RETURN_OK;
				case PHP_STREAM_SYNC_FSYNC:
					return php_stdiop_sync(stream, 0) == 0 ? PHP_STREAM_OPTION_RETURN_OK : PHP_STREAM_OPTION_RETURN_ERR;
				case PHP_STREAM_SYNC_FDSYNC:
					return php_stdiop_sync(stream, 1) == 0 ? PHP_STREAM_OPTION_RETURN_OK : PHP_STREAM_OPTION_RETURN_ERR;
			}
			/* Invalid option passed */
			return PHP_STREAM_OPTION_RETURN_ERR;

		case PHP_STREAM_OPTION_TRUNCATE_API:
			switch (value) {
				case PHP_STREAM_TRUNCATE_SUPPORTED:
					return fd == -1 ? PHP_STREAM_OPTION_RETURN_ERR : PHP_STREAM_OPTION_RETURN_OK;

				case PHP_STREAM_TRUNCATE_SET_SIZE: {
					ptrdiff_t new_size = *(ptrdiff_t*)ptrparam;
					if (new_size < 0) {
						return PHP_STREAM_OPTION_RETURN_ERR;
					}
#ifdef PHP_WIN32
					HANDLE h = (HANDLE) _get_osfhandle(fd);
					if (INVALID_HANDLE_VALUE == h) {
						return PHP_STREAM_OPTION_RETURN_ERR;
					}

					LARGE_INTEGER sz, old_sz;
					sz.QuadPart = 0;

					if (!SetFilePointerEx(h, sz, &old_sz, FILE_CURRENT)) {
						return PHP_STREAM_OPTION_RETURN_ERR;
					}

#ifdef _WIN64
					sz.QuadPart = new_size;
#else
					sz.HighPart = 0;
					sz.LowPart = new_size;
#endif
					if (!SetFilePointerEx(h, sz, NULL, FILE_BEGIN)) {
						return PHP_STREAM_OPTION_RETURN_ERR;
					}
					if (0 == SetEndOfFile(h)) {
						return PHP_STREAM_OPTION_RETURN_ERR;
					}
					if (!SetFilePointerEx(h, old_sz, NULL, FILE_BEGIN)) {
						return PHP_STREAM_OPTION_RETURN_ERR;
					}
					return PHP_STREAM_OPTION_RETURN_OK;
#else
					return ftruncate(fd, new_size) == 0 ? PHP_STREAM_OPTION_RETURN_OK : PHP_STREAM_OPTION_RETURN_ERR;
#endif
				}
			}
			return PHP_STREAM_OPTION_RETURN_NOTIMPL;

#ifdef PHP_WIN32
		case PHP_STREAM_OPTION_PIPE_BLOCKING:
			data->is_pipe_blocking = value;
			return PHP_STREAM_OPTION_RETURN_OK;
#endif
		case PHP_STREAM_OPTION_META_DATA_API:
			if (fd == -1) {
				return -1;
			}

			add_assoc_bool((zval*)ptrparam, "timed_out", data->timeout_event);
			if (data->async_io != NULL) {
				/* When async IO is active the fd is non-blocking at the OS level,
				 * but the logical blocking mode is tracked in is_blocked. */
				add_assoc_bool((zval*)ptrparam, "blocked", data->is_blocked);
			} else {
#ifdef O_NONBLOCK
				flags = fcntl(fd, F_GETFL, 0);
				add_assoc_bool((zval*)ptrparam, "blocked", (flags & O_NONBLOCK)? 0 : 1);
#else
				add_assoc_bool((zval*)ptrparam, "blocked", 1);
#endif
			}
			add_assoc_bool((zval*)ptrparam, "eof", stream->eof);
			return PHP_STREAM_OPTION_RETURN_OK;
		case PHP_STREAM_OPTION_ASYNC_EVENT_HANDLE:
			if (fd == -1) {
				return PHP_STREAM_OPTION_RETURN_NOTIMPL;
			}
#ifdef PHP_WIN32
			return PHP_STREAM_OPTION_RETURN_NOTIMPL;
#else
			{
				zend_async_poll_event_t **handle_ptr = (zend_async_poll_event_t **)ptrparam;

				if (data->poll_event == NULL) {
					data->poll_event = ZEND_ASYNC_NEW_POLL_EVENT(fd, 0, 0);
					if (UNEXPECTED(EG(exception) != NULL)) {
						return PHP_STREAM_OPTION_RETURN_ERR;
					}

					data->poll_event->base.start(&data->poll_event->base);

					if (UNEXPECTED(EG(exception) != NULL)) {
						return PHP_STREAM_OPTION_RETURN_ERR;
					}
				}

				zend_async_poll_proxy_t *proxy = ZEND_ASYNC_NEW_POLL_PROXY_EVENT(data->poll_event, value);
				if (UNEXPECTED(EG(exception) != NULL)) {
					return PHP_STREAM_OPTION_RETURN_ERR;
				}

				*handle_ptr = (zend_async_poll_event_t*)proxy;
				proxy->base.ref_count = 0;

				return PHP_STREAM_OPTION_RETURN_OK;
			}
#endif

		case PHP_STREAM_OPTION_ASYNC_IO:
			if (data->async_io != NULL && ptrparam != NULL) {
				*(zend_async_io_t **)ptrparam = data->async_io;
				return PHP_STREAM_OPTION_RETURN_OK;
			}
			return PHP_STREAM_OPTION_RETURN_NOTIMPL;

		case PHP_STREAM_OPTION_DETACH_ASYNC_IO:
			if (data->poll_event) {
				data->poll_event->base.dispose(&data->poll_event->base);
				data->poll_event = NULL;
			}

			if (data->async_io != NULL) {
				data->async_io->on_detach = NULL;
				data->async_io->state |= ZEND_ASYNC_IO_PRESERVE_FD;
				ZEND_ASYNC_IO_CLOSE(data->async_io);
				data->async_io->event.dispose(&data->async_io->event);
				data->async_io = NULL;
			}

			return PHP_STREAM_OPTION_RETURN_OK;

		case PHP_STREAM_OPTION_ALIGN_POSITION:
			if (data->async_io != NULL && ptrparam != NULL && zend_async_io_seek_fn != NULL) {
				zend_off_t *pos = (zend_off_t *)ptrparam;
				ZEND_ASYNC_IO_SEEK(data->async_io, *pos, SEEK_SET);
				return PHP_STREAM_OPTION_RETURN_OK;
			}
			return PHP_STREAM_OPTION_RETURN_NOTIMPL;

		case PHP_STREAM_OPTION_READ_TIMEOUT:
			if (data->is_pipe) {
				data->timeout = *(struct timeval *)ptrparam;
				data->timeout_event = false;
				return PHP_STREAM_OPTION_RETURN_OK;
			}
			return PHP_STREAM_OPTION_RETURN_NOTIMPL;

		default:
			return PHP_STREAM_OPTION_RETURN_NOTIMPL;
	}
}

/* This should be "const", but phpdbg overwrite it */
PHPAPI php_stream_ops	php_stream_stdio_ops = {
	php_stdiop_write, php_stdiop_read,
	php_stdiop_close, php_stdiop_flush,
	"STDIO",
	php_stdiop_seek,
	php_stdiop_cast,
	php_stdiop_stat,
	php_stdiop_set_option
};
/* }}} */

/* {{{ plain files opendir/readdir implementation */
static ssize_t php_plain_files_dirstream_read(php_stream *stream, char *buf, size_t count)
{
	DIR *dir = (DIR*)stream->abstract;
	struct dirent *result;
	php_stream_dirent *ent = (php_stream_dirent*)buf;

	/* avoid problems if someone mis-uses the stream */
	if (count != sizeof(php_stream_dirent))
		return -1;

	result = readdir(dir);
	if (result) {
		size_t len = strlen(result->d_name);
		if (UNEXPECTED(len >= sizeof(ent->d_name))) {
			return -1;
		}
		/* Include null byte */
		memcpy(ent->d_name, result->d_name, len+1);
#ifdef _DIRENT_HAVE_D_TYPE
		ent->d_type = result->d_type;
#else
		ent->d_type = DT_UNKNOWN;
#endif
		return sizeof(php_stream_dirent);
	}
	return 0;
}

static int php_plain_files_dirstream_close(php_stream *stream, int close_handle)
{
	return closedir((DIR *)stream->abstract);
}

static int php_plain_files_dirstream_rewind(php_stream *stream, zend_off_t offset, int whence, zend_off_t *newoffs)
{
	rewinddir((DIR *)stream->abstract);
	return 0;
}

static const php_stream_ops	php_plain_files_dirstream_ops = {
	NULL, php_plain_files_dirstream_read,
	php_plain_files_dirstream_close, NULL,
	"dir",
	php_plain_files_dirstream_rewind,
	NULL, /* cast */
	NULL, /* stat */
	NULL  /* set_option */
};

static php_stream *php_plain_files_dir_opener(php_stream_wrapper *wrapper, const char *path, const char *mode,
		int options, zend_string **opened_path, php_stream_context *context STREAMS_DC)
{
	DIR *dir = NULL;
	php_stream *stream = NULL;

	if (options & STREAM_USE_GLOB_DIR_OPEN) {
		return php_glob_stream_wrapper.wops->dir_opener((php_stream_wrapper*)&php_glob_stream_wrapper, path, mode, options, opened_path, context STREAMS_REL_CC);
	}

	if (((options & STREAM_DISABLE_OPEN_BASEDIR) == 0) && php_check_open_basedir(path)) {
		return NULL;
	}

	dir = VCWD_OPENDIR(path);

#ifdef PHP_WIN32
	if (!dir) {
		php_win32_docref1_from_error(GetLastError(), path);
	}

	if (dir && dir->finished) {
		closedir(dir);
		dir = NULL;
	}
#endif
	if (dir) {
		stream = php_stream_alloc(&php_plain_files_dirstream_ops, dir, 0, mode);
		if (stream == NULL)
			closedir(dir);
	}

	return stream;
}
/* }}} */

/* {{{ php_stream_fopen */
PHPAPI php_stream *_php_stream_fopen(const char *filename, const char *mode, zend_string **opened_path, int options STREAMS_DC)
{
	char realpath[MAXPATHLEN];
	int open_flags;
	int fd;
	php_stream *ret;
	int persistent = options & STREAM_OPEN_PERSISTENT;
	char *persistent_id = NULL;

	if (FAILURE == php_stream_parse_fopen_modes(mode, &open_flags)) {
		php_stream_wrapper_log_error(&php_plain_files_wrapper, options, "`%s' is not a valid mode for fopen", mode);
		return NULL;
	}

	if (options & STREAM_ASSUME_REALPATH) {
		strlcpy(realpath, filename, sizeof(realpath));
	} else {
		if (expand_filepath(filename, realpath) == NULL) {
			return NULL;
		}
	}

	if (persistent) {
		spprintf(&persistent_id, 0, "streams_stdio_%d_%s", open_flags, realpath);
		switch (php_stream_from_persistent_id(persistent_id, &ret)) {
			case PHP_STREAM_PERSISTENT_SUCCESS:
				if (opened_path) {
					//TODO: avoid reallocation???
					*opened_path = zend_string_init(realpath, strlen(realpath), 0);
				}
				ZEND_FALLTHROUGH;

			case PHP_STREAM_PERSISTENT_FAILURE:
				efree(persistent_id);
				return ret;
		}
	}
#ifdef PHP_WIN32
	fd = php_win32_ioutil_open(realpath, open_flags, 0666);
#else
	fd = open(realpath, open_flags, 0666);
#endif
	if (fd != -1)	{

		if (options & STREAM_OPEN_FOR_INCLUDE) {
			ret = php_stream_fopen_from_fd_int_rel(fd, mode, persistent_id);
		} else {
			/* skip the lseek(SEEK_CUR) system call to
			 * determine the current offset because we
			 * know newly opened files are at offset zero
			 * (unless the file has been opened in
			 * O_APPEND mode) */
			ret = php_stream_fopen_from_fd_rel(fd, mode, persistent_id, (open_flags & O_APPEND) == 0);
		}

		if (EG(active)) {
			/* clear stat cache as mtime and ctime might got changed - phar can use stream before
			 * cache is initialized so we need to check if the execution is active. */
			php_clear_stat_cache(0, NULL, 0);
		}

		if (ret)	{
			if (opened_path) {
				*opened_path = zend_string_init(realpath, strlen(realpath), 0);
			}
			if (persistent_id) {
				efree(persistent_id);
			}

			/* WIN32 always set ISREG flag */
#ifndef PHP_WIN32
			/* sanity checks for include/require.
			 * We check these after opening the stream, so that we save
			 * on fstat() syscalls */
			if (options & STREAM_OPEN_FOR_INCLUDE) {
				php_stdio_stream_data *self = (php_stdio_stream_data*)ret->abstract;
				int r;

				r = do_fstat(self, 0);
				if ((r == 0 && !S_ISREG(self->sb.st_mode))) {
					if (opened_path) {
						zend_string_release_ex(*opened_path, 0);
						*opened_path = NULL;
					}
					php_stream_close(ret);
					return NULL;
				}

				/* Make sure the fstat result is reused when we later try to get the
				 * file size. */
				self->no_forced_fstat = 1;
			}

			if (options & STREAM_USE_BLOCKING_PIPE) {
				php_stdio_stream_data *self = (php_stdio_stream_data*)ret->abstract;
				self->is_pipe_blocking = 1;
			}
#endif

			return ret;
		}
		close(fd);
	}
	if (persistent_id) {
		efree(persistent_id);
	}
	return NULL;
}
/* }}} */


static php_stream *php_plain_files_stream_opener(php_stream_wrapper *wrapper, const char *path, const char *mode,
		int options, zend_string **opened_path, php_stream_context *context STREAMS_DC)
{
	if (((options & STREAM_DISABLE_OPEN_BASEDIR) == 0) && php_check_open_basedir(path)) {
		return NULL;
	}

	return php_stream_fopen_rel(path, mode, opened_path, options);
}

static int php_plain_files_url_stater(php_stream_wrapper *wrapper, const char *url, int flags, php_stream_statbuf *ssb, php_stream_context *context)
{
	if (!(flags & PHP_STREAM_URL_STAT_IGNORE_OPEN_BASEDIR)) {
		if (strncasecmp(url, "file://", sizeof("file://") - 1) == 0) {
			url += sizeof("file://") - 1;
		}

		if (php_check_open_basedir_ex(url, (flags & PHP_STREAM_URL_STAT_QUIET) ? 0 : 1)) {
			return -1;
		}
	}

#ifdef PHP_WIN32
	if (flags & PHP_STREAM_URL_STAT_LINK) {
		return VCWD_LSTAT(url, &ssb->sb);
	}
#else
# ifdef HAVE_SYMLINK
	if (flags & PHP_STREAM_URL_STAT_LINK) {
		return VCWD_LSTAT(url, &ssb->sb);
	} else
# endif
#endif
		return VCWD_STAT(url, &ssb->sb);
}

static int php_plain_files_unlink(php_stream_wrapper *wrapper, const char *url, int options, php_stream_context *context)
{
	int ret;

	if (strncasecmp(url, "file://", sizeof("file://") - 1) == 0) {
		url += sizeof("file://") - 1;
	}

	if (php_check_open_basedir(url)) {
		return 0;
	}

	ret = VCWD_UNLINK(url);
	if (ret == -1) {
		if (options & REPORT_ERRORS) {
			char errstr[256];
			php_error_docref1(NULL, url, E_WARNING, "%s",
					php_socket_strerror_s(errno, errstr, sizeof(errstr)));
		}
		return 0;
	}

	/* Clear stat cache (and realpath cache) */
	php_clear_stat_cache(1, NULL, 0);

	return 1;
}

static int php_plain_files_rename(php_stream_wrapper *wrapper, const char *url_from, const char *url_to, int options, php_stream_context *context)
{
	int ret;

	if (!url_from || !url_to) {
		return 0;
	}

#ifdef PHP_WIN32
	if (!php_win32_check_trailing_space(url_from, strlen(url_from))) {
		php_win32_docref2_from_error(ERROR_INVALID_NAME, url_from, url_to);
		return 0;
	}
	if (!php_win32_check_trailing_space(url_to, strlen(url_to))) {
		php_win32_docref2_from_error(ERROR_INVALID_NAME, url_from, url_to);
		return 0;
	}
#endif

	if (strncasecmp(url_from, "file://", sizeof("file://") - 1) == 0) {
		url_from += sizeof("file://") - 1;
	}

	if (strncasecmp(url_to, "file://", sizeof("file://") - 1) == 0) {
		url_to += sizeof("file://") - 1;
	}

	if (php_check_open_basedir(url_from) || php_check_open_basedir(url_to)) {
		return 0;
	}

	ret = VCWD_RENAME(url_from, url_to);

	if (ret == -1) {
#ifndef PHP_WIN32
		char errstr[256];
# ifdef EXDEV
		if (errno == EXDEV) {
			zend_stat_t sb;
# if !defined(ZTS) && !defined(TSRM_WIN32)
			/* not sure what to do in ZTS case, umask is not thread-safe */
			int oldmask = umask(077);
# endif
			int success = 0;
			if (php_copy_file(url_from, url_to) == SUCCESS) {
				if (VCWD_STAT(url_from, &sb) == 0) {
					success = 1;
#  ifndef TSRM_WIN32
					/*
					 * Try to set user and permission info on the target.
					 * If we're not root, then some of these may fail.
					 * We try chown first, to set proper group info, relying
					 * on the system environment to have proper umask to not allow
					 * access to the file in the meantime.
					 */
					if (VCWD_CHOWN(url_to, sb.st_uid, sb.st_gid)) {
						php_error_docref2(NULL, url_from, url_to, E_WARNING, "%s",
								php_socket_strerror_s(errno, errstr, sizeof(errstr)));
						if (errno != EPERM) {
							success = 0;
						}
					}

					if (success) {
						if (VCWD_CHMOD(url_to, sb.st_mode)) {
							php_error_docref2(NULL, url_from, url_to, E_WARNING, "%s",
									php_socket_strerror_s(errno, errstr, sizeof(errstr)));
							if (errno != EPERM) {
								success = 0;
							}
						}
					}
#  endif
					if (success) {
						VCWD_UNLINK(url_from);
					}
				} else {
					php_error_docref2(NULL, url_from, url_to, E_WARNING, "%s",
							php_socket_strerror_s(errno, errstr, sizeof(errstr)));
				}
			} else {
				php_error_docref2(NULL, url_from, url_to, E_WARNING, "%s",
						php_socket_strerror_s(errno, errstr, sizeof(errstr)));
			}
#  if !defined(ZTS) && !defined(TSRM_WIN32)
			umask(oldmask);
#  endif
			return success;
		}
# endif
#endif

#ifdef PHP_WIN32
		php_win32_docref2_from_error(GetLastError(), url_from, url_to);
#else
		php_error_docref2(NULL, url_from, url_to, E_WARNING, "%s",
				php_socket_strerror_s(errno, errstr, sizeof(errstr)));
#endif
		return 0;
	}

	/* Clear stat cache (and realpath cache) */
	php_clear_stat_cache(1, NULL, 0);

	return 1;
}

static int php_plain_files_mkdir(php_stream_wrapper *wrapper, const char *dir, int mode, int options, php_stream_context *context)
{
	if (strncasecmp(dir, "file://", sizeof("file://") - 1) == 0) {
		dir += sizeof("file://") - 1;
	}

	if (!(options & PHP_STREAM_MKDIR_RECURSIVE)) {
		if (php_check_open_basedir(dir)) {
			return 0;
		}

		int ret = VCWD_MKDIR(dir, (mode_t)mode);
		if (ret < 0 && (options & REPORT_ERRORS)) {
			php_error_docref(NULL, E_WARNING, "%s", strerror(errno));
			return 0;
		}

		return 1;
	}

	char buf[MAXPATHLEN];
	if (!expand_filepath_with_mode(dir, buf, NULL, 0, CWD_EXPAND)) {
		php_error_docref(NULL, E_WARNING, "Invalid path");
		return 0;
	}

	if (php_check_open_basedir(buf)) {
		return 0;
	}

	/* we look for directory separator from the end of string, thus hopefully reducing our work load */
	char *p;
	zend_stat_t sb;
	size_t dir_len = strlen(dir), offset = 0;
	char *e = buf +  strlen(buf);

	if ((p = memchr(buf, DEFAULT_SLASH, dir_len))) {
		offset = p - buf + 1;
	}

	if (p && dir_len == 1) {
		/* buf == "DEFAULT_SLASH" */
	}
	else {
		/* find a top level directory we need to create */
		while ( (p = strrchr(buf + offset, DEFAULT_SLASH)) || (offset != 1 && (p = strrchr(buf, DEFAULT_SLASH))) ) {
			int n = 0;

			*p = '\0';
			while (p > buf && *(p-1) == DEFAULT_SLASH) {
				++n;
				--p;
				*p = '\0';
			}
			if (VCWD_STAT(buf, &sb) == 0) {
				while (1) {
					*p = DEFAULT_SLASH;
					if (!n) break;
					--n;
					++p;
				}
				break;
			}
		}
	}

	if (!p) {
		p = buf;
	}
	char errstr[256];
	while (true) {
		int ret = VCWD_MKDIR(buf, (mode_t) mode);
		if (ret < 0 && errno != EEXIST) {
			if (options & REPORT_ERRORS) {
				php_error_docref(NULL, E_WARNING, "%s", php_socket_strerror_s(errno, errstr, sizeof(errstr)));
			}
			return 0;
		}

		bool replaced_slash = false;
		while (++p != e) {
			if (*p == '\0') {
				replaced_slash = true;
				*p = DEFAULT_SLASH;
				if (*(p+1) != '\0') {
					break;
				}
			}
		}
		if (p == e || !replaced_slash) {
			/* No more directories to create */
			/* issue a warning to client when the last directory was created failed */
			if (ret < 0) {
				if (options & REPORT_ERRORS) {
					php_error_docref(NULL, E_WARNING, "%s", php_socket_strerror_s(errno, errstr, sizeof(errstr)));
				}
				return 0;
			}
			return 1;
		}
	}
}

static int php_plain_files_rmdir(php_stream_wrapper *wrapper, const char *url, int options, php_stream_context *context)
{
	if (strncasecmp(url, "file://", sizeof("file://") - 1) == 0) {
		url += sizeof("file://") - 1;
	}

	if (php_check_open_basedir(url)) {
		return 0;
	}

	char errstr[256];
#ifdef PHP_WIN32
	if (!php_win32_check_trailing_space(url, strlen(url))) {
		php_error_docref1(NULL, url, E_WARNING, "%s", php_socket_strerror_s(ENOENT, errstr, sizeof(errstr)));
		return 0;
	}
#endif

	if (VCWD_RMDIR(url) < 0) {
		php_error_docref1(NULL, url, E_WARNING, "%s", php_socket_strerror_s(errno, errstr, sizeof(errstr)));
		return 0;
	}

	/* Clear stat cache (and realpath cache) */
	php_clear_stat_cache(1, NULL, 0);

	return 1;
}

static int php_plain_files_metadata(php_stream_wrapper *wrapper, const char *url, int option, void *value, php_stream_context *context)
{
	struct utimbuf *newtime;
#ifndef PHP_WIN32
	uid_t uid;
	gid_t gid;
#endif
	mode_t mode;
	int ret = 0;
	char errstr[256];

#ifdef PHP_WIN32
	if (!php_win32_check_trailing_space(url, strlen(url))) {
		php_error_docref1(NULL, url, E_WARNING, "%s", php_socket_strerror_s(ENOENT, errstr, sizeof(errstr)));
		return 0;
	}
#endif

	if (strncasecmp(url, "file://", sizeof("file://") - 1) == 0) {
		url += sizeof("file://") - 1;
	}

	if (php_check_open_basedir(url)) {
		return 0;
	}

	switch(option) {
		case PHP_STREAM_META_TOUCH:
			newtime = (struct utimbuf *)value;
			if (VCWD_ACCESS(url, F_OK) != 0) {
				FILE *file = VCWD_FOPEN(url, "w");
				if (file == NULL) {
					php_error_docref1(NULL, url, E_WARNING, "Unable to create file %s because %s", url,
							php_socket_strerror_s(errno, errstr, sizeof(errstr)));
					return 0;
				}
				fclose(file);
			}

			ret = VCWD_UTIME(url, newtime);
			break;
#ifndef PHP_WIN32
		case PHP_STREAM_META_OWNER_NAME:
		case PHP_STREAM_META_OWNER:
			if(option == PHP_STREAM_META_OWNER_NAME) {
				if(php_get_uid_by_name((char *)value, &uid) != SUCCESS) {
					php_error_docref1(NULL, url, E_WARNING, "Unable to find uid for %s", (char *)value);
					return 0;
				}
			} else {
				uid = (uid_t)*(long *)value;
			}
			ret = VCWD_CHOWN(url, uid, -1);
			break;
		case PHP_STREAM_META_GROUP:
		case PHP_STREAM_META_GROUP_NAME:
			if(option == PHP_STREAM_META_GROUP_NAME) {
				if(php_get_gid_by_name((char *)value, &gid) != SUCCESS) {
					php_error_docref1(NULL, url, E_WARNING, "Unable to find gid for %s", (char *)value);
					return 0;
				}
			} else {
				gid = (gid_t)*(long *)value;
			}
			ret = VCWD_CHOWN(url, -1, gid);
			break;
#endif
		case PHP_STREAM_META_ACCESS:
			mode = (mode_t)*(zend_long *)value;
			ret = VCWD_CHMOD(url, mode);
			break;
		default:
			zend_value_error("Unknown option %d for stream_metadata", option);
			return 0;
	}
	if (ret == -1) {
		php_error_docref1(NULL, url, E_WARNING, "Operation failed: %s",
				php_socket_strerror_s(errno, errstr, sizeof(errstr)));
		return 0;
	}
	php_clear_stat_cache(0, NULL, 0);
	return 1;
}


static const php_stream_wrapper_ops php_plain_files_wrapper_ops = {
	php_plain_files_stream_opener,
	NULL,
	NULL,
	php_plain_files_url_stater,
	php_plain_files_dir_opener,
	"plainfile",
	php_plain_files_unlink,
	php_plain_files_rename,
	php_plain_files_mkdir,
	php_plain_files_rmdir,
	php_plain_files_metadata
};

/* TODO: We have to make php_plain_files_wrapper writable to support SWOOLE */
PHPAPI /*const*/ php_stream_wrapper php_plain_files_wrapper = {
	&php_plain_files_wrapper_ops,
	NULL,
	0
};

/* {{{ php_stream_fopen_with_path */
PHPAPI php_stream *_php_stream_fopen_with_path(const char *filename, const char *mode, const char *path, zend_string **opened_path, int options STREAMS_DC)
{
	/* code ripped off from fopen_wrappers.c */
	char *pathbuf, *end;
	const char *ptr;
	char trypath[MAXPATHLEN];
	php_stream *stream;
	size_t filename_length;
	zend_string *exec_filename;

	if (opened_path) {
		*opened_path = NULL;
	}

	if(!filename) {
		return NULL;
	}

	filename_length = strlen(filename);
#ifndef PHP_WIN32
	(void) filename_length;
#endif

	/* Relative path open */
	if (*filename == '.' && (IS_SLASH(filename[1]) || filename[1] == '.')) {
		/* further checks, we could have ....... filenames */
		ptr = filename + 1;
		if (*ptr == '.') {
			while (*(++ptr) == '.');
			if (!IS_SLASH(*ptr)) { /* not a relative path after all */
				goto not_relative_path;
			}
		}


		if (((options & STREAM_DISABLE_OPEN_BASEDIR) == 0) && php_check_open_basedir(filename)) {
			return NULL;
		}

		return php_stream_fopen_rel(filename, mode, opened_path, options);
	}

not_relative_path:

	/* Absolute path open */
	if (IS_ABSOLUTE_PATH(filename, filename_length)) {

		if (((options & STREAM_DISABLE_OPEN_BASEDIR) == 0) && php_check_open_basedir(filename)) {
			return NULL;
		}

		return php_stream_fopen_rel(filename, mode, opened_path, options);
	}

#ifdef PHP_WIN32
	if (IS_SLASH(filename[0])) {
		size_t cwd_len;
		char *cwd;
		cwd = virtual_getcwd_ex(&cwd_len);
		/* getcwd() will return always return [DRIVE_LETTER]:/) on windows. */
		*(cwd+3) = '\0';

		if (snprintf(trypath, MAXPATHLEN, "%s%s", cwd, filename) >= MAXPATHLEN) {
			php_error_docref(NULL, E_NOTICE, "%s/%s path was truncated to %d", cwd, filename, MAXPATHLEN);
		}

		efree(cwd);

		if (((options & STREAM_DISABLE_OPEN_BASEDIR) == 0) && php_check_open_basedir(trypath)) {
			return NULL;
		}

		return php_stream_fopen_rel(trypath, mode, opened_path, options);
	}
#endif

	if (!path || !*path) {
		return php_stream_fopen_rel(filename, mode, opened_path, options);
	}

	/* check in provided path */
	/* append the calling scripts' current working directory
	 * as a fallback case
	 */
	if (zend_is_executing() &&
	    (exec_filename = zend_get_executed_filename_ex()) != NULL) {
		const char *exec_fname = ZSTR_VAL(exec_filename);
		size_t exec_fname_length = ZSTR_LEN(exec_filename);

		while ((--exec_fname_length < SIZE_MAX) && !IS_SLASH(exec_fname[exec_fname_length]));
		if (exec_fname_length<=0) {
			/* no path */
			pathbuf = estrdup(path);
		} else {
			size_t path_length = strlen(path);

			pathbuf = (char *) emalloc(exec_fname_length + path_length +1 +1);
			memcpy(pathbuf, path, path_length);
			pathbuf[path_length] = DEFAULT_DIR_SEPARATOR;
			memcpy(pathbuf+path_length+1, exec_fname, exec_fname_length);
			pathbuf[path_length + exec_fname_length +1] = '\0';
		}
	} else {
		pathbuf = estrdup(path);
	}

	ptr = pathbuf;

	while (ptr && *ptr) {
		end = (char *) strchr(ptr, DEFAULT_DIR_SEPARATOR);
		if (end != NULL) {
			*end = '\0';
			end++;
		}
		if (*ptr == '\0') {
			goto stream_skip;
		}
		if (snprintf(trypath, MAXPATHLEN, "%s/%s", ptr, filename) >= MAXPATHLEN) {
			php_error_docref(NULL, E_NOTICE, "%s/%s path was truncated to %d", ptr, filename, MAXPATHLEN);
		}

		if (((options & STREAM_DISABLE_OPEN_BASEDIR) == 0) && php_check_open_basedir_ex(trypath, 0)) {
			goto stream_skip;
		}

		stream = php_stream_fopen_rel(trypath, mode, opened_path, options);
		if (stream) {
			efree(pathbuf);
			return stream;
		}
stream_skip:
		ptr = end;
	} /* end provided path */

	efree(pathbuf);
	return NULL;

}
/* }}} */
