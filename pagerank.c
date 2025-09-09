#define _GNU_SOURCE   // permette di usare estensioni gNU
#include <stdio.h>    // permette di usare scanf printf etc ...
#include <stdlib.h>   // conversioni stringa exit() etc ...
#include <stdbool.h>  // gestisce tipo bool
#include <assert.h>   // permette di usare la funzione assert
#include <string.h>   // funzioni per stringhe
#include <errno.h>    // richiesto per usare errno
#include <unistd.h>
#include <pthread.h> // funzioni per i thread
#include <math.h> // funzione fabs
#include "xerrori.h" 

#define BUFF_SIZE 1024

// struttura inmap
typedef struct inmap {
    int **edges; // array che rappresenta la lista di adiacenza: lista dei nodi che hanno archi entranti in ogni nodo
    int *count;  // array del numero di archi entranti per ogni nodo
    int *capacity; // array della capacità di edges per ogni nodo (serve per realloc)
} inmap;

// struttura dati del grafo
typedef struct {
    int N; // numero di nodi del grafo
    int *out; // array con il numero di archi uscenti da ogni nodo
    inmap *in; // inmap con gli insiemi di archi entranti in ogni nodo
} grafo;

// Struct contenente i parametri di input e output di ogni thread read-consumer nella lettura da file (parsing grafo)
typedef struct {
    int num_threads; // numero dei thread, usato per calcolare l'indice del mutex
    grafo *graph; // il grafo da aggiornare (gestione archi)
    int nodi; // numero di nodi del grafo
    int *buffer; // buffer con gli archi letti dal file
    int *pcindex; // puntatore all'indice del buffer

    pthread_mutex_t *buffer_mutex; // mutex per il buffer
    pthread_mutex_t *graph_mutex; // mutex per il grafo (usato come array di mutex nel main)

    sem_t *sem_free_slots;
    sem_t *sem_data_items;
    int archi_inseriti; // numero di archi messi nel grafo

} dati_parse_tbody;

/// Struct contenente i dati dei thread consumatori del pagerank
typedef struct {

    grafo *graph;        

    // Variabili per il calcolo    
    double *x; // Vettore XT (PageRank all'iterazione precedente)
    double *y; // Vettore YT (calcolato per ogni iterazione)
    double *xnext; // Vettore XTnext (PageRank attuale)
    double *somma_St; // Somma St per nodi senza archi uscenti
    int maxiter;             
    double tolleranza;       
    double damping_factor;               
    int *index; // Indice, usato come indice del nodo corrente
    double *err;             

    // Variabili utili al calcolo e alla gestione dei segnali
    int *numiter;
    double *max_pr; // Valore massimo del PageRank
    int *max_nodo; // Nodo con il massimo PageRank

    // Variabili per gestire le iterazioni del thread consumatore pagerank
    // Grazie a queste variabili i thread lavorano parallelamente su zone diverse del grafo
    int start;
    int end;
    
    // Meccanismi di sincronizzazione: mutex e barriere
    pthread_mutex_t *mutex_indice_nodo;
    pthread_mutex_t *mutex_maxpr;        
    pthread_mutex_t *mutex_err;
    pthread_mutex_t *mutex_St;   

    pthread_barrier_t *cond_barrier;

} dati_pagerank_tbody;

// Struct per gestire i segnali
typedef struct {

    pthread_mutex_t *mutex_signal; // usato dal thread che gestisce i segnali

    // valori condivisi con i thread che calcolano il pagerank
    int *current_iteration;
    double *pagerank_max;
    int *max_node;

} signal_data;

// Funzione per inizializzare un inmap
inmap* crea_inmap(int N) {
    inmap *im = malloc(sizeof(inmap));
    if(im == NULL) {
        xtermina("Errore di allocazione nella crea_inmap -- im\n", __LINE__, __FILE__);
    }
    im->edges = malloc(N * sizeof(int*));
    if(im->edges == NULL) {
        xtermina("Errore di allocazione nella crea_inmap -- im->edges\n", __LINE__, __FILE__);
    }
    im->count = calloc(N, sizeof(int));
    if(im->count == NULL) {
        xtermina("Errore di allocazione nella crea_inmap -- im->count\n", __LINE__, __FILE__);
    }
    im->capacity = malloc(N * sizeof(int));
    if(im->capacity == NULL) {
        xtermina("Errore di allocazione nella crea_inmap -- im->capacity\n", __LINE__, __FILE__);
    }
    for (int i = 0; i < N; i++) {
        im->edges[i] = malloc(1 * sizeof(int)); // capacità iniziale di 10
        if(im->edges[i] == NULL) {
            xtermina("Errore di allocazione nella crea_inmap -- im->edges[i]\n", __LINE__, __FILE__);
        }
        im->capacity[i] = 1;
    }
    return im;
}

// Funzione per creare e inizializzare un grafo
grafo* crea_grafo(int N) {
    grafo *g = malloc(sizeof(grafo));
    if (g == NULL) {
        xtermina("Errore di allocazione nella crea_grafo -- grafo\n", __LINE__, __FILE__);
    }
    g->N = N;
    g->out = calloc(N, sizeof(int));
    if (g->out == NULL) {
        xtermina("Errore di allocazione nella crea_grafo -- g->out\n", __LINE__, __FILE__);
    }
    g->in = crea_inmap(N);
    return g;
}

// Funzione per aggiungere un elemento a inmap
void add_to_inmap(inmap *im, int node, int edge) {
    if (im->count[node] == im->capacity[node]) {
        // reallocazione se serve
        im->capacity[node] *= 2;
        im->edges[node] = realloc(im->edges[node], im->capacity[node] * sizeof(int));
        if(im->edges[node] == NULL) {
            xtermina("Errore di riallocazione nella add_to_inmap -- im->edges\n", __LINE__, __FILE__);
        }
    }
    // aggiungo l'arco e al contempo aumento il numero di archi entranti in node
    im->edges[node][im->count[node]++] = edge;
}

// Funzione che dealloca la inmap
void free_inmap(inmap *im, int N) {
    if (im != NULL) {
        for (int i = 0; i < N; i++) {
            free(im->edges[i]);
        }
        free(im->edges);
        free(im->count);
        free(im->capacity);
        free(im);
    }
}

// Funzione che dealloca il grafo
void free_grafo(grafo *g) {
    if (g != NULL) {
        free(g->out);
        free_inmap(g->in, g->N);
        free(g);
    }
}

// Le funzioni utilizzate dai thread sono dichiarate qui come prototipi e definite alla fine dopo il main

// Prototipo della funzione dei thread consumatori nella lettura da file
void *parse_tbody(void *arg);
// Prototipo della funzione dei thread consumatori nel pagerank
void *pagerank_tbody(void *arg);
// Prototipo dela funzione del thread che gestisce i segnali
void *handler_tbody(void *arg);

// Funzione che lavora sul grafo e restituisce l'array con i valori pagerank
// la funzione memorizza in numiter il numero di iterazioni effettuate
double *pagerank(grafo *g, double d, double eps, int maxiter, int taux, int *numiter) {

    // INIZIALIZZAZIONE
    
    // Alloco tre array di g->N double che rappresentano X^T, Y^T e X^Tnext
    // Alla fine X^T e Y^T saranno deallocati e rimarrà solo l'array X^T+1 contenente il risultato

    double *XT = malloc(g->N * sizeof(double)); // page rank iterazione precedente
    if (XT == NULL) {
        xtermina("Errore di allocazione in pagerank -- XT\n", __LINE__, __FILE__);
    }
    double *YT = malloc(g->N * sizeof(double));
    if (YT == NULL) {
        xtermina("Errore di allocazione in pagerank -- YT\n", __LINE__, __FILE__);
    }
    double *XTnext = calloc(g->N, sizeof(double)); // page rank attuale
    if (XTnext == NULL) {
        xtermina("Errore di allocazione in pagerank -- XTnext\n", __LINE__, __FILE__);
    }

    // Somma St
    double *St;
    double valore_St = 0;
    St = &valore_St;

    // inizializzo il primo vettore pagerank, e al contempo inizializzo Y^T e tengo il conto della sommatoria St
    for (int i = 0; i < g->N; i++) {
        // all'inizio tutti i nodi hanno lo stesso pagerank 
        // (cioè X^T ha tutti elementi 1/N, 1/N, ..., 1/N)
        XT[i] = 1.0 / g->N;
        if(g->out[i] == 0) {
            *St += XT[i];
            YT[i] = 0;
        }
        else {
            YT[i] = XT[i] / g->out[i];
        }
    } // Considero questa come la prima iterazione conclusa (per questo numiter = 1)

    // Inizializzo i dati per i thread
    pthread_mutex_t mutex_indice_nodo = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mutex_max_pr = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mutex_err = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t mutex_St = PTHREAD_MUTEX_INITIALIZER;
    pthread_barrier_t barriera;
    pthread_barrier_init(&barriera, NULL, taux);

    pthread_mutex_t mutex_signal = PTHREAD_MUTEX_INITIALIZER;

    pthread_t thread_pagerank[taux];
    dati_pagerank_tbody dati_thread[taux];

    // variabili condivise da handler e thread consumatori insieme a numiter
    double pagerank_max_value = 0;
    int max_node = 0;

    int index = 0;
    double errore = 0;

    for(int i = 0; i < taux; i++) {
        dati_thread[i].graph = g;
        dati_thread[i].x = XT;
        dati_thread[i].y = YT;
        dati_thread[i].xnext = XTnext;
        dati_thread[i].maxiter = maxiter;
        dati_thread[i].tolleranza = eps;
        dati_thread[i].damping_factor = d;
        dati_thread[i].numiter = numiter;
        dati_thread[i].index = &index;
        dati_thread[i].err = &errore;
        dati_thread[i].somma_St = St;
        dati_thread[i].max_pr = &pagerank_max_value;
        dati_thread[i].max_nodo = &max_node;

        // Indici di partenza e fine per il lavoro dei thread (parallelizzazione delle varie zone del grafo)
        // ogni thread lavora solo su una parte del grafo
        // start viene usata anche come controllo perché solo un thread può aggiornare determinati valori
        // (come ad esempio il massimo pagerank)
        dati_thread[i].start = i * g->N / taux;
        dati_thread[i].end = (i + 1) * g->N / taux;

        dati_thread[i].mutex_indice_nodo = &mutex_indice_nodo;
        dati_thread[i].mutex_maxpr = &mutex_max_pr;
        dati_thread[i].mutex_err = &mutex_err;
        dati_thread[i].mutex_St = &mutex_St;

        dati_thread[i].cond_barrier = &barriera;

        xpthread_create(&thread_pagerank[i], NULL, &pagerank_tbody, &dati_thread[i], __LINE__, __FILE__);
    }

    // Creo un thread che serve unicamente a gestire i segnali
    pthread_t signal_handler;
    signal_data dati_signal;
    dati_signal.mutex_signal = &mutex_max_pr;

    // le variabili su cui il segnale viene gestito sono le stesse usate dai thread consumatori
    // per questo anche nell'handler acquisirò il mutex quando dovrà gestire un segnale
    // dato che accede al valore di queste variabili per visualizzarle
    dati_signal.current_iteration = numiter;
    dati_signal.pagerank_max = &pagerank_max_value;
    dati_signal.max_node = &max_node;

    xpthread_create(&signal_handler, NULL, &handler_tbody, &dati_signal, __LINE__, __FILE__);

    // Join del risultato
    for(int i = 0; i < taux; i++) {
        xpthread_join(thread_pagerank[i], NULL, __LINE__, __FILE__);
    }

    // Una volta che ho il risultato mando il segnale di terminazione (segnale -usr2)
    // dopodiché joino il thread che gestisce i segnali per farlo terminare
    pthread_kill(signal_handler, SIGUSR2);
    xpthread_join(signal_handler, NULL, __LINE__, __FILE__);

    // deallocazione di tutto
    pthread_barrier_destroy(&barriera);
    pthread_mutex_destroy(&mutex_indice_nodo);
    pthread_mutex_destroy(&mutex_max_pr);
    pthread_mutex_destroy(&mutex_err);
    pthread_mutex_destroy(&mutex_signal);
    pthread_mutex_destroy(&mutex_St);
    free(YT);
    free(XTnext);

    // Restituisco il vettore pagerank e il numero di iterazioni è stato memorizzato in numiter
    return XT;  

}

// MAIN
int main(int argc, char *argv[]) {

    // Voglio che il segnale SIGUSR1 venga gestito dal thread handler
    // Quindi ho riempito la maschera con tutti i segnali, ho tolto sigquit perché voglio che sia il main
    // a gestirlo, quindi togliendo sigquit e poi bloccando tutti i segnali al main rimane solo sigquit da gestire
    // In questo modo è possibile uscire dal programma con ctrl + backslash
    sigset_t mask;
    sigfillset(&mask);                    
    sigdelset(&mask, SIGQUIT);          
    pthread_sigmask(SIG_BLOCK, &mask, NULL);

    fprintf(stderr, "Main partito, PID: %d\n",gettid());

    // Variabili di utilità per la stampa finale
    int somma_archi_validi = 0;
    int somma_DE = 0;
    double somma_PR = 0;

    int K;          // migliori K nodi
    int M;          // maxiter
    double D;       // damping factor
    double E;       // tolleranza
    int T;          // thread ausiliari

    // Definizione dei parametri di default in caso di mancato input
    K = 3;
    M = 100;
    D = 0.9;
    E = 1.0e-7;
    T = 3;

    // Definizione dei parametri in caso di input
    int parametro;
    while((parametro = getopt(argc, argv, "k:m:d:e:t:")) != -1) {
        switch(parametro) {
            case 'k':
                K = atoi(optarg);
                break;
            case 'm':
                M = atoi(optarg);
                break;
            case 'd':
                D = atof(optarg);
                break;
            case 'e':
                E = atof(optarg);
                break;
            case 't':
                T = atoi(optarg);
                break;
        }
    }

    // Apertura del file
    char *F = argv[optind];
    if (F == NULL)
        xtermina("Specificare il file utilizzando l'opzione -f\n", __LINE__, __FILE__);
    FILE *filemtx = fopen(F, "r");
    if(filemtx == NULL)
        xtermina("Errore nell'apertura del file\n", __LINE__, __FILE__);
    
    // Inizio la lettura da file
    char *getline_buffer = NULL;
    size_t n = 0;
    ssize_t e;

    // Ignoro i commenti
    do {
        e = getline(&getline_buffer, &n, filemtx);
        if (e < 0) {
            free(getline_buffer);
            xtermina("File vuoto",__LINE__,__FILE__);
        }
    } while(getline_buffer[0] == '%');

    // Prima riga di dati
    // Leggo r
    int r;
    char *token = strtok(getline_buffer, " \t\n");
    r = atoi(token);

    // Creo il grafo
    grafo *g = crea_grafo(r); // r è il numero di nodi
    // mi ricordo di r perché se trovo un nodo che è > r allora è un arco non valido e devo fare exit(1)

    // Ora che ho il grafo, creo e mando in esecuzione i thread consumatori

    int cindex = 0;    
    int pindex = 0;
    int buffer[BUFF_SIZE];

    pthread_mutex_t buff_mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t graph_mu[T];
    // Per ottimizzare creo un numero di mutex per il grafo pari al numero di thread
    for (int i = 0; i < T; i++) {
        pthread_mutex_init(&graph_mu[i], NULL);
    }

    pthread_t thread_consumatori[T];
    dati_parse_tbody dati_thread[T];

    sem_t sem_free_slots, sem_data_items;
    // Inizializzazione del semaforo: all'inizio ci sono BUFF_SIZE/2 slot liberi nel buffer
    // quindi considero una coppia di nodi come un unico slot nel buffer
    xsem_init(&sem_free_slots,0,BUFF_SIZE/2,__LINE__,__FILE__);
    xsem_init(&sem_data_items,0,0,__LINE__,__FILE__);

    for(int i=0;i<T;i++) {
        // faccio partire il thread i
        dati_thread[i].nodi = r;
        dati_thread[i].graph = g;
        dati_thread[i].buffer = buffer;
        dati_thread[i].pcindex = &cindex;
        dati_thread[i].num_threads = T;

        dati_thread[i].buffer_mutex = &buff_mu;
        dati_thread[i].graph_mutex = graph_mu;

        dati_thread[i].sem_data_items = &sem_data_items;
        dati_thread[i].sem_free_slots = &sem_free_slots;
        dati_thread[i].archi_inseriti = 0;
        xpthread_create(&thread_consumatori[i],NULL,&parse_tbody,&dati_thread[i],__LINE__,__FILE__);
    }
    fputs("Thread ausiliari lettura file creati\nParsing del grafo in corso\n",stderr);

    // Leggo i e j dal file e li inserisco nel buffer 
    while ((e = getline(&getline_buffer, &n, filemtx)) >= 0) {
        // Memorizzo i-1 e j-1
        token = strtok(getline_buffer, " \t\n");
        int i = atoi(token) - 1;
        token = strtok(NULL, " \t\n");
        int j = atoi(token) - 1;

        if(i > r || j > r){
            fprintf(stderr,"Errore: il grafo ha %d nodi e ho trovato arco illegale %d --> %d\n",r,i,j);
            fprintf(stderr,"Non calcolo il pagerank\n");
            exit(1);
        }            

        // inserimento nel buffer
        xsem_wait(&sem_free_slots,__LINE__,__FILE__);
        xpthread_mutex_lock(&buff_mu,__LINE__,__FILE__);
        buffer[pindex++ % BUFF_SIZE] = i;
        buffer[pindex++ % BUFF_SIZE] = j;
        xpthread_mutex_unlock(&buff_mu,__LINE__,__FILE__);
        xsem_post(&sem_data_items,__LINE__,__FILE__);

    }

    // terminazione threads: inserisco -1 (valore di terminazione) nel buffer
    for(int i=0;i<T;i++) {

        xsem_wait(&sem_free_slots,__LINE__,__FILE__);
        xpthread_mutex_lock(&buff_mu,__LINE__,__FILE__);
        buffer[pindex++ % BUFF_SIZE] = -1;
        buffer[pindex++ % BUFF_SIZE] = -1;
        xpthread_mutex_unlock(&buff_mu,__LINE__,__FILE__);
        xsem_post(&sem_data_items,__LINE__,__FILE__);

    }
    fputs("Lettura file completata: Valori di terminazione scritti nel buffer\n",stderr);

    // join dei thread e calcolo risultato
    for(int i=0;i<T;i++) {
        xpthread_join(thread_consumatori[i],NULL,__LINE__,__FILE__);
    }

    // calcolo la somma degli archi inseriti calcolati da ogni thread
    for(int i=0;i<T;i++) {
        somma_archi_validi += dati_thread[i].archi_inseriti;
    }
    
    // Distruggo i mutex e i semafori per il parsing, ho finito di usarli
    xsem_destroy(&sem_data_items,__LINE__,__FILE__);
    xsem_destroy(&sem_free_slots,__LINE__,__FILE__);
    xpthread_mutex_destroy(&buff_mu,__LINE__,__FILE__);
    for(int i = 0; i < T; i++) {
        xpthread_mutex_destroy(&graph_mu[i],__LINE__,__FILE__);
    }

    fputs("Parsing del grafo completato\n",stderr);

    // Calcolo del pagerank e stampa dell'output
    int numiter = 1;
    fputs("Calcolo pagerank in corso\n",stderr);
    fputs("Mandare segnale -usr1 al main con il comando kill per avere informazioni sullo stato del calcolo\n",stderr);
    double *pr = pagerank(g, D, E, M, T, &numiter);
    fputs("Calcolo pagerank completato\n",stderr);

    printf("Number of nodes: %d\n", g->N);
    // calcolo la somma dei nodi dead end
    for(int i = 0; i<g->N; i++) {
        if(g->out[i] == 0) {
            somma_DE++;
        }
    }
    printf("Number of dead-end nodes: %d\n", somma_DE);
    printf("Number of valid arcs: %d\n", somma_archi_validi);

    if (M > numiter) {
        printf("Converged after %d iterations \n", numiter);
    }
    else {
        printf("Did not converge after %d iterations\n", M);
    }
    for(int i = 0; i<g->N; i++) {
        somma_PR += pr[i];
    }
    printf("Sum of ranks: %.4f   (should be 1)\n", somma_PR);

    // stampa dei migliori K nodi
    // scorro l'array pagerank e ogni volta stampo il nodo massimo corrente
    // se trovo due nodi con stesso pagerank, stampo prima quello con indice minore
    printf("Top %d nodes:\n", K);
    for(int i = 0; i < K; i++) {
        double max = 0;
        int max_index = 0;
        for(int j = 0; j < g->N; j++) {
            // trova il massimo pagerank, oppure se trova due nodi con stesso pagerank stampa quello di indice minore
            if(pr[j] > max || (pr[j] == max && j < max_index)) {
                max = pr[j];
                max_index = j;
            }
        }
        printf("  %d %.6f\n", max_index, pr[max_index]);
        pr[max_index] = 0;
    }

    // Dealloco ciò che rimane
    // Libero il grafo, l'array del pagerank e il buffer usato per la getline, chiudo il file
    free(getline_buffer);
    free_grafo(g);
    free(pr);
    if(fclose(filemtx) != 0)
        xtermina("Errore chiusura file",__LINE__,__FILE__);

    return 0;
}

// Funzione dei thread consumatori nella lettura da file e costruzione del grafo
void *parse_tbody(void *arg) {

    dati_parse_tbody *dati_thread = (dati_parse_tbody *)arg; 
    pthread_mutex_t *bm = dati_thread->buffer_mutex;
    pthread_mutex_t *gm = dati_thread->graph_mutex;
    grafo *g = dati_thread->graph;
    int numero_threads = dati_thread->num_threads;
    int i, j;

    do {

        // Acquisizione valori dal buffer
        xsem_wait(dati_thread->sem_data_items,__LINE__,__FILE__);
        xpthread_mutex_lock(bm,__LINE__,__FILE__);
        i = dati_thread->buffer[*(dati_thread->pcindex) % BUFF_SIZE];
        *(dati_thread->pcindex) +=1;
        j = dati_thread->buffer[*(dati_thread->pcindex) % BUFF_SIZE];
        *(dati_thread->pcindex) +=1;
        xpthread_mutex_unlock(bm,__LINE__,__FILE__);
        xsem_post(dati_thread->sem_free_slots,__LINE__,__FILE__);

        if (i == j) {
            continue; // Ignoro gli archi con stesso nodo sorgente e destinazione
        }

        // indice del mutex da acquisire
        // in questo modo ogni thread lavora su un sottoinsieme di nodi
        int mutex_index_j = j % numero_threads;
        int mutex_index_i = i % numero_threads;

        // Controllo se l'arco esiste già
        xpthread_mutex_lock(&gm[mutex_index_j],__LINE__,__FILE__);
        bool arco_esistente = false;
        for(int k=0; k<g->in->count[j]; k++) {
            if(g->in->edges[j][k] == i) {
                arco_esistente = true; // l'arco esiste già
                break;
            }
        }
        
        if (!arco_esistente) {
            // Aggiungo l'arco al grafo
            add_to_inmap(g->in, j, i);
            dati_thread->archi_inseriti++;
        }
        else { 
            // L'arco esisteva già quindi lo ignoro
            xpthread_mutex_unlock(&gm[mutex_index_j],__LINE__,__FILE__);
            continue;
        }

        xpthread_mutex_unlock(&gm[mutex_index_j],__LINE__,__FILE__);

        xpthread_mutex_lock(&gm[mutex_index_i],__LINE__,__FILE__);
        g->out[i]++;
        xpthread_mutex_unlock(&gm[mutex_index_i],__LINE__,__FILE__);

        // FINE -- grafo aggiornato

    } while(i != -1 && j != -1); // Continua finché non vede i valori di terminazione nel buffer

    return NULL;
}

void *pagerank_tbody(void *arg) {

    dati_pagerank_tbody *dati_rank = (dati_pagerank_tbody *)arg;

    pthread_mutex_t *mutex_indice_nodo = dati_rank->mutex_indice_nodo;
    pthread_mutex_t *mutex_max_pr = dati_rank->mutex_maxpr;
    pthread_mutex_t *mutex_err = dati_rank->mutex_err;
    pthread_mutex_t *mutex_St = dati_rank->mutex_St;

    while(1) {

        int indice_nodo_corrente = 0;

        while(1) {
            
            // facendo ++ sto aggiornando quella variabile condivisa, quindi serve il mutex
            xpthread_mutex_lock(mutex_indice_nodo, __LINE__, __FILE__);
            indice_nodo_corrente = (*(dati_rank->index))++;
            xpthread_mutex_unlock(mutex_indice_nodo, __LINE__, __FILE__);

            if (indice_nodo_corrente >= dati_rank->graph->N) {
                break;  // Esci se tutti i nodi sono stati processati
            }

            double somma_YT = 0;

            for(int i = 0; i < dati_rank->graph->in->count[indice_nodo_corrente]; i++) {
                int inlink = dati_rank->graph->in->edges[indice_nodo_corrente][i];
                somma_YT += dati_rank->y[inlink];
            }

            // Aggiornamento di xnext (pagerank attuale del nodo)
            // Aggiungo il contributo di YT
            dati_rank->xnext[indice_nodo_corrente] = dati_rank->damping_factor * somma_YT;
            // Aggiungo il teleporting
            dati_rank->xnext[indice_nodo_corrente] += (1 - dati_rank->damping_factor) / dati_rank->graph->N;
            // Aggoingo il contributo di St
            dati_rank->xnext[indice_nodo_corrente] += *(dati_rank->somma_St) * (dati_rank->damping_factor / dati_rank->graph->N);

            // Una volta aggiornato xnext controllo il massimo
            // Se il nodo corrente fa superare il massimo pr, aggiorno il massimo
            // Non posso farlo fare a un solo thread perché devo trovare il massimo tra TUTTI i nodi, non solo quelli che vede lui
            xpthread_mutex_lock(mutex_max_pr, __LINE__, __FILE__);
            if(dati_rank->xnext[indice_nodo_corrente] > *dati_rank->max_pr) {
                *dati_rank->max_pr = dati_rank->xnext[indice_nodo_corrente];
                *dati_rank->max_nodo = indice_nodo_corrente;
            }
            xpthread_mutex_unlock(mutex_max_pr, __LINE__, __FILE__);

        }

        // BARRIERA: Tutti i thread devono aver finito il loro calcolo di xnext
        pthread_barrier_wait(dati_rank->cond_barrier);

        // La somma St va resettata a ogni iterazione
        // Lo faccio fare a un solo thread, in modo da evitare mutex
        if (dati_rank->start == 0) {
            *(dati_rank->somma_St) = 0;
        }

        pthread_barrier_wait(dati_rank->cond_barrier);

        // Ciclo per aggiornare i valori di errore, XT, YT e St, ed errore
        for(int i = dati_rank->start; i < dati_rank->end; i++) {

            // Aggiorno l'errore
            xpthread_mutex_lock(mutex_err,__LINE__,__FILE__);
            *(dati_rank->err) += fabs(dati_rank->xnext[i] - dati_rank->x[i]);
            xpthread_mutex_unlock(mutex_err,__LINE__,__FILE__);

            // Aggiorno il valore di XT con XTnext
            dati_rank->x[i] = dati_rank->xnext[i];

            if(dati_rank->graph->out[i] == 0) {
                // Per ogni nodo dead end, aumento la somma St e setto a zero YT
                xpthread_mutex_lock(mutex_St,__LINE__,__FILE__);
                *(dati_rank->somma_St) += dati_rank->x[i];
                xpthread_mutex_unlock(mutex_St,__LINE__,__FILE__);

                dati_rank->y[i] = 0;             
            }
            else {
                // Per ogni nodo non dead end, aumento la somma YT
                dati_rank->y[i] = dati_rank->x[i] / dati_rank->graph->out[i];
            }

        }   

        // Tutti i thread devono aver finito il calcolo
        pthread_barrier_wait(dati_rank->cond_barrier);

        // Controlli per la convergenza
        // Qui non serve il mutex perché ho messo la barriera
        double *err = dati_rank->err;
        int iterazioni = *(dati_rank->numiter);

        // Se l'errore è minore della tolleranza oppure ho raggiunto maxiter, esco
        if(*err < dati_rank->tolleranza || iterazioni >= dati_rank->maxiter) {
            break;
        }

        // Attesa barriera
        pthread_barrier_wait(dati_rank->cond_barrier);

        // L'aggiornamento per la prossima iterazione va fatto da un solo thread, per questo metto il controllo
        // Lo faccio fare solo al primo thread in modo che gli altri non possano toccare queste variabili
        if(dati_rank->start == 0) {

            *dati_rank->max_nodo = 0;
            *dati_rank->max_pr = 0;

            *(dati_rank->err) = 0;
            *(dati_rank->numiter) += 1;

            *(dati_rank->index) = 0;

        }
        
        pthread_barrier_wait(dati_rank->cond_barrier);

    }
    return NULL;
}

// Funzione del thread handler dei segnali
// Visualizza su stderr il numero dell'iterazione, il valore massimo del pagerank e il nodo corrispondente
void *handler_tbody(void *arg) {

    // L'handler può gestire tutti i segnali tranne SIGINT
    sigset_t mask;
    sigfillset(&mask);
    sigdelset(&mask, SIGINT);

    signal_data *dati_segnale = (signal_data *)arg;
    pthread_mutex_t *mutex_signal = dati_segnale->mutex_signal;

    int signal_type;

    while(true) {

        int e = sigwait(&mask, &signal_type);
        if(e != 0) {
            perror("Errore sigwait");
            exit(2);
        }
        fprintf(stderr, "Handler svegliato dal segnale %d\n", signal_type);

        // Se arriva SIGUSR2 o qualche altro segnale che non sia SIGUSR1, termino
        if(signal_type == SIGUSR2 || signal_type != SIGUSR1) {
            fprintf(stderr,"Ricevuto segnale di terminazione\n");
            break;
        }

        // Se arrivo qui sono sicuro di essere nel segnale SIGUSR1
        // sto accedendo al valore condiviso del massimo pagerank e del nodo corrispondente, quindi uso il mutex
        // (il mutex è lo stesso mutex_max_pr usato dai thread che calcolano il pagerank)
        xpthread_mutex_lock(mutex_signal, __LINE__, __FILE__);
        fprintf(stderr,"Iterazione %d: max PR = %.6f, nodo = %d\n", *(dati_segnale->current_iteration), *(dati_segnale->pagerank_max), *(dati_segnale->max_node));
        xpthread_mutex_unlock(mutex_signal, __LINE__, __FILE__);
           
    }

    return NULL;
}

