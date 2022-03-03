// Linux
#include <fcntl.h>
#include <limits.h>
#include <pty.h>
#include <pwd.h>
#include <sys/wait.h>
#include <unistd.h>

// libc
#include <string.h>

// stdlib
#include <iostream>
#include <sstream>

// nst
#include "TTY.hxx"
#include "st.h"
#include "nst_config.h"

TTY g_tty;

// TODO: replace with libcosmos signal handler
void sigchld(int) {
	g_tty.sigChildEvent();
}

// TODO: check if we can C++ify this callback
void sendbreak(const Arg *)
{
	g_tty.sendBreak();
}

int TTY::create(const Params &pars) {

	if (!pars.out.empty()) {
		m_term->mode.set(Term::Mode::PRINT);
		if (pars.out == "-")
			iofd = STDOUT_FILENO;
		else
			iofd = open(pars.out.c_str(), O_WRONLY|O_CREAT, 0666);
		if (iofd < 0) {
			std::cerr << "Error opening " << pars.out << ": " << strerror(errno) << std::endl;
		}
	}

	// operate an a real TTY line, running stty on it
	if (!pars.line.empty()) {
		if ((m_cmdfd = open(pars.line.c_str(), O_RDWR)) < 0)
			die("open line '%s' failed: %s\n",
			    pars.line.c_str(), strerror(errno));
		dup2(m_cmdfd, 0);
		runStty(pars);
		return m_cmdfd;
	}

	// create a pseudo TTY
	int master, slave;

	/* seems to work fine on linux, openbsd and freebsd */
	if (openpty(&master, &slave, NULL, NULL, NULL) < 0)
		die("openpty failed: %s\n", strerror(errno));

	switch (m_pid = fork()) {
	case -1:
		die("fork failed: %s\n", strerror(errno));
		break;
	case 0:
		close(iofd);
		close(master);
		setsid(); /* create a new process group */
		dup2(slave, 0);
		dup2(slave, 1);
		dup2(slave, 2);
		if (ioctl(slave, TIOCSCTTY, NULL) < 0)
			die("ioctl TIOCSCTTY failed: %s\n", strerror(errno));
		if (slave > 2)
			close(slave);
		executeShell(pars);
		break;
	default:
		close(slave);
		m_cmdfd = master;
		signal(SIGCHLD, sigchld);
		break;
	}
	return m_cmdfd;
}

void TTY::runStty(const Params &pars) {
	std::stringstream cmd;
	cmd << nst::config::STTY_ARGS;

	bool first = true;

	for (auto &arg: pars.args) {
		if(first)
			first = false;
		else
			cmd << " ";
		cmd << arg;
	}

	if (system(cmd.str().c_str()) != 0)
		perror("Couldn't call stty");
}

size_t TTY::read() {
	static char buf[BUFSIZ];
	static int buflen = 0;
	int ret, written;

	/* append read bytes to unprocessed bytes */
	ret = ::read(m_cmdfd, buf+buflen, LEN(buf)-buflen);

	switch (ret) {
	case 0:
		// EOF
		exit(0);
	case -1:
		die("couldn't read from shell: %s\n", strerror(errno));
		return -1;
	default:
		buflen += ret;
		written = twrite(buf, buflen, 0);
		buflen -= written;
		/* keep any incomplete UTF-8 byte sequence for the next call */
		if (buflen > 0)
			memmove(buf, buf + written, buflen);
		return ret;
	}
}

void TTY::write(const char *s, size_t n, bool may_echo) {

	if (may_echo && m_term->mode.test(Term::Mode::TECHO))
		twrite(s, n, 1);

	if (!m_term->mode.test(Term::Mode::CRLF)) {
		writeRaw(s, n);
		return;
	}

	const char *next;

	/* This is similar to how the kernel handles ONLCR for ttys */
	while (n > 0) {
		if (*s == '\r') {
			next = s + 1;
			writeRaw("\r\n", 2);
		} else {
			next = (const char*)memchr(s, '\r', n);
			if (!next)
				next = s + n;
			writeRaw(s, next - s);
		}
		n -= next - s;
		s = next;
	}
}

void TTY::writeRaw(const char *s, size_t n) {
	fd_set wfd, rfd;
	ssize_t r;
	size_t lim = 256;

	/*
	 * Remember that we are using a pty, which might be a modem line.
	 * Writing too much will clog the line. That's why we are doing this
	 * dance.
	 * FIXME: Migrate the world to Plan 9.
	 */
	while (n > 0) {
		FD_ZERO(&wfd);
		FD_ZERO(&rfd);
		FD_SET(m_cmdfd, &wfd);
		FD_SET(m_cmdfd, &rfd);

		/* Check if we can write. */
		if (pselect(m_cmdfd+1, &rfd, &wfd, nullptr, nullptr, nullptr) < 0) {
			if (errno == EINTR)
				continue;
			die("select failed: %s\n", strerror(errno));
		}
		if (FD_ISSET(m_cmdfd, &wfd)) {
			/*
			 * Only write the bytes written by write() or the
			 * default of 256. This seems to be a reasonable value
			 * for a serial line. Bigger values might clog the I/O.
			 */
			if ((r = ::write(m_cmdfd, s, std::min(n, lim))) < 0)
				die("write error on tty: %s\n", strerror(errno));
			if ((size_t)r < n) {
				/*
				 * We weren't able to write out everything.
				 * This means the buffer is getting full
				 * again. Empty it.
				 */
				if (n < lim)
					lim = this->read();
				n -= r;
				s += r;
			} else {
				/* All bytes have been written. */
				break;
			}
		}
		if (FD_ISSET(m_cmdfd, &rfd))
			lim = this->read();
	}
}

void TTY::resize(int tw, int th) {
	struct winsize w;

	w.ws_row = m_term->row;
	w.ws_col = m_term->col;
	w.ws_xpixel = tw;
	w.ws_ypixel = th;

	if (ioctl(m_cmdfd, TIOCSWINSZ, &w) < 0) {
		std::cerr << "Couldn't set window size: " << strerror(errno) << "\n";
	}
}

void TTY::hangup() {
	/* Send SIGHUP to shell */
	kill(m_pid, SIGHUP);
}

void TTY::executeShell(const Params &pars)
{
	const char *sh, *arg = nullptr, *prog;
	const struct passwd *pw = nullptr;

	errno = 0;
	if ((pw = getpwuid(getuid())) == NULL) {
		if (errno)
			die("getpwuid: %s\n", strerror(errno));
		else
			die("who are you?\n");
	}

	if ((sh = getenv("SHELL")) == NULL)
		sh = (pw->pw_shell[0]) ? pw->pw_shell : pars.cmd.c_str();

	if (!pars.args.empty()) {
		prog = pars.args[0].c_str();
	} else if (nst::config::SCROLL) {
		prog = nst::config::SCROLL;
		arg = nst::config::UTMP ? nst::config::UTMP : sh;
	} else if (nst::config::UTMP) {
		prog = nst::config::UTMP;
	} else {
		prog = sh;
	}

	std::vector<const char*> cargs;

	if (!pars.args.empty()) {
		for (auto &v: pars.args) {
			cargs.push_back(v.c_str());
		}
		cargs.push_back(nullptr);
	} else {
		cargs = {prog, arg, nullptr};
	}

	unsetenv("COLUMNS");
	unsetenv("LINES");
	unsetenv("TERMCAP");
	setenv("LOGNAME", pw->pw_name, 1);
	setenv("USER", pw->pw_name, 1);
	setenv("SHELL", sh, 1);
	setenv("HOME", pw->pw_dir, 1);
	setenv("TERM", nst::config::TERMNAME, 1);

	signal(SIGCHLD, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
	signal(SIGALRM, SIG_DFL);

	execvp(prog, const_cast<char *const*>(cargs.data()));
	_exit(1);
}

void TTY::sigChildEvent() {
	int stat;
	pid_t p;

	if ((p = waitpid(m_pid, &stat, WNOHANG)) < 0)
		die("waiting for pid %hd failed: %s\n", m_pid, strerror(errno));

	if (p != m_pid)
		// should actually never happen
		return;

	if (WIFEXITED(stat) && WEXITSTATUS(stat))
		die("child exited with status %d\n", WEXITSTATUS(stat));
	else if (WIFSIGNALED(stat))
		die("child terminated due to signal %d\n", WTERMSIG(stat));
	_exit(0);
}

void TTY::sendBreak() {
	if (tcsendbreak(m_cmdfd, 0))
		perror("Error sending break");
}