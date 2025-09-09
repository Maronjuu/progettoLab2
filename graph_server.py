#! /usr/bin/env python3
import socket
import threading
import subprocess
import tempfile
import os
import struct
import logging
import signal
import sys
import concurrent.futures

HOST = '127.0.0.1'
PORT = 56147

logging.basicConfig(filename="graph_server.log", level=logging.INFO, format="%(asctime)s %(message)s")

# Funzione per gestire i singoli client
def tbody(conn, addr):
    try:
        # Riceve numero di nodi e numero di archi
        n = struct.unpack('!i', conn.recv(4))[0]
        a = struct.unpack('!i', conn.recv(4))[0]
        logging.info(f"Ricevuto richiesta da {addr}: Nodi: {n}, Archi: {a}")

        # Creazione file temporaneo
        with tempfile.NamedTemporaryFile(delete=False, mode='w') as tmpfile:
            temp_filename = tmpfile.name
            tmpfile.write(f"{n} {a}\n")  # Scrive il numero di nodi e archi

            # Variabili per il logging
            discarded_arcs = 0  # Conta gli archi scartati
            valid_arcs = 0      # Conta gli archi validi

            # Ricezione degli archi
            for _ in range(a):
                data = conn.recv(8)  # Riceve coppia di interi (u, v)
                if not data:
                    break
                u, v = struct.unpack('!2i', data)
                # logging.debug(f"Ricevuto arco: {u} {v}")

                if 1 <= u <= n and 1 <= v <= n:
                    # Arco valido, lo scriviamo
                    tmpfile.write(f"{u} {v}\n")
                    valid_arcs += 1
                    # logging.info(f"Aggiunto arco: {u} {v}")
                else:
                    # Arco non valido, lo scartiamo
                    discarded_arcs += 1
                    # logging.info(f"Arco non valido: {u} {v}")

        # Invocazione pagerank
        result = subprocess.run(['./pagerank', temp_filename], capture_output=True, text=True)

        if result.returncode == 0:
            conn.sendall(struct.pack('!i', 0))  # Exit code 0
            conn.sendall(result.stdout.encode())  # Risultato di pagerank
        else:
            conn.sendall(struct.pack('!i', result.returncode))  # Exit code errore
            conn.sendall(result.stderr.encode())  # Risultato stderr

        # Logging
        logging.info(f"Nodi: {n}")
        logging.info(f"Nome file temporaneo: {temp_filename}")
        logging.info(f"Archi validi: {valid_arcs}")
        logging.info(f"Archi scartati: {discarded_arcs}")
        logging.info(f"Exit code: {result.returncode}")
        logging.info(f"Richiesta da {addr} completata")


    except Exception as e:
        logging.error(f"Errore di gestione del client {addr}: {e}")
    finally:
        conn.close()
        if os.path.exists(temp_filename):
            os.remove(temp_filename)

# Funzione per avviare il server
def main(host=HOST, port=PORT):
    
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    # permette di riutilizzare la porta se il server viene chiuso
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_socket.bind((host, port))
    server_socket.listen()

    def handler(sig, frame):
        logging.info("Ricevuto SIGINT, attendo la terminazione dei thread e chiudo")
        server_socket.close()

        logging.info("Bye dal server")
        print("Bye dal server")
        sys.exit(0)

    signal.signal(signal.SIGINT, handler)

    logging.info(f"Server in ascolto su {host}:{port}...")
    # print(f"Server in ascolto su {host}:{port}...")

    with concurrent.futures.ThreadPoolExecutor() as executor:
        while True:
            conn, addr = server_socket.accept()
            executor.submit(tbody, conn, addr)
            # aggiungo il thread alla lista
            logging.info(f"Connesso a {addr}")     



if __name__ == "__main__":
    main()