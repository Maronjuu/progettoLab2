# progettoLab2
Questo è il mio progetto dell'esame di Laboratorio 2 dell'università. Il progetto consiste in un programma scritto in C che calcola il pagerank di un grafo passato come file mtx, con utilizzo di thread e segnali. È presente anche un client-server python che utilizza il programma C per calcolare il pagerank dei grafi richiesti dai client.

# Programma pagerank.c
Il programma pagerank.c prende in input un file di tipo mtx (matrice di adiacenza) e calcola il pagerank del grafo rappresentato. Il programma è stato sviluppato su un sistema Linux e utilizza le funzioni di libreria, le interfacce e le specifiche POSIX. Per compilare il programma è sufficiente aprire la cartella in cui si trovano tutti i file sul terminale e usare il comando make, in quanto le informazioni per la compilazione sono tutte contenute sul makefile.

I parametri OPZIONALI presi dal programma sono, in ordine:
- k K show top K nodes (default 3)
- m M maximum number of iterations (default 100)
- d D damping factor (default 0.9)
- e E max error (default 1.0e-7)
- t T number of auxiliary threads (default 3)

La chiamata del programma quindi si effettua con:
pagerank [-k K] [-m M] [-d D] [-e E] [-t T] infile (dove le lettere maiuscole sono da sostituire con i valori/numeri desiderati).

Il programma prevede l'utilizzo di segnali da tastiera per verificare lo stato corrente del calcolo. Si può mandare un segnale SIGUSR1 (utilizzando il comando kill -usr1 <PID> sostituendo <PID> con il pid del main) per verificare a che punto è il pagerank oppure se il calcolo non è ancora iniziato. Il programma comunica il proprio PID una volta partito, rendendolo noto per mandare eventuali segnali. L'output effettivo del programma viene mandato su stdout, mentre messaggi di debug e gestione del segnale vengono mandati su stderr, per cui si consiglia di utilizzare dei file di log in modo da separare le stampe. Ad esempio, facendo:

./pagerank 9nodi.mtx > 9nodi.rk 2> 9nodi.log

I messaggi di debug, di errore e di gestione dei segnali vengono mandati sul file 9nodi.log mentre l'output con il risultato del programma viene stampato su 9nodi.rk, lasciando il terminale libero. Facendo invece soltanto: 

./pagerank 9nodi.mtx 2> 9nodi.log

L'output effettivo verrà stampato sul terminale, mentre tutti gli altri messaggi sul file di log.

Si può provare con i file di esempio forniti. Il file 21archi.mtx contiene appositamente un errore nel grafo, ed è utilizzabile come prova per vedere che il programma si ferma se trova un grafo non valido.

Di seguito si trova la spiegazione su come viene parallelizzato il programma grazie ai thread.

## Parsing del grafo
Il parsing avviene mediante la tecnica produttore-consumatore.
Viene usato un mutex per accedere al buffer produttori-consumatori, e un array di mutex per l'accesso al grafo, che avrà un numero di mutex pari al numero di thread (per ragioni di ottimizzazione) in modo che ogni thread inserisca solo un sottonsieme di nodi. Abbiamo poi i semafori per il buffer pieno e il buffer vuoto. Dato che lavora su un buffer, abbiamo anche il puntatore alla posizione corrente nel buffer (pcindex).

Il main deposita tutte le coppie di nodi nel buffer produttori-consumatori. Il semaforo per gli slot vuoti nel buffer è inizializzato alla dimensione del buffer divisa per 2: in questo modo un solo slot del buffer viene considerato come una coppia di elementi (nodi).

#### Funzione parse_tbody
Ogni thread consumatore pesca una coppia di nodi dal buffer. Successivamente per aggiungere la coppia di nodi al grafo acquisisce il mutex per accedere al grafo, mediante l'indice corrispondente all'operazione di modulo (in questo modo un thread lavora solo su determinati nodi). Con il mutex acquisito viene controllato se l'arco tra i due nodi esiste già, se non c'è allora viene aggiunto. Infine viene aumentato il numero di archi uscenti dal nodo i, con un'altra operazione protetta da mutex.

## Calcolo del pagerank
Per il calcolo del pagerank i thread che eseguono le iterazioni parallelamente sul grafo utilizzano la struct dati_pagerank_tbody. In questa struct come meccanismi di sincronizzazione abbiamo: un mutex per l'indice del nodo corrente, un mutex per il massimo pagerank, uno per l'aggiornamento dell'errore e uno per l'aggiornamento della somma St (nodi senza archi uscenti). Per consentire ai thread di lavorare in zone diverse del grafo (e quindi in segmenti di array diversi), abbiamo due variabili start e end. Infine abbiamo una barriera che consente di far aspettare che tutti i thread abbiano finito determinati calcoli prima di proseguire con l'iterazione parallelamente. 

#### Funzione pagerank
La prima iterazione con i valori di inizializzazione viene fatta dalla funzione pagerank; successivamente partono i thread con l'esecuzione del corpo pagerank_tbody.
Nella funzione pagerank abbiamo anche la partenza del thread che gestisce i segnali. Questo thread deve poter vedere le variabili della struct che hanno il numero corrente di iterazioni, il massimo pagerank e il suo nodo corrispondente, visualizzabili mediante il segnale SIGUSR1.

#### Funzione pagerank_tbody
Ogni thread effettua il proprio calcolo di xnext[i] e di YT[i] in modo indipendente, senza l'uso di mutex, poiché ogni thread lavora su un segmento diverso dell'array (grazie all'indice del nodo corrente, precedentemente acquisito e anche incrementato con l'utilizzo di un mutex, facendo sì che tutti i thread abbiano un indice diverso). Quando si aggiorna il valore del massimo pagerank si utilizza un mutex per garantire l'accesso esclusivo.

Successivamente, i thread devono sincronizzarsi con una barriera prima di procedere (prima di proseguire devono aspettare tutti che si sia allo stesso punto dell'iterazione). Un solo thread, quello con start = 0, aggiorna la somma dei nodi senza archi uscenti, evitando l'uso di mutex, poiché solo lui accede a quella variabile. Dopodiché ancora una volta una barriera fa aspettare che tutti siano allo stesso punto.

A questo punto si aggiornano i vettori XT, YT, St e l'errore, con l'uso di mutex per proteggere l'aggiornamento dell'errore e di St. Per XT e YT non sono necessari mutex, poiché ogni thread lavora su un segmento separato dell'array (il for va da start a end). Un'altra barriera è utilizzata per sincronizzare i thread dopo l'aggiornamento.

Durante il controllo di convergenza, non è necessario un mutex poiché tutti i valori assegnabili alle variabili condivise (numiter e errore) sono identici per ogni thread grazie alla barriera precedente. Segue ancora una barriera prima di procedere.

Infine, ancora una volta un solo thread (con start = 0) aggiorna le variabili condivise per la prossima iterazione, senza mutex, in quanto in questo caso nessun altro thread accede a quelle variabili. Una barriera finale sincronizza i thread prima dell'inizio della nuova iterazione.

## Gestione dei segnali
Il segnale per visualizzare il massimo pagerank, il nodo corrispondente e il numero di iterazioni effettuate è SIGUSR1, da mandare al PID del main. Questo segnale viene ignorato dal main (tramite le apposite operazioni sulla maschera, quindi il main può gestire solo il segnale SIGQUIT) poiché deve essere gestito dal thread handler dei segnali, che parte nella funzione pagerank. Una volta che parte il calcolo del pagerank è possibile mandare questo segnale e vedere a schermo il valore di queste tre variabili.

Il thread che gestisce i segnali viene avviato nella funzione pagerank ed esegue la funzione handler_tbody. La visualizzazione a schermo dei valori richiesti è protetta da mutex in quanto stanno su variabili condivise anche dai thread che stanno facendo il calcolo.
Il segnale SIGUSR2 viene considerato come segnale di terminazione e viene mandato dalla stessa funzione pagerank una volta terminato il procedimento.

# Client-server python
I programmi client-server python consentono di avviare un server che chiama il programma pagerank.c, che può gestire più client alla volta che gli mandano dei file mtx. Per semplicità, il server utilizza un indirizzo standard loop interface, in modo che il client-server funzioni nella macchina stessa. La comunicazione avviene mediante socket.

Il server python crea un thread per ogni client che fa richiesta, tramite ThreadPoolExecutor. Non è necessaria la join esplicita dei thread in quanto se ne occupa già il costrutto with ThreadPoolExecutor as executor. A gestire il segnale SIGINT è il main con la funzione handler.
Ogni thread esegue la funzione tbody. Non viene usato alcun meccanismo di sincronizzazione in quanto i thread non usano nessuna variabile condivisa (viene creato un thread per ogni connessione diversa, che avrà un file temporaneo dedicato).
Il server rimane aperto e in ascolto finché non riceve il segnale SIGINT (ctrl+c da tastiera).

Il client python crea un thread per ogni file dato in input, in modo analogo al server con ThreadPoolExecutor.
Anche qui ogni thread esegue tbody e non viene usato alcun meccanismo di sincronizzazione perché ogni thread (che gestisce un file del client) stabilisce una connessione separata con il server, e il server a sua volta gestisce ciascuna connessione in un thread distinto e scrive i nodi nel suo file temporaneo. Non c'è quindi nessun accesso a memoria condivisa.

## pagerank.py
Questo file è il file scritto dal professore del corso di Laboratorio 2, utilizzato come riferimento per capire l'algoritmo pagerank.

<<<<<<< HEAD
=======

>>>>>>> 35c20c08bedd2633197479a8a9790e94ad9ce266
