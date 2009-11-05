/*
 * Utility routines.
 *
 * Licensed under GPLv2, see file COPYING in this tarball for details.
 */
#ifndef ABRTLIB_H_
#define ABRTLIB_H_

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h> /* sockaddr_in, sockaddr_in6 etc */
#include <termios.h>
#include <time.h>
#include <unistd.h>
/* Try to pull in PATH_MAX */
#include <limits.h>
#include <sys/param.h>
#ifndef PATH_MAX
# define PATH_MAX 256
#endif
#include <pwd.h>
#include <grp.h>
/* C++ bits */
#include <string>
#include <sstream>
#include <iostream>

/* Some libc's forget to declare these, do it ourself */
extern char **environ;
#if defined(__GLIBC__) && __GLIBC__ < 2
int vdprintf(int d, const char *format, va_list ap);
#endif


#define NORETURN __attribute__ ((noreturn))


/* Logging */
enum {
	LOGMODE_NONE = 0,
	LOGMODE_STDIO = (1 << 0),
	LOGMODE_SYSLOG = (1 << 1),
	LOGMODE_BOTH = LOGMODE_SYSLOG + LOGMODE_STDIO,
};
extern const char *msg_prefix;
extern const char *msg_eol;
extern int logmode;
extern int xfunc_error_retval;
extern void xfunc_die(void) NORETURN;
extern void die_out_of_memory(void) NORETURN;
extern void error_msg(const char *s, ...) __attribute__ ((format (printf, 1, 2)));
extern void error_msg_and_die(const char *s, ...) __attribute__ ((noreturn, format (printf, 1, 2)));
extern void perror_msg(const char *s, ...) __attribute__ ((format (printf, 1, 2)));
extern void simple_perror_msg(const char *s);
extern void perror_msg_and_die(const char *s, ...) __attribute__ ((noreturn, format (printf, 1, 2)));
extern void simple_perror_msg_and_die(const char *s) NORETURN;
extern void perror_nomsg_and_die(void) NORETURN;
extern void perror_nomsg(void);
extern void verror_msg(const char *s, va_list p, const char *strerr);
/* error_msg() and log() do the same thing:
 * they log a message on stderr, syslog, etc.
 * They are only semantically different: error_msg() implies that
 * the logged event is a warning/error, while log() does not.
 * Another reason is that log() is such a short and nice name. :)
 * It's a macro, not function, since it collides with log() from math.h
 */
#undef log
#define log(...) error_msg(__VA_ARGS__)

/* Verbosity level */
extern int g_verbose;
/* VERB1 log("what you sometimes want to see, even on a production box") */
#define VERB1 if (g_verbose >= 1)
/* VERB2 log("debug message, not going into insanely small details") */
#define VERB2 if (g_verbose >= 2)
/* VERB3 log("lots and lots of details") */
#define VERB3 if (g_verbose >= 3)
/* there is no level > 3 */


void* xmalloc(size_t size);
void* xrealloc(void *ptr, size_t size);
void* xzalloc(size_t size);
char* xstrdup(const char *s);
char* xstrndup(const char *s, int n);

char* skip_whitespace(const char *s);
char* skip_non_whitespace(const char *s);

extern ssize_t safe_read(int fd, void *buf, size_t count);
// NB: will return short read on error, not -1,
// if some data was read before error occurred
extern ssize_t full_read(int fd, void *buf, size_t count);
extern void xread(int fd, void *buf, size_t count);
extern ssize_t safe_write(int fd, const void *buf, size_t count);
// NB: will return short write on error, not -1,
// if some data was written before error occurred
extern ssize_t full_write(int fd, const void *buf, size_t count);
extern void xwrite(int fd, const void *buf, size_t count);
extern void xwrite_str(int fd, const char *str);

void xpipe(int filedes[2]);
void xdup(int from);
void xdup2(int from, int to);
off_t xlseek(int fd, off_t offset, int whence);
void xsetenv(const char *key, const char *value);
int xsocket(int domain, int type, int protocol);
void xbind(int sockfd, struct sockaddr *my_addr, socklen_t addrlen);
void xlisten(int s, int backlog);
ssize_t xsendto(int s, const void *buf, size_t len, const struct sockaddr *to, socklen_t tolen);
void xstat(const char *name, struct stat *stat_buf);
/* Just testing dent->d_type == DT_REG is wrong: some filesystems
 * do not report the type, they report DT_UNKNOWN for every dirent
 * (and this is not a bug in filesystem, this is allowed by standards).
 * This function handles this case. Note: it returns 0 on symlinks
 * even if they point to regular files.
 */
int is_regular_file(struct dirent *dent, const char *dirname);

void xmove_fd(int from, int to);
int ndelay_on(int fd);
int ndelay_off(int fd);
int close_on_exec_on(int fd);
char* xasprintf(const char *format, ...);

int xopen(const char *pathname, int flags);
int xopen3(const char *pathname, int flags, int mode);
void xunlink(const char *pathname);

/* copyfd_XX print read/write errors and return -1 if they occur */
off_t copyfd_eof(int src_fd, int dst_fd);
off_t copyfd_size(int src_fd, int dst_fd, off_t size);
void copyfd_exact_size(int src_fd, int dst_fd, off_t size);

unsigned long long monotonic_ns(void);
unsigned long long monotonic_us(void);
unsigned monotonic_sec(void);

/* networking helpers */
typedef struct len_and_sockaddr {
	socklen_t len;
	union {
		struct sockaddr sa;
		struct sockaddr_in sin;
		struct sockaddr_in6 sin6;
	} u;
} len_and_sockaddr;
enum {
	LSA_LEN_SIZE = offsetof(len_and_sockaddr, u),
	LSA_SIZEOF_SA = sizeof(struct sockaddr) > sizeof(struct sockaddr_in6) ?
			sizeof(struct sockaddr) : sizeof(struct sockaddr_in6),
};
void setsockopt_reuseaddr(int fd);
int setsockopt_broadcast(int fd);
int setsockopt_bindtodevice(int fd, const char *iface);
len_and_sockaddr* get_sock_lsa(int fd);
void xconnect(int s, const struct sockaddr *s_addr, socklen_t addrlen);
unsigned lookup_port(const char *port, const char *protocol, unsigned default_port);
int get_nport(const struct sockaddr *sa);
void set_nport(len_and_sockaddr *lsa, unsigned port);
len_and_sockaddr* host_and_af2sockaddr(const char *host, int port, sa_family_t af);
len_and_sockaddr* xhost_and_af2sockaddr(const char *host, int port, sa_family_t af);
len_and_sockaddr* host2sockaddr(const char *host, int port);
len_and_sockaddr* xhost2sockaddr(const char *host, int port);
len_and_sockaddr* xdotted2sockaddr(const char *host, int port);
int xsocket_type(len_and_sockaddr **lsap, int family, int sock_type);
int xsocket_stream(len_and_sockaddr **lsap);
int create_and_bind_stream_or_die(const char *bindaddr, int port);
int create_and_bind_dgram_or_die(const char *bindaddr, int port);
int create_and_connect_stream_or_die(const char *peer, int port);
int xconnect_stream(const len_and_sockaddr *lsa);
char* xmalloc_sockaddr2host(const struct sockaddr *sa);
char* xmalloc_sockaddr2host_noport(const struct sockaddr *sa);
char* xmalloc_sockaddr2hostonly_noport(const struct sockaddr *sa);
char* xmalloc_sockaddr2dotted(const struct sockaddr *sa);
char* xmalloc_sockaddr2dotted_noport(const struct sockaddr *sa);


/* Random utility functions */

/* Returns malloc'ed block */
char *encode_base64(const void *src, int length);
bool dot_or_dotdot(const char *filename);
char *last_char_is(const char *s, int c);
bool string_to_bool(const char *s);


/* C++ style stuff */

std::string ssprintf(const char *format, ...);
std::string get_home_dir(int uid);
std::string concat_path_file(const char *path, const char *filename);

template <class T>
std::string
to_string( T x )
{
    std::ostringstream o;
    o << x;
    return o.str();
}

#endif
