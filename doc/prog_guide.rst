..  SPDX-License-Identifier: BSD-3-Clause
    Copyright (c) 2021-2023, ByteDance Ltd. and/or its Affiliates
    Author: Yuanhan Liu <liuyuanhan.131@bytedance.com>

.. _prog_guide:

Libtpa Programming Guide
========================

Introduction
------------

Libtpa(Transport Protocol Acceleration) is a DPDK based userspace TCP
stack implementation. It provides customized APIs only. Therefore
modification is needed if you want to get the performance boost for
your applications.

This documentation provides detailed descriptions on those APIs.

Architecture Overview
---------------------

Before heading to the libtpa API descriptions, it's necessary to give
an overview guide about the libtpa architecture.

**shared-nothing model**

Libtpa is designed for high performance. It takes the shared-nothing
model to avoid lock in datapath. That means one TCP connection will
be processed in one worker thread only: all operations, including
connect, write, read, close, have to be invoked in the same worker
thread.

.. caution::

   You can not create the connection at worker A and then do the write
   in worker B. Doing so results to undefined behaviour, including crash.

**run to completion**

Libtpa also takes the classical DPDK running model: run to completion.
As described below in the worker API section, the app has to keep calling
``tpa_worker_run`` to drive the TCP stack.

.. note::

   That is an intruder. It basically means your application has to be
   reshaped to the run to completion model to use libtpa. We may introduce
   other models in future, to avoid such intrudes that have to be made to
   your applications.


Libtpa API
----------

Libtpa API can be loosely divided into five parts: connection management,
read/write, worker execution, event handling, and misc. Below gives
detailed descriptions for each of them.

Connection Management
~~~~~~~~~~~~~~~~~~~~~

Libtpa provides simplified APIs compared with POSIX APIs for managing
TCP connections.

**connect**

.. code-block:: c

  int tpa_connect_to(const char *server, uint16_t port,
                     const struct tpa_sock_opts *opts);


It starts a new active connection, where

* ``server`` specifies the server address. It has to be an ipv4
  or ipv6 address in string format.

* ``port`` specifies the remote port to connect to.

* ``opts`` specifies additional options can be applied to this new
  connection. It could simply be NULL when nothing special needed.
  So far, only the option ``local_port`` is relevant to active
  connections.

.. code-block:: c
   :caption: sock options

    /*
     * Provides extra options for socks going to be created by
     * tpa_connect_to and tpa_connect_to.
     */
    struct tpa_sock_opts {
        uint64_t bits_reserved;

        /*
         * A private data set by user for listen sock. It could be
         * retrieved by tpa_sock_info_get when a new sock is accepted.
         *
         * tpa_listen_on only.
         */
        void *data;

        /*
         * Specifies a local port to bind to.
         *
         * tpa_connect_to only.
         */
        uint16_t local_port;
        uint8_t reserved[128 - 18];  /* XXX: it's ugly */
    } __attribute__((packed));

.. note::

    As stated before, for socks created by ``tpa_connect_to``, they will
    be bound to the worker thread where this API is invoked.

    It's also worth mentioning that the connect API is non-blocking. You
    need to watch the OUT event if you want to know when the connection
    is established.

On success, it returns a sock id (let's call it ``sid`` from now on).
Like fd, a negative value means error. Here is an example:

.. code-block:: c

    int sid;

    sid = tpa_connect_to("::1", 80, NULL);
    if (sid < 0) {
        fprintf(stderr, "failed to connect: %s\n", strerror(errno));
        return -1;
    }

**listen**

.. code-block:: c

    int tpa_listen_on(const char *local, uint16_t port,
                      const struct tpa_sock_opts *opts);

It creates a sock listening on the given address and port. Where,

* ``local`` specifies the local address, it could be NULL to
  support dual stack. Or, it can be set to a specific IPv4 or IPv6
  address, then only the one specific IP version is supported.

* ``port`` specifies the local port to listen on

* ``opts`` works the same as ``tpa_connect_to``. Only ``data`` is
  relevant to a listen sock.

.. note::

    Passive connections are distributed to the workers by the NIC RSS:
    the worker whose rx queue receives the SYN gets the new sock. Due to
    the shared-nothing model, you can't distribute those socks to other
    workers at the application level after accepting them. If you have
    just a few socks, it's very likely some workers get more passive
    connections while some get fewer.

**accept**

.. code-block:: c

    int tpa_accept_burst(struct tpa_worker *worker, int *sid, int nr_sid);

It returns an array of newly accepted socks assigned to the given worker.
If you want to fetch detailed sock information, such as remote address,
the private data set at ``tpa_listen_on``, etc, the below function does
the job.

.. code-block:: c

    struct tpa_sock_info {
        struct tpa_worker *worker;

        /* it's the tpa_sock_opts.data set by user */
        void *data;

        struct tpa_ip local_ip;
        struct tpa_ip remote_ip;
        uint16_t local_port;
        uint16_t remote_port;

        uint8_t reserved[76];
    };

    int tpa_sock_info_get(int sid, struct tpa_sock_info *info);

Below is a short listen & accept demo.

.. code-block:: c
   :caption: listen & accept example

    int sid;

    if (tpa_listen_on(NULL, 80, NULL) < 0) {
        fprintf(stderr, "failed to listen on port 80: %s\n",
                strerror(errno));
        return -1;
    }

    while (1) {
        /* explained later */
        tpa_worker_run(worker);

        if (tpa_accept_burst(worker, &sid, 1) == 1)
            register_new_connection(sid);

        /* ... */
    }

**close**

.. code-block:: c

    void tpa_close(int sid);

It simply closes the connection identified by the given sid.


Read and Write
~~~~~~~~~~~~~~

Libtpa supports both zero copy for read and write. It also supports
non-zero copy write. It does not implement non-zero copy read API
though.

**read**

.. code-block:: c

    struct tpa_iovec {
        void    *iov_base;
        uint64_t iov_phys;
        uint32_t iov_len;
        uint32_t iov_reserved;
        void    *iov_param;
        union {
            void (*iov_read_done)(void *iov_base, void *iov_param);
            void (*iov_write_done)(void *iov_base, void *iov_param);
        };
    };

    ssize_t tpa_zreadv(int sid, struct tpa_iovec *iov, int nr_iov);

Where,

* ``tpa_iovec`` is a libtpa customized iovec struct, with few fields
  extended mainly for zero copy implementation.

  The new fields are:

   - ``iov_phys`` specifies the starting physical address of the iov.

   - ``iov_read/write_done`` is a callback the app/libtpa should invoke
     when the corresponding iov is read/written, respectively. It will
     be further explained.

   - ``iov_param`` is the extra param for the above callback.

Note that although this function just looks like the ``readv`` system
call, it has a huge semantic difference: all fields in this struct
are filled by libtpa (instead of by the APP like the ``readv`` system
call). This is for implementing the zero copy read, and the justice
is simple: only libtpa knows the data buffer address and size. Both
of them are unpredictable for the APP at the time this API gets invoked.

Thus, when the APP has done the processing of the read iov, it should
invoke the ``iov_read_done`` callback to reclaim the memory allocated by
libtpa. Here is an example:

.. code-block:: c
   :caption: tpa_zreadv example

    struct tpa_iovec iov;
    ssize_t ret;

    ret = tpa_zreadv(sid, &iov, 1);
    if (ret < 0) {
        if (errno == EAGAIN) {
            return 0;

         /* error happened; handle it here */
    }

    if (ret == 0) {
        /* EOF reached; close it */
        tpa_close(sid);
    }

    if (ret > 0) {
            /* process the read buffer at iov.iov_base */
            process_data(iov.iov_base, iov.iov_len);

            /* free it when the process is done */
            iov.iov_read_done(iov.iov_base, iov.iov_param);
    }

**write**

Libtpa has two write APIs.

.. code-block:: c

    ssize_t tpa_write(int sid, const void *buf, size_t count);
    ssize_t tpa_zwritev(int sid, const struct tpa_iovec *iov, int nr_iov);

The none-zero copy version works just like the ``write`` system call.

The zero copy version is also quite similar to the ``writev`` system call,
except three more fields need to be filled by the APP:

* ``iov_phys``: this is needed for NIC to do DMA, therefore zero copy.

* ``iov_write_done``: when the data is completely transferred (when the
  TCP ACK is received), libtpa will invoke this callback to let the APP
  be aware of it(to free the buffer, etc).

* ``iov_param``: an extra param filled by APP and will be echoed back
  to the APP when the above callback is invoked.

.. note::

   ``iov_phys`` is the IO address the NIC reads the buffer from. Only
   buffers libtpa handed out itself (say, the ones got from ``tpa_zreadv``)
   have a valid one: registering external memory is
   :ref:`not supported <extmem_reg>`.

   When ``iov_phys`` is set to 0, there is a special meaning in libtpa.
   It means the zero copy write is disabled, and libtpa will fallback
   to the non-zero copy version.


Below is a simple zwrite example with fallback being used:

.. code-block:: c
   :caption: tpa_zwritev example

    static void free_write_buffer(void *iov_base, void *iov_param)
    {
        free(iov_base);
    }

    ssize_t tpa_zwrite_example(size_t size)
    {
        struct tpa_iovec iov;
        ssize_t ret;

        iov.iov_len  = size;
        iov.iov_base = malloc(size);
        iov.iov_phys = 0;
        iov.iov_param = NULL;
        iov.iov_write_done = free_write_buffer;

        ret = tpa_zwritev(sid, &iov, 1);
        if (ret < 0)
            iov.iov_write_done(iov.iov_base, iov.iov_param);

        return ret;
    }

.. caution::

    Both ``tpa_write`` and ``tpa_zwritev`` are atomic, meaning either
    all data are written or nothing will. This has an advantage. It
    simplifies the error handling. We don't have to worry about that
    one iov is partially written.

    It has a drawback though. If the write is too large that the whole
    sock send queue can't hold it, then the write would always fail
    with EAGAIN error. You can workaround it by:

    - enlarging the sock send queue length by setting the cfg
      ``tcp.snd_queue_size`` (which is set to 512 by default). Or,

    - breaking the large write to many smaller ones

Worker Execution
~~~~~~~~~~~~~~~~

The worker is the core TCP stack processing unit. There should be one worker
per datapath thread. The number of worker is set by below function:

.. code-block:: c

    int tpa_init(int nr_worker);

This function also initializes the whole libtpa system, including the DPDK
initialization. It should be invoked first before all other libtpa functions
get invoked.

Then there is a per-worker initialization function:

.. code-block:: c

    struct tpa_worker *tpa_worker_init(void);

This function must be executed first at the corresponding worker
thread. It returns the worker pointer that is needed for the rest
worker APIs.  Note that this function must be executed once only.

A worker gets executed every time below function get invoked:

.. code-block:: c

    void tpa_worker_run(struct tpa_worker *worker);

It is the core of the libtpa, which basically does three things:

- ``tcp input``: receives packets from NIC, feeds them to the TCP stack
  (decaping the net headers, finding the right sock, etc) and then delivers
  the TCP payload to the sock receive queue if any.

- ``tcp output``: handles the write request from the APP, feeds them to
  the TCP stack (encaping the net headers, etc) and then sends them out
  to wire.

- ``tcp timeout``: handles the retransmission timeouts, etc.

Event Handling
~~~~~~~~~~~~~~

Libtpa provides two event related APIs. They are epoll alike, while they
are quite different in some ways.

.. code-block:: c

    struct tpa_event {
        uint32_t events;
        void *data;
    };

    int tpa_event_ctrl(int sid, int op, struct tpa_event *event);
    int tpa_event_poll(struct tpa_worker *worker, struct tpa_event *events, int max);

As you see, there is no ``epoll_create`` equivalent in libtpa. The
reason behind it is, as stated above, that every connection in libtpa
is bound to a specific worker. Thus, the APP only has to register or
remove some events to a specific connection with the API ``tpa_event_ctrl``,
then libtpa will find the correct worker so that a later call of
``tpa_event_poll(worker, ...)`` could catch them.

Misc
~~~~

.. _extmem_reg:

**external memory management**

Zero copy write with application owned buffers needs the memory region
to be registered (and DMA mapped) first. This is not supported: both
functions below fail with ``ENOTSUP``.

.. code-block:: c

    int tpa_extmem_register(void *virt_addr, size_t len, uint64_t *phys_addrs,
                            int nr_page, size_t page_size);
    int tpa_extmem_unregister(void *virt_addr, size_t len);

Examples
--------

Libtpa repo has few `example applications <https://github.com/bytedance/libtpa/tree/main/app>`_.
Both `swing <https://github.com/bytedance/libtpa/tree/main/app/swing>`_ and
`techo <https://github.com/bytedance/libtpa/tree/main/app/techo>`_ are good examples
to look at: both are short and simple.
