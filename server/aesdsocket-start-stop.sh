#!/bin/sh

# Nombre del ejecutable
DAEMON="aesdsocket"
# Ruta donde se instalará el binario en el target
DAEMON_PATH="/usr/bin/$DAEMON"

case "$1" in
    start)
        echo "Starting $DAEMON"
        # -S: Start
        # -n: Nombre del proceso
        # -a: Ruta al ejecutable
        # --: Indica que lo que sigue son argumentos para aesdsocket
        # -d: Argumento para que aesdsocket corra como daemon
        start-stop-daemon -S -n $DAEMON -a $DAEMON_PATH -- -d
        ;;
    stop)
        echo "Stopping $DAEMON"
        # -K: Kill/Stop
        # -n: Busca el proceso por nombre para detenerlo
        start-stop-daemon -K -n $DAEMON
        ;;
    *)
        echo "Usage: $0 {start|stop}"
        exit 1
esac

exit 0
