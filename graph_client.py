#! /usr/bin/env python3
import socket
import threading
import os
import struct
import logging
import sys
import time # per il tentativo di connessione
import concurrent.futures

logging.basicConfig(filename='graph_client.log', level=logging.INFO, format='%(asctime)s %(message)s')

HOST = '127.0.0.1'
PORT = 56147

# Funzione per inviare il grafo al server
def tbody(file_name, host=HOST, port=PORT):

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            # sock.connect((host,port))
            
            # Tento la connessione finché non la prende per 200 secondi
            for i in range(100):
                try:
                    sock.connect((host, port))  # Prova a connettersi
                    logging.info("Connessione al server stabilita")
                    break  # Esce dal loop quando la connessione ha successo
                except ConnectionRefusedError:
                    time.sleep(2)
                    if i == 99:
                        raise ConnectionRefusedError("Connessione al server non riuscita")

            with open(file_name, 'r') as f:
                n = a = None # Numero di nodi e archi inizializzato a None perché se sono None vuol dire che sono nella prima riga
                # e quindi devo inviare il numero di nodi e archi

                for line in f:
                    line = line.strip() # Funzione strip: rimuove spazi e a capo
                    if not line or line.startswith('%'):
                        # ignoro i commenti
                        continue

                    if n is None and a is None:
                        n, c, a = map(int, line.split())
                        # Invia il numero di nodi e archi
                        sock.sendall(struct.pack('!i', n))  # Numero di nodi
                        sock.sendall(struct.pack('!i', a))  # Numero di archi
                        # logging.info(f"{file_name} Invio il numero di nodi e archi: {n} {a}")
                        continue

                    u, v = map(int, line.split())
                    sock.sendall(struct.pack('!2i', u, v))  # Invia coppia di nodi
                    # logging.info(f"{file_name} Invio i nodi: {u} {v}")

            # Ricezione dell'exit code e risultato
            exit_code = struct.unpack('!i', sock.recv(4))[0]
            result = sock.recv(4096).decode()
            result = result.split('\n') # divido il risultato in righe per stamparlo con il nome del file
            print(f"{file_name} Exit code: {exit_code}")
            for line in result:
                if not line:
                    continue # controllo messo perché lasciava una riga vuota prima di stampare Bye
                print(f"{file_name} {line}")
            print(f"{file_name} Bye")


    except Exception as e:
        print(f"Errore nella gestione di {file_name}: {e}")

# Funzione principale per creare i thread
def main():
    if len(sys.argv) < 2:
        print("Uso: python graph_client.py <file1.mtx> <file2.mtx> ...")
        sys.exit(1)

    # Controllo che i file esistano e non siano link o cartelle
    for arg in sys.argv[1:]:
        if not os.path.exists(arg) or os.path.islink(arg) or os.path.isdir(arg):
            print(f"File {arg} non esistente oppure è un link o una directory")
            sys.exit(1)

    file_names = sys.argv[1:]

    with concurrent.futures.ThreadPoolExecutor() as executor:
        for file_name in file_names:
            executor.submit(tbody, file_name)
            logging.info(f"Thread per {file_name} partito")

if __name__ == "__main__":
    main()
