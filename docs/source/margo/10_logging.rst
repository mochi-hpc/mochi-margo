Customizing Margo logging
=========================

In the previous tutorials we have used functions like :code:`margo_info`
to display informations, usually setting an Margo instance's logging level
to :code:`MARGO_LOG_INFO` accordingly. By default, Margo's internal logger
will use fprintf to write the messages to :code:`stderr`. It is however
possible to inject custom logging functions into a Margo instance.
The code bellow showcases how to do this.

.. container:: toggle

    .. container:: header

       .. container:: btn btn-info

          server.c (show/hide)

    .. literalinclude:: ../../examples/margo/10_logging/server.c
       :language: cpp

