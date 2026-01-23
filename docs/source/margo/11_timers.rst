Working with Margo timers
=========================

In some contexts, it may be useful to schedule a function to be
invoked at a later time. This feature is provided by Margo timers.
The code bellow showcases this functionality.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          client.c (show/hide)

    .. literalinclude:: ../../examples/margo/11_timers/client.c
       :language: cpp

.. important::
   Timers rely on the progress loop to be running. Hence if some other
   ULTs keep the progress loop busy, the timer will be delayed. The provided
   timeout value is only a lower bound on the time a timer has to wait before
   triggering its callback.

.. important::
   As shown above, a timer can be restarted after its callback has been called
   or after being cancelled. The user must ensure that the timer is ready to be
   restarted before calling :code:`margo_timer_start()` again, otherwise this
   call will return an error.

.. note::
   It is possible to call :code:`margo_timer_start()` within a timer callback
   to restart the timer. Hence if you need a callback to be called after 1, 2, and 3
   seconds, you can either create 3 timers that you start with a different timeout
   value, or you can have a single timer restart itself twice with a 1 second
   timeout each time.
