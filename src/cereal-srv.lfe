(defmodule cereal-srv
  (export all))

(defun send () 0)
(defun connect () 1)
(defun disconnect () 2)
(defun open () 3)
(defun close () 4)
(defun speed () 5)
(defun parity-odd () 6)
(defun parity-even () 7)
(defun break () 8)

(defun run (pid port)
  (receive
    (`#(,port #(data ,bytes))
     (! pid `#(data ,bytes))
     (run pid port))
    (`#(send ,bytes)
     (send-serial port `(,(send) ,bytes))
     (run pid port))
    (#(connect)
     (send-serial port `(,(connect)))
     (run pid port))
    (#(disconnect)
     (send-serial port `(,(disconnect)))
     (run pid port))
    (`#(open ,tty)
     (send-serial port `(,(open) ,tty))
     (run pid port))
    (`#(close)
     (send-serial port `(,(close)))
     (run pid port))
    (`#(speed ,in-speed ,out-speed)
     (send-serial port
                  (list* (speed)
                         (cereal-util:convert-speed in-speed out-speed)))
     (run pid port))
    (`#(speed ,speed)
     (send-serial port
                  (list* (speed)
                         (cereal-util:convert-speed speed)))
     (run pid port))
    (#(parity odd)
     (send-serial port `(,(parity-odd)))
     (run pid port))
    (#(parity even)
     (send-serial port `(,(parity-even)))
     (run pid port))
    (#(break)
     (send-serial port `(,(break)))
     (run pid port))
    (#(stop)
     (send-serial port `(,(close)))
     #(ok cereal-stopped))
    (`#(EXIT ,port ,why)
     (io:format "Exited with reason: ~p~n" `(,why))
     (exit why))
    (msg
     (lfe_io:format "Received unknown message: ~p~n" `(,msg)))))

(defun send-serial (port msg)
  (! port `#(,(self) #(command ,msg))))