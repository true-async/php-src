/*
+----------------------------------------------------------------------+
  | Copyright (c) The PHP Group                                          |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | https://www.php.net/license/3_01.txt                                 |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Edmond                                                       |
  +----------------------------------------------------------------------+
*/
#ifndef NETWORK_ASYNC_H
#define NETWORK_ASYNC_H

#include "php_network.h"

BEGIN_EXTERN_C()

void network_async_set_socket_blocking(php_socket_t socket, bool blocking);
ZEND_API int php_poll2_async(php_pollfd *ufds, unsigned int nfds, int timeout);
ZEND_API int php_select_async(php_socket_t max_fd, fd_set *rfds, fd_set *wfds, fd_set *efds, struct timeval *tv);

ZEND_API int php_network_getaddrinfo_async(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
ZEND_API struct hostent* php_network_gethostbyname_async(const char *name);
ZEND_API zend_string* php_network_gethostbyaddr_async(const char *ip);
ZEND_API void php_network_freeaddrinfo_async(struct addrinfo *ai);
ZEND_API int php_network_getaddresses_async(const char *host, int socktype, struct sockaddr ***sal, zend_string **error_string);

END_EXTERN_C()

#endif //NETWORK_ASYNC_H