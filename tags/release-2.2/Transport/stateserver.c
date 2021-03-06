/************************************************************
 * Provide EARLAB experiment state to socket connected apps *
 * (Used to drive graphical display of experiment state)    *
 ************************************************************/

/**********************
 * BEGIN Boiler Plate *
 **********************/
#include <stdio.h>
#include <ctype.h>
#include <signal.h>
#include <string.h>
#include <local/std.h>

#ifdef sgi
# define _SGI_MP_SOURCE
#endif

#ifdef EXPORT
# undef  EXPORT
# define EXPORT /* Nothing */
#endif

#if (defined(lo_end) && !defined(LOEND))
# define LOEND
#endif

#ifdef _BSD
# define _BSD 42
#endif
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/socket.h>
#ifdef aix
# include <sys/ioctl.h>
#endif
#ifndef sun
# include <sys/select.h>
#endif
#ifdef sgi
# include <sys/syssgi.h>
#endif
#include <sys/time.h>
#include <netinet/in.h>

#if (defined(sgi) || defined(aix) || defined(linux))
#include <netinet/tcp.h>
#endif

#if (defined(rs6000p4))
# define MEMSYNC mem_sync()
#else
# define MEMSYNC /* Don't need no stinking mem sync */
#endif

/*
 * FD set stuff for "select" and friends
 */
#ifdef aix
#define MAX_FDS 2048
#define FD_SIZE (MAX_FDS/sizeof(int))

typedef struct sellist {
	int fd[FD_SIZE];
	int msgs[1];
} FDSET;
#else
typedef fd_set FDSET;
#endif

#define FDINIT(set)     FD_ZERO(set);
#define FDADD(fd, set)  FD_SET(fd, set)
#define FDTEST(fd, set) FD_ISSET(fd, set)

typedef struct sigaction SIGCTL;

typedef unsigned long      Ulong;
typedef unsigned long long ULONG;

#define BADRC(rc) ((rc==0) || (rc<0&&errno!=EWOULDBLOCK&&errno!=EAGAIN))

#define SERVICE   "earlab"
#define DEFLTPORT 9876
#define K 1024

typedef struct sockaddr_in SOCKADDR;
typedef struct hostent     HOST;
typedef struct servent     SERVENT;
typedef struct timeval     TIMEVAL;

/**********************
 * END Boiler Plate *
 **********************/

#include <daffie/event.h>
#include <daffie/eclient.h>

#include "efievent.h"

INTERNAL struct {
	char *name;
	int  id;
} Module[] = {
	{ "Sig*",          0},
	{ "[rR]ead*",      0},
	{ "Mid[eE]ar*",    1},
	{ "Gamma*",        2},
	{ "Hair[cC]ell*",  3},
	{ "[Ww]rite*",     4},

	{ NULL, -1}
};

INTERNAL char *StateName[6];

INTERNAL  struct {
	FD   sock;
	int  alive;
} Sock[100];

INTERNAL int   Port  = 9876;
INTERNAL int   Nsock = 0;
INTERNAL int   Socklock;
INTERNAL int   Socksize = 1;
INTERNAL int   Debug = 1;
INTERNAL int   Done  = 0;
INTERNAL char *Errmsg = NULL;
INTERNAL int   StateMap[EFI_STATUS_MAXTYPE+1][EFI_STATUS_MAXSTATUS+1];

INTERNAL void send_state(char type, int id, int state);
INTERNAL void efi_init(void);
INTERNAL void listener(char *notused);
INTERNAL int  open_connection(char *proc, int hostip, int port);
INTERNAL void conn_broken(int fd);
INTERNAL int  ioblock(FD fd, int flag);
INTERNAL int  iowait(FD fd, int millisec, int rdflag, int wrflag);
INTERNAL int  setup_listener(int port);
INTERNAL void addr_str(int no, char *str);
INTERNAL void addr_hex(int no, char *str);
INTERNAL void addr_encode(int no, char *str);
INTERNAL int  addr_decode(char *str);


main(int argc, char *argv[]) {
	ECLIENT_LIST clients[MAX_CLIENT], *clp;
	EVENT_EFI_STATUS *estat;
	EVENT        e;
	int          id, nc, rc, from, state;
	int          i, j;
	char        *server;
	char        *pgm;

	/* USAGE: stateserver event_server local_port */

	if (pgm = strrchr(argv[0], '/')) ++pgm;
	else pgm = argv[0];

	++argv; --argc;

	if (argc && streq(argv[0], "-help")) {
		printf("usage: %s [event_server] [local_port]\n", pgm);
		exit(0);
	}

	/* grab event server if given */
	if (argc) {
		server = argv[0];
		++argv; --argc;
	}
	else server = "samadams.bu.edu";

	/* grab port if given */
	if (argc) {
		Port = atoi(argv[0]);
		++argv; --argc;
	}

	efi_init();

	/*
	 * Join a world
	 */
	id = event_join(server, &nc);
	if (!id) {
		fprintf(stderr,
			"%s: couldn't connect to event server %s\n",
			pgm, server);
		exit(1);
	}
	fprintf(stderr, "%s: Joined %s as client id %d (%d clients)\n",
		pgm, server, id, nc);

	event_register("server", "experiment-state", pgm);

	/* Select only state messages */
	event_select_type(0, ET_SYS_MAX+1, ET_MAX);
	event_select_type(1, ET_EFI_STATUS, ET_EFI_STATUS);

	/* Use Daffie services to find modules */
	eclient_monitor();

	/* Allocate a lock */
	Socklock = mp_alloclock();

	/* Start up the socket listener */
	mp_task(listener, NULL, 0);

	/* Initialize the state mapping table */
	for (i = 0; i <= EFI_STATUS_MAXTYPE; ++i)
	  for (j = 0; j <= EFI_STATUS_MAXSTATUS; ++j)
	    StateMap[i][j] = -1;

	/*** Control Layer States ***
	     None = 0,
	     Unloaded = 1,
	     Loaded = 2,
	     Idle = 3,
	     Running = 4 ,
	     SendData = 5
	*****************************/

	StateName[0] = "None";
	StateName[1] = "Unloaded";
	StateName[2] = "Loaded";
	StateName[3] = "Idle";
	StateName[4] = "Running";
	StateName[5] = "SendData";
	StateMap[EFI_STATUS_LOAD][EFI_STATUS_STARTING]    = 1;
	StateMap[EFI_STATUS_PARAM][EFI_STATUS_READY]      = 2;
	StateMap[EFI_STATUS_START][EFI_STATUS_READY]      = 3;
	StateMap[EFI_STATUS_ADVANCE][EFI_STATUS_STARTING] = 4;
	StateMap[EFI_STATUS_ADVANCE][EFI_STATUS_READY]    = 5;
	StateMap[EFI_STATUS_ADVANCE][EFI_STATUS_IDLE]     = 3;
	StateMap[EFI_STATUS_STOP][EFI_STATUS_READY]       = 1;

	while (!Done) {
		/* Wait for and read an event */
		event_wait();
		if (!event_receive(&e)) continue;

		/* Try to deal gracefully with lost server connection */
		if (e.ev_head.type == ET_SHUTDOWN && e.ev_head.from == id) {
			fprintf(stderr, "%s: server shutdown\n", pgm);
			break;
		}

		/* If this is not a STATUS print a message and ignore */
		if (e.ev_head.type != ET_EFI_STATUS) {
			fprintf(stderr, "%s: received %s (%d) from %d\n",
				pgm,
				event_lookup_name(e.ev_head.type),
				e.ev_head.type,
				e.ev_head.from);
			continue;
		}

		/*
		 * Format up a message
		 *  1) Map DAFFIE clients to Earlab modules
		 *  2) Map internal state messages to Control Layer state
		 */

		/* Get client id of sender */
		from  = e.ev_head.from;

		/* Set pointer to STATUS event */
		estat = (EVENT_EFI_STATUS *)&e;

		/* See if this is a message to be passed as-is */
		if (estat->type == EFI_STATUS_CONTROL) {
			state = estat->status;
			id    = estat->detail;

			fprintf(stderr,
				"%s: passing %s for module id %d\n",
				pgm, StateName[state], id);

			send_state('M', id, state);
			continue;
		}

		/* See if this is a frame complete message */
		if (estat->type == EFI_STATUS_FRAME) {
			int step, tot;

			step = estat->status;
			tot  = estat->detail;

			fprintf(stderr,
				"%s: passing Frame %d of %d complete\n",
				pgm, step, tot);

			send_state('F', step, tot);
			continue;
		}

		/* Get client list */
		nc = eclient_match(clients, "*", "earlab", "*", "*");

		/* Lookup client by DAFFIE id */
		clp = NULL;
		for (j = 0; j < nc; ++j) {
			if (clients[j].id == from) {
				clp = &clients[j];
				break;
			}
		}

		/* Make sure we have a match */
		if (clp == NULL) {
			fprintf(stderr,
				"%s: ignoring state from non-earlab client %d\n",
				pgm, from);
			continue;
		}

		/* HACK ALERT - Lookup Earlab module id from compiled list */
		id = -1;
		for (j = 0; Module[j].name != NULL; ++j) {
			if (strmatch(Module[j].name, clp->instance)) {
				id = Module[j].id;
				break;
			}
		}
		if (id < 0) {
			fprintf(stderr,
				"%s: couldn't find module id for %s:%s,%s (%d)\n",
				pgm, clp->host, clp->species, clp->instance, clp->id);
			continue;
		}

		/* Map message to upper layer state definitions */
		state = StateMap[estat->type][estat->status];

		if (state < 0) {
			fprintf(stderr,
				"%s: ignoring type-status %d-%d from %s (client %d)\n",
				pgm, estat->type, estat->status, clp->species, from);
			continue;
		}

		fprintf(stderr,
			"%s: reporting %s for %s@%s (client id %d)\n",
			pgm, StateName[state], clp->species, clp->host, from);

		/* Send it to clients */
		send_state('M', id, state);

	}

	event_leave();
}

/*
 * Format a message and send it to all attached clients
 */
INTERNAL void
send_state(char type, int id, int state) {
	char    msgbuf[100];
	int     msglen;
	int     i, rc;

	sprintf(msgbuf, "%c %d %d;", type, id, state);
	msglen = strlen(msgbuf);

	mp_lock(Socklock);
	for (i = 0; i < Nsock; ++i) {
		if (!Sock[i].alive) continue;

		printf("Writing %s to fd %d\n", msgbuf, Sock[i].sock);
		rc = write(Sock[i].sock, msgbuf, msglen);
		if (rc <= 0) {
			fprintf(stderr, "Error on fd %d - closing\n", Sock[i].sock);
			Sock[i].alive = 0;
		}
	}

	mp_unlock(Socklock);
}

/*
 * DAFFIE definitions for EFI events
 */
#include "eventdef.h"
#include "agentd.h"

INTERNAL void
efi_init(void) {
	event_verbose(0);
	event_init();
#include "efieventdef.h"
#include "agentddef.h"
}


/*
 * Sigpipe handler
 */
INTERNAL void
conn_broken(int fd) {
	int i;

	fprintf(stderr, "conn_broken: Caught sigpipe on %d\n", fd);
	for (i = 0; i < Nsock; ++i)
	  if (Sock[i].sock == fd) {
		fprintf(stderr, "conn_broken: Marking fd at Sock[%d]\n", i);
		Sock[i].alive = 0;
		return;
	  }

	return;
}

/*
 * Listen for and manage client connections
 */
INTERNAL void
listener(char *notused) {
	unsigned int rval;
	int      sockfd, port;
	int      connfd;
	SOCKADDR connsock;
	int      i;
	int      len;
	int      ipaddr;
	char     buf[200], connname[200];
	HOST    *host;

	rval = setup_listener(Port);
	if (!rval) {
		fprintf(stderr, "listener: setup failed!! exiting\n");
		Done = 1;
		mp_exit(0);
	}

	sockfd = (rval >> 16);
	port   = rval & 0xffff;

	ioblock(sockfd, 0);

	FOREVER {
		/* Clean up broken sockets */
		mp_lock(Socklock);

		for (i = 0; i < Nsock; ++i) {
			if (Sock[i].alive) continue;

			if (Nsock == 1) {
				Nsock == 0;
				break;
			}
			Sock[i].sock  = Sock[Nsock-1].sock;
			Sock[i].alive = Sock[Nsock-1].alive;
			Sock[Nsock-1].sock  = 0;
			Sock[Nsock-1].alive = 0;

			--Nsock;
		}
		mp_unlock(Socklock);

		if (!iowait(sockfd, 2000, 1, 0)) continue;

		len = sizeof(connsock);
		bzero((void *)&connsock, len);
		connfd = accept(sockfd, (struct sockaddr *)&connsock, (socklen_t *)&len);

		if (BADRC(connfd)) {
			static int bad = 0;

			fprintf(stderr,
				"listener: error accepting connection on port %d, fd %d\n",
				port, sockfd);
			perror("listener");

			if (++bad > 10) {
				fprintf(stderr, "listener: too many errors - exiting\n");

				Done = 1;
				mp_exit(0);
			}
			continue;
		}
		/*
		 * Get peer hostname name
		 */

		/* Convert IP address to a string */
		ipaddr = ntohl(connsock.sin_addr.s_addr);
		addr_str(ipaddr, buf);

		/* Lookup the host by address */
#if defined(sgi) || defined(aix)
		host = gethostbyaddr(ipaddr, 4, AF_INET);
#else
		host = gethostbyaddr(buf, strlen(buf), AF_INET);
#endif
		if (host != NULL) strcpy(connname, host->h_name);
		else              strcpy(connname, buf);

		/* Set blocking i/o */
		ioblock(connfd, 1);

		/* Catch socket shutdowns */
		on_sigpipe(connfd, conn_broken);

		addr_str(ipaddr, buf);
		fprintf(stderr,
			"listener: accepted connection from %s (%s) on fd %d\n",
			connname, buf, connfd);

		Sock[Nsock].sock  = connfd;
		Sock[Nsock].alive = 1;
		MEMSYNC;
		++Nsock;

	}
}

/**********************************************************************
 * OPEN_CONNECTION - Create a socket to given host:port and handshake *
 **********************************************************************/
INTERNAL int
open_connection(char *proc, int hostip, int port) {
	char    *s;
	SOCKADDR sock;
	HOST     *host;
	int      opt;
	int      tryxshake;
	int      shmem, reqshmem;
	FD       fd;
	EVENT_JOIN  ej;
	int      first, done;
	int      rc;

	shmem = reqshmem = 0;

	if (hostip == 0) {
		shmem = 1;
	}

	/* Attempt to open the connection */
	bzero((void *)&sock, sizeof(SOCKADDR));

	if (Debug)
	   printf("%s: creating socket\n", proc);

	/* Create the socket */
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		fprintf(stderr, "%s: can't create socket\n", proc);
		return(0);
	}

	/*
	 * Catch broken socket connections
	 */
	on_sigpipe(fd, conn_broken);

	/* Set socket options */
	opt = Socksize*K;
	rc = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt));
	if (rc < 0) {
		on_sigpipe(fd, NULL);
		close(fd);
		fprintf(stderr, "%s: failed to set read socket buffer size\n", proc);
		perror(proc);
		return(0);
	}

	opt = Socksize*K;
	rc = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(opt));
	if (rc < 0) {
		on_sigpipe(fd, NULL);
		close(fd);
		fprintf(stderr, "%s: failed to set write socket buffer size\n", proc);
		perror(proc);
		return(0);
	}

	/* Set NODELAY option */
#ifdef TCP_NODELAY
	opt = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
#endif

	/* Set LINGER option */
#ifdef SO_LINGER
	opt = 1;
	setsockopt(fd, SOL_SOCKET, SO_LINGER, &opt, sizeof(opt));
#endif

	/* Set FASTACK option */
#ifdef TCP_FASTACK
/***** This causes very bad things to happen on SGIs ***************
	setsockopt(fd, SOL_SOCKET, TCP_FASTACK, &opt, sizeof(opt));
*********************************************************************/
#endif

	sock.sin_family      = AF_INET;
	sock.sin_port        = htons(port);
	sock.sin_addr.s_addr = htonl(hostip);

	/*
	 * Make the connection
	 */
	if (Debug)
	   printf("%s: connecting to %d.%d.%d.%d:%d\n",
		  proc,
		  (hostip>>24)&0xff, (hostip>>16)&0xff, (hostip>>8)&0xff, (hostip>>0)&0xff,
		  port);

	rc = connect(fd, (struct sockaddr *)&sock, sizeof(sock));
	if (rc < 0) {
		/* Close the socket */
		on_sigpipe(fd, NULL);
		close(fd);

		fprintf(stderr, "%s: can't connect to %d.%d.%d.%d:%d\n",
			proc,
			(hostip>>24)&0xff, (hostip>>16)&0xff, (hostip>>8)&0xff, (hostip>>0)&0xff,
			port);

		perror(proc);
		return(0);
	}

	if (Debug) ;
	   printf("%s: connection to %d.%d.%d.%d:%d complete on fd %d\n",
		  proc,
		  (hostip>>24)&0xff, (hostip>>16)&0xff, (hostip>>8)&0xff, (hostip>>0)&0xff,
		  port,
		  fd);

	return(fd);
}

/*
 * Enable/disable blocking I/O on a file
 */
INTERNAL int
ioblock(FD fd, int flag) {
	int on, rc;

	/*
	 * Set non-blocking i/o
	 */
	if (flag == 0) {
#ifdef posix
		on = O_NONBLOCK;
		rc = (fcntl(fd, F_SETFL, on) < 0);
#endif
#ifdef sgi
		/* on = O_NONBLOCK|O_NDELAY; */
		on = FNONBLOCK;
		rc = (fcntl(fd, F_SETFL, on) < 0);
#endif
#ifdef sun
		on = FNBIO|O_NDELAY;
		rc = (fcntl(fd, F_SETFL, on) < 0);
#endif
#ifdef aix
		on = 1;
		rc = (ioctl(fd, FIONBIO, on) < 0);
#endif
	}

	/*
	 * Set blocking i/o
	 */
	 else {
#ifdef posix
		on = 0;
		rc = (fcntl(fd, F_SETFL, on) < 0);
#endif
#ifdef sgi
		on = 0;
		rc = (fcntl(fd, F_SETFL, on) < 0);
#endif

#ifdef sun
		on = 0;
		rc = (fcntl(fd, F_SETFL, on) < 0);
#endif

#ifdef aix
		on = 0;
		rc = (ioctl(fd, FIONBIO, on) < 0);
#endif
	}

	return(rc==0);
}

/*
 * Wait for I/O completion
 */
INTERNAL int
iowait(FD fd, int millisec, int rdflag, int wrflag) {
	TIMEVAL timeout;
	fd_set  waitrd, waitwr, *wr, *ww;
	int     rc;

	FD_ZERO(&waitrd);
	FD_ZERO(&waitwr);
	FD_SET(fd, &waitrd);
	FD_SET(fd, &waitwr);
	if (rdflag) wr = &waitrd;
	else        wr = NULL;
	if (wrflag) ww = &waitwr;
	else        ww = NULL;
	if (millisec > 1000) {
		timeout.tv_sec = millisec / 1000;
		timeout.tv_usec = 1000*(millisec%1000);
	}
	else {
		timeout.tv_sec  = 0;
		timeout.tv_usec = 1000*millisec;
	}
	rc = select(fd+1, wr, ww, NULL, &timeout);
	if (rc < 0) {
		fprintf(stderr,
			"iowait: proc %d thread %d select syscall failed on fd %d - errno %d\n",
			getpid(), mp_gettid(), fd, errno);
		perror("iowait");
	}
	return(rc > 0);
}

/*
 * Set up listener port
 */
INTERNAL int
setup_listener(int port) {
	SOCKADDR server;
	HOST     *host;
	SERVENT  *servent;
	char     servername[256];
	FD       sockid;
	int      service;
	int      opt;
	int      rc;

	bzero((void *)&server, sizeof(SOCKADDR));

	/*
	 * Create the socket
	 */
	sockid = socket(AF_INET, SOCK_STREAM, 0);
	if (sockid < 0) {
		Errmsg = "can't create socket";
		return(0);
	}

	/* Set socket options */
#ifdef SO_REUSEADDR
	opt = 1;
	setsockopt(sockid, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

#ifdef SO_REUSEPORT
	opt = 1;
	setsockopt(sockid, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

	opt = Socksize*K;
	rc = setsockopt(sockid, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt));
	if (rc < 0) {
		fprintf(stderr, "setup: failed to set read socket buffer size\n");
		exit(1);
	}

	opt = Socksize*K;
	rc = setsockopt(sockid, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(opt));
	if (rc < 0) {
		fprintf(stderr, "setup: failed to set write socket buffer size\n");
		exit(1);
	}

	/* Set NODELAY option */
	opt = 1;
	setsockopt(sockid, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

	/* Set FASTACK option */
#ifdef TCP_FASTACK
    /******** This causes really bad stuff to happen on SGI clients ******
	opt = 1;
	setsockopt(sockid, IPPROTO_TCP, TCP_FASTACK, &opt, sizeof(opt));
     **********************************************************************/
#endif

	/*
	 * Set up Internet Adress of server
	 */
	if (!port) {
		 servent = getservbyname(SERVICE, NULL);
		 if (servent) service = servent->s_port;
		 else         service = DEFLTPORT;
	}
	else service = port;

	gethostname(servername, 128);
	host = gethostbyname(servername);
	if (host == NULL) {
		Errmsg = "Can't get host address";
		return(0);
	}
	server.sin_family = host->h_addrtype;
	server.sin_port   = htons(service);
	server.sin_addr.s_addr = INADDR_ANY;

	/* Bind the socket to the server address */
	if (bind(sockid, (struct sockaddr *)&server, sizeof(server)) < 0) {
		Errmsg = "can't bind socket";
		return(0);
	}

	/* Establish maximum queue length */
	if (listen(sockid, 5) < 0) {
		Errmsg = "can't listen on socket";
		return(0);
	}
	return((sockid<<16) | service);
}


/*
 * Convert an Internet address to a "dotted" decimal string
 */
INTERNAL void
addr_str(int no, char *str) {
	sprintf(str, "%d.%d.%d.%d",
		(no>>24)&0xff, (no>>16)&0xff, (no>>8)&0xff, no&0xff);
}


/*
 * Convert an Internet address to a "dotted" hex string
 */
INTERNAL void
addr_hex(int no, char *str) {
	sprintf(str, "%02x.%02x.%02x.%02x",
		(no>>24)&0xff, (no>>16)&0xff, (no>>8)&0xff, no&0xff);
}

/*
 * Encode an Internet address
 */
INTERNAL void
addr_encode(int no, char *str) {
	int  i;

	str[8] = '\0';
	for (i = 7; i > -1; --i) {
		str[i] = (no&15) + 'a';
		no >>= 4;
	}
}

INTERNAL int
addr_decode(char *str) {
	int i = 0;

	while (*str) {
		if (*str < 'a' || *str > 'p') return(0);
		i <<= 4;
		i += *str++ - 'a';
	}
	return(i);
}

